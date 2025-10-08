#pragma once

#include <chrono>
#include <vector>
#include <memory>
#include <string>
#include <functional>
#include <cuda_runtime.h>
#include "core/uint256.h"
#include "ComputeCore/gpu/performance/performance_logger.h"

namespace puzzle71::gpu::sync {

struct SyncOptimizationResult {
    int eliminated_sync_calls = 0;
    std::chrono::microseconds time_saved{0};
    double performance_improvement_factor = 0.0;
    bool accuracy_maintained = false;
    std::string optimization_details;
    std::chrono::microseconds baseline_time{0};
    std::chrono::microseconds optimized_time{0};
    size_t memory_usage_reduction_bytes = 0;
    double gpu_utilization_improvement = 0.0;
};

struct FusedKernelConfig {
    size_t group_table_size = 256;                   // Number of precomputed points
    int points_per_thread = 128;                     // Parallelism factor
    int block_size = 512;                            // Threads per block
    size_t shared_memory_size = 0;                   // Calculated based on config
    bool enable_cooperative_launch = true;           // Use cooperative groups
    bool use_shared_memory_optimization = true;      // Store intermediate results in shared memory
};

class SynchronizationOptimizer {
public:
    SynchronizationOptimizer();
    virtual ~SynchronizationOptimizer();

    // Primary optimization methods (T021 core functionality)
    virtual SyncOptimizationResult OptimizeInitializationSequence() = 0;
    virtual SyncOptimizationResult FuseKernelOperations() = 0;
    virtual SyncOptimizationResult EnableAsynchronousProcessing() = 0;
    virtual bool ValidateOptimizationAccuracy() const = 0;

    // Configuration and monitoring methods
    virtual void SetOptimizationConfig(const FusedKernelConfig& config) = 0;
    virtual FusedKernelConfig GetCurrentConfig() const = 0;
    virtual SyncOptimizationResult GetLastOptimizationResult() const = 0;

    // Advanced optimization methods
    virtual SyncOptimizationResult OptimizeKernelLaunchPattern() = 0;
    virtual SyncOptimizationResult ReduceMemorySynchronizationOverhead() = 0;
    virtual SyncOptimizationResult OptimizeStreamSynchronization() = 0;

    // Performance analysis methods
    virtual std::chrono::microseconds MeasureBaselinePerformance() = 0;
    virtual std::chrono::microseconds MeasureOptimizedPerformance() = 0;
    virtual double CalculateSyncOverheadReduction() = 0;

    // Validation and testing methods
    virtual bool ValidateFusedKernelCorrectness(const core::UInt256& test_key) = 0;
    virtual bool CrossValidateWithBaseline(size_t num_test_keys = 1000) = 0;
    virtual std::vector<std::string> GetOptimizationRecommendations() = 0;

    // Factory method
    static std::unique_ptr<SynchronizationOptimizer> Create(int gpu_id = 0);

protected:
    FusedKernelConfig config_;
    SyncOptimizationResult last_result_;
    int gpu_id_ = 0;
    mutable std::mutex optimizer_mutex_;

    // Internal validation methods
    virtual bool ValidateKernelLaunchParameters() const = 0;
    virtual bool ValidateSharedMemoryRequirements() const = 0;
    virtual bool ValidateCooperativeLaunchCapabilities() const = 0;
};

// Concrete implementation
class CudaSynchronizationOptimizer : public SynchronizationOptimizer {
public:
    explicit CudaSynchronizationOptimizer(int gpu_id = 0);
    ~CudaSynchronizationOptimizer() override;

    // SynchronizationOptimizer interface implementation
    SyncOptimizationResult OptimizeInitializationSequence() override;
    SyncOptimizationResult FuseKernelOperations() override;
    SyncOptimizationResult EnableAsynchronousProcessing() override;
    bool ValidateOptimizationAccuracy() const override;

    void SetOptimizationConfig(const FusedKernelConfig& config) override;
    FusedKernelConfig GetCurrentConfig() const override;
    SyncOptimizationResult GetLastOptimizationResult() const override;

    SyncOptimizationResult OptimizeKernelLaunchPattern() override;
    SyncOptimizationResult ReduceMemorySynchronizationOverhead() override;
    SyncOptimizationResult OptimizeStreamSynchronization() override;

    std::chrono::microseconds MeasureBaselinePerformance() override;
    std::chrono::microseconds MeasureOptimizedPerformance() override;
    double CalculateSyncOverheadReduction() override;

    bool ValidateFusedKernelCorrectness(const core::UInt256& test_key) override;
    bool CrossValidateWithBaseline(size_t num_test_keys = 1000) override;
    std::vector<std::string> GetOptimizationRecommendations() override;

    // Additional required virtual function implementations
    bool ValidateKernelLaunchParameters() const override;
    bool ValidateSharedMemoryRequirements() const override;
    bool ValidateCooperativeLaunchCapabilities() const override;

private:
    int gpu_id_;

    // CUDA kernel management
    bool LoadFusedKernel();
    bool LaunchFusedInitializationKernel(const core::UInt256& start_key);
    bool ValidateKernelResults(const std::vector<core::UInt256>& expected_results);

    // Performance measurement methods
    std::chrono::microseconds MeasureSequentialInitializationTime();
    std::chrono::microseconds MeasureFusedInitializationTime();
    std::chrono::microseconds MeasureSynchronizationOverhead();

    // Configuration optimization methods
    FusedKernelConfig CalculateOptimalConfiguration();
    bool ValidateConfiguration(const FusedKernelConfig& config) const;
    void UpdateConfigurationForGpuArchitecture();

    // Memory management for fused operations
    bool AllocateFusedKernelMemory();
    void CleanupFusedKernelMemory();
    bool OptimizeMemoryLayout();

    // Error handling and recovery
    bool HandleCudaError(cudaError_t error, const std::string& context);
    bool AttemptKernelRecovery();
    void FallbackToSequentialMode();

    // CUDA objects
    void* fused_kernel_device_memory_ = nullptr;
    void* group_table_device_memory_ = nullptr;
    void* results_device_memory_ = nullptr;
    cudaStream_t optimization_stream_;
    cudaEvent_t start_event_;
    cudaEvent_t end_event_;

    // State tracking
    bool kernel_loaded_ = false;
    bool optimization_enabled_ = false;
    bool fallback_mode_active_ = false;
    std::chrono::steady_clock::time_point last_optimization_time_;
};

// Utility functions
namespace sync_utils {
    std::string FormatOptimizationResult(const SyncOptimizationResult& result);
    FusedKernelConfig CreateDefaultConfig();
    FusedKernelConfig CreateOptimizedConfigForGpu(int compute_capability);
    bool IsCooperativeLaunchSupported(int gpu_id);
    size_t CalculateSharedMemoryRequirements(const FusedKernelConfig& config);
    double EstimatePerformanceImprovement(int eliminated_sync_calls,
                                         std::chrono::microseconds time_saved);
}

} // namespace puzzle71::gpu::sync