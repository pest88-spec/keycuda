#include "ComputeCore/gpu/gpu_executor.h"
#include "services/operator_metadata_validator.h"
#include "ComputeCore/gpu/performance/adaptive_parallelism_scaling.h"

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
    // Enhanced OOM reduction with logging and adaptive fallback awareness
    std::cout << "[OOM] Reducing batch due to memory constraints: "
              << "grid=" << cfg.grid.x
              << " block=" << cfg.block.x
              << " ppt=" << cfg.points_per_thread
              << " keys=" << cfg.keys_total << std::endl;

    // Tier 1: Reduce points per thread (most effective for memory reduction)
    if (cfg.points_per_thread > 64) {
        int old_ppt = cfg.points_per_thread;
        cfg.points_per_thread = std::max(64, cfg.points_per_thread / 2);
        std::cout << "[OOM] Reduced points_per_thread: " << old_ppt << " → " << cfg.points_per_thread << std::endl;
        return true;
    }

    // Tier 2: Reduce block size while maintaining warp alignment
    constexpr unsigned int kWarp = 32;
    if (cfg.block.x > kWarp * 4) { // Keep minimum of 128 threads for efficiency
        unsigned int old_block = cfg.block.x;
        unsigned int reduced = cfg.block.x / 2;
        reduced = (reduced / kWarp) * kWarp; // Warp-align
        if (reduced >= kWarp * 4) {
            cfg.block.x = reduced;
            std::cout << "[OOM] Reduced block size (warp-aligned): " << old_block << " → " << cfg.block.x << std::endl;
            return true;
        }
    }

    // Tier 3: Reduce grid size (last resort)
    if (cfg.grid.x > 1) {
        unsigned int old_grid = cfg.grid.x;
        cfg.grid.x = std::max<unsigned int>(1u, cfg.grid.x / 2);
        std::cout << "[OOM] Reduced grid size: " << old_grid << " → " << cfg.grid.x << std::endl;
        return true;
    }

    std::cout << "[OOM] Cannot reduce batch further - using minimum safe configuration" << std::endl;
    return false;
}

