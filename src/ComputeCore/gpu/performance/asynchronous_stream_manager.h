#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <chrono>
#include <mutex>
#include <queue>
#include <functional>
#include <cuda_runtime.h>
#include <nlohmann/json.hpp>

namespace keycuda {
namespace gpu {
namespace performance {

/**
 * @brief Advanced asynchronous CUDA stream management for optimal overlapping transfers
 *
 * Provides comprehensive stream management for overlapping computation and memory transfers:
 * - Intelligent stream allocation and management
 * - Dependency tracking and automatic synchronization
 * - Load balancing across multiple streams
 * - Stream performance monitoring and optimization
 * - Automatic stream priority adjustment
 * - Advanced scheduling algorithms
 * - Transfer-computation overlap optimization
 */

enum class StreamPriority {
    LOW = 0,
    NORMAL = 1,
    HIGH = 2,
    CRITICAL = 3
};

enum class StreamType {
    COMPUTE,          // CUDA kernel execution
    MEMORY_TRANSFER,  // Host-device memory transfers
    PREFETCH,         // Prefetch operations
    OVERLAP,          // Overlapping compute and transfer
    SYNCHRONIZATION   // Synchronization operations
};

struct StreamConfig {
    int stream_id;
    StreamType type;
    StreamPriority priority;
    cudaStream_t cuda_stream;
    std::string name;
    bool is_dedicated;
    size_t max_concurrent_operations;
    std::chrono::microseconds timeout_duration;
    bool enable_auto_priority;
};

struct StreamOperation {
    enum class Type {
        KERNEL_LAUNCH,
        MEMORY_COPY_H2D,
        MEMORY_COPY_D2H,
        MEMORY_COPY_D2D,
        PREFETCH,
        SYNCHRONIZATION,
        CUSTOM
    };

    int operation_id;
    Type operation_type;
    std::function<void()> operation;
    std::vector<int> dependency_stream_ids;
    int stream_id;
    StreamPriority priority;
    std::chrono::system_clock::time_point submit_time;
    std::chrono::microseconds estimated_duration;

    // Callbacks
    std::function<void()> completion_callback;
    std::function<void(const std::string&)> error_callback;

    // Timing
    cudaEvent_t start_event;
    cudaEvent_t end_event;
    bool is_completed;
    bool has_error;
    std::string error_message;
};

struct StreamPerformanceMetrics {
    int stream_id;
    std::string stream_name;
    StreamType type;

    // Timing metrics
    std::chrono::microseconds total_execution_time;
    std::chrono::microseconds average_operation_time;
    std::chrono::microseconds kernel_time;
    std::chrono::microseconds transfer_time;
    std::chrono::microseconds idle_time;

    // Operation metrics
    int total_operations;
    int completed_operations;
    int failed_operations;
    double success_rate;

    // Overlap metrics
    double compute_transfer_overlap_ratio;
    double stream_utilization;
    std::chrono::microseconds total_wait_time;

    // Performance metrics
    double bandwidth_utilization;
    double occupancy_rate;
    std::vector<std::chrono::microseconds> operation_history;

    // Timestamp
    std::chrono::system_clock::time_point last_updated;
};

class AsynchronousStreamManager {
public:
    explicit AsynchronousStreamManager(
        int num_streams = 4,
        bool enable_advanced_scheduling = true,
        bool enable_performance_monitoring = true
    );

    ~AsynchronousStreamManager();

    // Stream management
    int CreateStream(
        StreamType type = StreamType::COMPUTE,
        StreamPriority priority = StreamPriority::NORMAL,
        const std::string& name = "",
        bool is_dedicated = false
    );

    void DestroyStream(int stream_id);
    bool IsValidStream(int stream_id) const;
    cudaStream_t GetCudaStream(int stream_id) const;
    const StreamConfig* GetStreamConfig(int stream_id) const;

    // Operation scheduling
    int ScheduleOperation(
        std::function<void()> operation,
        StreamType type = StreamType::COMPUTE,
        StreamPriority priority = StreamPriority::NORMAL,
        const std::vector<int>& dependencies = {},
        const std::string& operation_name = ""
    );

    int ScheduleKernelLaunch(
        std::function<void()> kernel_func,
        int stream_id = -1,
        StreamPriority priority = StreamPriority::NORMAL,
        const std::vector<int>& dependencies = {}
    );

    int ScheduleMemoryTransfer(
        void* dst,
        const void* src,
        size_t size,
        cudaMemcpyKind kind,
        int stream_id = -1,
        StreamPriority priority = StreamPriority::NORMAL,
        const std::vector<int>& dependencies = {}
    );

    int SchedulePrefetch(
        void* ptr,
        size_t size,
        int device,
        int stream_id = -1,
        StreamPriority priority = StreamPriority::NORMAL
    );

    // Advanced scheduling features
    void EnableAutoLoadBalancing(bool enabled);
    void EnableDynamicPriorityAdjustment(bool enabled);
    void SetSchedulingAlgorithm(const std::string& algorithm);
    void SetMaxConcurrentOperations(int stream_id, int max_ops);

    // Synchronization and dependency management
    void AddDependency(int operation_id, int dependency_id);
    void SynchronizeStream(int stream_id);
    void SynchronizeAllStreams();
    void WaitForOperation(int operation_id);
    void WaitForAllOperations();

    bool IsOperationCompleted(int operation_id) const;
    bool IsStreamIdle(int stream_id) const;
    bool AreAllStreamsIdle() const;

