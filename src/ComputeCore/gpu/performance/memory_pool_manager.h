#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <unordered_map>
#include <chrono>
#include <mutex>
#include <atomic>
#include <set>
#include <queue>
#include <cuda_runtime.h>
#include <nlohmann/json.hpp>

namespace keycuda {
namespace gpu {
namespace performance {

/**
 * @brief Advanced memory pooling system with intelligent allocation optimization
 *
 * Provides comprehensive memory pooling capabilities for optimal GPU memory management:
 * - Automatic pool allocation and management
 * - Intelligent size-based pooling with fragmentation minimization
 * - Multi-tier memory hierarchy (small, medium, large pools)
 * - Dynamic pool resizing and rebalancing
 * - Memory usage analytics and optimization recommendations
 * - Zero-copy and unified memory support
 * - Garbage collection and memory defragmentation
 * - Thread-safe concurrent allocation and deallocation
 */

enum class MemoryType {
    DEVICE_MEMORY,        // Standard device memory
    HOST_PINNED_MEMORY,   // Pinned host memory
    UNIFIED_MEMORY,       // Unified memory (CUDA 6.0+)
    MANAGED_MEMORY,       // Managed memory
    ZERO_COPY_MEMORY,     // Zero-copy memory
    ARRAY_MEMORY,         // CUDA array memory
    TEXTURE_MEMORY,       // Texture memory
    CONSTANT_MEMORY       // Constant memory
};

enum class PoolStrategy {
    FIXED_SIZE_POOLS,     // Fixed-size pools for common sizes
    POWER_OF_TWO_POOLS,   // Power-of-two sized pools
    ADAPTIVE_POOLS,       // Adaptive sizing based on usage
    TIERED_POOLS,         // Multi-tier pools (small/medium/large)
    SEGREGATED_POOLS,     // Segregated by size ranges
    BUDDY_SYSTEM,         // Buddy system allocation
    SLAB_ALLOCATOR,       // Slab allocator pattern
    HYBRID_STRATEGY       // Combination of strategies
};

enum class AllocationStrategy {
    BEST_FIT,            // Best fit allocation
    FIRST_FIT,           // First fit allocation
    WORST_FIT,           // Worst fit allocation
    NEXT_FIT,            // Next fit allocation
    BUDDY_ALLOCATION,    // Buddy system allocation
    SLAB_ALLOCATION,     // Slab allocation
    ADAPTIVE_FIT,        // Adaptive fitting based on patterns
    PREDICTIVE_FIT       // Predictive allocation based on history
};

struct PoolConfig {
    size_t pool_id;
    MemoryType memory_type;
    size_t block_size;
    size_t block_count;
    size_t total_size;
    size_t allocated_blocks;
    size_t free_blocks;
    double utilization_percentage;

    // Performance metrics
    std::chrono::system_clock::time_point last_access;
    int allocation_count;
    int deallocation_count;
    std::chrono::microseconds average_allocation_time;
    std::chrono::microseconds average_deallocation_time;

    // Pool characteristics
    bool allow_growth;
    bool allow_shrinking;
    size_t max_block_count;
    size_t min_block_count;
    double growth_factor;
    double shrink_threshold;

    std::string name;
    std::chrono::system_clock::time_point created_time;
};

struct AllocationInfo {
    void* pointer;
    size_t size;
    size_t pool_id;
    MemoryType memory_type;
    std::chrono::system_clock::time_point allocation_time;
    std::chrono::system_clock::time_point last_access_time;
    int access_count;
    bool is_active;

    // Metadata
    std::string allocation_tag;
    std::string owner_name;
    int priority;
    bool can_be_reclaimed;

    // Performance tracking
    std::chrono::microseconds allocation_duration;
    std::chrono::microseconds deallocation_duration;
};

struct PoolMetrics {
    size_t total_pools;
    size_t total_memory_bytes;
    size_t allocated_memory_bytes;
    size_t free_memory_bytes;
    size_t reserved_memory_bytes;

    // Efficiency metrics
    double overall_utilization;
    double fragmentation_percentage;
    double allocation_efficiency;
    double deallocation_efficiency;

