#pragma once

#include <cuda_runtime.h>
#include <memory>
#include <vector>
#include "core/uint256.h"
#include "ComputeCore/gpu/performance/performance_logger.h"

namespace puzzle71::gpu::performance {

/**
 * Fused initialization kernel configuration
 */
struct FusedInitConfig {
    size_t group_table_size = 256;                 // Number of precomputed points (G, 2G, 4G, ..., 2²⁵⁵G)
    int points_per_thread = 128;                   // Parallelism factor
    int block_size = 512;                          // Threads per block
    size_t shared_memory_size = 0;                  // Calculated based on config
    bool use_shared_memory = true;                 // Store intermediate results in shared memory
    bool enable_cooperative_groups = true;          // Use cooperative groups for synchronization

    // Memory requirements
    size_t device_memory_required = 0;              // Total device memory needed
    size_t host_memory_required = 0;               // Total host memory needed
};

/**
 * Fused initialization kernel result
 */
struct FusedInitResult {
    bool success = false;                          // Whether fused initialization succeeded
    std::chrono::microseconds execution_time{0};   // Total execution time
    size_t memory_allocated_bytes = 0;             // Memory allocated for operation
    int steps_completed = 0;                       // Number of initialization steps completed (should be 256)
    double performance_improvement_factor = 0.0;    // Improvement over sequential initialization
    std::string error_message;                     // Error description if failed
    bool accuracy_validated = false;               // Whether results were validated
    std::chrono::microseconds baseline_time{0};    // Time for sequential initialization
};

/**
 * Fused Initialization Kernel Manager
 *
 * Replaces 256 sequential multiplyStepKernel calls with a single fused kernel
 * that performs all group table initialization steps in parallel.
 */
class FusedInitializationKernel {
public:
    FusedInitializationKernel(int gpu_id = 0);
    ~FusedInitializationKernel();

    // Primary interface methods
    FusedInitResult InitializeGroupTable(const FusedInitConfig& config = {});
    FusedInitResult ValidateInitialization(const core::UInt256& test_key);

    // Configuration methods
    void SetConfiguration(const FusedInitConfig& config);
    FusedInitConfig GetConfiguration() const;
    FusedInitResult GetLastResult() const;

    // Performance analysis methods
    std::chrono::microseconds MeasureSequentialInitialization();
    std::chrono::microseconds MeasureFusedInitialization();
    double CalculatePerformanceImprovement();

    // Validation methods
    bool ValidateFusedKernelResults();
    bool CrossValidateWithSequential(size_t num_test_cases = 100);

    // Memory management
    bool AllocateDeviceMemory();
    void CleanupDeviceMemory();
    size_t GetMemoryRequirements() const;

    // Status and diagnostics
    bool IsKernelLoaded() const;
    bool IsMemoryAllocated() const;
    std::vector<std::string> GetDiagnostics() const;

    // Factory method
    static std::unique_ptr<FusedInitializationKernel> Create(int gpu_id = 0);

private:
    int gpu_id_;
    FusedInitConfig config_;
    FusedInitResult last_result_;
    mutable std::mutex optimizer_mutex_;

    // CUDA objects
    void* device_group_table_ = nullptr;            // Device memory for group table
    void* device_private_keys_ = nullptr;           // Device memory for private keys
    void* device_chain_ = nullptr;                  // Device memory for chain
    void* device_base_points_ = nullptr;            // Device memory for base points (G, 2G, etc.)
    void* host_group_table_ = nullptr;              // Host memory for validation
    cudaStream_t compute_stream_;                   // CUDA stream for operations
    cudaEvent_t start_event_;                      // Timing event
    cudaEvent_t end_event_;                        // Timing event

    // State tracking
    bool kernel_loaded_ = false;
    bool memory_allocated_ = false;
    bool initialization_complete_ = false;

    // Internal methods
    bool LoadFusedKernel();
    bool CalculateMemoryRequirements();
    bool ValidateConfiguration() const;
    bool ValidateConfiguration(const FusedInitConfig& config) const;
    void UpdateConfiguration();

    // CUDA kernel launching
    bool LaunchFusedInitializationKernel();
    std::chrono::microseconds SynchronizeAndMeasureTime();

    // Memory operations
    bool CopyGroupTableToDevice();
    bool CopyResultsToHost();

    // Error handling
    bool HandleCudaError(cudaError_t error, const std::string& context);
    void RecordError(const std::string& error);

    // Diagnostics
    void UpdateDiagnostics();
    std::vector<std::string> diagnostics_;
};

// CUDA kernel function declarations
extern "C" {
    /**
     * Fused initialization kernel that replaces 256 sequential multiplyStepKernel calls
     *
     * @param private_keys Device pointer to private keys
     * @param points_per_thread Number of points each thread processes
     * @param chain Device pointer to chain memory
     * @param base_points Device pointer to precomputed base points (G, 2G, 4G, ..., 2²⁵⁵G)
     * @param group_table_size Size of the group table (256)
     * @param shared_memory_ptr Pointer to shared memory for intermediate results
     */
    __global__ void fusedInitializationKernel(
        const unsigned int* private_keys,
        int points_per_thread,
        unsigned int* chain,
        const unsigned int* base_points,
        int group_table_size,
        unsigned int* shared_memory_ptr
    );

    /**
     * Validation kernel to verify fused initialization results
     */
    __global__ void validateInitializationKernel(
        const unsigned int* private_keys,
        const unsigned int* group_table,
        int points_per_thread,
        int group_table_size,
        bool* validation_results
    );
}

// Utility functions
namespace fused_init_utils {
    std::string FormatInitResult(const FusedInitResult& result);
    FusedInitConfig CreateOptimalConfig(int compute_capability);
    bool IsCooperativeLaunchSupported(int gpu_id);
    size_t CalculateSharedMemorySize(const FusedInitConfig& config);
    double EstimatePerformanceGain(int eliminated_sync_calls,
                                 std::chrono::microseconds time_saved);
    std::vector<std::string> GetOptimizationRecommendations(const FusedInitConfig& config);
}

} // namespace puzzle71::gpu::performance