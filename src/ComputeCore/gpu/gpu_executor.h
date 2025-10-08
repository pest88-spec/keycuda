#pragma once

#include "ComputeCore/gpu/batch_planner.h"
#include "ComputeCore/shards/shard_walker.h"
#include "ComputeCore/adapters/reference/keyfinder_adapter.h"
#include "ComputeCore/gpu/device_buffers.h"
#include "ComputeCore/gpu/device_memory.h"
#include "ComputeCore/gpu/device_results.h"
#include "ComputeCore/gpu/performance/fused_initialization_kernel.h"
#include "ComputeCore/gpu/performance/memory_optimizer.h"
#include "ComputeCore/gpu/performance/bandwidth_validator.h"
#include "ComputeCore/gpu/performance/asynchronous_stream_manager.h"
#include "ComputeCore/gpu/performance/double_buffer_manager.h"
#include "ComputeCore/gpu/performance/memory_coalescing_optimizer.h"
#include "ComputeCore/gpu/performance/memory_bandwidth_profiler.h"
#include "ComputeCore/gpu/performance/memory_pool_manager.h"
#include "ComputeCore/gpu/performance/memory_prefetch_manager.h"
#include "ComputeCore/gpu/performance/memory_transfer_batcher.h"
#include "CudaKeySearchDevice/CudaDeviceKeys.h"
#include "KeyFinderLib/KeySearchTypes.h"
#include <memory>

#include <cuda_runtime.h>

#include <array>
#include <cstdint>
#include <vector>
#include <string>

namespace puzzle71::gpu {

struct StepResult {
    core::UInt256 next_scalar;
    std::uint64_t processed_keys{0};
    std::uint64_t elapsed_us{0};
    std::vector<reference_adapter::ComputationResult> candidates;
    double keys_per_sec{0.0};
    std::uint32_t dropped_candidates{0};
};

// Forward declaration for services namespace
namespace puzzle71::services { class OperatorMetadataValidator; }

class GpuExecutor {
public:
    GpuExecutor(int device_id,
                bool compressed,
                const std::array<std::uint32_t, 5>& target_hash160,
                bool verbose);
    ~GpuExecutor();
    GpuExecutor(const GpuExecutor&) = delete;
    GpuExecutor& operator=(const GpuExecutor&) = delete;
    GpuExecutor(GpuExecutor&&) noexcept = default;
    GpuExecutor& operator=(GpuExecutor&&) noexcept = default;

    void PrepareBatch(const BatchConfig& config,
                      const core::UInt256& start_scalar);

    StepResult Execute();

    // Operator metadata enforcement
    void SetOperatorValidator(std::shared_ptr<puzzle71::services::OperatorMetadataValidator> validator);
    void SetCurrentOperator(const std::string& operator_id);

    // Adaptive parallelism scaling control
    void EnableAdaptiveScaling(bool enabled);
    std::string GetAdaptiveScalingReport() const;
    void SetScalingStrategy(const std::string& strategy);

    // Enhanced adaptive scaling integration methods
    bool ApplyDynamicScalingDuringExecution(double current_throughput_mkeys_per_sec);
    std::vector<puzzle71::gpu::performance::ParallelismConfiguration> GetAdaptiveScalingAlternatives(size_t max_memory_mb) const;
    bool PredictMemoryExhaustion(double safety_margin = 0.1) const;
    double GetMemoryEfficiencyScore() const;

    // Memory optimization control
    void EnableMemoryOptimization(bool enabled);
    std::string GetMemoryOptimizationReport() const;
    void SetMemoryOptimizationLevel(int level);  // 0=none, 1=basic, 2=advanced
    bool ValidateMemoryBandwidthUtilization(double target_percentage = 80.0);
    std::string GetBandwidthPerformanceReport() const;

private:
    void SmartCleanup();
    int device_id_{0};
    bool compressed_{true};
    cudaDeviceProp props_{};

    BatchConfig config_{};
    core::UInt256 batch_start_{};

