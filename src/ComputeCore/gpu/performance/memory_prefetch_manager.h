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
 * @brief Advanced memory prefetching system for optimizing repeated access patterns
 *
 * Provides comprehensive prefetching capabilities for optimal memory access performance:
 * - Automatic access pattern detection and analysis
 * - Intelligent prefetch scheduling based on usage patterns
 * - Multi-level cache hierarchy optimization (L1, L2, HBM)
 * - Prefetch queue management with priority handling
 * - Adaptive prefetch strategies based on workload characteristics
 * - Bandwidth-aware prefetch throttling
 * - Prefetch effectiveness monitoring and optimization
 * - Thread-safe concurrent prefetch operations
 */

enum class PrefetchStrategy {
    ALWAYS_PREFETCH,        // Always prefetch detected patterns
    ADAPTIVE_PREFETCH,      // Adapt based on performance impact
    BANDWIDTH_AWARE,        // Consider available bandwidth
    LATENCY_AWARE,          // Prioritize high-latency accesses
    PATTERN_BASED,          // Prefetch based on detected patterns
    PREDICTIVE_PREFETCH,    // ML-based prediction of future accesses
    CONSERVATIVE_PREFETCH,  // Prefetch only when confidence is high
    AGGRESSIVE_PREFETCH     // Prefetch aggressively for maximum performance
};

enum class AccessPatternType {
    SEQUENTIAL,             // Sequential memory access
    STRIDED,               // Strided access with fixed stride
    RANDOM,                // Random access patterns
    IRREGULAR,             // Irregular access patterns
    RECURRING,             // Recurring access to same locations
temporal_LOCALITY,        // Temporal locality patterns
    SPATIAL_LOCALITY,      // Spatial locality patterns
    HYBRID_PATTERN         // Combination of multiple patterns
};

enum class PrefetchPriority {
    CRITICAL,              // Critical prefetches (immediate need)
    HIGH,                  // High priority prefetches
    NORMAL,                // Normal priority prefetches
    LOW,                   // Low priority prefetches
    BACKGROUND             // Background prefetches
};

struct AccessPattern {
    AccessPatternType type;
    void* base_address;
    size_t region_size;
    size_t stride_size;
    int access_frequency;
    double confidence_score;
    std::chrono::microseconds average_interval;
    std::chrono::system_clock::time_point last_access;
    std::chrono::system_clock::time_point next_predicted_access;

    // Pattern characteristics
    std::vector<size_t> access_offsets;
    std::vector<std::chrono::system_clock::time_point> access_timestamps;
    double spatial_locality_score;
    double temporal_locality_score;
    size_t unique_addresses_accessed;

    // Performance impact
    std::chrono::microseconds average_latency_without_prefetch;
    std::chrono::microseconds average_latency_with_prefetch;
    double prefetch_effectiveness;
};

struct PrefetchRequest {
    size_t request_id;
    void* source_address;
    void* destination_address;
    size_t size;
    PrefetchPriority priority;
    AccessPatternType pattern_type;
    std::chrono::system_clock::time_point request_time;
    std::chrono::system_clock::time_point deadline;

    // Performance metrics
    std::chrono::microseconds prefetch_latency;
    bool completed_successfully;
    bool was_useful;          // Did the prefetch get used before eviction?
    std::chrono::system_clock::time_point completion_time;

    // Metadata
    std::string source_tag;
    std::string destination_tag;
    double confidence_score;
    int retry_count;
    bool is_async;
};

struct PrefetchMetrics {
    // Basic metrics
    int total_prefetch_requests;
    int successful_prefetches;
    int failed_prefetches;
    int useful_prefetches;
    int wasted_prefetches;

    // Performance metrics
    double prefetch_success_rate;
    double prefetch_usefulness_rate;
    std::chrono::microseconds average_prefetch_latency;
    std::chrono::microseconds min_prefetch_latency;
    std::chrono::microseconds max_prefetch_latency;

    // Bandwidth metrics
    double prefetch_bandwidth_utilization;
    double total_prefetched_bytes;
    double useful_prefetched_bytes;
    double wasted_prefetched_bytes;

    // Pattern analysis
    std::map<AccessPatternType, int> prefetches_by_pattern;
    std::map<PrefetchPriority, int> prefetches_by_priority;
    std::vector<AccessPattern> detected_patterns;

    // Impact metrics
    double overall_performance_improvement;
    double cache_hit_rate_improvement;
    double memory_latency_reduction;

    // Queue metrics
    int average_queue_depth;
    int max_queue_depth;
    std::chrono::microseconds average_queue_wait_time;

    std::chrono::system_clock::time_point last_updated;
};

