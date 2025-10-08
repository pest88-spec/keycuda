#include "ComputeCore/gpu/performance/memory_manager.h"
#include <stdexcept>
#include <algorithm>
#include <iostream>
#include <numeric>

namespace puzzle71::gpu::performance {

// Factory method implementation
std::unique_ptr<PerformanceMemoryManager> PerformanceMemoryManager::Create(int gpu_id) {
    return std::make_unique<PerformanceMemoryManager>(gpu_id);
}

// Constructor
PerformanceMemoryManager::PerformanceMemoryManager(int gpu_id, size_t initial_pool_size_mb)
    : gpu_id_(gpu_id) {

    // Create base memory manager
    base_manager_ = std::make_unique<MemoryManager>(gpu_id, 0);  // Auto-detect limits

    // Initialize async streams if enabled
    if (async_enabled_) {
        InitializeAsyncStreams();
    }

    // Initial constraint detection
    UpdateMemoryConstraints();

    // Preallocate common pool sizes
    PreallocateCommonSizes();

    // Start performance monitoring
    StartPerformanceMonitoring();

    std::cout << "PerformanceMemoryManager initialized for GPU " << gpu_id
              << " with initial pool size: " << initial_pool_size_mb << "MB" << std::endl;
}

// Destructor
PerformanceMemoryManager::~PerformanceMemoryManager() {
    StopPerformanceMonitoring();
    CleanupAsyncStreams();
}

// Memory constraint detection
MemoryConstraints PerformanceMemoryManager::DetectMemoryConstraints() {
    std::lock_guard<std::mutex> lock(constraints_mutex_);
    UpdateConstraintsInternal();
    return current_constraints_;
}

MemoryConstraints PerformanceMemoryManager::GetCurrentConstraints() const {
    std::lock_guard<std::mutex> lock(constraints_mutex_);
    return current_constraints_;
}

bool PerformanceMemoryManager::IsMemoryPressureDetected() const {
    std::lock_guard<std::mutex> lock(constraints_mutex_);
    return current_constraints_.memory_pressure_detected;
}

void PerformanceMemoryManager::UpdateMemoryConstraints() {
    if (memory_monitoring_enabled_) {
        std::lock_guard<std::mutex> lock(constraints_mutex_);
        UpdateConstraintsInternal();
    }
}

// Adaptive points_per_thread management
int PerformanceMemoryManager::CalculateOptimalPointsPerThread(int base_points_per_thread, size_t batch_size) {
    std::lock_guard<std::mutex> lock(constraints_mutex_);

    // Estimate memory requirements per point
    size_t memory_per_point_estimate = 1024;  // 1KB per point (conservative estimate)
    size_t required_memory = batch_size * memory_per_point_estimate * base_points_per_thread;

    // Check if we have sufficient memory
    if (required_memory > current_constraints_.safe_memory_threshold_mb * 1024 * 1024) {
        // Reduce points per thread based on available memory
        int recommended = CalculatePointsPerThreadForMemory(
            current_constraints_.safe_memory_threshold_mb,
            memory_per_point_estimate
        );
        recommended_points_per_thread_ = std::max(min_points_per_thread_,
                                                 std::min(recommended, base_points_per_thread));
    } else {
        recommended_points_per_thread_ = std::min(max_points_per_thread_, base_points_per_thread);
    }

    return recommended_points_per_thread_;
}

int PerformanceMemoryManager::GetRecommendedPointsPerThread() const {
    std::lock_guard<std::mutex> lock(constraints_mutex_);
    return recommended_points_per_thread_;
}

void PerformanceMemoryManager::SetPointsPerThreadLimits(int min_ppt, int max_ppt) {
    std::lock_guard<std::mutex> lock(constraints_mutex_);
    min_points_per_thread_ = std::max(1, min_ppt);
    max_points_per_thread_ = std::max(min_points_per_thread_, max_ppt);
}

// Performance-optimized allocation
void* PerformanceMemoryManager::AllocateOptimized(size_t size, bool use_async) {
    auto start_time = std::chrono::steady_clock::now();

    // Check memory constraints before allocation
    UpdateMemoryConstraints();

    void* ptr = nullptr;
    if (use_async && async_enabled_) {
        cudaStream_t stream = GetAsyncStream();
        ptr = AllocateAsync(size, stream);
    } else {
        ptr = base_manager_->Allocate(size, true);
    }

    // Record allocation timing
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    RecordAllocationTime(duration);

    return ptr;
}

void PerformanceMemoryManager::DeallocateOptimized(void* ptr) {
    if (!ptr) return;

    auto start_time = std::chrono::steady_clock::now();

    base_manager_->Deallocate(ptr);

    // Record deallocation timing
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    RecordDeallocationTime(duration);

    // Check if pool optimization is needed
    CheckPoolOptimization();
}

// Asynchronous memory operations
cudaStream_t PerformanceMemoryManager::GetAsyncStream() {
    if (!async_enabled_ || async_streams_.empty()) {
        return 0;  // Default stream
    }

    cudaStream_t stream = async_streams_[current_stream_index_];
    current_stream_index_ = (current_stream_index_ + 1) % async_streams_.size();
    return stream;
}

void* PerformanceMemoryManager::AllocateAsync(size_t size, cudaStream_t stream) {
    void* ptr = nullptr;
    cudaError_t err = cudaMallocAsync(&ptr, size, stream);
    if (err != cudaSuccess) {
        throw std::runtime_error(std::string("cudaMallocAsync failed: ") + cudaGetErrorString(err));
    }
    return ptr;
}

void PerformanceMemoryManager::DeallocateAsync(void* ptr, cudaStream_t stream) {
    if (!ptr) return;

    cudaError_t err = cudaFreeAsync(ptr, stream);
    if (err != cudaSuccess) {
        std::cerr << "Warning: cudaFreeAsync failed: " << cudaGetErrorString(err) << std::endl;
    }
}

// Memory bandwidth optimization
void PerformanceMemoryManager::PrefetchToDevice(void* ptr, size_t size) {
    if (!advanced_features_enabled_ || !ptr) return;

    int current_device;
    cudaGetDevice(&current_device);
    cudaMemPrefetchAsync(ptr, size, current_device, 0);
}

void PerformanceMemoryManager::PrefetchToHost(void* ptr, size_t size) {
    if (!advanced_features_enabled_ || !ptr) return;

    cudaMemPrefetchAsync(ptr, size, cudaCpuDeviceId, 0);
}

void PerformanceMemoryManager::AdviseReadMostly(void* ptr, size_t size) {
    if (!advanced_features_enabled_ || !ptr) return;

    cudaAdviseSetAccessedBy(cudaMemRangeReadMostly, ptr, size, cudaCpuDeviceId);
}

void PerformanceMemoryManager::AdvisePreferredLocationDevice(void* ptr, size_t size) {
    if (!advanced_features_enabled_ || !ptr) return;

    int current_device;
    cudaGetDevice(&current_device);
    cudaMemPrefetchAsync(ptr, size, current_device, 0);
}

// Performance monitoring
MemoryPerformanceMetrics PerformanceMemoryManager::GetPerformanceMetrics() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    UpdatePerformanceMetrics();
    return performance_metrics_;
}