    DeviceBuffers host_scalars_;
    gpu::DeviceArray<puzzle71::gpu::DeviceCandidate> device_candidates_;
    gpu::DeviceArray<std::uint32_t> device_candidate_count_;
    gpu::DeviceArray<std::uint32_t> device_candidate_overflow_;
    std::vector<puzzle71::gpu::DeviceCandidate> host_candidates_;

    CudaDeviceKeys device_keys_;
    bool verbose_{false};

    // Performance optimization: fused initialization kernel
    std::unique_ptr<puzzle71::gpu::performance::FusedInitializationKernel> fused_init_kernel_;
    bool use_fused_initialization_{true};

    // Adaptive parallelism scaling for dynamic optimization
    std::unique_ptr<puzzle71::gpu::performance::AdaptiveParallelismScaling> adaptive_scaling_;
    bool adaptive_scaling_enabled_{true};

    // Memory optimization and bandwidth validation
    std::unique_ptr<puzzle71::gpu::performance::MemoryOptimizer> memory_optimizer_;
    std::unique_ptr<puzzle71::gpu::performance::BandwidthValidator> bandwidth_validator_;
    bool memory_optimization_enabled_{true};
    int memory_optimization_level_{2};  // 0=none, 1=basic, 2=advanced

    // Phase 5 advanced memory optimization components
    std::unique_ptr<puzzle71::gpu::performance::AsynchronousStreamManager> async_stream_manager_;
    std::unique_ptr<puzzle71::gpu::performance::DoubleBufferManager> double_buffer_manager_;
    std::unique_ptr<puzzle71::gpu::performance::MemoryCoalescingOptimizer> coalescing_optimizer_;
    std::unique_ptr<puzzle71::gpu::performance::MemoryBandwidthProfiler> bandwidth_profiler_;
    std::unique_ptr<puzzle71::gpu::performance::MemoryPoolManager> memory_pool_manager_;
    std::unique_ptr<puzzle71::gpu::performance::MemoryPrefetchManager> prefetch_manager_;
    std::unique_ptr<puzzle71::gpu::performance::MemoryTransferBatcher> transfer_batcher_;

    // Asynchronous memory transfer optimization
    cudaStream_t compute_stream_;
    cudaStream_t transfer_stream_;
    bool streams_initialized_{false};
    BatchConfig last_config_{};
    bool gpu_initialized_{false};
    core::UInt256 expected_next_scalar_;
    bool has_expected_next_{false};

    void InitializeDeviceKeys(const std::vector<secp256k1::uint256>& scalars,
                              int points_per_thread,
                              dim3 grid,
                              dim3 block);

    // Performance optimization methods
    void InitializeDeviceKeysFused(const std::vector<secp256k1::uint256>& scalars,
                                   int points_per_thread,
                                   dim3 grid,
                                   dim3 block);
    void InitializeDeviceKeysSequential(const std::vector<secp256k1::uint256>& scalars,
                                      int points_per_thread,
                                      dim3 grid,
                                      dim3 block);
    bool ShouldUseFusedInitialization() const;

    void PrepareResultBuffers(std::size_t capacity);

    // Asynchronous memory transfer methods
    void InitializeStreams();
    void CleanupStreams();
    void InitializeDeviceKeysAsync(const std::vector<secp256k1::uint256>& scalars,
                                   int points_per_thread,
                                   dim3 grid,
                                   dim3 block);

    // Operator metadata enforcement helpers
    std::string GetEnvironmentName() const;
    void RecordExecutionSuccess(const services::OperationMetadata& operation_metadata, const StepResult& result);
    void RecordExecutionFailure(const services::OperationMetadata& operation_metadata, const std::string& reason);

    // Operator metadata enforcement members
    std::shared_ptr<puzzle71::services::OperatorMetadataValidator> operator_validator_;
    std::string current_operator_id_{"UNKNOWN"};
};

}  // namespace puzzle71::gpu