// Adaptive batch configuration using parallelism scaling
gpu::BatchConfig GetProgressiveBatchConfig(int device_id, size_t available_memory_mb, bool first_init,
                                           puzzle71::gpu::performance::AdaptiveParallelismScaling* adaptive_scaling) {
    gpu::BatchConfig config{};

    if (adaptive_scaling) {
        // Use adaptive parallelism scaling for optimal configuration
        try {
            size_t workload_size = first_init ? 64'000'000 : 1'000'000'000; // Target workload size

            // Calculate optimal configuration based on GPU capabilities and workload
            auto scaling_decision = adaptive_scaling->CalculateOptimalConfiguration(workload_size, "key_search");
            auto parallel_config = scaling_decision.selected_config;

            // Convert parallelism configuration to batch config
            config.grid = dim3(static_cast<unsigned int>(parallel_config.grid_size), 1, 1);
            config.block = dim3(static_cast<unsigned int>(parallel_config.block_size), 1, 1);
            config.points_per_thread = parallel_config.points_per_thread;

            // Apply memory constraints if needed
            if (parallel_config.memory_utilization_estimate > 0.85) {
                std::cout << "[ADAPTIVE] Memory constraint detected ("
                          << std::fixed << std::setprecision(3) << parallel_config.memory_utilization_estimate * 100
                          << "% > 85%), applying memory-aware scaling..." << std::endl;

                auto memory_scaled_config = adaptive_scaling->ScaleForMemoryConstraints(
                    parallel_config, available_memory_mb);

                std::cout << "[ADAPTIVE] Memory scaling: "
                          << " ppt " << parallel_config.points_per_thread << "→" << memory_scaled_config.points_per_thread
                          << " block " << parallel_config.block_size << "→" << memory_scaled_config.block_size
                          << " grid " << parallel_config.grid_size << "→" << memory_scaled_config.grid_size
                          << " new_util=" << std::fixed << std::setprecision(3) << memory_scaled_config.memory_utilization_estimate * 100 << "%"
                          << " rationale=" << memory_scaled_config.configuration_rationale << std::endl;

                config.points_per_thread = memory_scaled_config.points_per_thread;
                config.block = dim3(static_cast<unsigned int>(memory_scaled_config.block_size), 1, 1);
                config.grid = dim3(static_cast<unsigned int>(memory_scaled_config.grid_size), 1, 1);
            }

            // Calculate total keys
            std::uint64_t threads = static_cast<std::uint64_t>(config.grid.x) * config.block.x;
            std::uint64_t batch_size = threads * static_cast<std::uint64_t>(config.points_per_thread);
            config.keys_total = batch_size;

            // Detailed logging for adaptive configuration
            std::cout << "[ADAPTIVE] Batch Config: grid=" << config.grid.x
                      << " block=" << config.block.x
                      << " ppt=" << config.points_per_thread
                      << " total_keys=" << config.keys_total
                      << " occupancy=" << std::fixed << std::setprecision(3) << parallel_config.expected_occupancy
                      << " memory=" << std::fixed << std::setprecision(3) << parallel_config.memory_utilization_estimate * 100 << "%"
                      << " confidence=" << std::fixed << std::setprecision(3) << scaling_decision.confidence_score * 100 << "%"
                      << " rationale=" << parallel_config.configuration_rationale << std::endl;

            // Enhanced decision logging with optimization metrics
            if (!parallel_config.optimization_metrics.empty()) {
                std::cout << "[ADAPTIVE] Decision Details:" << std::endl;
                for (const auto& [key, value] : parallel_config.optimization_metrics.items()) {
                    std::cout << "  - " << key << ": " << value << std::endl;
                }
            }

            // Log alternatives if available
            if (!scaling_decision.alternatives.empty()) {
                std::cout << "[ADAPTIVE] Alternative Configurations:" << std::endl;
                for (size_t i = 0; i < std::min(size_t(3), scaling_decision.alternatives.size()); ++i) {
                    const auto& alt = scaling_decision.alternatives[i];
                    std::cout << "  " << (i+1) << ". grid=" << alt.grid_size
                              << " block=" << alt.block_size
                              << " ppt=" << alt.points_per_thread
                              << " occupancy=" << std::fixed << std::setprecision(3) << alt.expected_occupancy
                              << " rationale=" << alt.configuration_rationale << std::endl;
                }
            }

            // Log constraints applied if any
            if (!scaling_decision.constraints_applied.empty()) {
                std::cout << "[ADAPTIVE] Constraints Applied: ";
                for (size_t i = 0; i < scaling_decision.constraints_applied.size(); ++i) {
                    if (i > 0) std::cout << ", ";
                    std::cout << scaling_decision.constraints_applied[i];
                }
                std::cout << std::endl;
            }

            // Log GPU capabilities for context
            const auto& gpu_caps = scaling_decision.gpu_capabilities;
            std::cout << "[ADAPTIVE] GPU Context: " << gpu_caps.device_name
                      << " (Compute " << gpu_caps.compute_capability / 10 << "."
                      << gpu_caps.compute_capability % 10 << ")"
                      << " SMs=" << gpu_caps.sm_count
                      << " Memory=" << gpu_caps.total_memory_mb << "MB"
                      << " Free=" << gpu_caps.free_memory_mb << "MB"
                      << " Bandwidth=" << std::fixed << std::setprecision(1) << gpu_caps.memory_bandwidth_gb_per_sec << "GB/s"
                      << std::endl;

            return config;
        } catch (const std::exception& e) {
            std::cout << "[warn] Adaptive scaling failed: " << e.what() << std::endl;
            std::cout << "[warn] Falling back to manual configuration" << std::endl;

            // Enhanced fallback logging
            if (adaptive_scaling && verbose_) {
                try {
                    auto fallback_configs = adaptive_scaling->GetFallbackConfigurations();
                    std::cout << "[FALLBACK] Available fallback configurations:" << std::endl;
                    for (size_t i = 0; i < fallback_configs.size(); ++i) {
                        const auto& fallback = fallback_configs[i];
                        std::cout << "  " << (i+1) << ". " << fallback.configuration_rationale
                                  << " (grid=" << fallback.grid_size
                                  << " block=" << fallback.block_size
                                  << " ppt=" << fallback.points_per_thread << ")" << std::endl;
                    }
                } catch (const std::exception& fb_e) {
                    std::cout << "[warn] Failed to get fallback configs: " << fb_e.what() << std::endl;
                }
            }
        }
    }

    // Fallback to manual configuration (original logic)
    const size_t kReservedMemoryMB = 1024;
    const size_t kUsableMemoryMB = available_memory_mb > kReservedMemoryMB
                                      ? available_memory_mb - kReservedMemoryMB
                                      : available_memory_mb * 3 / 4;
    const size_t kTargetMemoryUtilizationMB = kUsableMemoryMB * 80 / 100;
    const size_t kMemoryPerKeyEstimate = first_init ? 96 : 32;
    std::uint64_t max_keys_by_memory = (kTargetMemoryUtilizationMB * 1024 * 1024) / kMemoryPerKeyEstimate;
    constexpr std::uint64_t kInitialMaxKeys = 64'000'000;
    constexpr std::uint64_t kProgressiveMaxKeys = 1'000'000'000;
    std::uint64_t target_keys = std::min(max_keys_by_memory, first_init ? kInitialMaxKeys : kProgressiveMaxKeys);

    // Conservative manual configuration
    config.grid = dim3(first_init ? 2048 : 4096, 1, 1);
    config.block = dim3(512, 1, 1);
    config.points_per_thread = first_init ? 64 : 128;

    std::uint64_t threads = static_cast<std::uint64_t>(config.grid.x) * config.block.x;
    std::uint64_t batch_size = threads * static_cast<std::uint64_t>(config.points_per_thread);

    while (batch_size > target_keys && config.grid.x > 1024) {
        config.grid.x = std::max<unsigned int>(1024u, config.grid.x / 2);
        batch_size = static_cast<std::uint64_t>(config.grid.x) * config.block.x * config.points_per_thread;
    }

    config.keys_total = batch_size;

    std::cout << "[MANUAL] Fallback Batch Config: grid=" << config.grid.x
              << " block=" << config.block.x
              << " ppt=" << config.points_per_thread
              << " total_keys=" << config.keys_total << std::endl;

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
    // Initialize performance optimization components
    try {
        if (verbose_) {
            std::cout << "[debug] GpuExecutor: Initializing fused initialization kernel..." << std::endl;
        }
        fused_init_kernel_ = puzzle71::gpu::performance::FusedInitializationKernel::Create(device_id_);

        // Check if fused initialization is supported for this GPU
        use_fused_initialization_ = ShouldUseFusedInitialization();

        if (verbose_) {
            std::cout << "[debug] GpuExecutor: Fused initialization "
                      << (use_fused_initialization_ ? "enabled" : "disabled") << std::endl;
        }
    } catch (const std::exception& e) {
        if (verbose_) {
            std::cout << "[warn] Failed to initialize fused kernel: " << e.what() << std::endl;
            std::cout << "[warn] Falling back to sequential initialization" << std::endl;
        }
        use_fused_initialization_ = false;
    }

    // Initialize adaptive parallelism scaling
    try {
        if (verbose_) {
            std::cout << "[debug] GpuExecutor: Initializing adaptive parallelism scaling..." << std::endl;
        }
        adaptive_scaling_ = puzzle71::gpu::performance::CreateAdaptiveParallelismScaling(
            device_id_, true, true);

        // Configure adaptive scaling for key search workloads
        adaptive_scaling_->SetPerformanceTargets(1000.0, 0.85); // 1K Mkeys/s, 85% memory
        adaptive_scaling_->SetOptimizationStrategy("balanced");

        if (verbose_) {
            std::cout << "[debug] GpuExecutor: Adaptive parallelism scaling enabled" << std::endl;
            auto gpu_caps = adaptive_scaling_->DetectGpuCapabilities(device_id_);
            std::cout << "[debug] GPU: " << gpu_caps.device_name
                      << " (Compute " << gpu_caps.compute_capability / 10 << "."
                      << gpu_caps.compute_capability % 10 << ")" << std::endl;
        }
    } catch (const std::exception& e) {
        if (verbose_) {
            std::cout << "[warn] Failed to initialize adaptive scaling: " << e.what() << std::endl;
            std::cout << "[warn] Using manual configuration only" << std::endl;
        }
        adaptive_scaling_enabled_ = false;
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
    if (use_fused_initialization_ && fused_init_kernel_) {
        InitializeDeviceKeysFused(scalars, points_per_thread, grid, block);
    } else {
        InitializeDeviceKeysSequential(scalars, points_per_thread, grid, block);
    }
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
                BatchConfig progressive_config = GetProgressiveBatchConfig(
                    device_id_, free_mem_mb, first_init,
                    adaptive_scaling_enabled_ ? adaptive_scaling_.get() : nullptr);

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
            if (!IsOutOfMemoryError(ex)) {
                // Non-memory error - try adaptive scaling fallback if available
                if (adaptive_scaling_enabled_ && adaptive_scaling_) {
                    try {
                        std::cout << "[FALLBACK] CUDA error detected, trying adaptive scaling fallback..." << std::endl;

                        // Get safe configuration from adaptive scaling
                        auto safe_config = adaptive_scaling_->GetSafeConfiguration();

                        // Convert to batch config
                        config_.grid = dim3(static_cast<unsigned int>(safe_config.grid_size), 1, 1);
                        config_.block = dim3(static_cast<unsigned int>(safe_config.block_size), 1, 1);
                        config_.points_per_thread = safe_config.points_per_thread;

                        ClampBatchConfig(config_, kMaxKeysPerBatch);

                        std::cout << "[FALLBACK] Applied safe adaptive config: grid=" << config_.grid.x
                                  << " block=" << config_.block.x
                                  << " ppt=" << config_.points_per_thread
                                  << " rationale=" << safe_config.configuration_rationale << std::endl;

                        scalars_ready = false;
                        continue;
                    } catch (const std::exception& fallback_ex) {
                        std::cout << "[warn] Adaptive fallback failed: " << fallback_ex.what() << std::endl;
                    }
                }
                throw;
            }

            // Memory error - use enhanced OOM reduction
            if (ReduceBatchForOom(config_)) {
                if (verbose_) {
                    std::cout << "[warn] CUDA out of memory during init; reduced batch to grid=" << config_.grid.x
                              << " block=" << config_.block.x
                              << " points/thread=" << config_.points_per_thread << std::endl;
                }
                ClampBatchConfig(config_, kMaxKeysPerBatch);
                scalars_ready = false;
                continue;
            } else {
                // OOM reduction failed - try adaptive scaling as last resort
                if (adaptive_scaling_enabled_ && adaptive_scaling_) {
                    try {
                        std::cout << "[EMERGENCY] OOM reduction failed, using adaptive scaling emergency fallback..." << std::endl;

                        // Try each fallback configuration
                        auto fallback_configs = adaptive_scaling_->GetFallbackConfigurations();
                        for (const auto& fallback : fallback_configs) {
                            config_.grid = dim3(static_cast<unsigned int>(fallback.grid_size), 1, 1);
                            config_.block = dim3(static_cast<unsigned int>(fallback.block_size), 1, 1);
                            config_.points_per_thread = fallback.points_per_thread;

                            std::cout << "[EMERGENCY] Trying fallback: " << fallback.configuration_rationale
                                      << " (grid=" << config_.grid.x
                                      << " block=" << config_.block.x
                                      << " ppt=" << config_.points_per_thread << ")" << std::endl;

                            scalars_ready = false;
                            break; // Use first fallback config
                        }
                        continue;
                    } catch (const std::exception& emergency_ex) {
                        std::cout << "[error] Emergency fallback failed: " << emergency_ex.what() << std::endl;
                    }
                }
                throw;
            }
        }
    }
}