struct PrefetchRecommendation {
    std::string description;
    PrefetchStrategy recommended_strategy;
    AccessPatternType target_pattern;
    double expected_improvement;
    double confidence_level;
    std::chrono::microseconds implementation_effort;
    int priority;

    // Specific recommendations
    bool suggest_pattern_detection;
    bool suggest_queue_optimization;
    bool suggest_bandwidth_throttling;
    bool suggest_priority_adjustment;

    std::vector<std::string> action_items;
};

class MemoryPrefetchManager {
public:
    explicit MemoryPrefetchManager(int device_id = 0);
    ~MemoryPrefetchManager();

    // Initialization and cleanup
    bool Initialize(PrefetchStrategy strategy = PrefetchStrategy::ADAPTIVE_PREFETCH);
    void Cleanup();
    bool IsInitialized() const;

    // Prefetch control
    void EnablePrefetching(bool enabled);
    bool IsPrefetchingEnabled() const;
    void SetPrefetchStrategy(PrefetchStrategy strategy);
    PrefetchStrategy GetPrefetchStrategy() const;

    // Access pattern tracking
    void RecordAccess(
        void* address,
        size_t size,
        AccessPatternType pattern_hint = AccessPatternType::RANDOM
    );

    void RecordSequentialAccess(void* address, size_t size);
    void RecordStridedAccess(void* address, size_t size, size_t stride);
    void RecordRandomAccess(void* address, size_t size);
    void RecordRecurringAccess(void* address, size_t size);

    // Pattern detection
    std::vector<AccessPattern> DetectAccessPatterns(
        const std::vector<std::pair<void*, size_t>>& access_history
    );

    AccessPattern AnalyzeAccessPattern(
        void* base_address,
        size_t region_size,
        std::chrono::seconds time_window = std::chrono::seconds(10)
    );

    // Prefetch operations
    size_t PrefetchToGPU(
        void* host_ptr,
        void* device_ptr,
        size_t size,
        PrefetchPriority priority = PrefetchPriority::NORMAL,
        const std::string& tag = ""
    );

    size_t PrefetchToL2Cache(
        void* device_ptr,
        size_t size,
        PrefetchPriority priority = PrefetchPriority::NORMAL
    );

    size_t PrefetchAsync(
        void* source_ptr,
        void* destination_ptr,
        size_t size,
        cudaStream_t stream,
        PrefetchPriority priority = PrefetchPriority::NORMAL
    );

    // Pattern-based prefetching
    void PrefetchBasedOnPattern(
        const AccessPattern& pattern,
        PrefetchPriority priority = PrefetchPriority::NORMAL
    );

    void PrefetchPredictiveAccesses(
        std::chrono::seconds time_horizon = std::chrono::seconds(5)
    );

    // Prefetch queue management
    void CancelPrefetch(size_t request_id);
    void CancelPrefetchesByTag(const std::string& tag);
    void SetPrefetchPriority(size_t request_id, PrefetchPriority priority);
    PrefetchRequest GetPrefetchRequest(size_t request_id) const;

    // Batch prefetching
    std::vector<size_t> BatchPrefetch(
        const std::vector<std::tuple<void*, void*, size_t>>& prefetch_list,
        PrefetchPriority priority = PrefetchPriority::NORMAL
    );

    // Prefetch monitoring
    bool IsPrefetchComplete(size_t request_id) const;
    std::chrono::microseconds GetPrefetchLatency(size_t request_id) const;
    bool WasPrefetchUseful(size_t request_id) const;

    // Performance metrics
    PrefetchMetrics GetPrefetchMetrics() const;
    double GetPrefetchSuccessRate() const;
    double GetPrefetchUsefulnessRate() const;
    std::chrono::microseconds GetAveragePrefetchLatency() const;

    // Pattern analysis
    std::vector<AccessPattern> GetDetectedPatterns() const;
    std::map<AccessPatternType, int> GetPatternFrequency() const;
    AccessPattern GetMostFrequentPattern() const;

    // Optimization recommendations
    std::vector<PrefetchRecommendation> GetOptimizationRecommendations() const;
    bool OptimizePrefetchStrategy();
    void UpdatePrefetchConfiguration();

    // Bandwidth management
    void SetMaxPrefetchBandwidth(double bandwidth_gb_per_sec);
    double GetMaxPrefetchBandwidth() const;
    double GetCurrentPrefetchBandwidth() const;
    void EnableBandwidthThrottling(bool enabled);

    // Cache-aware prefetching
    void EnableCacheAwarePrefetching(bool enabled);
    void SetCacheSize(size_t l1_size, size_t l2_size);
    void PrefetchToCacheLevel(
        void* address,
        size_t size,
        int cache_level, // 1 for L1, 2 for L2
        PrefetchPriority priority = PrefetchPriority::NORMAL
    );

