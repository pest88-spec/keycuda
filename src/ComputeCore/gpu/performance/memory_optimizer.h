#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <chrono>
#include <mutex>
#include <queue>
#include <cuda_runtime.h>
#include <nlohmann/json.hpp>

// Forward declarations
namespace keycuda {
namespace core {
class UInt256;
}
namespace gpu {
namespace performance {
class BandwidthProfiler;
class MemoryPool;
}
}
}

namespace keycuda {
namespace gpu {
namespace performance {

/**
 * @brief Memory access pattern optimization for GPU key search operations
 *
 * Provides comprehensive memory optimization for elliptic curve key search:
 * - Asynchronous CUDA stream management for overlapping transfers
 * - Double-buffering for host-device communication
 * - Memory access pattern optimization for better coalescing
 * - Memory pooling with allocation optimization
 * - Prefetching strategies for repeated access patterns
 * - Transfer batching optimization
 * - Bandwidth utilization targeting >80% theoretical maximum
 */

struct MemoryOptimizationConfig {
    bool enable_unified_memory = false;            // Use unified memory where beneficial
    bool enable_memory_pooling = true;             // Pool allocations for efficiency
    bool enable_prefetching = true;                // Proactive memory management
    bool enable_double_buffering = true;           // Overlap host-device transfers
    bool enable_async_streams = true;              // Asynchronous CUDA stream usage
    bool enable_transfer_batching = true;          // Batch memory transfers
    bool enable_coalescing_optimization = true;    // Optimize memory access patterns

    size_t pool_size_mb = 1024;                    // Memory pool size in MB
    size_t buffer_count = 2;                       // Double-buffering count
    size_t stream_count = 4;                       // Number of CUDA streams
    double target_bandwidth_utilization = 0.80;    // 80% bandwidth target
    size_t prefetch_distance_mb = 256;             // Prefetch ahead distance
    size_t batch_size_mb = 64;                     // Transfer batch size

    std::string optimization_level = "balanced";   // conservative, balanced, aggressive
    bool enable_detailed_profiling = false;        // Detailed performance tracking
    std::chrono::microseconds profiling_interval{1000000}; // Profiling sample interval
};

struct MemoryMetrics {
    double bandwidth_utilization_gb_per_sec;       // Achieved bandwidth
    double theoretical_bandwidth_gb_per_sec;       // Maximum theoretical bandwidth
    double memory_efficiency_percentage;           // Utilization efficiency
    size_t peak_memory_usage_mb;                   // Peak memory consumption
    std::chrono::microseconds transfer_time;       // Transfer overhead
    std::chrono::microseconds kernel_time;         // Kernel execution time
    std::chrono::microseconds total_time;          // Total processing time
    int allocation_count;                          // Number of allocations
    size_t cache_hit_bytes;                        // Cache hits in bytes
    size_t cache_miss_bytes;                       // Cache misses in bytes
    double cache_hit_rate;                         // Cache hit percentage

    // Detailed bandwidth metrics
    double host_to_device_bandwidth_gb_per_sec;    // H2D transfer bandwidth
    double device_to_host_bandwidth_gb_per_sec;    // D2H transfer bandwidth
    double device_memory_bandwidth_gb_per_sec;     // Device memory bandwidth
    double shared_memory_efficiency;               // Shared memory utilization
    double l2_cache_hit_rate;                      // L2 cache performance

    // Advanced metrics
    int memory_stalls;                             // Number of memory stall events
    int bank_conflicts;                            // Shared memory bank conflicts
    double coalescing_efficiency;                  // Memory coalescing score
    std::chrono::microseconds prefetch_effectiveness; // Time saved by prefetching

    std::chrono::system_clock::time_point timestamp; // Measurement timestamp
    json detailed_metrics;                          // Additional metrics as JSON
};

struct AccessPattern {
    enum class Type {
        SEQUENTIAL,        // Sequential memory access
        RANDOM,            // Random memory access
        STRIDED,           // Strided access pattern
        COALESCED,         // Coalesced memory access
        UNCOALESCED,       // Uncoalesced memory access
        REPEATING,         // Repeating access pattern
        IRREGULAR          // Irregular access pattern
    };

