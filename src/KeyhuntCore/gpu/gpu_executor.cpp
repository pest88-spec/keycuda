#include "KeyhuntCore/gpu/gpu_executor.h"

#include "KeyhuntCore/adapters/bitcrack/conversions.h"
#include "KeyhuntCore/gpu/batch_planner.h"
#include "compare/kernels/hash160_fused.h"
#include "cuda_runtime.h"
#include "CudaKeySearchDevice/CudaDeviceKeys.h"
#include "CudaKeySearchDevice/cudabridge.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace puzzle71::gpu {

namespace {

std::vector<secp256k1::uint256> ToBitCrackScalars(const std::vector<core::UInt256>& scalars) {
    std::vector<secp256k1::uint256> out;
    out.reserve(scalars.size());
    for (const auto& scalar : scalars) {
        out.push_back(::bitcrack_adapter::ToBitCrack(scalar));
    }
    return out;
}

core::UInt256 FromDeviceWords(const std::uint32_t words[8]) {
    secp256k1::uint256 value(words, secp256k1::uint256::BigEndian);
    return ::bitcrack_adapter::FromBitCrack(value);
}

void FinalizePrivateKey(core::UInt256* out,
                        const core::UInt256& start,
                        std::uint64_t offset) {
    *out = core::Incremented(start, offset);
}

bool IsOutOfMemoryError(const std::runtime_error& ex) {
    const std::string message = ex.what();
    return message.find("out of memory") != std::string::npos ||
           message.find("memory allocation") != std::string::npos ||
           message.find("cudaMalloc") != std::string::npos;
}

bool ReduceBatchForOom(gpu::BatchConfig& cfg) {
    if (cfg.points_per_thread > 1) {
        cfg.points_per_thread = std::max(1, cfg.points_per_thread / 2);
        return true;
    }

    constexpr unsigned int kWarp = 32;
    if (cfg.block.x > kWarp) {
        unsigned int reduced = cfg.block.x / 2;
        reduced = (reduced / kWarp) * kWarp;
        if (reduced >= kWarp) {
            cfg.block.x = reduced;
            return true;
        }
    }

    if (cfg.grid.x > 1) {
        cfg.grid.x = std::max<unsigned int>(1u, cfg.grid.x / 2);
        return true;
    }

    return false;
}

}  // namespace

GpuExecutor::GpuExecutor(int device_id,
                         bool compressed,
                         const std::array<std::uint32_t, 5>& target_hash160,
                         bool verbose)
    : device_id_(device_id), compressed_(compressed), props_{}, config_{}, batch_start_{},
      host_scalars_{}, device_candidates_{}, device_candidate_count_{}, host_candidates_{},
      device_keys_{}, verbose_(verbose) {
    if (verbose_) {
        std::cout << "[debug] GpuExecutor: Setting device " << device_id << std::endl;
    }

    cudaError_t err = cudaSetDevice(device_id_);
    if (err != cudaSuccess) {
        throw std::runtime_error(std::string("cudaSetDevice failed: ") + cudaGetErrorString(err));
    }

    if (verbose_) {
        std::cout << "[debug] GpuExecutor: Getting device properties..." << std::endl;
    }
    if (cudaGetDeviceProperties(&props_, device_id_) != cudaSuccess) {
        throw std::runtime_error("cudaGetDeviceProperties failed");
    }

    if (verbose_) {
        std::cout << "[debug] GpuExecutor: Uploading target HASH160..." << std::endl;
    }
    auto status = puzzle71::compare::UploadTargetHash160(target_hash160);
    if (status != cudaSuccess) {
        throw std::runtime_error(std::string("Failed to upload target HASH160: ") + cudaGetErrorString(status));
    }
    if (verbose_) {
        std::cout << "[debug] GpuExecutor: Constructor complete" << std::endl;
    }
}

GpuExecutor::~GpuExecutor() {
    SmartCleanup();
    device_keys_.clearPrivateKeys();
    cleanupChainBuf();
}

void GpuExecutor::SmartCleanup() {
    try {
        device_candidates_.Release();
        device_candidate_count_.Release();
        host_candidates_.clear();
        host_candidates_.shrink_to_fit();
        cudaError_t status = cudaDeviceSynchronize();
        if (verbose_ && status != cudaSuccess) {
            std::cerr << "[warn] cudaDeviceSynchronize during cleanup: "
                      << cudaGetErrorString(status) << std::endl;
        }
    } catch (const std::exception& ex) {
        if (verbose_) {
            std::cerr << "[warn] smart cleanup failed: " << ex.what() << std::endl;
        }
    }
}