    // Performance metrics
    std::chrono::microseconds average_allocation_time;
    std::chrono::microseconds average_deallocation_time;
    int allocations_per_second;
    int deallocations_per_second;

    // Pool breakdown
    std::map<MemoryType, size_t> memory_by_type;
    std::map<size_t, size_t> blocks_by_size;
    std::map<int, int> pools_by_utilization;

    // History
    std::vector<std::pair<std::chrono::system_clock::time_point, double>> utilization_history;
    std::vector<std::pair<std::chrono::system_clock::time_point, size_t>> allocation_count_history;

    std::chrono::system_clock::time_point last_updated;
};

struct MemoryPoolRecommendation {
    std::string description;
    PoolStrategy recommended_strategy;
    std::vector<size_t> recommended_pool_sizes;
    double expected_improvement;
    std::chrono::microseconds implementation_effort;
    int priority;

    // Specific recommendations
    bool suggest_pool_creation;
    bool suggest_pool_merging;
    bool suggest_pool_resizing;
    bool suggest_defragmentation;

    std::vector<std::string> action_items;
};

class MemoryPoolManager {
public:
    explicit MemoryPoolManager(int device_id = 0);
    ~MemoryPoolManager();

    // Initialization and cleanup
    bool Initialize(PoolStrategy strategy = PoolStrategy::ADAPTIVE_POOLS);
    void Cleanup();
    bool IsInitialized() const;

    // Pool management
    size_t CreatePool(
        MemoryType memory_type,
        size_t block_size,
        size_t initial_block_count,
        const std::string& pool_name = "",
        bool allow_growth = true
    );

    bool DestroyPool(size_t pool_id);
    bool ResizePool(size_t pool_id, size_t new_block_count);
    void SetPoolStrategy(PoolStrategy strategy);
    PoolStrategy GetPoolStrategy() const;

    // Memory allocation
    void* Allocate(
        size_t size,
        MemoryType memory_type = MemoryType::DEVICE_MEMORY,
        AllocationStrategy strategy = AllocationStrategy::BEST_FIT,
        const std::string& tag = ""
    );

    void* AllocateAligned(
        size_t size,
        size_t alignment,
        MemoryType memory_type = MemoryType::DEVICE_MEMORY,
        const std::string& tag = ""
    );

    void* AllocateFromPool(
        size_t pool_id,
        size_t size,
        const std::string& tag = ""
    );

    // Memory deallocation
    void Deallocate(void* pointer);
    void DeallocateFromPool(void* pointer, size_t pool_id);
    bool DeallocateAll();
    bool DeallocateByTag(const std::string& tag);

    // Advanced allocation features
    void* AllocateZeroCopy(size_t size, const std::string& tag = "");
    void* AllocateUnified(size_t size, const std::string& tag = "");
    void* AllocateManaged(size_t size, const std::string& tag = "");

    // Memory operations
    bool Reallocate(void** pointer, size_t new_size);
    bool ResizeAllocation(void* pointer, size_t new_size);
    void* ReallocateFromPool(size_t pool_id, void* pointer, size_t new_size);

    // Pool analytics
    PoolConfig GetPoolConfig(size_t pool_id) const;
    std::vector<PoolConfig> GetAllPoolConfigs() const;
    PoolMetrics GetPoolMetrics() const;
    std::vector<AllocationInfo> GetActiveAllocations() const;

    // Memory usage analysis
    size_t GetTotalAllocatedMemory() const;
    size_t GetTotalFreeMemory() const;
    size_t GetFragmentedMemory() const;
    double GetFragmentationPercentage() const;
    double GetUtilizationPercentage() const;

    // Performance monitoring
    std::chrono::microseconds GetAverageAllocationTime() const;
    std::chrono::microseconds GetAverageDeallocationTime() const;
    int GetAllocationsPerSecond() const;
    int GetDeallocationsPerSecond() const;

    // Pool optimization
    std::vector<MemoryPoolRecommendation> GetOptimizationRecommendations() const;
    bool OptimizePools();
    bool DefragmentPools();
    bool RebalancePools();
    void CompactPools();