    Type pattern_type;
    size_t stride_bytes;                           // Access stride in bytes
    size_t access_size_bytes;                      // Size per access
    double locality_factor;                        // Temporal locality (0.0-1.0)
    double spatial_locality;                       // Spatial locality (0.0-1.0)
    std::vector<size_t> access_offsets;            // Access offset pattern
    bool is_read_pattern;                          // True for reads, false for writes

    std::chrono::microseconds pattern_duration;    // Duration of this pattern
    size_t access_count;                           // Number of accesses
    double efficiency_score;                       // Pattern efficiency (0.0-1.0)
};

struct MemoryTransferBatch {
    size_t batch_id;                               // Unique batch identifier
    void* host_ptr;                                // Host memory pointer
    void* device_ptr;                              // Device memory pointer
    size_t size_bytes;                             // Transfer size
    bool is_read_operation;                        // True for H2D, false for D2H
    int stream_id;                                 // CUDA stream for transfer
    cudaEvent_t start_event;                       // Transfer start event
    cudaEvent_t end_event;                         // Transfer end event
    std::chrono::system_clock::time_point submit_time; // Submission timestamp

    bool is_completed;                             // Transfer completion status
    std::chrono::microseconds transfer_time;       // Actual transfer time
    double bandwidth_gb_per_sec;                   // Achieved bandwidth
};

struct PrefetchStrategy {
    enum class Algorithm {
        ADAPTIVE,          // Adaptive prefetching based on access patterns
        SEQUENTIAL,        // Sequential prefetching
        STRIDED,           // Strided prefetching
        PREDICTIVE,        // Predictive based on history
        THRESHOLD_BASED    // Threshold-based prefetching
    };

    Algorithm algorithm;
    size_t prefetch_ahead_mb;                      // How far ahead to prefetch
    double confidence_threshold;                    // Confidence for predictive prefetching
    std::chrono::microseconds prefetch_latency;    // Expected prefetch latency
    size_t max_concurrent_prefetches;              // Concurrent prefetch limit

    bool enable_adaptive_sizing;                   // Dynamic prefetch size adjustment
    double memory_pressure_threshold;              // Prefetch reduction under pressure
    std::map<std::string, double> algorithm_weights; // Algorithm weighting
};

struct MemoryPool {
    void* pool_ptr = nullptr;
    size_t pool_size = 0;
    size_t allocated_size = 0;
    std::vector<void*> free_blocks;
    std::map<void*, size_t> allocated_blocks;
    bool is_unified_memory = false;
};

struct AsyncMemoryTransfer {
    cudaEvent_t start_event = nullptr;
    cudaEvent_t end_event = nullptr;
    void* device_ptr = nullptr;
    void* host_ptr = nullptr;
    size_t size = 0;
    cudaStream_t stream = 0;
    bool is_complete = false;
    std::chrono::high_resolution_clock::time_point submit_time;
};

class MemoryOptimizer {
public:
    explicit MemoryOptimizer(const MemoryOptimizationConfig& config = {});
    ~MemoryOptimizer();

    // Core optimization operations
    MemoryMetrics OptimizeMemoryAccess(
        const core::UInt256& start_key,
        const core::UInt256& end_key,
        const MemoryOptimizationConfig& config = {}
    );

    MemoryMetrics OptimizeMemoryTransfer(
        void* host_data,
        void* device_data,
        size_t size_bytes,
        bool is_host_to_device = true
    );

    // Asynchronous operations
    void EnableAsynchronousTransfers();
    void DisableAsynchronousTransfers();
    bool IsAsynchronousEnabled() const;

    // CUDA stream management
    int CreateStream();
    void DestroyStream(int stream_id);
    cudaStream_t GetStream(int stream_id) const;
    void SynchronizeStream(int stream_id);
    void SynchronizeAllStreams();