MemoryPoolStats PerformanceMemoryManager::GetPoolStats() const {
    MemoryPoolStats stats;

    auto base_stats = base_manager_->GetStats();
    stats.total_pool_size_mb = base_stats.total_allocated_mb;
    stats.used_pool_size_mb = base_stats.total_allocated_mb - base_stats.pool_free_mb;
    stats.free_pool_size_mb = base_stats.pool_free_mb;

    // Calculate fragmentation ratio
    if (stats.total_pool_size_mb > 0) {
        stats.fragmentation_ratio = static_cast<double>(base_stats.pool_free_mb) / stats.total_pool_size_mb;
    }

    // Analyze pool utilization by size tier
    auto tiers = AnalyzePoolTiers();
    stats.pool_utilization_by_size.clear();
    for (const auto& tier : tiers) {
        stats.pool_utilization_by_size.push_back(tier.utilization);
    }

    return stats;
}

void PerformanceMemoryManager::ResetPerformanceMetrics() {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    performance_metrics_ = MemoryPerformanceMetrics{};
    allocation_times_.clear();
    deallocation_times_.clear();
}

void PerformanceMemoryManager::StartPerformanceMonitoring() {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    monitoring_active_ = true;
    monitoring_start_time_ = std::chrono::steady_clock::now();
}

void PerformanceMemoryManager::StopPerformanceMonitoring() {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    monitoring_active_ = false;
}

// Memory pool optimization
void PerformanceMemoryManager::OptimizeMemoryPool() {
    if (!pool_optimization_enabled_) return;

    base_manager_->OptimizePool();

    // Analyze and optimize pool tiers
    auto tiers = AnalyzePoolTiers();
    OptimizePoolTiers(tiers);

    last_pool_optimization_ = std::chrono::steady_clock::now();

    std::cout << "Memory pool optimization completed" << std::endl;
}