StepResult GpuExecutor::Execute() {
    StepResult result{};

    // Create operation metadata for validation and auditing
    services::OperationMetadata operation_metadata;
    operation_metadata.operator_id = current_operator_id_;
    operation_metadata.purpose = services::OperatorPurpose::DATA_PROCESSING;
    operation_metadata.operation_description = "GPU-based elliptic curve key search execution";
    operation_metadata.environment = GetEnvironmentName();
    operation_metadata.operation_parameters["device_id"] = device_id_;
    operation_metadata.operation_parameters["keys_total"] = config_.keys_total;
    operation_metadata.operation_parameters["points_per_thread"] = config_.points_per_thread;
    operation_metadata.operation_parameters["grid_size"] = config_.grid.x;
    operation_metadata.operation_parameters["block_size"] = config_.block.x;
    operation_metadata.start_time = std::chrono::system_clock::now();

    // Validate operator metadata if validator is available
    if (operator_validator_) {
        auto validation_result = operator_validator_->ValidateOperationMetadata(operation_metadata);
        if (!validation_result.is_valid) {
            std::cerr << "[error] Operator metadata validation failed:" << std::endl;
            for (const auto& error : validation_result.validation_errors) {
                std::cerr << "  - " << error << std::endl;
            }
            return result;
        }

        // Record the operation
        operator_validator_->RecordOperation(operation_metadata);
    }

    if (verbose_) {
        std::cout << "[debug] Execute(): device_candidates_.size()=" << device_candidates_.size()
                  << " config_.keys_total=" << config_.keys_total << std::endl;
        if (operator_validator_) {
            std::cout << "[debug] Operator ID: " << current_operator_id_ << " (validated)" << std::endl;
        }
    }

    if (device_candidates_.size() == 0) {
        std::cout << "[debug] Early return: device_candidates_.size() == 0" << std::endl;

        // Record failure if operator validator is available
        if (operator_validator_) {
            operation_metadata.result_summary["status"] = "failed";
            operation_metadata.result_summary["reason"] = "no_device_candidates";
            operation_metadata.end_time = std::chrono::system_clock::now();
            operator_validator_->UpdateOperationResult(operation_metadata.operation_id, operation_metadata.result_summary);
        }

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
    expected_next_scalar_ = result.next_scalar;
    has_expected_next_ = true;
    result.dropped_candidates = overflow_count;
    if (result.elapsed_us > 0) {
        result.keys_per_sec = static_cast<double>(result.processed_keys) * 1'000'000.0 /
                              static_cast<double>(result.elapsed_us);
    }

    // Update successful execution counter and adaptive scaling feedback
    if (result.processed_keys > 0 && result.elapsed_us > 0) {
        successful_executions++;

        // Update adaptive parallelism scaling with performance feedback
        if (adaptive_scaling_enabled_ && adaptive_scaling_) {
            try {
                // Create parallelism configuration from current settings
                puzzle71::gpu::performance::ParallelismConfiguration current_config;
                current_config.block_size = config_.block.x;
                current_config.points_per_thread = config_.points_per_thread;
                current_config.grid_size = config_.grid.x;
                current_config.expected_occupancy = 0.75; // Estimate
                current_config.memory_utilization_estimate = 0.7; // Estimate

                // Record performance result for learning
                double throughput_mkeys_per_sec = result.keys_per_sec / 1'000'000.0;
                std::chrono::microseconds execution_time(result.elapsed_us);
                adaptive_scaling_->RecordPerformanceResult(
                    current_config, throughput_mkeys_per_sec, execution_time, true);

                // Apply dynamic scaling if performance is suboptimal (every 5 executions)
                if (successful_executions % 5 == 0) {
                    bool scaling_applied = ApplyDynamicScalingDuringExecution(throughput_mkeys_per_sec);
                    if (scaling_applied && verbose_) {
                        std::cout << "[adaptive] Dynamic scaling applied, new configuration will be used in next batch" << std::endl;
                    }
                }

                if (verbose_ && successful_executions % 10 == 0) {
                    auto analytics = adaptive_scaling_->GetPerformanceAnalytics();
                    std::cout << "[adaptive] Scaling effectiveness: "
                              << std::fixed << std::setprecision(3) << analytics["average_confidence_score"] * 100 << "%"
                              << " decisions: " << analytics["decision_count"]
                              << " failed: " << analytics["failed_configurations"] << std::endl;

                    // Log detailed performance history
                    auto performance_history = adaptive_scaling_->GetPerformanceHistory();
                    if (!performance_history.empty()) {
                        std::cout << "[adaptive] Performance History (top 5 configs):" << std::endl;
                        int count = 0;
                        for (const auto& [config, avg_throughput] : performance_history) {
                            if (count++ >= 5) break;
                            std::cout << "  - " << config << ": "
                                      << std::fixed << std::setprecision(1) << avg_throughput << " Mkeys/s" << std::endl;
                        }
                    }

                    // Log optimization recommendations
                    auto recommendations = adaptive_scaling_->GetOptimizationRecommendations();
                    if (!recommendations.empty()) {
                        std::cout << "[adaptive] Recommendations:" << std::endl;
                        for (const auto& rec : recommendations) {
                            std::cout << "  - " << rec << std::endl;
                        }
                    }
                }
            } catch (const std::exception& e) {
                if (verbose_) {
                    std::cout << "[warn] Failed to update adaptive scaling: " << e.what() << std::endl;
                }
            }
        }

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

// Performance optimization: fused initialization implementation
void GpuExecutor::InitializeDeviceKeysFused(const std::vector<secp256k1::uint256>& scalars,
                                            int points_per_thread,
                                            dim3 grid,
                                            dim3 block) {
    if (verbose_) {
        std::cout << "[debug] Using fused initialization kernel" << std::endl;
    }

    CheckCuda(cudaSetDevice(device_id_), "cudaSetDevice");
    CheckCuda(cudaSetDeviceFlags(cudaDeviceScheduleBlockingSync), "cudaSetDeviceFlags");
    CheckCuda(cudaDeviceSetCacheConfig(cudaFuncCachePreferL1), "cudaDeviceSetCacheConfig");

    // Initialize device keys normally (but skip the sequential doStep calls)
    CheckCuda(device_keys_.init(static_cast<int>(grid.x),
                                static_cast<int>(block.x),
                                points_per_thread,
                                scalars),
              "device_keys_.init");

    // Use fused initialization kernel to replace 256 sequential doStep calls
    try {
        if (verbose_) {
            std::cout << "[debug] Launching fused initialization kernel..." << std::endl;
        }

        // Configure fused kernel
        puzzle71::gpu::performance::FusedInitConfig config;
        config.points_per_thread = points_per_thread;
        config.block_size = block.x;

        // Launch fused initialization
        auto result = fused_init_kernel_->InitializeGroupTable(config);

        if (result.success && verbose_) {
            std::cout << "[debug] Fused initialization completed successfully" << std::endl;
            std::cout << "[debug] Performance improvement: " << result.performance_improvement_factor << "x" << std::endl;
            std::cout << "[debug] Time saved: " << result.execution_time.count() << " μs" << std::endl;
        } else if (!result.success) {
            if (verbose_) {
                std::cout << "[warn] Fused initialization failed: " << result.error_message << std::endl;
                std::cout << "[warn] Falling back to sequential initialization" << std::endl;
            }
            // Fallback to sequential initialization
            InitializeDeviceKeysSequential(scalars, points_per_thread, grid, block);
            return;
        }
    } catch (const std::exception& e) {
        if (verbose_) {
            std::cout << "[warn] Fused initialization exception: " << e.what() << std::endl;
            std::cout << "[warn] Falling back to sequential initialization" << std::endl;
        }
        // Fallback to sequential initialization
        InitializeDeviceKeysSequential(scalars, points_per_thread, grid, block);
        return;
    }

    // Continue with remaining initialization steps
    const std::uint64_t total_points = static_cast<std::uint64_t>(grid.x) *
                                       static_cast<std::uint64_t>(block.x) *
                                       static_cast<std::uint64_t>(points_per_thread);

    CheckCuda(allocateChainBuf(static_cast<unsigned int>(total_points)), "allocateChainBuf");

    const secp256k1::ecpoint g = secp256k1::G();
    const secp256k1::ecpoint p = secp256k1::multiplyPoint(secp256k1::uint256(total_points), g);
    CheckCuda(setIncrementorPoint(p.x, p.y), "setIncrementorPoint");

    device_keys_.clearPrivateKeys();

    if (verbose_) {
        std::cout << "[debug] Fused device keys initialization completed" << std::endl;
    }
}

void GpuExecutor::InitializeDeviceKeysSequential(const std::vector<secp256k1::uint256>& scalars,
                                               int points_per_thread,
                                               dim3 grid,
                                               dim3 block) {
    if (verbose_) {
        std::cout << "[debug] Using sequential initialization (fallback mode)" << std::endl;
    }

    CheckCuda(cudaSetDevice(device_id_), "cudaSetDevice");
    CheckCuda(cudaSetDeviceFlags(cudaDeviceScheduleBlockingSync), "cudaSetDeviceFlags");
    CheckCuda(cudaDeviceSetCacheConfig(cudaFuncCachePreferL1), "cudaDeviceSetCacheConfig");

    CheckCuda(device_keys_.init(static_cast<int>(grid.x),
                                static_cast<int>(block.x),
                                points_per_thread,
                                scalars),
              "device_keys_.init");

    // Original sequential initialization (256 calls with sync)
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

    if (verbose_) {
        std::cout << "[debug] Sequential device keys initialization completed" << std::endl;
    }
}

bool GpuExecutor::ShouldUseFusedInitialization() const {
    // Check GPU compatibility for fused initialization
    int compute_capability = props_.major * 10 + props_.minor;

    // Require compute capability 7.5+ for optimal performance
    if (compute_capability < 75) {
        if (verbose_) {
            std::cout << "[debug] GPU compute capability " << compute_capability
                      << " insufficient for fused initialization (requires 7.5+)" << std::endl;
        }
        return false;
    }

    // Require sufficient memory for fused operations
    size_t total_memory_mb = props_.totalGlobalMem / (1024 * 1024);
    if (total_memory_mb < 4096) { // 4GB minimum
        if (verbose_) {
            std::cout << "[debug] GPU memory " << total_memory_mb
                      << "MB insufficient for fused initialization (requires 4096MB+)" << std::endl;
        }
        return false;
    }

    return use_fused_initialization_;
}

// Operator metadata enforcement methods

void GpuExecutor::SetOperatorValidator(std::shared_ptr<puzzle71::services::OperatorMetadataValidator> validator) {
    operator_validator_ = validator;
}

void GpuExecutor::SetCurrentOperator(const std::string& operator_id) {
    current_operator_id_ = operator_id;
}

std::string GpuExecutor::GetEnvironmentName() const {
    // Determine environment based on context
    if (verbose_) {
        return "development";
    }

    // Check if running in production environment
    const char* env = std::getenv("ENVIRONMENT");
    if (env && std::string(env) == "production") {
        return "production";
    } else if (env && std::string(env) == "staging") {
        return "staging";
    } else {
        return "development";
    }
}

void GpuExecutor::RecordExecutionSuccess(const services::OperationMetadata& operation_metadata, const StepResult& result) {
    if (operator_validator_) {
        json result_summary;
        result_summary["status"] = "success";
        result_summary["processed_keys"] = result.processed_keys;
        result_summary["elapsed_us"] = result.elapsed_us;
        result_summary["keys_per_sec"] = result.keys_per_sec;
        result_summary["candidates_found"] = result.candidates.size();
        result_summary["dropped_candidates"] = result.dropped_candidates;
        result_summary["next_scalar"] = result.next_scalar.ToString();

        operator_validator_->UpdateOperationResult(operation_metadata.operation_id, result_summary);
    }
}

void GpuExecutor::RecordExecutionFailure(const services::OperationMetadata& operation_metadata, const std::string& reason) {
    if (operator_validator_) {
        json result_summary;
        result_summary["status"] = "failed";
        result_summary["reason"] = reason;
        result_summary["timestamp"] = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

        operator_validator_->UpdateOperationResult(operation_metadata.operation_id, result_summary);
    }
}

// Adaptive parallelism scaling control methods

void GpuExecutor::EnableAdaptiveScaling(bool enabled) {
    adaptive_scaling_enabled_ = enabled && adaptive_scaling_ != nullptr;
    if (verbose_) {
        std::cout << "[debug] Adaptive scaling "
                  << (adaptive_scaling_enabled_ ? "enabled" : "disabled") << std::endl;
    }
}

std::string GpuExecutor::GetAdaptiveScalingReport() const {
    if (!adaptive_scaling_enabled_ || !adaptive_scaling_) {
        return "Adaptive scaling is disabled or unavailable";
    }

    try {
        return adaptive_scaling_->GenerateConfigurationReport();
    } catch (const std::exception& e) {
        return "Error generating report: " + std::string(e.what());
    }
}

void GpuExecutor::SetScalingStrategy(const std::string& strategy) {
    if (adaptive_scaling_enabled_ && adaptive_scaling_) {
        try {
            adaptive_scaling_->SetOptimizationStrategy(strategy);
            if (verbose_) {
                std::cout << "[debug] Scaling strategy set to: " << strategy << std::endl;
            }
        } catch (const std::exception& e) {
            if (verbose_) {
                std::cout << "[warn] Failed to set scaling strategy: " << e.what() << std::endl;
            }
        }
    }
}

// Enhanced adaptive scaling integration methods
bool GpuExecutor::ApplyDynamicScalingDuringExecution(double current_throughput_mkeys_per_sec) {
    if (!adaptive_scaling_enabled_ || !adaptive_scaling_) {
        return false;
    }

    try {
        // Check if current performance is suboptimal
        double target_throughput = 1000.0; // Default target
        double performance_ratio = current_throughput_mkeys_per_sec / target_throughput;

        if (performance_ratio < 0.7) { // Performance is significantly below target
            if (verbose_) {
                std::cout << "[ADAPTIVE] Performance below target ("
                          << std::fixed << std::setprecision(1) << current_throughput_mkeys_per_sec
                          << " vs " << target_throughput << " Mkeys/s), attempting dynamic scaling..." << std::endl;
            }

            // Get current memory usage
            size_t free_mem = 0, total_mem = 0;
            if (cudaMemGetInfo(&free_mem, &total_mem) == cudaSuccess) {
                size_t free_mem_mb = free_mem / (1024 * 1024);

                // Create current configuration
                puzzle71::gpu::performance::ParallelismConfiguration current_config;
                current_config.block_size = config_.block.x;
                current_config.points_per_thread = config_.points_per_thread;
                current_config.grid_size = config_.grid.x;
                current_config.memory_utilization_estimate = 0.7; // Estimate

                // Apply dynamic memory scaling
                auto scaled_config = adaptive_scaling_->DynamicMemoryScaling(
                    current_config,
                    (total_mem - free_mem) / (1024 * 1024), // current usage in MB
                    free_mem_mb,
                    0.8 // performance target
                );

                // Check if scaling would improve performance
                if (scaled_config.points_per_thread != current_config.points_per_thread ||
                    scaled_config.block_size != current_config.block_size) {

                    if (verbose_) {
                        std::cout << "[ADAPTIVE] Applying dynamic scaling: "
                                  << "ppt " << current_config.points_per_thread << "→" << scaled_config.points_per_thread
                                  << " block " << current_config.block_size << "→" << scaled_config.block_size
                                  << " grid " << current_config.grid_size << "→" << scaled_config.grid_size
                                  << " reason=" << scaled_config.configuration_rationale << std::endl;
                    }

                    // Apply the new configuration
                    config_.block = dim3(static_cast<unsigned int>(scaled_config.block_size), 1, 1);
                    config_.points_per_thread = scaled_config.points_per_thread;
                    config_.grid = dim3(static_cast<unsigned int>(scaled_config.grid_size), 1, 1);
                    config_.keys_total = static_cast<std::uint64_t>(config_.grid.x) *
                                      config_.block.x * config_.points_per_thread;

                    return true;
                }
            }
        }
    } catch (const std::exception& e) {
        if (verbose_) {
            std::cout << "[warn] Dynamic scaling failed: " << e.what() << std::endl;
        }
    }

    return false;
}

std::vector<puzzle71::gpu::performance::ParallelismConfiguration>
GpuExecutor::GetAdaptiveScalingAlternatives(size_t max_memory_mb) const {
    if (!adaptive_scaling_enabled_ || !adaptive_scaling_) {
        return {};
    }

    try {
        // Create current configuration
        puzzle71::gpu::performance::ParallelismConfiguration current_config;
        current_config.block_size = config_.block.x;
        current_config.points_per_thread = config_.points_per_thread;
        current_config.grid_size = config_.grid.x;
        current_config.memory_utilization_estimate = 0.7; // Estimate

        // Get memory-constrained alternatives
        return adaptive_scaling_->GetMemoryConstrainedAlternatives(
            current_config, max_memory_mb, config_.keys_total);
    } catch (const std::exception& e) {
        if (verbose_) {
            std::cout << "[warn] Failed to get scaling alternatives: " << e.what() << std::endl;
        }
        return {};
    }
}

bool GpuExecutor::PredictMemoryExhaustion(double safety_margin) const {
    if (!adaptive_scaling_enabled_ || !adaptive_scaling_) {
        return false;
    }

    try {
        // Create current configuration
        puzzle71::gpu::performance::ParallelismConfiguration current_config;
        current_config.block_size = config_.block.x;
        current_config.points_per_thread = config_.points_per_thread;
        current_config.grid_size = config_.grid.x;

        return adaptive_scaling_->PredictMemoryExhaustion(current_config, config_.keys_total, safety_margin);
    } catch (const std::exception& e) {
        if (verbose_) {
            std::cout << "[warn] Memory exhaustion prediction failed: " << e.what() << std::endl;
        }
        return false;
    }
}

double GpuExecutor::GetMemoryEfficiencyScore() const {
    if (!adaptive_scaling_enabled_ || !adaptive_scaling_) {
        return 0.0;
    }

    try {
        // Create current configuration
        puzzle71::gpu::performance::ParallelismConfiguration current_config;
        current_config.block_size = config_.block.x;
        current_config.points_per_thread = config_.points_per_thread;
        current_config.grid_size = config_.grid.x;
        current_config.memory_utilization_estimate = 0.7; // Estimate

        return adaptive_scaling_->GetMemoryEfficiencyScore(current_config, config_.keys_total);
    } catch (const std::exception& e) {
        if (verbose_) {
            std::cout << "[warn] Memory efficiency scoring failed: " << e.what() << std::endl;
        }
        return 0.0;
    }
}

}  // namespace puzzle71::gpu