    // Adaptive prefetching
    void EnableAdaptivePrefetching(bool enabled);
    void SetAdaptationThreshold(double threshold);
    void UpdatePrefetchEffectiveness(size_t request_id, bool was_useful);

    // Prefetch scheduling
    void SchedulePeriodicPrefetch(
        void* address,
        size_t size,
        std::chrono::seconds interval,
        PrefetchPriority priority = PrefetchPriority::LOW
    );

    void CancelPeriodicPrefetch(void* address);

    // Configuration
    struct PrefetchConfig {
        PrefetchStrategy default_strategy = PrefetchStrategy::ADAPTIVE_PREFETCH;
        bool enable_pattern_detection = true;
        bool enable_predictive_prefetch = false;
        bool enable_adaptive_prefetching = true;
        bool enable_bandwidth_throttling = true;
        bool enable_cache_aware_prefetching = true;

        // Performance tuning
        int max_prefetch_queue_depth = 64;
        int max_concurrent_prefetches = 8;
        double max_prefetch_bandwidth_gb_per_sec = 50.0; // 50 GB/s
        std::chrono::microseconds min_prefetch_interval{100}; // 100 μs

        // Pattern detection
        std::chrono::seconds pattern_detection_window{10}; // 10 seconds
        int min_accesses_for_pattern = 5;
        double pattern_confidence_threshold = 0.7;
        int max_tracked_patterns = 100;

        // Prefetch parameters
        std::chrono::seconds prefetch_horizon{5}; // 5 seconds
        double prefetch_usefulness_threshold = 0.5;
        std::chrono::seconds max_prefetch_age{30}; // 30 seconds

        // Cache configuration
        size_t l1_cache_size = 128 * 1024; // 128KB
        size_t l2_cache_size = 40 * 1024 * 1024; // 40MB
        double cache_utilization_threshold = 0.8;

        // Adaptive parameters
        double adaptation_threshold = 0.1; // 10% performance change threshold
        std::chrono::seconds adaptation_interval{60}; // 1 minute
        int min_samples_for_adaptation = 100;

        // Priority weights
        double critical_priority_weight = 10.0;
        double high_priority_weight = 5.0;
        double normal_priority_weight = 1.0;
        double low_priority_weight = 0.5;
        double background_priority_weight = 0.1;
    };

    void UpdateConfiguration(const PrefetchConfig& config);
    PrefetchConfig GetCurrentConfiguration() const;

    // Analytics and reporting
    json GetPrefetchAnalytics() const;
    std::string GeneratePrefetchReport() const;
    void ExportPrefetchData(const std::string& filename) const;
    void ExportPatternData(const std::string& filename) const;

    // Error handling
    enum class ErrorType {
        NONE = 0,
        INITIALIZATION_FAILED,
        INVALID_POINTER,
        OUT_OF_MEMORY,
        CUDA_ERROR,
        PREFETCH_FAILED,
        PATTERN_DETECTION_FAILED,
        CONFIGURATION_ERROR,
        QUEUE_FULL,
        BANDWIDTH_EXCEEDED
    };

    ErrorType GetLastError() const;
    std::string GetErrorMessage() const;
    bool AttemptErrorRecovery();

private:
    int device_id_;
    PrefetchConfig config_;
    PrefetchStrategy current_strategy_;
    bool initialized_;
    bool prefetching_enabled_;

    // Device properties
    cudaDeviceProp device_properties_;
    size_t l2_cache_size_;
    size_t memory_bandwidth_gb_per_sec_;

    // Access pattern tracking
    std::map<void*, std::vector<std::pair<std::chrono::system_clock::time_point, size_t>>> access_history_;
    std::vector<AccessPattern> detected_patterns_;
    std::map<AccessPatternType, int> pattern_frequency_;

    // Prefetch queue management
    std::map<size_t, PrefetchRequest> prefetch_requests_;
    std::queue<size_t> prefetch_queue_;
    std::map<PrefetchPriority, std::queue<size_t>> priority_queues_;
    std::atomic<size_t> next_request_id_;

    // Active prefetches
    std::map<cudaStream_t, std::vector<size_t>> stream_prefetches_;
    std::map<void*, size_t> address_to_request_;

    // Periodic prefetches
    struct PeriodicPrefetch {
        void* address;
        size_t size;
        std::chrono::seconds interval;
        PrefetchPriority priority;
        std::chrono::system_clock::time_point next_prefetch_time;
        size_t request_id;
        bool active;
    };
    std::vector<PeriodicPrefetch> periodic_prefetches_;

