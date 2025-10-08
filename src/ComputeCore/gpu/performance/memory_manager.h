#pragma once

#include <cuda_runtime.h>
#include <memory>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <chrono>
#include "../memory_manager.h"

namespace puzzle71::gpu::performance {

/**
 * Performance Memory Manager - Enhanced memory management for GPU performance optimization
 *
 * Extends the base MemoryManager with performance-specific features:
 * - GPU memory monitoring and constraint detection
 * - Adaptive points_per_thread reduction for memory constraints
 * - Memory pool allocation optimization foundation
 * - Asynchronous memory transfer support
 * - Memory bandwidth optimization
 */
class PerformanceMemoryManager {
public:
    struct MemoryConstraints {
        size_t available_memory_mb = 0;
        size_t safe_memory_threshold_mb = 0;  // Safety margin (10% below max)
        double memory_utilization_percent = 0.0;
        bool memory_pressure_detected = false;
        int recommended_points_per_thread = 128;  // Adaptive recommendation
    };

    struct MemoryPerformanceMetrics {
        double bandwidth_utilization_gb_per_sec = 0.0;
        double allocation_efficiency_percent = 0.0;
        std::chrono::microseconds avg_allocation_time{0};
        std::chrono::microseconds avg_deallocation_time{0};
        size_t total_allocations = 0;
        size_t pool_hits = 0;
        size_t pool_misses = 0;
        double pool_hit_rate_percent = 0.0;
    };

    struct MemoryPoolStats {
        size_t total_pool_size_mb = 0;
        size_t used_pool_size_mb = 0;
        size_t free_pool_size_mb = 0;
        std::vector<size_t> pool_utilization_by_size;  // Utilization per pool size tier
        double fragmentation_ratio = 0.0;
    };

    explicit PerformanceMemoryManager(int gpu_id, size_t initial_pool_size_mb = 1024);
    virtual ~PerformanceMemoryManager();

    // Disable copying
    PerformanceMemoryManager(const PerformanceMemoryManager&) = delete;
    PerformanceMemoryManager& operator=(const PerformanceMemoryManager&) = delete;

    // Memory constraint detection and monitoring
    MemoryConstraints DetectMemoryConstraints();
    MemoryConstraints GetCurrentConstraints() const;
    bool IsMemoryPressureDetected() const;
    void UpdateMemoryConstraints();

    // Adaptive points_per_thread management
    int CalculateOptimalPointsPerThread(int base_points_per_thread, size_t batch_size);
    int GetRecommendedPointsPerThread() const;
    void SetPointsPerThreadLimits(int min_ppt, int max_ppt);

    // Performance-optimized allocation
    void* AllocateOptimized(size_t size, bool use_async = false);
    void DeallocateOptimized(void* ptr);

    // Asynchronous memory operations
    cudaStream_t GetAsyncStream();
    void* AllocateAsync(size_t size, cudaStream_t stream = nullptr);
    void DeallocateAsync(void* ptr, cudaStream_t stream = nullptr);

    // Memory bandwidth optimization
    void PrefetchToDevice(void* ptr, size_t size);
    void PrefetchToHost(void* ptr, size_t size);
    void AdviseReadMostly(void* ptr, size_t size);
    void AdvisePreferredLocationDevice(void* ptr, size_t size);

    // Performance monitoring
    MemoryPerformanceMetrics GetPerformanceMetrics() const;
    MemoryPoolStats GetPoolStats() const;
    void ResetPerformanceMetrics();
    void StartPerformanceMonitoring();
    void StopPerformanceMonitoring();

    // Memory pool optimization
    void OptimizeMemoryPool();
    void PreallocateCommonSizes();
    void TrimPool(size_t target_free_mb);
    void DefragmentPool();

    // Configuration
    void SetMemoryPressureThreshold(double threshold_percent);
    void SetPoolOptimizationInterval(std::chrono::seconds interval);
    void EnableAdvancedFeatures(bool enable);

    // Factory method
    static std::unique_ptr<PerformanceMemoryManager> Create(int gpu_id);

private:
    int gpu_id_;
    std::unique_ptr<MemoryManager> base_manager_;

    // Memory constraint tracking
    mutable std::mutex constraints_mutex_;
    MemoryConstraints current_constraints_;
    double memory_pressure_threshold_ = 0.85;  // 85% utilization triggers pressure
    bool memory_monitoring_enabled_ = true;

