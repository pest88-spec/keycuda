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
#include <stdexcept>
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

}  // namespace

GpuExecutor::GpuExecutor(int device_id,
                         bool compressed,
                         const std::array<std::uint32_t, 5>& target_hash160)
    : device_id_(device_id), compressed_(compressed) {
    std::cout << "[debug] GpuExecutor: Setting device " << device_id << std::endl;

    cudaError_t err = cudaSetDevice(device_id_);
    if (err != cudaSuccess) {
        throw std::runtime_error(std::string("cudaSetDevice failed: ") + cudaGetErrorString(err));
    }

    std::cout << "[debug] GpuExecutor: Getting device properties..." << std::endl;
    if (cudaGetDeviceProperties(&props_, device_id_) != cudaSuccess) {
        throw std::runtime_error("cudaGetDeviceProperties failed");
    }

    std::cout << "[debug] GpuExecutor: Uploading target HASH160..." << std::endl;
    auto status = puzzle71::compare::UploadTargetHash160(target_hash160);
    if (status != cudaSuccess) {
        throw std::runtime_error(std::string("Failed to upload target HASH160: ") + cudaGetErrorString(status));
    }
    std::cout << "[debug] GpuExecutor: Constructor complete" << std::endl;
}

GpuExecutor::~GpuExecutor() {
    cleanupChainBuf();
    device_keys_.clearPublicKeys();
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
    if (config_.keys_total == 0 || config_.keys_total > computed_total) {
        config_.keys_total = std::min<std::uint64_t>(computed_total, kMaxKeysPerBatch);
    }

    host_scalars_.Configure(config_.grid, config_.block);
    DeviceBatch batch = host_scalars_.PrepareBatch(batch_start_, config_.keys_total);

    auto scalars = ToBitCrackScalars(batch.scalars);
    InitializeDeviceKeys(scalars, config_.points_per_thread, config_.grid, config_.block);
    PrepareResultBuffers(batch.scalars.size());
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

    auto start = std::chrono::steady_clock::now();
    CheckCuda(puzzle71::kernel::LaunchFusedKernel(config_.grid,
                                                  config_.block,
                                                  config_.points_per_thread,
                                                  compression_flag),
              "LaunchFusedKernel");
    CheckCuda(cudaDeviceSynchronize(), "cudaDeviceSynchronize");
    auto end = std::chrono::steady_clock::now();

    result.elapsed_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());

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
    if (result.elapsed_ms > 0) {
        result.keys_per_sec = static_cast<double>(result.processed_keys) * 1000.0 /
                              static_cast<double>(result.elapsed_ms);
    }

    return result;
}

}  // namespace puzzle71::gpu