    // Performance metrics
    PrefetchMetrics metrics_;
    std::vector<std::pair<std::chrono::system_clock::time_point, double>> performance_history_;
    std::chrono::system_clock::time_point last_adaptation_time_;

    // Bandwidth management
    bool bandwidth_throttling_enabled_;
    double current_prefetch_bandwidth_;
    double total_prefetched_bytes_;
    std::chrono::system_clock::time_point bandwidth_measurement_start_;

    // Cache management
    bool cache_aware_prefetching_enabled_;
    size_t l1_cache_size_;
    size_t l2_cache_size_;
    std::map<void*, size_t> cached_data_;

    // Adaptive prefetching
    bool adaptive_prefetching_enabled_;
    double adaptation_threshold_;
    std::vector<std::pair<size_t, bool>> prefetch_effectiveness_history_;

    // Thread safety
    mutable std::mutex access_mutex_;
    mutable std::mutex queue_mutex_;
    mutable std::mutex metrics_mutex_;
    mutable std::mutex patterns_mutex_;

    // Background processing
    std::unique_ptr<std::thread> prefetch_thread_;
    std::unique_ptr<std::thread> pattern_detection_thread_;
    std::atomic<bool> shutdown_requested_;
    std::condition_variable queue_cv_;

    // Error handling
    ErrorType last_error_;
    std::string last_error_message_;

    // Internal methods
    bool InitializeDeviceProperties();
    void InitializePrefetchQueues();

    // Access pattern detection
    AccessPatternType DetectPatternType(
        const std::vector<std::pair<std::chrono::system_clock::time_point, size_t>>& accesses
    );

    size_t DetectStride(const std::vector<std::pair<std::chrono::system_clock::time_point, size_t>>& accesses);
    double CalculateConfidenceScore(const AccessPattern& pattern);
    void UpdateDetectedPatterns(const AccessPattern& pattern);

    // Prefetch execution
    void ExecutePrefetch(PrefetchRequest& request);
    void ExecuteAsyncPrefetch(PrefetchRequest& request, cudaStream_t stream);
    void ExecuteGPUPrefetch(PrefetchRequest& request);
    void ExecuteL2Prefetch(PrefetchRequest& request);

    // Queue management
    void EnqueuePrefetch(const PrefetchRequest& request);
    PrefetchRequest DequeuePrefetch();
    void ProcessPrefetchQueue();
    void UpdateQueueMetrics();

    // Prefetch scheduling
    void SchedulePrefetch(const PrefetchRequest& request);
    void CancelPrefetchInternal(size_t request_id);
    void PrioritizeQueue();

    // Bandwidth management
    void UpdateBandwidthUsage();
    bool CanExecutePrefetch(size_t size) const;
    void ThrottlePrefetchIfNeeded();

    // Cache management
    bool CanFitInCache(size_t size, int cache_level) const;
    void UpdateCacheUsage(void* address, size_t size);
    void EvictFromCacheIfNeeded(size_t required_space);

    // Adaptive prefetching
    void UpdatePrefetchStrategy();
    void AnalyzePrefetchEffectiveness();
    void AdaptStrategyBasedOnPerformance();

    // Periodic prefetch management
    void ProcessPeriodicPrefetches();
    void ScheduleNextPeriodicPrefetch(PeriodicPrefetch& prefetch);

    // Background threads
    void PrefetchThreadFunction();
    void PatternDetectionThreadFunction();

    // Performance monitoring
    void UpdatePrefetchMetrics(const PrefetchRequest& request);
    void CalculatePerformanceMetrics();
    void RecordPrefetchLatency(std::chrono::microseconds latency);

    // Utility methods
    std::string GetPatternTypeName(AccessPatternType type) const;
    std::string GetPriorityName(PrefetchPriority priority) const;
    std::string GetStrategyName(PrefetchStrategy strategy) const;
    double CalculatePriorityWeight(PrefetchPriority priority) const;

    // Error handling
    void SetError(ErrorType error, const std::string& message);
    bool RecoverFromError(ErrorType error);

    // Constants
    static constexpr int MAX_ACCESS_HISTORY_SIZE = 10000;
    static constexpr int MAX_DETECTED_PATTERNS = 100;
    static constexpr int MAX_PREFETCH_REQUESTS = 1000;
    static constexpr std::chrono::seconds DEFAULT_PATTERN_DETECTION_WINDOW{10};
    static constexpr double DEFAULT_CONFIDENCE_THRESHOLD = 0.7;
};

/**
 * @brief Factory function to create memory prefetch manager instance
 */
std::unique_ptr<MemoryPrefetchManager> CreateMemoryPrefetchManager(
    int device_id = 0
);

} // namespace performance
} // namespace gpu
} // namespace keycuda