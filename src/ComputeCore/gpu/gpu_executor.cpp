#include "ComputeCore/gpu/gpu_executor.h"

#include "ComputeCore/adapters/reference/conversions.h"
#include "ComputeCore/gpu/batch_planner.h"
#include "compare/kernels/hash160_fused.h"
#include "cuda_runtime.h"
#include "CudaKeySearchDevice/CudaDeviceKeys.h"
#include "CudaKeySearchDevice/cudabridge.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <utility>

namespace puzzle71::gpu {

namespace {

std::vector<secp256k1::uint256> ToReferenceScalars(const std::vector<core::UInt256>& scalars) {
    std::vector<secp256k1::uint256> out;
    out.reserve(scalars.size());
    for (const auto& scalar : scalars) {
        out.push_back(::reference_adapter::ToReferenceFormat(scalar));
    }
    return out;
}

core::UInt256 FromDeviceWords(const std::uint32_t words[8]) {
    secp256k1::uint256 value(words, secp256k1::uint256::BigEndian);
    return ::reference_adapter::FromReferenceFormat(value);
}

core::UInt256 FromDeviceWords64(const std::uint64_t words[4]) {
    std::uint32_t words32[8];
    for (int i = 0; i < 4; ++i) {
        words32[i*2] = static_cast<std::uint32_t>(words[i] >> 32);
        words32[i*2+1] = static_cast<std::uint32_t>(words[i] & 0xFFFFFFFF);
    }
    secp256k1::uint256 value(words32, secp256k1::uint256::BigEndian);
    return ::reference_adapter::FromReferenceFormat(value);
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

// Ultra-aggressive batch configuration for maximum GPU utilization
gpu::BatchConfig GetProgressiveBatchConfig(int /* device_id */, size_t available_memory_mb, bool first_init) {
    gpu::BatchConfig config{};

    // Target 80-90% GPU memory usage for maximum utilization
    const size_t kReservedMemoryMB = 1024;      // Reserve 1GB for system/OS
    const size_t kUsableMemoryMB = available_memory_mb > kReservedMemoryMB
                                      ? available_memory_mb - kReservedMemoryMB
                                      : available_memory_mb * 3 / 4;

    // Aggressive memory usage target (80% of usable memory)
    const size_t kTargetMemoryUtilizationMB = kUsableMemoryMB * 80 / 100;

    // More aggressive memory estimates - assume 32 bytes per key (very conservative)
    const size_t kMemoryPerKeyEstimate = first_init ? 96 : 32;  // Aggressive estimate

    // Calculate maximum keys based on available memory
    std::uint64_t max_keys_by_memory = (kTargetMemoryUtilizationMB * 1024 * 1024) / kMemoryPerKeyEstimate;

    // Ultra-aggressive batch size targets for maximum GPU utilization
    constexpr std::uint64_t kInitialMaxKeys = 64'000'000;      // 64M keys for first init
    constexpr std::uint64_t kProgressiveMaxKeys = 1'000'000'000; // 1B keys for subsequent

    std::uint64_t target_keys = std::min(max_keys_by_memory, first_init ? kInitialMaxKeys : kProgressiveMaxKeys);

    // Ultra-high utilization configuration for RTX A4000 (16GB VRAM)
    if (first_init) {
        // Aggressive start to engage GPU immediately
        config.grid = dim3(4096, 1, 1);     // 4096 blocks
        config.block = dim3(256, 1, 1);     // 256 threads per block (optimal)
        config.points_per_thread = 128;     // 128 points per thread
    } else {
        // Maximum configuration for sustained high utilization
        config.grid = dim3(16384, 1, 1);    // 16K blocks (much higher)
        config.block = dim3(256, 1, 1);     // 256 threads per block
        config.points_per_thread = 512;     // 512 points per thread (very aggressive)
    }

    std::uint64_t threads = static_cast<std::uint64_t>(config.grid.x) * config.block.x;
    std::uint64_t batch_size = threads * static_cast<std::uint64_t>(config.points_per_thread);

    // Scale down only if absolutely necessary for memory constraints
    while (batch_size > target_keys && config.grid.x > 2048) {  // Minimum 2048 grids
        config.grid.x = std::max<unsigned int>(2048u, config.grid.x / 2);
        batch_size = static_cast<std::uint64_t>(config.grid.x) * config.block.x * config.points_per_thread;
    }

    while (batch_size > target_keys && config.points_per_thread > 64) {  // Minimum 64 PPT
        config.points_per_thread = std::max(64, config.points_per_thread / 2);
        batch_size = static_cast<std::uint64_t>(config.grid.x) * config.block.x * config.points_per_thread;
    }

    config.keys_total = batch_size;

    // Detailed logging for GPU utilization debugging
    std::cout << "[GPU-UTIL] Batch Config: grid=" << config.grid.x
              << " block=" << config.block.x
              << " ppt=" << config.points_per_thread
              << " total_keys=" << config.keys_total
              << " estimated_memory_mb=" << (batch_size * kMemoryPerKeyEstimate / (1024*1024))
              << " target_memory_mb=" << kTargetMemoryUtilizationMB
              << " memory_utilization_target=" << (kTargetMemoryUtilizationMB * 100 / available_memory_mb) << "%" << std::endl;

    return config;
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

    // Streams temporarily disabled due to stability issues
    // InitializeStreams();
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
    CleanupStreams();  // Clean up asynchronous streams
    gpu_initialized_ = false;
}

void GpuExecutor::SmartCleanup() {
    try {
        device_candidates_.Release();
        device_candidate_count_.Release();
        device_candidate_overflow_.Release();
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
    if (verbose_) {
        std::cout << "[debug] PrepareResultBuffers called with capacity=" << capacity << std::endl;
    }

    if (capacity == 0) {
        capacity = static_cast<std::size_t>(config_.block.x) *
                   static_cast<std::size_t>(config_.grid.x);
        if (capacity == 0) {
            capacity = 1;
        }
    }

    capacity = std::min<std::size_t>(capacity, kMaxCandidateBuffer);

    // Only reallocate if we need more capacity - avoid memory churn
    if (device_candidates_.size() < capacity) {
        if (verbose_) {
            std::cout << "[debug] Reallocating device_candidates_ from "
                      << device_candidates_.size() << " to " << capacity << std::endl;
        }
        device_candidates_.Allocate(capacity);
        device_candidate_count_.Allocate(1);
        device_candidate_overflow_.Allocate(1);

        // Only reset counters when reallocating
        CheckCuda(cudaMemset(device_candidate_count_.data(), 0, sizeof(std::uint32_t)),
                  "cudaMemset(result_count)");
        CheckCuda(cudaMemset(device_candidate_overflow_.data(), 0, sizeof(std::uint32_t)),
                  "cudaMemset(result_overflow)");

        host_candidates_.resize(capacity);

        DeviceResultBuffer buffer{};
        buffer.candidates = device_candidates_.data();
        buffer.count = device_candidate_count_.data();
        buffer.dropped = device_candidate_overflow_.data();
        buffer.capacity = static_cast<std::uint32_t>(capacity);

        CheckCuda(puzzle71::kernel::SetResultBuffer(buffer), "SetResultBuffer");
    } else {
        // Just reset counters for existing buffers
        CheckCuda(cudaMemset(device_candidate_count_.data(), 0, sizeof(std::uint32_t)),
                  "cudaMemset(result_count)");
        CheckCuda(cudaMemset(device_candidate_overflow_.data(), 0, sizeof(std::uint32_t)),
                  "cudaMemset(result_overflow)");
    }

    if (verbose_) {
        std::cout << "[debug] PrepareResultBuffers complete, device_candidates_.size()="
                  << device_candidates_.size() << " requested=" << capacity << std::endl;
    }
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

    // Store original config before progressive changes for proper comparison
    BatchConfig original_config = config_;

    bool config_changed = !gpu_initialized_ ||
                          config_.grid.x != last_config_.grid.x ||
                          config_.block.x != last_config_.block.x ||
                          config_.points_per_thread != last_config_.points_per_thread;

    bool contiguous_scan = gpu_initialized_ && !config_changed && has_expected_next_ &&
                           start_scalar.Compare(expected_next_scalar_) == 0;
    bool need_reseed = !contiguous_scan;

    std::vector<secp256k1::uint256> scalars;
    bool scalars_ready = false;
    if (need_reseed) {
        has_expected_next_ = false;
    }

    while (true) {
        size_t free_mem = 0;
        size_t total_mem = 0;
        if (cudaMemGetInfo(&free_mem, &total_mem) == cudaSuccess) {
            std::size_t min_free = static_cast<std::size_t>(512ULL * 1024 * 1024);  // keep at least 512 MB free
            std::size_t free_mem_mb = free_mem / (1024 * 1024);

            if (verbose_) {
                std::cout << "[debug] GPU memory: used="
                          << (total_mem - free_mem) / (1024 * 1024)
                          << "MB free=" << free_mem_mb << "MB"
                          << " threshold=" << min_free / (1024 * 1024) << "MB" << std::endl;
            }

            // Use progressive batch configuration strategy
            bool first_init = !gpu_initialized_;
            if (first_init || free_mem < min_free * 2) {  // Use progressive strategy on first init or low memory
                std::uint64_t target_keys = config_.keys_total;
                BatchConfig progressive_config = GetProgressiveBatchConfig(device_id_, free_mem_mb, first_init);

                // Ensure the progressive config respects our target batch size
                if (target_keys > 0 && target_keys < progressive_config.keys_total) {
                    // Adjust to match requested batch size if smaller - don't use oversized progressive config
                    if (verbose_) {
                        std::cout << "[debug] Target batch size " << target_keys
                                  << " is smaller than progressive config " << progressive_config.keys_total
                                  << ", using conservative approach" << std::endl;
                    }
                    // Keep the original config but ensure it's safe
                    ClampBatchConfig(config_, std::min(target_keys, static_cast<std::uint64_t>(1048576))); // Max 1M for safety
                } else {
                    if (verbose_) {
                        std::cout << "[debug] Using progressive batch config ("
                                  << (first_init ? "first_init" : "low_memory") << "): grid="
                                  << progressive_config.grid.x << " block=" << progressive_config.block.x
                                  << " points/thread=" << progressive_config.points_per_thread
                                  << " keys_total=" << progressive_config.keys_total << std::endl;
                    }
                    config_ = progressive_config;
                }

                // Check if progressive changes modified the config significantly
                if (config_.grid.x != original_config.grid.x ||
                    config_.block.x != original_config.block.x ||
                    config_.points_per_thread != original_config.points_per_thread) {
                    config_changed = true;
                    if (verbose_) {
                        std::cout << "[debug] Progressive config changed launch parameters, forcing reseed" << std::endl;
                    }
                }
                // Continue with normal initialization flow - don't break here
            }

            if (free_mem < min_free) {
                if (ReduceBatchForOom(config_)) {
                    ClampBatchConfig(config_, kMaxKeysPerBatch);
                    if (verbose_) {
                        std::cout << "[warn] Low GPU memory detected, reducing batch to grid="
                                  << config_.grid.x << " block=" << config_.block.x
                                  << " points/thread=" << config_.points_per_thread << std::endl;
                    }
                    config_changed = true;
                    continue;
                }
            }
        }

        std::uint64_t threads = ComputeThreadCount(config_.grid, config_.block);
        config_.keys_total = threads * static_cast<std::uint64_t>(config_.points_per_thread);

        if (need_reseed) {
            if (!scalars_ready || config_changed) {
                if (verbose_) {
                    std::cout << "[debug] Reconfiguring host scalars due to "
                              << (need_reseed ? "reseed" : "")
                              << (need_reseed && config_changed ? " and " : "")
                              << (config_changed ? "config change" : "") << std::endl;
                }
                host_scalars_.Configure(config_.grid, config_.block, config_.points_per_thread);
                DeviceBatch batch = host_scalars_.PrepareBatch(batch_start_, config_.keys_total);
                                scalars = ToReferenceScalars(batch.scalars);
                scalars_ready = true;
            }
        }

        if (need_reseed) {
            std::uint64_t total_points = static_cast<std::uint64_t>(config_.grid.x) *
                                         static_cast<std::uint64_t>(config_.block.x) *
                                         static_cast<std::uint64_t>(config_.points_per_thread);
            if (scalars.size() != total_points) {
                std::ostringstream oss;
                oss << "Scalar count mismatch: expected " << total_points
                    << " got " << scalars.size();
                throw std::runtime_error(oss.str());
            }
        }

        auto initialize_with_current_config = [&]() {
            InitializeDeviceKeys(scalars, config_.points_per_thread, config_.grid, config_.block);
            PrepareResultBuffers(config_.keys_total);
            last_config_ = config_;
            gpu_initialized_ = true;
        };

        try {
            if (need_reseed) {
                cleanupChainBuf();
                device_keys_.clearPublicKeys();
                device_keys_.clearPrivateKeys();
                initialize_with_current_config();
            } else {
                PrepareResultBuffers(config_.keys_total);
                last_config_ = config_;
            }
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
            scalars_ready = false;
            continue;
        }
    }
}

StepResult GpuExecutor::Execute() {
    StepResult result{};

    if (verbose_) {
        std::cout << "[debug] Execute(): device_candidates_.size()=" << device_candidates_.size()
                  << " config_.keys_total=" << config_.keys_total << std::endl;
    }

    if (device_candidates_.size() == 0) {
        std::cout << "[debug] Early return: device_candidates_.size() == 0" << std::endl;
        return result;
    }

    // GPU utilization-focused scaling strategy
    static std::uint64_t successful_executions = 0;
    if (gpu_initialized_ && successful_executions > 0) {
        // Monitor memory usage and utilization to guide scaling
        size_t free_mem_mb = 0, total_mem_mb = 0;
        cudaMemGetInfo(&free_mem_mb, &total_mem_mb);
        size_t used_mem_mb = total_mem_mb - free_mem_mb;
        double memory_utilization = static_cast<double>(used_mem_mb) / total_mem_mb * 100.0;

        if (verbose_) {
            std::cout << "[debug] Memory utilization: " << memory_utilization << "% ("
                      << used_mem_mb << "/" << total_mem_mb << " MB)" << std::endl;
        }

        // Scaling based on memory utilization and performance
        if (successful_executions % 1 == 0) {  // Check every execution
            if (memory_utilization < 50.0) {
                // Low memory usage - can be more aggressive
                if (config_.points_per_thread < 512) {
                    int old_points = config_.points_per_thread;
                    config_.points_per_thread = std::min(512, config_.points_per_thread * 4);  // 4x jumps
                    if (verbose_ && old_points != config_.points_per_thread) {
                        std::cout << "[debug] Aggressive scaling (low mem): increased points_per_thread from "
                                  << old_points << " to " << config_.points_per_thread << std::endl;
                    }
                }
                else if (config_.grid.x < 32768) {
                    unsigned int old_grid = config_.grid.x;
                    config_.grid.x = std::min(32768u, config_.grid.x * 4);  // 4x jumps
                    if (verbose_ && old_grid != config_.grid.x) {
                        std::cout << "[debug] Aggressive scaling (low mem): increased grid from "
                                  << old_grid << " to " << config_.grid.x << std::endl;
                    }
                }
            }
            else if (memory_utilization < 75.0) {
                // Moderate memory usage - conservative scaling
                if (config_.points_per_thread < 384) {
                    int old_points = config_.points_per_thread;
                    config_.points_per_thread = std::min(384, config_.points_per_thread * 2);  // 2x jumps
                    if (verbose_ && old_points != config_.points_per_thread) {
                        std::cout << "[debug] Conservative scaling: increased points_per_thread from "
                                  << old_points << " to " << config_.points_per_thread << std::endl;
                    }
                }
                else if (config_.grid.x < 16384) {
                    unsigned int old_grid = config_.grid.x;
                    config_.grid.x = std::min(16384u, config_.grid.x * 2);  // 2x jumps
                    if (verbose_ && old_grid != config_.grid.x) {
                        std::cout << "[debug] Conservative scaling: increased grid from "
                                  << old_grid << " to " << config_.grid.x << std::endl;
                    }
                }
            }
            else {
                // High memory usage - be very careful
                if (successful_executions % 5 == 0) {  // Only check every 5 executions
                    if (config_.points_per_thread < 256) {
                        int old_points = config_.points_per_thread;
                        config_.points_per_thread = std::min(256, config_.points_per_thread + 32);
                        if (verbose_ && old_points != config_.points_per_thread) {
                            std::cout << "[debug] Minimal scaling (high mem): increased points_per_thread from "
                                      << old_points << " to " << config_.points_per_thread << std::endl;
                        }
                    }
                }
            }
        }
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
    std::uint32_t overflow_count = 0;

    // Use synchronous transfers for now (async optimization temporarily disabled)
    CheckCuda(cudaMemcpy(&candidate_count,
                         device_candidate_count_.data(),
                         sizeof(candidate_count),
                         cudaMemcpyDeviceToHost),
              "cudaMemcpy(result_count)");
    candidate_count = std::min<std::uint32_t>(candidate_count,
                                              static_cast<std::uint32_t>(host_candidates_.size()));

    if (device_candidate_overflow_.data()) {
        CheckCuda(cudaMemcpy(&overflow_count,
                             device_candidate_overflow_.data(),
                             sizeof(overflow_count),
                             cudaMemcpyDeviceToHost),
                  "cudaMemcpy(result_overflow)");
    }

    if (candidate_count > 0) {
        CheckCuda(cudaMemcpy(host_candidates_.data(),
                             device_candidates_.data(),
                             candidate_count * sizeof(DeviceCandidate),
                             cudaMemcpyDeviceToHost),
                  "cudaMemcpy(candidates)");
    }

    std::vector<reference_adapter::ComputationResult> out;
    out.reserve(candidate_count);

    const std::uint64_t total_threads = static_cast<std::uint64_t>(config_.block.x) * config_.grid.x;

    for (std::uint32_t i = 0; i < candidate_count; ++i) {
        const DeviceCandidate& cand = host_candidates_[i];
        reference_adapter::ComputationResult converted{};

        const std::uint64_t offset = static_cast<std::uint64_t>(cand.idx) * total_threads +
                                     (static_cast<std::uint64_t>(cand.block) * config_.block.x + cand.thread);
        FinalizePrivateKey(&converted.private_key, batch_start_, offset);

        converted.x = FromDeviceWords64(cand.x);
        converted.y = FromDeviceWords64(cand.y);
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
    expected_next_scalar_ = result.next_scalar;
    has_expected_next_ = true;
    result.dropped_candidates = overflow_count;
    if (result.elapsed_us > 0) {
        result.keys_per_sec = static_cast<double>(result.processed_keys) * 1'000'000.0 /
                              static_cast<double>(result.elapsed_us);
    }

    // Update successful execution counter for progressive scaling
    if (result.processed_keys > 0 && result.elapsed_us > 0) {
        successful_executions++;
        if (verbose_ && successful_executions % 5 == 0) {
            std::cout << "[debug] Performance scaling: " << successful_executions
                      << " successful executions, current config: grid=" << config_.grid.x
                      << " block=" << config_.block.x << " points/thread=" << config_.points_per_thread
                      << " rate=" << std::fixed << std::setprecision(1) << result.keys_per_sec / 1'000'000.0
                      << " Mkeys/s" << std::endl;
        }
    }

    SmartCleanup();

    return result;
}

void GpuExecutor::InitializeStreams() {
    if (streams_initialized_) {
        return;
    }

    if (verbose_) {
        std::cout << "[debug] Initializing CUDA streams for asynchronous operations..." << std::endl;
    }

    // Create stream for memory transfer operations only (simpler approach)
    cudaError_t err = cudaStreamCreateWithFlags(&transfer_stream_, cudaStreamNonBlocking);
    if (err != cudaSuccess) {
        throw std::runtime_error(std::string("Failed to create transfer stream: ") + cudaGetErrorString(err));
    }

    // Use default stream for compute
    compute_stream_ = 0;

    streams_initialized_ = true;

    if (verbose_) {
        std::cout << "[debug] CUDA streams initialized successfully" << std::endl;
    }
}

void GpuExecutor::CleanupStreams() {
    if (!streams_initialized_) {
        return;
    }

    if (verbose_) {
        std::cout << "[debug] Cleaning up CUDA streams..." << std::endl;
    }

    // Only destroy the transfer stream (compute stream is default stream)
    if (transfer_stream_) {
        cudaStreamSynchronize(transfer_stream_);
        cudaStreamDestroy(transfer_stream_);
        transfer_stream_ = 0;
    }

    streams_initialized_ = false;

    if (verbose_) {
        std::cout << "[debug] CUDA streams cleaned up successfully" << std::endl;
    }
}

void GpuExecutor::InitializeDeviceKeysAsync(const std::vector<secp256k1::uint256>& scalars,
                                             int points_per_thread,
                                             dim3 grid,
                                             dim3 block) {
    // For now, just call the synchronous version
    // The async memory transfer optimization is mainly in the Execute() method
    InitializeDeviceKeys(scalars, points_per_thread, grid, block);
}

}  // namespace puzzle71::gpu
