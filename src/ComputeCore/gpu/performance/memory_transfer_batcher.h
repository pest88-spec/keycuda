#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <unordered_map>
#include <chrono>
#include <mutex>
#include <atomic>
#include <queue>
#include <thread>
#include <condition_variable>
#include <functional>
#include <cuda_runtime.h>
#include <nlohmann/json.hpp>

namespace keycuda {
namespace gpu {
namespace performance {

/**
 * @brief Advanced memory transfer batching system for optimizing host-device communication
 *
 * Provides comprehensive transfer batching capabilities for maximum memory bandwidth utilization:
 * - Automatic transfer grouping and batching
 * - Intelligent batch size optimization based on workload characteristics
 * - Multi-queue batch management with priority handling
 * - Adaptive batching strategies based on performance feedback
 * - Bandwidth-aware batch sizing and scheduling
 * - Transfer merging and coalescing optimization
 * - Real-time batch performance monitoring and tuning
 * - Thread-safe concurrent batch operations
 */

enum class TransferDirection {
    HOST_TO_DEVICE,         // H2D transfers
    DEVICE_TO_HOST,         // D2H transfers
    DEVICE_TO_DEVICE,       // D2D transfers
    PEER_TO_PEER,           // P2P transfers between GPUs
    UNIFIED_MEMORY_SYNC     // Unified memory synchronization
};

enum class BatchingStrategy {
    FIXED_SIZE_BATCHES,     // Fixed-size batches
    TIME_BASED_BATCHING,    // Time-based batch accumulation
    VOLUME_BASED_BATCHING,  // Volume-based batching (bytes)
    LATENCY_OPTIMIZED,      // Optimize for transfer latency
    BANDWIDTH_OPTIMIZED,    // Optimize for memory bandwidth
    ADAPTIVE_BATCHING,      // Adaptive batching based on performance
    PRIORITY_AWARE,         // Priority-based batching
    PATTERN_AWARE           // Pattern-aware batching
};

enum class BatchPriority {
    CRITICAL,               // Critical batches (immediate processing)
    HIGH,                   // High priority batches
    NORMAL,                 // Normal priority batches
    LOW,                    // Low priority batches
    BACKGROUND              // Background batches
};

enum class BatchType {
    SEQUENTIAL_BATCH,       // Sequential transfers
    CONCURRENT_BATCH,       // Concurrent transfers using multiple streams
    MERGED_BATCH,          // Merged contiguous transfers
    PIPELINED_BATCH,       // Pipelined batch processing
    HYBRID_BATCH           // Hybrid batch type
};

struct TransferRequest {
    size_t request_id;
    void* source_address;
    void* destination_address;
    size_t size;
    TransferDirection direction;
    BatchPriority priority;
    std::chrono::system_clock::time_point request_time;
    std::string tag;

    // Completion tracking
    std::function<void(size_t, bool)> completion_callback;
    std::chrono::system_clock::time_point completion_time;
    std::chrono::microseconds transfer_duration;
    bool completed_successfully;
    bool was_merged;

    // Performance metrics
    double bandwidth_achieved;
    double efficiency_percentage;
    int retry_count;

    // Batch association
    size_t batch_id;
    int position_in_batch;
};

struct TransferBatch {
    size_t batch_id;
    BatchType type;
    BatchPriority priority;
    TransferDirection direction;
    std::chrono::system_clock::time_point creation_time;
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point completion_time;

    // Transfer list
    std::vector<TransferRequest> transfers;
    size_t total_size;
    size_t total_transfers;

    // Batch configuration
    bool allow_merging;
    bool allow_pipelining;
    bool allow_concurrent_execution;
    int max_concurrent_streams;
    std::vector<cudaStream_t> streams;

    // Performance metrics
    std::chrono::microseconds batch_duration;
    double effective_bandwidth;
    double theoretical_bandwidth;
    double efficiency_percentage;
    double merging_ratio;
    std::chrono::microseconds overhead_time;

    // Optimization metrics
    bool was_optimized;
    std::string optimization_details;
    std::vector<std::string> optimization_suggestions;
};

struct BatchingMetrics {
    // Basic metrics
    size_t total_batches_created;
    size_t total_batches_completed;
    size_t total_transfers_batched;
    size_t total_bytes_batched;

    // Performance metrics
    double average_batch_size_bytes;
    double average_transfers_per_batch;
    std::chrono::microseconds average_batch_duration;
    std::chrono::microseconds min_batch_duration;
    std::chrono::microseconds max_batch_duration;

    // Efficiency metrics
    double overall_efficiency_percentage;
    double bandwidth_utilization;
    double merging_efficiency;
    double pipelining_efficiency;
    std::chrono::microseconds average_overhead;