    // Points per thread management
    int min_points_per_thread_ = 64;
    int max_points_per_thread_ = 256;
    int recommended_points_per_thread_ = 128;

    // Asynchronous operations
    std::vector<cudaStream_t> async_streams_;
    size_t current_stream_index_ = 0;
    bool async_enabled_ = true;

    // Performance monitoring
    mutable std::mutex metrics_mutex_;
    MemoryPerformanceMetrics performance_metrics_;
    bool monitoring_active_ = false;
    std::chrono::steady_clock::time_point monitoring_start_time_;

    // Allocation timing
    std::vector<std::chrono::microseconds> allocation_times_;
    std::vector<std::chrono::microseconds> deallocation_times_;
    static constexpr size_t MAX_TIMING_SAMPLES = 1000;

    // Memory pool optimization
    std::chrono::seconds pool_optimization_interval_{30};  // Optimize every 30 seconds
    std::chrono::steady_clock::time_point last_pool_optimization_;
    bool pool_optimization_enabled_ = true;

    // Advanced features
    bool advanced_features_enabled_ = true;

    // Private helper methods
    void InitializeAsyncStreams();
    void CleanupAsyncStreams();
    void UpdateConstraintsInternal();
    int CalculatePointsPerThreadForMemory(size_t available_memory_mb, size_t required_memory_per_point);
    void RecordAllocationTime(std::chrono::microseconds duration);
    void RecordDeallocationTime(std::chrono::microseconds duration);
    void UpdatePerformanceMetrics();
    void CheckPoolOptimization();

    // Memory bandwidth helpers
    double EstimateMemoryBandwidthUtilization();
    void OptimizeForBandwidth();

    // Pool optimization helpers
    struct PoolSizeTier {
        size_t size_bytes;
        size_t count;
        size_t used_count;
        double utilization;
    };
    std::vector<PoolSizeTier> AnalyzePoolTiers() const;
    void OptimizePoolTiers(const std::vector<PoolSizeTier>& tiers);
};

/**
 * RAII Performance Memory Guard
 *
 * Provides automatic memory management with performance optimizations
 */
template<typename T>
class PerformanceMemoryGuard {
public:
    PerformanceMemoryGuard(PerformanceMemoryManager& manager,
                          std::size_t count = 1,
                          bool use_async = false)
        : manager_(manager), count_(count), use_async_(use_async) {
        ptr_ = static_cast<T*>(manager_.AllocateOptimized(sizeof(T) * count, use_async));
    }

    ~PerformanceMemoryGuard() {
        if (ptr_) {
            manager_.DeallocateOptimized(ptr_);
        }
    }

    // Disable copying
    PerformanceMemoryGuard(const PerformanceMemoryGuard&) = delete;
    PerformanceMemoryGuard& operator=(const PerformanceMemoryGuard&) = delete;

    // Enable moving
    PerformanceMemoryGuard(PerformanceMemoryGuard&& other) noexcept
        : manager_(other.manager_), ptr_(other.ptr_), count_(other.count_),
          use_async_(other.use_async_) {
        other.ptr_ = nullptr;
    }

    PerformanceMemoryGuard& operator=(PerformanceMemoryGuard&& other) noexcept {
        if (this != &other) {
            if (ptr_) {
                manager_.DeallocateOptimized(ptr_);
            }
            manager_ = other.manager_;
            ptr_ = other.ptr_;
            count_ = other.count_;
            use_async_ = other.use_async_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    T* get() const { return ptr_; }
    T* operator->() const { return ptr_; }
    T& operator*() const { return *ptr_; }
    std::size_t size() const { return count_; }
    operator bool() const { return ptr_ != nullptr; }

    // Performance-specific methods
    void PrefetchToDevice() const {
        if (ptr_) {
            manager_.PrefetchToDevice(ptr_, sizeof(T) * count_);
        }
    }

    void PrefetchToHost() const {
        if (ptr_) {
            manager_.PrefetchToHost(ptr_, sizeof(T) * count_);
        }
    }

private:
    PerformanceMemoryManager& manager_;
    T* ptr_{nullptr};
    std::size_t count_{0};
    bool use_async_{false};
};

} // namespace puzzle71::gpu::performance