void PerformanceMemoryManager::PreallocateCommonSizes() {
    // Preallocate common sizes for performance optimization
    const std::vector<size_t> common_sizes = {
        1024,        // 1KB
        4096,        // 4KB
        16384,       // 16KB
        65536,       // 64KB
        262144,      // 256KB
        1048576,     // 1MB
        4194304,     // 4MB
        16777216     // 16MB
    };

    for (size_t size : common_sizes) {
        try {
            base_manager_->Allocate(size, true);
        } catch (const std::exception& e) {
            // If allocation fails, skip this size
            continue;
        }
    }
}

void PerformanceMemoryManager::TrimPool(size_t target_free_mb) {
    auto stats = base_manager_->GetStats();
    if (stats.pool_free_mb <= target_free_mb) {
        return;  // Already within target
    }

    size_t excess_mb = stats.pool_free_mb - target_free_mb;
    size_t excess_bytes = excess_mb * 1024 * 1024;

    // Free excess pool memory
    base_manager_->OptimizePool();
}

void PerformanceMemoryManager::DefragmentPool() {
    // Trigger pool optimization which includes defragmentation
    OptimizeMemoryPool();
}

// Configuration
void PerformanceMemoryManager::SetMemoryPressureThreshold(double threshold_percent) {
    std::lock_guard<std::mutex> lock(constraints_mutex_);
    memory_pressure_threshold_ = std::max(0.1, std::min(0.95, threshold_percent));
}

void PerformanceMemoryManager::SetPoolOptimizationInterval(std::chrono::seconds interval) {
    pool_optimization_interval_ = interval;
}

void PerformanceMemoryManager::EnableAdvancedFeatures(bool enable) {
    advanced_features_enabled_ = enable;
}

// Private helper methods
void PerformanceMemoryManager::InitializeAsyncStreams() {
    const int num_streams = 4;  // Create 4 async streams

    for (int i = 0; i < num_streams; ++i) {
        cudaStream_t stream;
        cudaError_t err = cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking);
        if (err == cudaSuccess) {
            async_streams_.push_back(stream);
        }
    }

    std::cout << "Initialized " << async_streams_.size() << " async streams" << std::endl;
}

void PerformanceMemoryManager::CleanupAsyncStreams() {
    for (cudaStream_t stream : async_streams_) {
        cudaStreamDestroy(stream);
    }
    async_streams_.clear();
}

void PerformanceMemoryManager::UpdateConstraintsInternal() {
    // Get current memory stats
    auto stats = base_manager_->GetStats();

    current_constraints_.available_memory_mb = stats.available_mb;
    current_constraints_.memory_utilization_percent = stats.utilization_percent;
    current_constraints_.safe_memory_threshold_mb = stats.device_total_mb * 8 / 10;  // 80% safety margin

    // Detect memory pressure
    current_constraints_.memory_pressure_detected =
        stats.utilization_percent > (memory_pressure_threshold_ * 100.0);

    // Update recommended points per thread based on memory constraints
    if (current_constraints_.memory_pressure_detected) {
        recommended_points_per_thread_ = std::max(min_points_per_thread_,
                                                 recommended_points_per_thread_ / 2);
    }
}

int PerformanceMemoryManager::CalculatePointsPerThreadForMemory(size_t available_memory_mb,
                                                               size_t required_memory_per_point) {
    size_t available_bytes = available_memory_mb * 1024 * 1024;
    int max_points = static_cast<int>(available_bytes / required_memory_per_point);

    // Apply constraints and ensure reasonable range
    max_points = std::max(min_points_per_thread_,
                          std::min(max_points, max_points_per_thread_));

    return max_points;
}

void PerformanceMemoryManager::RecordAllocationTime(std::chrono::microseconds duration) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    if (allocation_times_.size() >= MAX_TIMING_SAMPLES) {
        allocation_times_.erase(allocation_times_.begin());
    }

    allocation_times_.push_back(duration);
    performance_metrics_.total_allocations++;

    // Update pool hit/miss tracking
    // This is a simplified tracking - in a real implementation, we'd track more precisely
    if (duration.count() < 100) {  // Less than 100 microseconds = likely pool hit
        performance_metrics_.pool_hits++;
    } else {
        performance_metrics_.pool_misses++;
    }
}

void PerformanceMemoryManager::RecordDeallocationTime(std::chrono::microseconds duration) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    if (deallocation_times_.size() >= MAX_TIMING_SAMPLES) {
        deallocation_times_.erase(deallocation_times_.begin());
    }

    deallocation_times_.push_back(duration);
}