    // Garbage collection
    void EnableGarbageCollection(bool enabled);
    void SetGCTimeInterval(std::chrono::seconds interval);
    void ForceGarbageCollection();
    bool ReclaimIdleMemory(std::chrono::seconds idle_threshold);

    // Memory usage tracking
    AllocationInfo GetAllocationInfo(void* pointer) const;
    void SetAllocationTag(void* pointer, const std::string& tag);
    void SetAllocationPriority(void* pointer, int priority);
    bool IsAllocationActive(void* pointer) const;

    // Memory access pattern analysis
    struct AccessPattern {
        size_t size;
        int frequency;
        std::chrono::microseconds average_lifetime;
        double reuse_rate;
        std::vector<std::chrono::system_clock::time_point> access_times;
    };

    std::map<size_t, AccessPattern> AnalyzeAccessPatterns() const;
    std::vector<size_t> GetOptimalPoolSizes() const;

    // Configuration
    struct ManagerConfig {
        PoolStrategy default_strategy = PoolStrategy::ADAPTIVE_POOLS;
        AllocationStrategy default_allocation_strategy = AllocationStrategy::BEST_FIT;

        // Pool sizing
        std::vector<size_t> default_pool_sizes = {
            1024,          // 1KB
            4096,          // 4KB
            16384,         // 16KB
            65536,         // 64KB
            262144,        // 256KB
            1048576,       // 1MB
            4194304,       // 4MB
            16777216       // 16MB
        };

        size_t initial_pool_capacity = 1024 * 1024; // 1MB
        size_t max_total_memory = 1024ull * 1024 * 1024; // 1GB
        double growth_factor = 2.0;
        double shrink_threshold = 0.25;
        double utilization_threshold = 0.80;

        // Performance tuning
        int max_pools = 32;
        int allocation_history_size = 10000;
        std::chrono::seconds gc_interval{60}; // 1 minute
        std::chrono::seconds idle_reclaim_threshold{300}; // 5 minutes

        // Advanced features
        bool enable_defragmentation = true;
        bool enable_adaptive_sizing = true;
        bool enable_access_pattern_analysis = true;
        bool enable_allocation_tracking = true;
        bool enable_performance_monitoring = true;
        bool enable_zero_copy_optimization = true;
        bool enable_unified_memory = true;

        // Thread safety
        bool enable_concurrent_allocation = true;
        int max_concurrent_operations = 16;
    };

    void UpdateConfiguration(const ManagerConfig& config);
    ManagerConfig GetCurrentConfiguration() const;

    // Analytics and reporting
    json GetPoolAnalytics() const;
    std::string GeneratePoolReport() const;
    void ExportPoolData(const std::string& filename) const;
    void ExportAllocationHistory(const std::string& filename) const;

    // Error handling
    enum class ErrorType {
        NONE = 0,
        INITIALIZATION_FAILED,
        OUT_OF_MEMORY,
        INVALID_POOL_ID,
        INVALID_POINTER,
        ALLOCATION_FAILED,
        DEALLOCATION_FAILED,
        POOL_RESIZE_FAILED,
        CUDA_ERROR,
        CONFIGURATION_ERROR,
        THREADING_ERROR
    };

    ErrorType GetLastError() const;
    std::string GetErrorMessage() const;
    bool AttemptErrorRecovery();

private:
    int device_id_;
    ManagerConfig config_;
    PoolStrategy current_strategy_;
    bool initialized_;

    // Device properties
    cudaDeviceProp device_properties_;
    size_t total_device_memory_;
    size_t available_device_memory_;

    // Pool storage
    std::map<size_t, PoolConfig> pools_;
    std::map<void*, AllocationInfo> allocations_;
    std::unordered_map<size_t, std::queue<void*>> free_blocks_;

    // Pool management
    std::atomic<size_t> next_pool_id_;
    std::set<size_t> active_pool_ids_;
    std::map<MemoryType, std::vector<size_t>> pools_by_type_;

    // Memory tracking
    std::atomic<size_t> total_allocated_memory_;
    std::atomic<size_t> total_free_memory_;
    std::atomic<size_t> total_reserved_memory_;