    // Performance monitoring
    StreamPerformanceMetrics GetStreamMetrics(int stream_id) const;
    std::vector<StreamPerformanceMetrics> GetAllStreamMetrics() const;
    json GetPerformanceAnalytics() const;
    std::string GeneratePerformanceReport() const;

    // Advanced performance optimization
    void OptimizeForWorkload(const std::map<StreamType, int>& workload_distribution);
    void RebalanceStreams();
    void AdjustPriorities(const std::map<int, StreamPriority>& new_priorities);

    // Batch operations
    struct BatchConfig {
        int max_batch_size = 16;
        std::chrono::microseconds batch_timeout{1000};
        bool enable_batching = true;
        bool prioritize_by_dependencies = true;
    };

    void SetBatchConfig(const BatchConfig& config);
    int ScheduleBatch(const std::vector<std::function<void()>>& operations);

    // Error handling and recovery
    enum class ErrorType {
        NONE = 0,
        CUDA_ERROR,
        TIMEOUT_ERROR,
        DEPENDENCY_ERROR,
        RESOURCE_ERROR,
        SYNCHRONIZATION_ERROR
    };

    ErrorType GetLastError() const;
    std::string GetErrorMessage() const;
    bool AttemptErrorRecovery();
    void SetErrorCallback(std::function<void(ErrorType, const std::string&)> callback);

    // Configuration
    struct ManagerConfig {
        int max_streams = 16;
        int max_operations_per_stream = 64;
        std::chrono::microseconds default_timeout{5000000}; // 5 seconds
        bool enable_profiling = true;
        bool enable_auto_recovery = true;
        bool enable_advanced_scheduling = true;
        std::string scheduling_algorithm = "priority_queue";
        double load_balance_threshold = 0.8;
    };

    void UpdateConfiguration(const ManagerConfig& config);
    ManagerConfig GetCurrentConfiguration() const;

    // Resource management
    size_t GetMemoryUsage() const;
    int GetActiveStreamCount() const;
    int GetPendingOperationCount() const;
    void ResetMetrics();
    void Cleanup();

private:
    // Core data structures
    std::map<int, StreamConfig> streams_;
    std::map<int, StreamOperation> operations_;
    std::queue<int> pending_operations_;
    std::map<int, std::vector<int>> operation_dependencies_;

    // Performance tracking
    std::map<int, StreamPerformanceMetrics> stream_metrics_;
    mutable std::mutex metrics_mutex_;

    // Configuration
    ManagerConfig config_;
    BatchConfig batch_config_;

    // State management
    bool initialized_;
    bool load_balancing_enabled_;
    bool dynamic_priority_enabled_;
    ErrorType last_error_;
    std::string last_error_message_;

    // Thread management
    std::unique_ptr<std::thread> scheduler_thread_;
    std::atomic<bool> shutdown_requested_;

    // Callbacks
    std::function<void(ErrorType, const std::string&)> error_callback_;

    // Synchronization
    mutable std::mutex streams_mutex_;
    mutable std::mutex operations_mutex_;
    std::condition_variable scheduler_cv_;

    // Internal methods
    void Initialize();
    void CleanupResources();
    int GetNextStreamId();
    int GetNextOperationId();

    // Scheduling algorithms
    void SchedulerThreadFunction();
    void ProcessPendingOperations();
    int SelectOptimalStream(StreamType type, StreamPriority priority);
    bool CanExecuteOperation(int operation_id);
    void ExecuteOperation(int operation_id);
    void MarkOperationCompleted(int operation_id, bool success, const std::string& error = "");

    // Dependency management
    bool AreDependenciesSatisfied(int operation_id) const;
    void RemoveDependency(int operation_id, int dependency_id);
    void ProcessCompletedDependencies(int operation_id);

    // Load balancing
    void PerformLoadBalancing();
    int GetLeastLoadedStream(StreamType type) const;
    void UpdateStreamLoad(int stream_id, std::chrono::microseconds duration);

    // Priority management
    void AdjustPrioritiesDynamically();
    StreamPriority CalculateOptimalPriority(int operation_id, StreamType type);

    // Performance monitoring
    void UpdateStreamMetrics(int stream_id, const StreamOperation& operation);
    void RecordOperationTiming(int operation_id, std::chrono::microseconds duration);
    void CalculateStreamUtilization(int stream_id);

    // Error handling
    void SetError(ErrorType error, const std::string& message);
    void HandleOperationError(int operation_id, const std::string& error);
    bool RecoverFromError(ErrorType error);

    // Batch processing
    void ProcessBatchOperations();
    std::vector<int> GetBatchableOperations();

    // Profiling and analytics
    void StartProfiling();
    void StopProfiling();
    void UpdateAnalytics();

    // Constants
    static constexpr int DEFAULT_STREAM_COUNT = 4;
    static constexpr int MAX_STREAMS = 64;
    static constexpr int MAX_OPERATIONS = 1024;
    static constexpr std::chrono::microseconds SCHEDULER_INTERVAL{100}; // 100 microseconds
};

/**
 * @brief Factory function to create asynchronous stream manager instance
 */
std::unique_ptr<AsynchronousStreamManager> CreateAsynchronousStreamManager(
    int num_streams = 4,
    bool enable_advanced_scheduling = true,
    bool enable_performance_monitoring = true
);

} // namespace performance
} // namespace gpu
} // namespace keycuda