void PerformanceMemoryManager::UpdatePerformanceMetrics() {
    // Calculate average allocation time
    if (!allocation_times_.empty()) {
        auto sum = std::accumulate(allocation_times_.begin(), allocation_times_.end(),
                                  std::chrono::microseconds{0});
        performance_metrics_.avg_allocation_time =
            std::chrono::duration_cast<std::chrono::microseconds>(sum / allocation_times_.size());
    }

    // Calculate average deallocation time
    if (!deallocation_times_.empty()) {
        auto sum = std::accumulate(deallocation_times_.begin(), deallocation_times_.end(),
                                  std::chrono::microseconds{0});
        performance_metrics_.avg_deallocation_time =
            std::chrono::duration_cast<std::chrono::microseconds>(sum / deallocation_times_.size());
    }

    // Calculate pool hit rate
    if (performance_metrics_.total_allocations > 0) {
        performance_metrics_.pool_hit_rate_percent =
            (static_cast<double>(performance_metrics_.pool_hits) / performance_metrics_.total_allocations) * 100.0;
    }

    // Estimate memory bandwidth utilization
    performance_metrics_.bandwidth_utilization_gb_per_sec = EstimateMemoryBandwidthUtilization();

    // Calculate allocation efficiency
    auto stats = base_manager_->GetStats();
    if (stats.total_allocated_mb > 0) {
        size_t used_memory = stats.total_allocated_mb - stats.pool_free_mb;
        performance_metrics_.allocation_efficiency_percent =
            (static_cast<double>(used_memory) / stats.total_allocated_mb) * 100.0;
    }
}

void PerformanceMemoryManager::CheckPoolOptimization() {
    if (!pool_optimization_enabled_) return;

    auto now = std::chrono::steady_clock::now();
    auto time_since_last_opt = now - last_pool_optimization_;

    if (time_since_last_opt > pool_optimization_interval_) {
        OptimizeMemoryPool();
    }
}

double PerformanceMemoryManager::EstimateMemoryBandwidthUtilization() {
    // This is a simplified estimation
    // In a real implementation, we'd use NVIDIA's performance counters or NVML

    auto stats = base_manager_->GetStats();
    double utilization = stats.utilization_percent;

    // Estimate bandwidth based on memory utilization
    // This is a rough approximation
    return (utilization / 100.0) * 500.0;  // Assume 500 GB/s theoretical max
}

void PerformanceMemoryManager::OptimizeForBandwidth() {
    // Enable various bandwidth optimizations
    if (advanced_features_enabled_) {
        // This would include:
        // - Unified memory optimizations
        // - Memory access pattern optimizations
        // - Prefetching strategies
        // Implementation would depend on specific use case
    }
}

std::vector<PerformanceMemoryManager::PoolSizeTier> PerformanceMemoryManager::AnalyzePoolTiers() const {
    // Analyze pool usage by size tiers
    std::vector<PoolSizeTier> tiers;

    // This is a simplified analysis
    // In a real implementation, we'd analyze actual pool statistics
    const std::vector<std::pair<size_t, std::string>> tier_sizes = {
        {1024, "1KB"},
        {4096, "4KB"},
        {16384, "16KB"},
        {65536, "64KB"},
        {262144, "256KB"},
        {1048576, "1MB"},
        {4194304, "4MB"},
        {16777216, "16MB"}
    };

    for (const auto& size_info : tier_sizes) {
        PoolSizeTier tier;
        tier.size_bytes = size_info.first;
        tier.count = 0;  // Would be filled with actual pool data
        tier.used_count = 0;  // Would be filled with actual usage data
        tier.utilization = tier.count > 0 ?
                          static_cast<double>(tier.used_count) / tier.count : 0.0;
        tiers.push_back(tier);
    }

    return tiers;
}

void PerformanceMemoryManager::OptimizePoolTiers(const std::vector<PoolSizeTier>& tiers) {
    // Optimize pool based on tier analysis
    // This is a simplified optimization strategy

    for (const auto& tier : tiers) {
        if (tier.utilization > 0.9) {
            // High utilization - consider preallocating more of this size
            std::cout << "High utilization for pool size " << tier.size_bytes
                      << " bytes: " << (tier.utilization * 100.0) << "%" << std::endl;
        } else if (tier.utilization < 0.1 && tier.count > 10) {
            // Low utilization with many blocks - consider reducing this tier
            std::cout << "Low utilization for pool size " << tier.size_bytes
                      << " bytes: " << (tier.utilization * 100.0) << "%" << std::endl;
        }
    }
}

} // namespace puzzle71::gpu::performance