void GpuExecutor::InitializeDeviceKeys(const std::vector<secp256k1::uint256>& scalars,
                                       int points_per_thread,
                                       dim3 grid,
                                       dim3 block) {
    CheckCuda(cudaSetDevice(device_id_), "cudaSetDevice");
    CheckCuda(cudaSetDeviceFlags(cudaDeviceScheduleBlockingSync), "cudaSetDeviceFlags");
    CheckCuda(cudaDeviceSetCacheConfig(cudaFuncCachePreferL1), "cudaDeviceSetCacheConfig");

    cleanupChainBuf();

    CheckCuda(device_keys_.init(static_cast<int>(grid.x),
                                static_cast<int>(block.x),
                                points_per_thread,
                                scalars),
              "device_keys_.init");

    for (int i = 1; i <= 256; ++i) {
        CheckCuda(device_keys_.doStep(), "device_keys_.doStep");
    }

    const std::uint64_t total_points = static_cast<std::uint64_t>(grid.x) *
                                       static_cast<std::uint64_t>(block.x) *
                                       static_cast<std::uint64_t>(points_per_thread);

    CheckCuda(allocateChainBuf(static_cast<unsigned int>(total_points)), "allocateChainBuf");

    const secp256k1::ecpoint g = secp256k1::G();
    const secp256k1::ecpoint p = secp256k1::multiplyPoint(secp256k1::uint256(total_points), g);
    CheckCuda(setIncrementorPoint(p.x, p.y), "setIncrementorPoint");

    device_keys_.clearPrivateKeys();
}

void GpuExecutor::PrepareResultBuffers(std::size_t capacity) {
    if (capacity == 0) {
        capacity = static_cast<std::size_t>(config_.block.x) *
                   static_cast<std::size_t>(config_.grid.x);
        if (capacity == 0) {
            capacity = 1;
        }
    }

    capacity = std::min<std::size_t>(capacity, kMaxCandidateBuffer);

    device_candidates_.Allocate(capacity);
    device_candidate_count_.Allocate(1);
    CheckCuda(cudaMemset(device_candidate_count_.data(), 0, sizeof(std::uint32_t)),
              "cudaMemset(result_count)");

    host_candidates_.resize(capacity);

    DeviceResultBuffer buffer{};
    buffer.candidates = device_candidates_.data();
    buffer.count = device_candidate_count_.data();
    buffer.capacity = static_cast<std::uint32_t>(capacity);

    CheckCuda(puzzle71::kernel::SetResultBuffer(buffer), "SetResultBuffer");
}

void GpuExecutor::PrepareBatch(const BatchConfig& config,
                               const core::UInt256& start_scalar) {
    config_ = config;
    batch_start_ = start_scalar;

    if (config_.grid.x == 0 || config_.block.x == 0 || config_.points_per_thread <= 0) {
        throw std::invalid_argument("BatchConfig missing launch parameters");
    }

    std::uint64_t limit = config_.keys_total;
    if (limit == 0 || limit > kMaxKeysPerBatch) {
        limit = kMaxKeysPerBatch;
    }
    ClampBatchConfig(config_, limit);
    std::uint64_t threads = ComputeThreadCount(config_.grid, config_.block);
    std::uint64_t computed_total = threads * static_cast<std::uint64_t>(config_.points_per_thread);
    if (computed_total == 0) {
        throw std::runtime_error("Invalid launch configuration after clamping");
    }
    if (computed_total > kMaxKeysPerBatch) {
        throw std::runtime_error("Computed batch exceeds maximum limit");
    }
    config_.keys_total = computed_total;

    if (verbose_) {
        std::cout << "[debug] PrepareBatch grid=" << config_.grid.x
                  << " block=" << config_.block.x
                  << " points/thread=" << config_.points_per_thread
                  << " keys_total=" << config_.keys_total
                  << " start=" << start_scalar.ToHex() << std::endl;
    }

    while (true) {
        size_t free_mem = 0;
        size_t total_mem = 0;
        if (cudaMemGetInfo(&free_mem, &total_mem) == cudaSuccess) {
            std::size_t min_free = static_cast<std::size_t>(total_mem / 10);  // keep 10% free
            if (verbose_) {
                std::cout << "[debug] GPU memory: used="
                          << (total_mem - free_mem) / (1024 * 1024)
                          << "MB free=" << free_mem / (1024 * 1024) << "MB"
                          << " threshold=" << min_free / (1024 * 1024) << "MB" << std::endl;
            }
            if (free_mem < min_free) {
                if (ReduceBatchForOom(config_)) {
                    ClampBatchConfig(config_, kMaxKeysPerBatch);
                    if (verbose_) {
                        std::cout << "[warn] Low GPU memory detected, reducing batch to grid="
                                  << config_.grid.x << " block=" << config_.block.x
                                  << " points/thread=" << config_.points_per_thread << std::endl;
                    }
                    continue;
                }
            }
        }

        std::uint64_t threads = ComputeThreadCount(config_.grid, config_.block);
        config_.keys_total = threads * static_cast<std::uint64_t>(config_.points_per_thread);

        host_scalars_.Configure(config_.grid, config_.block, config_.points_per_thread);
        DeviceBatch batch = host_scalars_.PrepareBatch(batch_start_, config_.keys_total);

        auto scalars = ToBitCrackScalars(batch.scalars);

        std::uint64_t total_points = static_cast<std::uint64_t>(config_.grid.x) *
                                     static_cast<std::uint64_t>(config_.block.x) *
                                     static_cast<std::uint64_t>(config_.points_per_thread);
        if (scalars.size() != total_points) {
            std::ostringstream oss;
            oss << "Scalar count mismatch: expected " << total_points
                << " got " << scalars.size();
            throw std::runtime_error(oss.str());
        }

        cleanupChainBuf();
        device_keys_.clearPublicKeys();
        device_keys_.clearPrivateKeys();

        try {
            InitializeDeviceKeys(scalars, config_.points_per_thread, config_.grid, config_.block);
            PrepareResultBuffers(batch.scalars.size());
            break;
        } catch (const std::runtime_error& ex) {
            if (!IsOutOfMemoryError(ex) || !ReduceBatchForOom(config_)) {
                throw;
            }
            if (verbose_) {
                std::cout << "[warn] CUDA out of memory during init; reducing batch to grid=" << config_.grid.x
                          << " block=" << config_.block.x
                          << " points/thread=" << config_.points_per_thread << std::endl;
            }
            ClampBatchConfig(config_, kMaxKeysPerBatch);
            continue;
        }
    }
}