    // Double-buffering operations
    struct DoubleBuffer {
        void* buffer[2];                            // Two buffers for alternating
        size_t buffer_size;                         // Size of each buffer
        int active_buffer;                          // Currently active buffer (0 or 1)
        int stream_id;                              // Associated CUDA stream
        bool is_transfer_in_progress;               // Ongoing transfer status
    };

    int CreateDoubleBuffer(size_t buffer_size);
    void DestroyDoubleBuffer(int buffer_id);
    DoubleBuffer* GetDoubleBuffer(int buffer_id);
    void SwapBuffers(int buffer_id);
    void TransferToActiveBuffer(int buffer_id, const void* data, size_t size);

    // Memory pooling
    void InitializeMemoryPool(size_t pool_size_mb);
    void* AllocateFromPool(size_t size_bytes, size_t alignment = 256);
    void DeallocateToPool(void* ptr);
    size_t GetPoolUtilization() const;
    size_t GetPoolFreeMemory() const;
    void ResetPool();

    // Access pattern optimization
    AccessPattern AnalyzeAccessPattern(const void* data, size_t size_bytes);
    void OptimizeAccessPattern(const AccessPattern& pattern);
    std::vector<AccessPattern> DetectAccessPatterns(const void* data, size_t size_bytes);

    // Prefetching operations
    void SetPrefetchStrategy(const PrefetchStrategy& strategy);
    void PrefetchAsync(void* device_ptr, size_t size_bytes, int stream_id = -1);
    void PrefetchToDevice(void* host_ptr, void* device_ptr, size_t size_bytes);
    void PrefetchToHost(void* device_ptr, void* host_ptr, size_t size_bytes);

    // Transfer batching
    int QueueTransferBatch(void* host_ptr, void* device_ptr, size_t size_bytes, bool is_read);
    void ExecuteTransferBatches();
    void WaitForTransferBatch(int batch_id);
    std::vector<MemoryTransferBatch> GetTransferHistory() const;

    // Bandwidth profiling
    void EnableBandwidthProfiling();
    void DisableBandwidthProfiling();
    MemoryMetrics GetCurrentMemoryMetrics() const;
    MemoryMetrics GetAverageMemoryMetrics(std::chrono::minutes time_window = std::chrono::minutes(5)) const;
    std::vector<MemoryMetrics> GetMemoryMetricsHistory() const;

    // Performance monitoring
    double GetMemoryBandwidthUtilization() const;
    double GetCacheHitRate() const;
    double GetCoalescingEfficiency() const;
    size_t GetPeakMemoryUsage() const;
    std::chrono::microseconds GetAverageTransferTime() const;

    // Advanced optimization
    void OptimizeForKernelLaunch(size_t expected_workload_size);
    void OptimizeForDataSize(size_t data_size_bytes);
    void OptimizeForAccessPattern(const AccessPattern& pattern);
    void OptimizeForArchitecture(int compute_capability);

    // Memory access coalescing
    struct CoalescingInfo {
        bool is_coalesced;                          // Whether access is coalesced
        size_t warp_size;                           // Warp size (typically 32)
        size_t aligned_access_size;                 // Aligned access size
        size_t misaligned_penalties;                // Number of misaligned accesses
        double efficiency_score;                    // Coalescing efficiency (0.0-1.0)
        std::vector<size_t> thread_offsets;         // Per-thread memory offsets
    };

    CoalescingInfo AnalyzeCoalescing(const void* device_ptr, size_t size_bytes, int block_size);
    void OptimizeCoalescing(const CoalescingInfo& info);

    // Configuration management
    void UpdateConfiguration(const MemoryOptimizationConfig& config);
    MemoryOptimizationConfig GetCurrentConfiguration() const;
    void ResetToDefaults();

    // Validation and testing
    bool ValidateMemoryOperations(const void* test_data, size_t size_bytes);
    bool BandwidthTest(size_t test_size_mb = 1024, int num_iterations = 10);
    bool LatencyTest(size_t test_size_kb = 64, int num_iterations = 1000);

    // Analytics and reporting
    json GetPerformanceAnalytics() const;
    std::string GeneratePerformanceReport() const;
    std::string GetOptimizationRecommendations() const;
    void ExportMetrics(const std::string& filename) const;