    // Breakdown by type
    std::map<BatchType, int> batches_by_type;
    std::map<TransferDirection, int> batches_by_direction;
    std::map<BatchPriority, int> batches_by_priority;

    // Queue metrics
    std::chrono::microseconds average_queue_wait_time;
    int max_queue_depth;
    double queue_utilization;

    // Optimization impact
    double performance_improvement_percentage;
    size_t memory_saved_bytes;
    size_t transfers_merged;
    double latency_reduction_percentage;

    std::chrono::system_clock::time_point last_updated;
};

struct BatchingRecommendation {
    std::string description;
    BatchingStrategy recommended_strategy;
    BatchType optimal_batch_type;
    size_t optimal_batch_size;
    double expected_improvement;
    std::chrono::microseconds implementation_effort;
    int priority;

    // Specific recommendations
    bool suggest_batch_size_optimization;
    bool suggest_stream_count_optimization;
    bool suggest_merging_optimization;
    bool suggest_pipelining_optimization;

    std::vector<std::string> action_items;
};

class MemoryTransferBatcher {
public:
    explicit MemoryTransferBatcher(int device_id = 0);
    ~MemoryTransferBatcher();

    // Initialization and cleanup
    bool Initialize(BatchingStrategy strategy = BatchingStrategy::ADAPTIVE_BATCHING);
    void Cleanup();
    bool IsInitialized() const;

    // Batching control
    void EnableBatching(bool enabled);
    bool IsBatchingEnabled() const;
    void SetBatchingStrategy(BatchingStrategy strategy);
    BatchingStrategy GetBatchingStrategy() const;

    // Transfer submission
    size_t SubmitTransfer(
        void* source,
        void* destination,
        size_t size,
        TransferDirection direction,
        BatchPriority priority = BatchPriority::NORMAL,
        const std::string& tag = ""
    );

    size_t SubmitTransferWithCallback(
        void* source,
        void* destination,
        size_t size,
        TransferDirection direction,
        std::function<void(size_t, bool)> callback,
        BatchPriority priority = BatchPriority::NORMAL,
        const std::string& tag = ""
    );

    // Batch submission
    size_t SubmitBatch(
        const std::vector<std::tuple<void*, void*, size_t, TransferDirection>>& transfers,
        BatchType type = BatchType::SEQUENTIAL_BATCH,
        BatchPriority priority = BatchPriority::NORMAL
    );

    // Batch management
    void StartBatchProcessing();
    void StopBatchProcessing();
    void PauseBatchProcessing();
    void ResumeBatchProcessing();
    bool IsBatchProcessingActive() const;

    // Batch optimization
    bool OptimizeBatch(size_t batch_id);
    bool OptimizeAllBatches();
    void EnableAutoOptimization(bool enabled);
    void SetOptimizationInterval(std::chrono::seconds interval);

    // Batching parameters
    void SetMaxBatchSize(size_t max_size_bytes);
    void SetMaxTransfersPerBatch(int max_transfers);
    void SetBatchTimeout(std::chrono::milliseconds timeout);
    void SetMaxConcurrentBatches(int max_batches);

    size_t GetMaxBatchSize() const;
    int GetMaxTransfersPerBatch() const;
    std::chrono::milliseconds GetBatchTimeout() const;
    int GetMaxConcurrentBatches() const;

    // Stream management
    void SetStreamCount(int stream_count);
    int GetStreamCount() const;
    std::vector<cudaStream_t> GetStreams() const;

    // Batch monitoring
    TransferBatch GetBatchInfo(size_t batch_id) const;
    std::vector<TransferBatch> GetActiveBatches() const;
    std::vector<TransferBatch> GetCompletedBatches() const;
    std::vector<TransferRequest> GetPendingTransfers() const;

    // Performance metrics
    BatchingMetrics GetBatchingMetrics() const;
    double GetAverageBatchEfficiency() const;
    std::chrono::microseconds GetAverageBatchDuration() const;
    double GetBandwidthUtilization() const;

    // Transfer tracking
    TransferRequest GetTransferInfo(size_t request_id) const;
    bool IsTransferComplete(size_t request_id) const;
    std::chrono::microseconds GetTransferDuration(size_t request_id) const;
    double GetTransferEfficiency(size_t request_id) const;

    // Queue management
    void CancelTransfer(size_t request_id);
    void CancelBatch(size_t batch_id);
    void SetTransferPriority(size_t request_id, BatchPriority priority);
    void SetBatchPriority(size_t batch_id, BatchPriority priority);

    // Batch types
    void EnableMerging(bool enabled);
    void EnablePipelining(bool enabled);
    void EnableConcurrentExecution(bool enabled);