StepResult GpuExecutor::Execute() {
    StepResult result{};

    if (device_candidates_.size() == 0) {
        return result;
    }

    const int compression_flag = compressed_ ? PointCompressionType::COMPRESSED
                                             : PointCompressionType::UNCOMPRESSED;

    CheckCuda(cudaMemset(device_candidate_count_.data(), 0, sizeof(std::uint32_t)),
              "cudaMemset(result_count)");

    if (verbose_) {
        std::cout << "[debug] Launching fused kernel grid=" << config_.grid.x
                  << " block=" << config_.block.x
                  << " points/thread=" << config_.points_per_thread << std::endl;
    }
    auto start = std::chrono::high_resolution_clock::now();
    auto launch_status = puzzle71::kernel::LaunchFusedKernel(config_.grid,
                                                             config_.block,
                                                             config_.points_per_thread,
                                                             compression_flag);
    if (launch_status != cudaSuccess) {
        throw std::runtime_error(std::string("LaunchFusedKernel failed: ") + cudaGetErrorString(launch_status));
    }

    auto sync_status = cudaDeviceSynchronize();
    if (sync_status != cudaSuccess) {
        throw std::runtime_error(std::string("cudaDeviceSynchronize failed: ") + cudaGetErrorString(sync_status));
    }
    if (verbose_) {
        std::cout << "[debug] Kernel completed" << std::endl;
    }
    auto end = std::chrono::high_resolution_clock::now();

    result.elapsed_us = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());

    if (verbose_) {
        std::cout << "[debug] Kernel completed elapsed_us=" << result.elapsed_us << std::endl;
    }

    std::uint32_t candidate_count = 0;
    CheckCuda(cudaMemcpy(&candidate_count,
                         device_candidate_count_.data(),
                         sizeof(candidate_count),
                         cudaMemcpyDeviceToHost),
              "cudaMemcpy(result_count)");
    candidate_count = std::min<std::uint32_t>(candidate_count,
                                              static_cast<std::uint32_t>(host_candidates_.size()));

    if (candidate_count > 0) {
        CheckCuda(cudaMemcpy(host_candidates_.data(),
                             device_candidates_.data(),
                             candidate_count * sizeof(DeviceCandidate),
                             cudaMemcpyDeviceToHost),
                  "cudaMemcpy(candidates)");
    }

    std::vector<bitcrack_adapter::KeySearchResult> out;
    out.reserve(candidate_count);

    const std::uint64_t total_threads = static_cast<std::uint64_t>(config_.block.x) * config_.grid.x;

    for (std::uint32_t i = 0; i < candidate_count; ++i) {
        const DeviceCandidate& cand = host_candidates_[i];
        bitcrack_adapter::KeySearchResult converted{};

        const std::uint64_t offset = static_cast<std::uint64_t>(cand.idx) * total_threads +
                                     (static_cast<std::uint64_t>(cand.block) * config_.block.x + cand.thread);
        FinalizePrivateKey(&converted.private_key, batch_start_, offset);

        converted.x = FromDeviceWords(cand.x);
        converted.y = FromDeviceWords(cand.y);
        converted.is_compressed = cand.compressed != 0;
        for (int j = 0; j < 5; ++j) {
            converted.digest[j] = cand.digest[j];
        }

        out.push_back(std::move(converted));
    }

    result.candidates = std::move(out);

    std::uint64_t processed_keys = config_.keys_total;
    result.processed_keys = processed_keys;
    result.next_scalar = core::Incremented(batch_start_, processed_keys);
    if (result.elapsed_us > 0) {
        result.keys_per_sec = static_cast<double>(result.processed_keys) * 1'000'000.0 /
                              static_cast<double>(result.elapsed_us);
    }

    SmartCleanup();

    return result;
}

}  // namespace puzzle71::gpu