    // Performance tracking
    std::atomic<int> total_allocations_;
    std::atomic<int> total_deallocations_;
    std::atomic<int> concurrent_allocations_;
    std::vector<std::chrono::microseconds> allocation_times_;
    std::vector<std::chrono::microseconds> deallocation_times_;

    // Access pattern tracking
    std::map<size_t, AccessPattern> access_patterns_;
    std::vector<std::pair<std::chrono::system_clock::time_point, size_t>> allocation_history_;

    // Garbage collection
    bool gc_enabled_;
    std::unique_ptr<std::thread> gc_thread_;
    std::atomic<bool> shutdown_requested_;
    std::chrono::seconds gc_interval_;

    // Thread safety
    mutable std::mutex pools_mutex_;
    mutable std::mutex allocations_mutex_;
    mutable std::mutex metrics_mutex_;
    mutable std::mutex gc_mutex_;
    std::condition_variable allocation_cv_;

    // Error handling
    ErrorType last_error_;
    std::string last_error_message_;

    // Internal methods
    bool InitializeDeviceProperties();
    void InitializeDefaultPools();

    // Pool management
    size_t FindOrCreatePool(
        MemoryType memory_type,
        size_t size,
        AllocationStrategy strategy
    );

    size_t CreatePoolInternal(
        MemoryType memory_type,
        size_t block_size,
        size_t initial_block_count,
        const std::string& pool_name
    );

    void* AllocateFromPoolInternal(size_t pool_id, size_t size, const std::string& tag);
    void DeallocateFromPoolInternal(void* pointer, size_t pool_id);

    // Memory allocation helpers
    void* AllocateMemory(MemoryType type, size_t size);
    void DeallocateMemory(MemoryType type, void* pointer, size_t size);

    // Pool strategies
    size_t FindBestFitPool(size_t size, MemoryType type);
    size_t FindFirstFitPool(size_t size, MemoryType type);
    size_t FindPoolByStrategy(size_t size, MemoryType type, AllocationStrategy strategy);

    // Pool optimization
    void ResizePoolIfNeeded(size_t pool_id);
    void MergePools(size_t pool_id1, size_t pool_id2);
    void SplitPool(size_t pool_id, size_t new_size);

    // Memory management
    void UpdateMemoryMetrics();
    void UpdatePoolMetrics(size_t pool_id);
    void CompactPool(size_t pool_id);
    void DefragmentMemory();

    // Access pattern analysis
    void RecordAccessPattern(size_t size, std::chrono::system_clock::time_point timestamp);
    void AnalyzeAllocationPatterns();
    std::vector<size_t> CalculateOptimalPoolSizes() const;

    // Garbage collection
    void GarbageCollectionThread();
    void CleanupIdleAllocations();
    void ReclaimUnusedMemory();

    // Performance monitoring
    void RecordAllocationTime(std::chrono::microseconds duration);
    void RecordDeallocationTime(std::chrono::microseconds duration);
    void UpdatePerformanceMetrics();

    // Error handling
    void SetError(ErrorType error, const std::string& message);
    bool RecoverFromError(ErrorType error);

    // Utility methods
    std::string GetMemoryTypeName(MemoryType type) const;
    std::string GetPoolStrategyName(PoolStrategy strategy) const;
    size_t RoundUpToPowerOfTwo(size_t size) const;
    size_t GetOptimalBlockSize(size_t requested_size) const;
    bool CanFitInPool(size_t pool_id, size_t size) const;

    // Constants
    static constexpr size_t MIN_BLOCK_SIZE = 64; // 64 bytes minimum
    static constexpr size_t MAX_BLOCK_SIZE = 1024ull * 1024 * 1024; // 1GB maximum
    static constexpr size_t DEFAULT_ALIGNMENT = 256; // 256-byte alignment for CUDA
    static constexpr int MAX_ALLOCATION_HISTORY = 100000;
    static constexpr double FRAGMENTATION_THRESHOLD = 0.15; // 15% fragmentation threshold
};

/**
 * @brief Factory function to create memory pool manager instance
 */
std::unique_ptr<MemoryPoolManager> CreateMemoryPoolManager(
    int device_id = 0
);

} // namespace performance
} // namespace gpu
} // namespace keycuda