    // Error handling
    enum class OptimizationError {
        NONE = 0,
        INSUFFICIENT_MEMORY,
        CUDA_ERROR,
        INVALID_CONFIGURATION,
        STREAM_CREATION_FAILED,
        ALLOCATION_FAILED,
        TRANSFER_FAILED,
        PREFETCH_FAILED
    };

    OptimizationError GetLastError() const;
    std::string GetErrorString(OptimizationError error) const;
    bool AttemptErrorRecovery(OptimizationError error);

    // Resource cleanup
    void Cleanup();
    void ResetMetrics();

private:
    MemoryOptimizationConfig config_;

    // CUDA resources
    std::vector<cudaStream_t> streams_;
    std::vector<cudaEvent_t> events_;
    std::map<int, DoubleBuffer> double_buffers_;

    // Memory management
    std::unique_ptr<MemoryPool> memory_pool_;
    std::unique_ptr<BandwidthProfiler> bandwidth_profiler_;

    // Transfer management
    std::vector<MemoryTransferBatch> transfer_batches_;
    std::vector<MemoryTransferBatch> completed_transfers_;
    std::queue<int> pending_batch_ids_;

    // Performance tracking
    std::vector<MemoryMetrics> metrics_history_;
    mutable std::mutex metrics_mutex_;

    // Access pattern analysis
    std::vector<AccessPattern> detected_patterns_;
    PrefetchStrategy current_prefetch_strategy_;

    // State management
    bool asynchronous_enabled_;
    bool profiling_enabled_;
    bool initialized_;
    OptimizationError last_error_;

    // Resource monitoring
    size_t total_memory_allocated_;
    size_t peak_memory_usage_;
    int active_stream_count_;

    // Internal methods
    void InitializeCudaResources();
    void CleanupCudaResources();
    bool ValidateConfiguration() const;

    // Transfer optimization
    void OptimizeTransferSize(size_t& size_bytes);
    void OptimizeTransferAlignment(void*& ptr, size_t& size_bytes);
    void OptimizeStreamAssignment(int& stream_id, size_t transfer_size);

    // Pattern analysis helpers
    AccessPattern DetectSequentialPattern(const void* data, size_t size_bytes);
    AccessPattern DetectStridedPattern(const void* data, size_t size_bytes);
    AccessPattern DetectRandomPattern(const void* data, size_t size_bytes);
    double CalculatePatternEfficiency(const AccessPattern& pattern);

    // Prefetching helpers
    void ExecuteAdaptivePrefetch(void* ptr, size_t size_bytes);
    void ExecuteSequentialPrefetch(void* ptr, size_t size_bytes);
    void ExecutePredictivePrefetch(void* ptr, size_t size_bytes);

    // Bandwidth optimization
    void OptimizeHostToDeviceBandwidth();
    void OptimizeDeviceToHostBandwidth();
    void OptimizeDeviceMemoryBandwidth();
    void OptimizeSharedMemoryUsage();

    // Metrics collection
    void RecordMemoryMetrics(const MemoryMetrics& metrics);
    MemoryMetrics CalculateCurrentMetrics() const;
    void UpdatePeakMemoryUsage(size_t current_usage);

    // Error handling helpers
    void SetError(OptimizationError error);
    bool RecoverFromCudaError();
    void LogError(OptimizationError error, const std::string& context);

    // Constants
    static constexpr size_t DEFAULT_ALIGNMENT = 256;
    static constexpr int MAX_STREAMS = 16;
    static constexpr int MAX_DOUBLE_BUFFERS = 32;
    static constexpr size_t MIN_TRANSFER_SIZE = 1024; // 1KB
    static constexpr size_t MAX_TRANSFER_SIZE = 1024 * 1024 * 1024; // 1GB
};

/**
 * @brief Factory function to create memory optimizer instance
 */
std::unique_ptr<MemoryOptimizer> CreateMemoryOptimizer(
    const MemoryOptimizationConfig& config = {}
);

} // namespace performance
} // namespace gpu
} // namespace keycuda