    // Adaptive batching
    void EnableAdaptiveBatching(bool enabled);
    void SetAdaptationThreshold(double threshold);
    void UpdateBatchingBasedOnPerformance();

    // Analytics and reporting
    json GetBatchingAnalytics() const;
    std::string GenerateBatchingReport() const;
    void ExportBatchingData(const std::string& filename) const;
    void ExportTransferHistory(const std::string& filename) const;

    // Optimization recommendations
    std::vector<BatchingRecommendation> GetOptimizationRecommendations() const;
    bool ApplyOptimizationRecommendations();

    // Configuration
    struct BatcherConfig {
        BatchingStrategy default_strategy = BatchingStrategy::ADAPTIVE_BATCHING;
        bool enable_auto_optimization = true;
        bool enable_merging = true;
        bool enable_pipelining = true;
        bool enable_concurrent_execution = true;
        bool enable_adaptive_batching = true;

        // Batch parameters
        size_t max_batch_size_bytes = 64 * 1024 * 1024; // 64MB
        int max_transfers_per_batch = 32;
        std::chrono::milliseconds batch_timeout{10}; // 10ms
        int max_concurrent_batches = 8;

        // Stream configuration
        int stream_count = 4;
        bool enable_stream_pooling = true;
        std::chrono::seconds stream_timeout{30}; // 30 seconds

        // Performance targets
        double target_efficiency = 0.85; // 85% target efficiency
        double target_bandwidth_utilization = 0.80; // 80% bandwidth utilization
        std::chrono::microseconds max_acceptable_latency{5000}; // 5ms max latency

        // Adaptive parameters
        double adaptation_threshold = 0.1; // 10% performance change threshold
        std::chrono::seconds adaptation_interval{60}; // 1 minute
        int min_samples_for_adaptation = 100;

        // Merging parameters
        size_t min_merge_size = 4096; // 4KB minimum merge size
        double merge_efficiency_threshold = 0.7; // 70% merge efficiency threshold
        size_t max_merge_gap = 1024; // 1KB maximum gap for merging

        // Pipelining parameters
        int pipeline_depth = 3;
        std::chrono::microseconds min_pipeline_overlap{100}; // 100μs minimum overlap
        double pipeline_efficiency_threshold = 0.8;

        // Priority weights
        double critical_priority_weight = 10.0;
        double high_priority_weight = 5.0;
        double normal_priority_weight = 1.0;
        double low_priority_weight = 0.5;
        double background_priority_weight = 0.1;
    };

    void UpdateConfiguration(const BatcherConfig& config);
    BatcherConfig GetCurrentConfiguration() const;

    // Error handling
    enum class ErrorType {
        NONE = 0,
        INITIALIZATION_FAILED,
        INVALID_POINTER,
        OUT_OF_MEMORY,
        CUDA_ERROR,
        BATCH_CREATION_FAILED,
        TRANSFER_FAILED,
        OPTIMIZATION_FAILED,
        CONFIGURATION_ERROR,
        QUEUE_FULL,
        STREAM_CREATION_FAILED
    };

    ErrorType GetLastError() const;
    std::string GetErrorMessage() const;
    bool AttemptErrorRecovery();

private:
    int device_id_;
    BatcherConfig config_;
    BatchingStrategy current_strategy_;
    bool initialized_;
    bool batching_enabled_;
    bool batch_processing_active_;

    // Device properties
    cudaDeviceProp device_properties_;
    size_t memory_bandwidth_gb_per_sec_;

    // Transfer queues
    std::map<BatchPriority, std::queue<TransferRequest>> pending_transfers_;
    std::queue<TransferBatch> batch_queue_;
    std::map<size_t, TransferBatch> active_batches_;
    std::map<size_t, TransferBatch> completed_batches_;

    // Request tracking
    std::atomic<size_t> next_request_id_;
    std::atomic<size_t> next_batch_id_;
    std::map<size_t, TransferRequest> transfer_requests_;
    std::map<size_t, TransferBatch> transfer_batches_;

    // Stream management
    std::vector<cudaStream_t> streams_;
    std::queue<cudaStream_t> available_streams_;
    std::mutex streams_mutex_;

    // Performance metrics
    BatchingMetrics metrics_;
    std::vector<std::pair<std::chrono::system_clock::time_point, double>> performance_history_;
    std::chrono::system_clock::time_point last_optimization_time_;

    // Batch optimization
    bool auto_optimization_enabled_;
    std::chrono::seconds optimization_interval_;
    std::unique_ptr<std::thread> optimization_thread_;

    // Batch processing
    std::unique_ptr<std::thread> batch_processing_thread_;
    std::atomic<bool> shutdown_requested_;
    std::condition_variable batch_cv_;
    std::mutex batch_mutex_;

    // Adaptive batching
    bool adaptive_batching_enabled_;
    double adaptation_threshold_;
    std::vector<std::pair<size_t, double>> batch_performance_history_;

    // Thread safety
    mutable std::mutex pending_mutex_;
    mutable std::mutex active_mutex_;
    mutable std::mutex completed_mutex_;
    mutable std::mutex metrics_mutex_;

    // Error handling
    ErrorType last_error_;
    std::string last_error_message_;

    // Internal methods
    bool InitializeDeviceProperties();
    bool InitializeStreams();

    // Batch creation and management
    size_t CreateBatch(const std::vector<TransferRequest>& transfers, BatchType type, BatchPriority priority);
    TransferBatch CreateBatchInternal(const std::vector<TransferRequest>& transfers, BatchType type, BatchPriority priority);
    void ProcessBatch(TransferBatch& batch);
    void CompleteBatch(TransferBatch& batch);

    // Batch execution strategies
    void ExecuteSequentialBatch(TransferBatch& batch);
    void ExecuteConcurrentBatch(TransferBatch& batch);
    void ExecuteMergedBatch(TransferBatch& batch);
    void ExecutePipelinedBatch(TransferBatch& batch);

    // Transfer execution
    bool ExecuteTransfer(TransferRequest& request, cudaStream_t stream = 0);
    bool ExecuteTransferAsync(TransferRequest& request, cudaStream_t stream);

    // Batch optimization
    bool OptimizeBatchSize(TransferBatch& batch);
    bool OptimizeBatchOrdering(TransferBatch& batch);
    bool MergeContiguousTransfers(TransferBatch& batch);
    bool OptimizeStreamAssignment(TransferBatch& batch);

    // Batching strategies
    std::vector<TransferRequest> CreateBatchBySize(BatchPriority priority);
    std::vector<TransferRequest> CreateBatchByTime(BatchPriority priority);
    std::vector<TransferRequest> CreateBatchByVolume(BatchPriority priority);
    std::vector<TransferRequest> CreateAdaptiveBatch(BatchPriority priority);

    // Transfer merging
    bool CanMergeTransfers(const TransferRequest& a, const TransferRequest& b) const;
    TransferRequest MergeTransfers(const TransferRequest& a, const TransferRequest& b) const;
    std::vector<TransferRequest> OptimizeTransferList(std::vector<TransferRequest> transfers);

    // Stream management
    cudaStream_t GetAvailableStream();
    void ReturnStream(cudaStream_t stream);
    void CleanupStreams();

    // Performance monitoring
    void UpdateBatchingMetrics(const TransferBatch& batch);
    void CalculatePerformanceMetrics();
    void RecordBatchPerformance(size_t batch_id, double efficiency);

    // Adaptive batching
    void UpdateBatchingStrategy();
    void AnalyzeBatchPerformance();
    void AdaptBatchParameters();

    // Background processing
    void BatchProcessingThreadFunction();
    void OptimizationThreadFunction();

    // Queue management
    void AddToPendingQueue(const TransferRequest& request);
    TransferRequest GetNextTransfer(BatchPriority priority);
    bool HasPendingTransfers() const;
    void PrioritizeQueues();

    // Utility methods
    std::string GetDirectionName(TransferDirection direction) const;
    std::string GetBatchTypeName(BatchType type) const;
    std::string GetPriorityName(BatchPriority priority) const;
    std::string GetStrategyName(BatchingStrategy strategy) const;
    double CalculatePriorityWeight(BatchPriority priority) const;

    // Error handling
    void SetError(ErrorType error, const std::string& message);
    bool RecoverFromError(ErrorType error);

    // Constants
    static constexpr size_t DEFAULT_MAX_BATCH_SIZE = 64 * 1024 * 1024; // 64MB
    static constexpr int DEFAULT_MAX_TRANSFERS_PER_BATCH = 32;
    static constexpr std::chrono::milliseconds DEFAULT_BATCH_TIMEOUT{10};
    static constexpr int DEFAULT_STREAM_COUNT = 4;
    static constexpr size_t MIN_TRANSFER_SIZE = 1; // 1 byte minimum
    static constexpr double DEFAULT_TARGET_EFFICIENCY = 0.85;
};

/**
 * @brief Factory function to create memory transfer batcher instance
 */
std::unique_ptr<MemoryTransferBatcher> CreateMemoryTransferBatcher(
    int device_id = 0
);

} // namespace performance
} // namespace gpu
} // namespace keycuda