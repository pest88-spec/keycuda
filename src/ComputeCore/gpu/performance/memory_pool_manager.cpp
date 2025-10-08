#include "memory_pool_manager.h"
#include <algorithm>
#include <fstream>
#include <thread>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <functional>

namespace keycuda {
namespace gpu {
namespace performance {

MemoryPoolManager::MemoryPoolManager(int device_id)
    : device_id_(device_id)
    , current_strategy_(PoolStrategy::ADAPTIVE_POOLS)
    , initialized_(false)
    , total_device_memory_(0)
    , available_device_memory_(0)
    , next_pool_id_(1)
    , total_allocated_memory_(0)
    , total_free_memory_(0)
    , total_reserved_memory_(0)
    , total_allocations_(0)
    , total_deallocations_(0)
    , concurrent_allocations_(0)
    , gc_enabled_(false)
    , shutdown_requested_(false)
    , gc_interval_(std::chrono::seconds(60))
    , last_error_(ErrorType::NONE)
    , last_error_message_()
{
    gc_thread_ = nullptr;
}

MemoryPoolManager::~MemoryPoolManager() {
    Cleanup();
}

bool MemoryPoolManager::Initialize(PoolStrategy strategy) {
    std::lock_guard<std::mutex> lock(pools_mutex_);

    if (initialized_) {
        SetError(ErrorType::INITIALIZATION_FAILED, "Memory pool manager already initialized");
        return false;
    }

    current_strategy_ = strategy;

    // Initialize device properties
    if (!InitializeDeviceProperties()) {
        return false;
    }

    // Initialize default pools based on strategy
    InitializeDefaultPools();

    // Start garbage collection thread if enabled
    if (config_.enable_defragmentation) {
        EnableGarbageCollection(true);
    }

    initialized_ = true;
    return true;
}

void MemoryPoolManager::Cleanup() {
    std::lock_guard<std::mutex> lock(pools_mutex_);

    // Stop garbage collection thread
    if (gc_thread_ && gc_thread_->joinable()) {
        shutdown_requested_ = true;
        gc_thread_->join();
        gc_thread_.reset();
    }

    // Deallocate all memory
    DeallocateAll();

    // Clear pools
    pools_.clear();
    free_blocks_.clear();
    active_pool_ids_.clear();
    pools_by_type_.clear();

    // Clear allocations
    allocations_.clear();

    // Clear tracking data
    access_patterns_.clear();
    allocation_history_.clear();
    allocation_times_.clear();
    deallocation_times_.clear();

    initialized_ = false;
}

bool MemoryPoolManager::IsInitialized() const {
    std::lock_guard<std::mutex> lock(pools_mutex_);
    return initialized_;
}

size_t MemoryPoolManager::CreatePool(
    MemoryType memory_type,
    size_t block_size,
    size_t initial_block_count,
    const std::string& pool_name,
    bool allow_growth
) {
    std::lock_guard<std::mutex> lock(pools_mutex_);

    if (!initialized_) {
        SetError(ErrorType::INITIALIZATION_FAILED, "Memory pool manager not initialized");
        return 0;
    }

    return CreatePoolInternal(memory_type, block_size, initial_block_count, pool_name);
}

bool MemoryPoolManager::DestroyPool(size_t pool_id) {
    std::lock_guard<std::mutex> lock(pools_mutex_);

    auto it = pools_.find(pool_id);
    if (it == pools_.end()) {
        SetError(ErrorType::INVALID_POOL_ID, "Pool ID not found");
        return false;
    }

    const PoolConfig& pool = it->second;

    // Deallocate all blocks in the pool
    auto free_blocks_it = free_blocks_.find(pool_id);
    if (free_blocks_it != free_blocks_.end()) {
        while (!free_blocks_it->second.empty()) {
            void* block = free_blocks_it->second.front();
            free_blocks_it->second.pop();
            DeallocateMemory(pool.memory_type, block, pool.block_size);
        }
        free_blocks_.erase(free_blocks_it);
    }

    // Remove pool from tracking
    pools_.erase(it);
    active_pool_ids_.erase(pool_id);

    // Remove from type mapping
    auto type_it = pools_by_type_.find(pool.memory_type);
    if (type_it != pools_by_type_.end()) {
        auto& pool_list = type_it->second;
        pool_list.erase(std::remove(pool_list.begin(), pool_list.end(), pool_id), pool_list.end());
        if (pool_list.empty()) {
            pools_by_type_.erase(type_it);
        }
    }

    return true;
}

bool MemoryPoolManager::ResizePool(size_t pool_id, size_t new_block_count) {
    std::lock_guard<std::mutex> lock(pools_mutex_);

    auto it = pools_.find(pool_id);
    if (it == pools_.end()) {
        SetError(ErrorType::INVALID_POOL_ID, "Pool ID not found");
        return false;
    }

    PoolConfig& pool = it->second;
    size_t current_count = pool.block_count;

    if (new_block_count > current_count) {
        // Grow pool
        size_t blocks_to_add = new_block_count - current_count;
        for (size_t i = 0; i < blocks_to_add; ++i) {
            void* block = AllocateMemory(pool.memory_type, pool.block_size);
            if (block) {
                free_blocks_[pool_id].push(block);
                pool.block_count++;
            } else {
                // Allocation failed, rollback partial changes
                SetError(ErrorType::ALLOCATION_FAILED, "Failed to allocate memory for pool growth");
                return false;
            }
        }
    } else if (new_block_count < current_count) {
        // Shrink pool
        size_t blocks_to_remove = current_count - new_block_count;
        auto free_blocks_it = free_blocks_.find(pool_id);

        if (free_blocks_it != free_blocks_.end()) {
            for (size_t i = 0; i < blocks_to_remove && !free_blocks_it->second.empty(); ++i) {
                void* block = free_blocks_it->second.front();
                free_blocks_it->second.pop();
                DeallocateMemory(pool.memory_type, block, pool.block_size);
                pool.block_count--;
            }
        }
    }

    pool.total_size = pool.block_size * pool.block_count;
    UpdatePoolMetrics(pool_id);
    return true;
}

void MemoryPoolManager::SetPoolStrategy(PoolStrategy strategy) {
    std::lock_guard<std::mutex> lock(pools_mutex_);
    current_strategy_ = strategy;
}

PoolStrategy MemoryPoolManager::GetPoolStrategy() const {
    std::lock_guard<std::mutex> lock(pools_mutex_);
    return current_strategy_;
}

void* MemoryPoolManager::Allocate(
    size_t size,
    MemoryType memory_type,
    AllocationStrategy strategy,
    const std::string& tag
) {
    if (!initialized_) {
        SetError(ErrorType::INITIALIZATION_FAILED, "Memory pool manager not initialized");
        return nullptr;
    }

    if (size == 0) {
        return nullptr;
    }

    auto start_time = std::chrono::high_resolution_clock::now();

    // Find or create appropriate pool
    size_t pool_id = FindOrCreatePool(memory_type, size, strategy);
    if (pool_id == 0) {
        return nullptr;
    }

    // Allocate from pool
    void* pointer = AllocateFromPoolInternal(pool_id, size, tag);
    if (!pointer) {
        return nullptr;
    }

    // Record allocation time
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    RecordAllocationTime(duration);

    // Update statistics
    total_allocations_++;
    total_allocated_memory_ += size;
    RecordAccessPattern(size, std::chrono::system_clock::now());

    return pointer;
}

void* MemoryPoolManager::AllocateAligned(
    size_t size,
    size_t alignment,
    MemoryType memory_type,
    const std::string& tag
) {
    // For aligned allocation, we'll use the base allocation with alignment considerations
    // In a real implementation, this would use aligned memory allocation APIs

    size_t aligned_size = size + alignment - 1;
    void* pointer = Allocate(aligned_size, memory_type, AllocationStrategy::BEST_FIT, tag);

    if (pointer) {
        // Align the pointer
        uintptr_t addr = reinterpret_cast<uintptr_t>(pointer);
        uintptr_t aligned_addr = (addr + alignment - 1) & ~(alignment - 1);
        return reinterpret_cast<void*>(aligned_addr);
    }

    return nullptr;
}

void* MemoryPoolManager::AllocateFromPool(
    size_t pool_id,
    size_t size,
    const std::string& tag
) {
    std::lock_guard<std::mutex> lock(pools_mutex_);

    if (!initialized_) {
        SetError(ErrorType::INITIALIZATION_FAILED, "Memory pool manager not initialized");
        return nullptr;
    }

    auto it = pools_.find(pool_id);
    if (it == pools_.end()) {
        SetError(ErrorType::INVALID_POOL_ID, "Pool ID not found");
        return nullptr;
    }

    if (size > it->second.block_size) {
        SetError(ErrorType::ALLOCATION_FAILED, "Requested size exceeds pool block size");
        return nullptr;
    }

    return AllocateFromPoolInternal(pool_id, size, tag);
}

void MemoryPoolManager::Deallocate(void* pointer) {
    if (!pointer) {
        return;
    }

    std::lock_guard<std::mutex> lock(allocations_mutex_);

    auto it = allocations_.find(pointer);
    if (it == allocations_.end()) {
        SetError(ErrorType::INVALID_POINTER, "Invalid pointer for deallocation");
        return;
    }

    AllocationInfo& allocation = it->second;
    allocation.is_active = false;

    auto start_time = std::chrono::high_resolution_clock::now();

    // Return to pool
    DeallocateFromPoolInternal(pointer, allocation.pool_id);

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    RecordDeallocationTime(duration);

    // Update statistics
    total_deallocations_++;
    total_allocated_memory_ -= allocation.size;
    allocations_.erase(it);
}

void MemoryPoolManager::DeallocateFromPool(void* pointer, size_t pool_id) {
    std::lock_guard<std::mutex> lock(pools_mutex_);
    DeallocateFromPoolInternal(pointer, pool_id);
}

bool MemoryPoolManager::DeallocateAll() {
    std::lock_guard<std::mutex> lock(pools_mutex_);
    std::lock_guard<std::mutex> alloc_lock(allocations_mutex_);

    // Return all allocations to their pools
    for (auto& alloc_pair : allocations_) {
        void* pointer = alloc_pair.first;
        const AllocationInfo& allocation = alloc_pair.second;

        if (allocation.is_active) {
            DeallocateFromPoolInternal(pointer, allocation.pool_id);
        }
    }

    allocations_.clear();
    total_allocated_memory_ = 0;

    return true;
}

bool MemoryPoolManager::DeallocateByTag(const std::string& tag) {
    if (tag.empty()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(pools_mutex_);
    std::lock_guard<std::mutex> alloc_lock(allocations_mutex_);

    std::vector<void*> to_deallocate;

    for (auto& alloc_pair : allocations_) {
        if (alloc_pair.second.allocation_tag == tag && alloc_pair.second.is_active) {
            to_deallocate.push_back(alloc_pair.first);
        }
    }

    for (void* pointer : to_deallocate) {
        auto it = allocations_.find(pointer);
        if (it != allocations_.end()) {
            DeallocateFromPoolInternal(pointer, it->second.pool_id);
            total_allocated_memory_ -= it->second.size;
            allocations_.erase(it);
        }
    }

    return !to_deallocate.empty();
}

void* MemoryPoolManager::AllocateZeroCopy(size_t size, const std::string& tag) {
    if (!config_.enable_zero_copy_optimization) {
        return nullptr;
    }

    void* pointer = Allocate(size, MemoryType::ZERO_COPY_MEMORY, AllocationStrategy::BEST_FIT, tag);
    if (pointer) {
        // In a real implementation, this would use cudaHostAlloc with cudaHostAllocMapped
        // For now, we'll use regular allocation
    }
    return pointer;
}

void* MemoryPoolManager::AllocateUnified(size_t size, const std::string& tag) {
    if (!config_.enable_unified_memory) {
        return nullptr;
    }

    return Allocate(size, MemoryType::UNIFIED_MEMORY, AllocationStrategy::BEST_FIT, tag);
}

void* MemoryPoolManager::AllocateManaged(size_t size, const std::string& tag) {
    return Allocate(size, MemoryType::MANAGED_MEMORY, AllocationStrategy::BEST_FIT, tag);
}

bool MemoryPoolManager::Reallocate(void** pointer, size_t new_size) {
    if (!pointer || !*pointer) {
        *pointer = Allocate(new_size);
        return *pointer != nullptr;
    }

    auto start_time = std::chrono::high_resolution_clock::now();

    // Get current allocation info
    std::lock_guard<std::mutex> lock(allocations_mutex_);
    auto it = allocations_.find(*pointer);
    if (it == allocations_.end()) {
        SetError(ErrorType::INVALID_POINTER, "Invalid pointer for reallocation");
        return false;
    }

    AllocationInfo& allocation = it->second;
    size_t old_size = allocation.size;

    if (new_size <= old_size) {
        // Shrink allocation - keep same pointer
        allocation.size = new_size;
        total_allocated_memory_ -= (old_size - new_size);
        return true;
    }

    // Need to allocate new block
    void* new_pointer = Allocate(new_size, allocation.memory_type, AllocationStrategy::BEST_FIT, allocation.allocation_tag);
    if (!new_pointer) {
        return false;
    }

    // Copy old data to new location
    if (old_size > 0) {
        cudaMemcpy(new_pointer, *pointer, std::min(old_size, new_size), cudaMemcpyDefault);
    }

    // Deallocate old block
    DeallocateFromPoolInternal(*pointer, allocation.pool_id);

    // Update allocation info
    allocation.pointer = new_pointer;
    allocation.size = new_size;
    allocation.allocation_time = std::chrono::system_clock::now();

    *pointer = new_pointer;

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    RecordAllocationTime(duration);

    return true;
}

bool MemoryPoolManager::ResizeAllocation(void* pointer, size_t new_size) {
    return Reallocate(&pointer, new_size);
}

void* MemoryPoolManager::ReallocateFromPool(size_t pool_id, void* pointer, size_t new_size) {
    if (!pointer) {
        return AllocateFromPool(pool_id, new_size);
    }

    // For simplicity, we'll use the general reallocation
    if (Reallocate(&pointer, new_size)) {
        return pointer;
    }

    return nullptr;
}

PoolConfig MemoryPoolManager::GetPoolConfig(size_t pool_id) const {
    std::lock_guard<std::mutex> lock(pools_mutex_);

    auto it = pools_.find(pool_id);
    if (it != pools_.end()) {
        return it->second;
    }

    PoolConfig empty_config;
    empty_config.pool_id = 0;
    return empty_config;
}

std::vector<PoolConfig> MemoryPoolManager::GetAllPoolConfigs() const {
    std::lock_guard<std::mutex> lock(pools_mutex_);

    std::vector<PoolConfig> configs;
    for (const auto& pair : pools_) {
        configs.push_back(pair.second);
    }

    return configs;
}

PoolMetrics MemoryPoolManager::GetPoolMetrics() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    PoolMetrics metrics;
    metrics.total_pools = pools_.size();
    metrics.total_memory_bytes = total_reserved_memory_.load();
    metrics.allocated_memory_bytes = total_allocated_memory_.load();
    metrics.free_memory_bytes = total_free_memory_.load();
    metrics.reserved_memory_bytes = total_reserved_memory_.load();

    // Calculate utilization
    if (metrics.total_memory_bytes > 0) {
        metrics.overall_utilization =
            (static_cast<double>(metrics.allocated_memory_bytes) /
             static_cast<double>(metrics.total_memory_bytes)) * 100.0;
    } else {
        metrics.overall_utilization = 0.0;
    }

    // Calculate fragmentation
    metrics.fragmentation_percentage = GetFragmentationPercentage();

    // Performance metrics
    if (!allocation_times_.empty()) {
        auto total_time = std::accumulate(allocation_times_.begin(), allocation_times_.end(),
                                        std::chrono::microseconds(0));
        metrics.average_allocation_time = total_time / allocation_times_.size();
    }

    if (!deallocation_times_.empty()) {
        auto total_time = std::accumulate(deallocation_times_.begin(), deallocation_times_.end(),
                                        std::chrono::microseconds(0));
        metrics.average_deallocation_time = total_time / deallocation_times_.size();
    }

    metrics.allocations_per_second = GetAllocationsPerSecond();
    metrics.deallocations_per_second = GetDeallocationsPerSecond();

    // Pool breakdown
    for (const auto& pair : pools_) {
        const PoolConfig& pool = pair.second;
        metrics.memory_by_type[pool.memory_type] += pool.total_size;
        metrics.blocks_by_size[pool.block_size] += pool.block_count;

        int utilization_bucket = static_cast<int>(pool.utilization_percentage / 10) * 10;
        metrics.pools_by_utilization[utilization_bucket]++;
    }

    metrics.last_updated = std::chrono::system_clock::now();
    return metrics;
}

std::vector<AllocationInfo> MemoryPoolManager::GetActiveAllocations() const {
    std::lock_guard<std::mutex> lock(allocations_mutex_);

    std::vector<AllocationInfo> active_allocations;
    for (const auto& pair : allocations_) {
        if (pair.second.is_active) {
            active_allocations.push_back(pair.second);
        }
    }

    return active_allocations;
}

size_t MemoryPoolManager::GetTotalAllocatedMemory() const {
    return total_allocated_memory_.load();
}

size_t MemoryPoolManager::GetTotalFreeMemory() const {
    return total_free_memory_.load();
}

size_t MemoryPoolManager::GetFragmentedMemory() const {
    std::lock_guard<std::mutex> lock(pools_mutex_);

    size_t fragmented_memory = 0;
    for (const auto& pair : pools_) {
        const PoolConfig& pool = pair.second;
        if (pool.free_blocks > 0 && pool.allocated_blocks > 0) {
            // Calculate fragmentation for this pool
            size_t pool_fragmentation = pool.free_blocks * pool.block_size;
            fragmented_memory += pool_fragmentation;
        }
    }

    return fragmented_memory;
}

double MemoryPoolManager::GetFragmentationPercentage() const {
    size_t total_memory = total_reserved_memory_.load();
    size_t fragmented_memory = GetFragmentedMemory();

    if (total_memory > 0) {
        return (static_cast<double>(fragmented_memory) / static_cast<double>(total_memory)) * 100.0;
    }

    return 0.0;
}

double MemoryPoolManager::GetUtilizationPercentage() const {
    size_t total_memory = total_reserved_memory_.load();
    size_t allocated_memory = total_allocated_memory_.load();

    if (total_memory > 0) {
        return (static_cast<double>(allocated_memory) / static_cast<double>(total_memory)) * 100.0;
    }

    return 0.0;
}

std::chrono::microseconds MemoryPoolManager::GetAverageAllocationTime() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    if (allocation_times_.empty()) {
        return std::chrono::microseconds(0);
    }

    auto total_time = std::accumulate(allocation_times_.begin(), allocation_times_.end(),
                                    std::chrono::microseconds(0));
    return total_time / allocation_times_.size();
}

std::chrono::microseconds MemoryPoolManager::GetAverageDeallocationTime() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    if (deallocation_times_.empty()) {
        return std::chrono::microseconds(0);
    }

    auto total_time = std::accumulate(deallocation_times_.begin(), deallocation_times_.end(),
                                    std::chrono::microseconds(0));
    return total_time / deallocation_times_.size();
}

int MemoryPoolManager::GetAllocationsPerSecond() const {
    // This would need time-based tracking for accurate calculation
    // Simplified implementation
    return total_allocations_.load() / 60; // Rough estimate
}

int MemoryPoolManager::GetDeallocationsPerSecond() const {
    // This would need time-based tracking for accurate calculation
    // Simplified implementation
    return total_deallocations_.load() / 60; // Rough estimate
}

std::vector<MemoryPoolRecommendation> MemoryPoolManager::GetOptimizationRecommendations() const {
    std::lock_guard<std::mutex> lock(pools_mutex_);

    std::vector<MemoryPoolRecommendation> recommendations;

    // Check for high fragmentation
    double fragmentation = GetFragmentationPercentage();
    if (fragmentation > FRAGMENTATION_THRESHOLD * 100.0) {
        MemoryPoolRecommendation rec;
        rec.description = "High memory fragmentation detected";
        rec.recommended_strategy = PoolStrategy::ADAPTIVE_POOLS;
        rec.expected_improvement = fragmentation / 100.0;
        rec.implementation_effort = std::chrono::milliseconds(1000);
        rec.priority = static_cast<int>(fragmentation);
        rec.suggest_defragmentation = true;
        rec.action_items.push_back("Run pool defragmentation");
        rec.action_items.push_back("Consider pool consolidation");
        recommendations.push_back(rec);
    }

    // Check for underutilized pools
    for (const auto& pair : pools_) {
        const PoolConfig& pool = pair.second;
        if (pool.utilization_percentage < 25.0 && pool.total_size > 1024 * 1024) {
            MemoryPoolRecommendation rec;
            rec.description = "Underutilized pool detected: " + pool.name;
            rec.recommended_strategy = current_strategy_;
            rec.expected_improvement = (100.0 - pool.utilization_percentage) / 100.0;
            rec.implementation_effort = std::chrono::milliseconds(500);
            rec.priority = static_cast<int>(pool.utilization_percentage);
            rec.suggest_pool_resizing = true;
            rec.action_items.push_back("Reduce pool size or merge with other pools");
            recommendations.push_back(rec);
        }
    }

    return recommendations;
}

bool MemoryPoolManager::OptimizePools() {
    std::lock_guard<std::mutex> lock(pools_mutex_);

    bool optimized = false;

    // Defragment pools if needed
    if (GetFragmentationPercentage() > FRAGMENTATION_THRESHOLD * 100.0) {
        optimized |= DefragmentPools();
    }

    // Rebalance pools
    optimized |= RebalancePools();

    // Compact pools
    CompactPools();

    return optimized;
}

bool MemoryPoolManager::DefragmentPools() {
    // Simplified defragmentation implementation
    // In a real implementation, this would move allocated blocks to reduce fragmentation

    bool defragmented = false;

    for (auto& pair : pools_) {
        PoolConfig& pool = pair.second;

        if (pool.free_blocks > pool.allocated_blocks) {
            // Pool has more free than allocated blocks, consider shrinking
            if (pool.block_count > pool.min_block_count) {
                size_t new_block_count = std::max(pool.allocated_blocks * 2, pool.min_block_count);
                if (ResizePool(pool.pool_id, new_block_count)) {
                    defragmented = true;
                }
            }
        }
    }

    return defragmented;
}

bool MemoryPoolManager::RebalancePools() {
    // Simplified pool rebalancing implementation
    // In a real implementation, this would analyze usage patterns and adjust pool sizes accordingly

    bool rebalanced = false;

    // Analyze access patterns and adjust pool sizes
    auto optimal_sizes = CalculateOptimalPoolSizes();

    for (size_t i = 0; i < optimal_sizes.size() && i < config_.default_pool_sizes.size(); ++i) {
        size_t optimal_size = optimal_sizes[i];
        size_t current_size = config_.default_pool_sizes[i];

        if (optimal_size != current_size) {
            // In a real implementation, this would create new pools with optimal sizes
            // and migrate allocations as needed
            rebalanced = true;
        }
    }

    return rebalanced;
}

void MemoryPoolManager::CompactPools() {
    std::lock_guard<std::mutex> lock(pools_mutex_);

    // Remove empty pools
    auto it = pools_.begin();
    while (it != pools_.end()) {
        const PoolConfig& pool = it->second;
        if (pool.allocated_blocks == 0 && pool.block_count == 0) {
            // Remove empty pool
            size_t pool_id = pool.pool_id;
            active_pool_ids_.erase(pool_id);
            it = pools_.erase(it);
        } else {
            ++it;
        }
    }

    // Trim allocation history if needed
    if (allocation_times_.size() > config_.allocation_history_size) {
        size_t remove_count = allocation_times_.size() - config_.allocation_history_size;
        allocation_times_.erase(allocation_times_.begin(),
                               allocation_times_.begin() + remove_count);
    }

    if (deallocation_times_.size() > config_.allocation_history_size) {
        size_t remove_count = deallocation_times_.size() - config_.allocation_history_size;
        deallocation_times_.erase(deallocation_times_.begin(),
                                 deallocation_times_.begin() + remove_count);
    }
}

void MemoryPoolManager::EnableGarbageCollection(bool enabled) {
    std::lock_guard<std::mutex> lock(gc_mutex_);

    if (enabled && !gc_enabled_) {
        gc_enabled_ = true;
        shutdown_requested_ = false;
        gc_thread_ = std::make_unique<std::thread>(
            &MemoryPoolManager::GarbageCollectionThread, this
        );
    } else if (!enabled && gc_enabled_) {
        gc_enabled_ = false;
        shutdown_requested_ = true;
        if (gc_thread_ && gc_thread_->joinable()) {
            gc_thread_->join();
        }
        gc_thread_.reset();
    }
}

void MemoryPoolManager::SetGCTimeInterval(std::chrono::seconds interval) {
    std::lock_guard<std::mutex> lock(gc_mutex_);
    gc_interval_ = interval;
}

void MemoryPoolManager::ForceGarbageCollection() {
    std::lock_guard<std::mutex> lock(gc_mutex_);
    CleanupIdleAllocations();
    ReclaimUnusedMemory();
    CompactPools();
}

bool MemoryPoolManager::ReclaimIdleMemory(std::chrono::seconds idle_threshold) {
    std::lock_guard<std::mutex> lock(pools_mutex_);
    std::lock_guard<std::mutex> alloc_lock(allocations_mutex_);

    auto now = std::chrono::system_clock::now();
    std::vector<void*> to_reclaim;

    for (auto& pair : allocations_) {
        AllocationInfo& allocation = pair.second;
        if (allocation.is_active &&
            (now - allocation.last_access_time) > idle_threshold &&
            allocation.can_be_reclaimed) {
            to_reclaim.push_back(pair.first);
        }
    }

    for (void* pointer : to_reclaim) {
        auto it = allocations_.find(pointer);
        if (it != allocations_.end()) {
            DeallocateFromPoolInternal(pointer, it->second.pool_id);
            total_allocated_memory_ -= it->second.size;
            allocations_.erase(it);
        }
    }

    return !to_reclaim.empty();
}

AllocationInfo MemoryPoolManager::GetAllocationInfo(void* pointer) const {
    std::lock_guard<std::mutex> lock(allocations_mutex_);

    auto it = allocations_.find(pointer);
    if (it != allocations_.end()) {
        return it->second;
    }

    AllocationInfo empty_info;
    empty_info.pointer = nullptr;
    empty_info.size = 0;
    empty_info.is_active = false;
    return empty_info;
}

void MemoryPoolManager::SetAllocationTag(void* pointer, const std::string& tag) {
    std::lock_guard<std::mutex> lock(allocations_mutex_);

    auto it = allocations_.find(pointer);
    if (it != allocations_.end()) {
        it->second.allocation_tag = tag;
    }
}

void MemoryPoolManager::SetAllocationPriority(void* pointer, int priority) {
    std::lock_guard<std::mutex> lock(allocations_mutex_);

    auto it = allocations_.find(pointer);
    if (it != allocations_.end()) {
        it->second.priority = priority;
    }
}

bool MemoryPoolManager::IsAllocationActive(void* pointer) const {
    std::lock_guard<std::mutex> lock(allocations_mutex_);

    auto it = allocations_.find(pointer);
    return it != allocations_.end() && it->second.is_active;
}

std::map<size_t, MemoryPoolManager::AccessPattern> MemoryPoolManager::AnalyzeAccessPatterns() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    return access_patterns_;
}

std::vector<size_t> MemoryPoolManager::GetOptimalPoolSizes() const {
    return CalculateOptimalPoolSizes();
}

void MemoryPoolManager::UpdateConfiguration(const ManagerConfig& config) {
    std::lock_guard<std::mutex> lock(pools_mutex_);
    config_ = config;
}

MemoryPoolManager::ManagerConfig MemoryPoolManager::GetCurrentConfiguration() const {
    std::lock_guard<std::mutex> lock(pools_mutex_);
    return config_;
}

json MemoryPoolManager::GetPoolAnalytics() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    json analytics;

    // Basic metrics
    PoolMetrics metrics = GetPoolMetrics();
    analytics["total_pools"] = metrics.total_pools;
    analytics["total_memory_bytes"] = metrics.total_memory_bytes;
    analytics["allocated_memory_bytes"] = metrics.allocated_memory_bytes;
    analytics["free_memory_bytes"] = metrics.free_memory_bytes;
    analytics["overall_utilization"] = metrics.overall_utilization;
    analytics["fragmentation_percentage"] = metrics.fragmentation_percentage;

    // Performance metrics
    analytics["average_allocation_time_us"] =
        std::chrono::duration_cast<std::chrono::microseconds>(metrics.average_allocation_time).count();
    analytics["average_deallocation_time_us"] =
        std::chrono::duration_cast<std::chrono::microseconds>(metrics.average_deallocation_time).count();
    analytics["allocations_per_second"] = metrics.allocations_per_second;
    analytics["deallocations_per_second"] = metrics.deallocations_per_second;

    // Pool breakdown
    analytics["memory_by_type"] = json::object();
    for (const auto& pair : metrics.memory_by_type) {
        analytics["memory_by_type"][std::to_string(static_cast<int>(pair.first))] = pair.second;
    }

    analytics["blocks_by_size"] = json::object();
    for (const auto& pair : metrics.blocks_by_size) {
        analytics["blocks_by_size"][std::to_string(pair.first)] = pair.second;
    }

    // Pool configurations
    analytics["pool_configs"] = json::array();
    auto pool_configs = GetAllPoolConfigs();
    for (const auto& config : pool_configs) {
        json pool_json = {
            {"pool_id", config.pool_id},
            {"memory_type", static_cast<int>(config.memory_type)},
            {"block_size", config.block_size},
            {"block_count", config.block_count},
            {"allocated_blocks", config.allocated_blocks},
            {"free_blocks", config.free_blocks},
            {"utilization_percentage", config.utilization_percentage},
            {"name", config.name}
        };
        analytics["pool_configs"].push_back(pool_json);
    }

    // Recommendations
    auto recommendations = GetOptimizationRecommendations();
    analytics["recommendations"] = json::array();
    for (const auto& rec : recommendations) {
        json rec_json = {
            {"description", rec.description},
            {"expected_improvement", rec.expected_improvement},
            {"priority", rec.priority},
            {"suggest_defragmentation", rec.suggest_defragmentation},
            {"suggest_pool_resizing", rec.suggest_pool_resizing},
            {"action_items", rec.action_items}
        };
        analytics["recommendations"].push_back(rec_json);
    }

    return analytics;
}

std::string MemoryPoolManager::GeneratePoolReport() const {
    auto analytics = GetPoolAnalytics();

    std::stringstream report;
    report << "=== Memory Pool Manager Report ===\n\n";

    // Basic metrics
    report << "Memory Usage:\n";
    report << "  Total Pools: " << analytics["total_pools"].get<int>() << "\n";
    report << "  Total Memory: " << analytics["total_memory_bytes"].get<size_t>() / (1024*1024) << " MB\n";
    report << "  Allocated Memory: " << analytics["allocated_memory_bytes"].get<size_t>() / (1024*1024) << " MB\n";
    report << "  Free Memory: " << analytics["free_memory_bytes"].get<size_t>() / (1024*1024) << " MB\n";
    report << "  Utilization: " << std::fixed << std::setprecision(1)
           << analytics["overall_utilization"].get<double>() << "%\n";
    report << "  Fragmentation: " << std::fixed << std::setprecision(1)
           << analytics["fragmentation_percentage"].get<double>() << "%\n\n";

    // Performance metrics
    report << "Performance:\n";
    report << "  Avg Allocation Time: " << analytics["average_allocation_time_us"].get<long long>() << " μs\n";
    report << "  Avg Deallocation Time: " << analytics["average_deallocation_time_us"].get<long long>() << " μs\n";
    report << "  Allocations/sec: " << analytics["allocations_per_second"].get<int>() << "\n";
    report << "  Deallocations/sec: " << analytics["deallocations_per_second"].get<int>() << "\n\n";

    // Pool breakdown
    report << "Pool Breakdown:\n";
    for (const auto& pool : analytics["pool_configs"]) {
        report << "  Pool " << pool["pool_id"].get<size_t>() << " (" << pool["name"].get<std::string>() << "):\n";
        report << "    Type: " << pool["memory_type"].get<int>() << "\n";
        report << "    Block Size: " << pool["block_size"].get<size_t>() / 1024 << " KB\n";
        report << "    Blocks: " << pool["allocated_blocks"].get<size_t>() << "/" << pool["block_count"].get<size_t>() << "\n";
        report << "    Utilization: " << std::fixed << std::setprecision(1)
               << pool["utilization_percentage"].get<double>() << "%\n";
    }

    // Recommendations
    if (analytics["recommendations"].size() > 0) {
        report << "\nRecommendations:\n";
        for (const auto& rec : analytics["recommendations"]) {
            report << "  - " << rec["description"].get<std::string>()
                   << " (Priority: " << rec["priority"].get<int>() << ")\n";
        }
    }

    return report.str();
}

void MemoryPoolManager::ExportPoolData(const std::string& filename) const {
    auto analytics = GetPoolAnalytics();

    std::ofstream file(filename);
    if (file.is_open()) {
        file << analytics.dump(2);
        file.close();
    }
}

void MemoryPoolManager::ExportAllocationHistory(const std::string& filename) const {
    std::lock_guard<std::mutex> lock(allocations_mutex_);

    json history = json::array();
    for (const auto& pair : allocations_) {
        const AllocationInfo& allocation = pair.second;
        json alloc_json = {
            {"pointer", reinterpret_cast<uintptr_t>(allocation.pointer)},
            {"size", allocation.size},
            {"pool_id", allocation.pool_id},
            {"memory_type", static_cast<int>(allocation.memory_type)},
            {"allocation_time", std::chrono::duration_cast<std::chrono::seconds>(
                allocation.allocation_time.time_since_epoch()).count()},
            {"last_access_time", std::chrono::duration_cast<std::chrono::seconds>(
                allocation.last_access_time.time_since_epoch()).count()},
            {"access_count", allocation.access_count},
            {"is_active", allocation.is_active},
            {"allocation_tag", allocation.allocation_tag},
            {"priority", allocation.priority}
        };
        history.push_back(alloc_json);
    }

    std::ofstream file(filename);
    if (file.is_open()) {
        file << history.dump(2);
        file.close();
    }
}

MemoryPoolManager::ErrorType MemoryPoolManager::GetLastError() const {
    std::lock_guard<std::mutex> lock(pools_mutex_);
    return last_error_;
}

std::string MemoryPoolManager::GetErrorMessage() const {
    std::lock_guard<std::mutex> lock(pools_mutex_);
    return last_error_message_;
}

bool MemoryPoolManager::AttemptErrorRecovery() {
    std::lock_guard<std::mutex> lock(pools_mutex_);

    switch (last_error_) {
        case ErrorType::CUDA_ERROR:
            // Reset CUDA context
            cudaDeviceReset();
            return InitializeDeviceProperties();

        case ErrorType::OUT_OF_MEMORY:
            // Try to reclaim memory
            ForceGarbageCollection();
            return true;

        case ErrorType::INITIALIZATION_FAILED:
            // Attempt re-initialization
            return Initialize(current_strategy_);

        default:
            return false;
    }
}

// Private methods

bool MemoryPoolManager::InitializeDeviceProperties() {
    cudaError_t result = cudaGetDeviceProperties(&device_properties_, device_id_);
    if (result != cudaSuccess) {
        SetError(ErrorType::CUDA_ERROR, "Failed to get device properties");
        return false;
    }

    total_device_memory_ = device_properties_.totalGlobalMem;
    available_device_memory_ = total_device_memory_;

    return true;
}

void MemoryPoolManager::InitializeDefaultPools() {
    // Create default pools based on strategy
    std::vector<size_t> pool_sizes = config_.default_pool_sizes;

    switch (current_strategy_) {
        case PoolStrategy::POWER_OF_TWO_POOLS:
            // Ensure power-of-two sizes
            for (size_t& size : pool_sizes) {
                size = RoundUpToPowerOfTwo(size);
            }
            break;

        case PoolStrategy::ADAPTIVE_POOLS:
            // Use calculated optimal sizes if available
            if (!access_patterns_.empty()) {
                pool_sizes = CalculateOptimalPoolSizes();
            }
            break;

        default:
            break;
    }

    // Create pools
    for (size_t block_size : pool_sizes) {
        if (block_size >= MIN_BLOCK_SIZE && block_size <= MAX_BLOCK_SIZE) {
            size_t pool_id = CreatePoolInternal(
                MemoryType::DEVICE_MEMORY,
                block_size,
                config_.initial_pool_capacity / block_size,
                "Pool_" + std::to_string(block_size)
            );
            if (pool_id > 0) {
                active_pool_ids_.insert(pool_id);
            }
        }
    }
}

size_t MemoryPoolManager::FindOrCreatePool(
    MemoryType memory_type,
    size_t size,
    AllocationStrategy strategy
) {
    // Find existing pool
    size_t pool_id = FindPoolByStrategy(size, memory_type, strategy);
    if (pool_id > 0) {
        return pool_id;
    }

    // Create new pool if needed
    size_t optimal_block_size = GetOptimalBlockSize(size);
    size_t initial_block_count = std::max(config_.initial_pool_capacity / optimal_block_size, size_t(1));

    return CreatePoolInternal(memory_type, optimal_block_size, initial_block_count, "AutoPool");
}

size_t MemoryPoolManager::CreatePoolInternal(
    MemoryType memory_type,
    size_t block_size,
    size_t initial_block_count,
    const std::string& pool_name
) {
    if (total_reserved_memory_ + (block_size * initial_block_count) > config_.max_total_memory) {
        SetError(ErrorType::OUT_OF_MEMORY, "Cannot create pool: would exceed maximum memory limit");
        return 0;
    }

    PoolConfig pool;
    pool.pool_id = next_pool_id_++;
    pool.memory_type = memory_type;
    pool.block_size = block_size;
    pool.block_count = initial_block_count;
    pool.total_size = block_size * initial_block_count;
    pool.allocated_blocks = 0;
    pool.free_blocks = initial_block_count;
    pool.utilization_percentage = 0.0;
    pool.last_access = std::chrono::system_clock::now();
    pool.allocation_count = 0;
    pool.deallocation_count = 0;
    pool.average_allocation_time = std::chrono::microseconds(0);
    pool.average_deallocation_time = std::chrono::microseconds(0);
    pool.allow_growth = true;
    pool.allow_shrinking = true;
    pool.max_block_count = initial_block_count * 4;
    pool.min_block_count = std::max(initial_block_count / 4, size_t(1));
    pool.growth_factor = config_.growth_factor;
    pool.shrink_threshold = config_.shrink_threshold;
    pool.name = pool_name.empty() ? "Pool_" + std::to_string(pool.pool_id) : pool_name;
    pool.created_time = std::chrono::system_clock::now();

    // Allocate initial blocks
    std::queue<void*> free_blocks;
    for (size_t i = 0; i < initial_block_count; ++i) {
        void* block = AllocateMemory(memory_type, block_size);
        if (block) {
            free_blocks.push(block);
        } else {
            // Cleanup on failure
            while (!free_blocks.empty()) {
                DeallocateMemory(memory_type, free_blocks.front(), block_size);
                free_blocks.pop();
            }
            SetError(ErrorType::ALLOCATION_FAILED, "Failed to allocate memory for pool");
            return 0;
        }
    }

    // Add pool to tracking
    pools_[pool.pool_id] = pool;
    free_blocks_[pool.pool_id] = free_blocks;
    active_pool_ids_.insert(pool.pool_id);
    pools_by_type_[memory_type].push_back(pool.pool_id);

    // Update memory tracking
    total_reserved_memory_ += pool.total_size;
    total_free_memory_ += pool.total_size;

    return pool.pool_id;
}

void* MemoryPoolManager::AllocateFromPoolInternal(size_t pool_id, size_t size, const std::string& tag) {
    auto pool_it = pools_.find(pool_id);
    if (pool_it == pools_.end()) {
        SetError(ErrorType::INVALID_POOL_ID, "Pool ID not found");
        return nullptr;
    }

    PoolConfig& pool = pool_it->second;
    auto free_blocks_it = free_blocks_.find(pool_id);

    if (free_blocks_it == free_blocks_.end() || free_blocks_it->second.empty()) {
        // No free blocks, try to grow pool
        if (pool.allow_growth && pool.block_count < pool.max_block_count) {
            size_t blocks_to_add = std::max(size_t(1), static_cast<size_t>(pool.block_count * (pool.growth_factor - 1.0)));
            blocks_to_add = std::min(blocks_to_add, pool.max_block_count - pool.block_count);

            for (size_t i = 0; i < blocks_to_add; ++i) {
                void* block = AllocateMemory(pool.memory_type, pool.block_size);
                if (block) {
                    free_blocks_[pool_id].push(block);
                    pool.block_count++;
                    pool.free_blocks++;
                    pool.total_size = pool.block_size * pool.block_count;
                }
            }
        }

        // Check again after growth attempt
        free_blocks_it = free_blocks_.find(pool_id);
        if (free_blocks_it == free_blocks_.end() || free_blocks_it->second.empty()) {
            SetError(ErrorType::OUT_OF_MEMORY, "No free blocks available in pool");
            return nullptr;
        }
    }

    // Allocate block
    void* pointer = free_blocks_it->second.front();
    free_blocks_it->second.pop();
    pool.free_blocks--;
    pool.allocated_blocks++;
    pool.allocation_count++;
    pool.last_access = std::chrono::system_clock::now();

    // Update utilization
    if (pool.block_count > 0) {
        pool.utilization_percentage =
            (static_cast<double>(pool.allocated_blocks) / static_cast<double>(pool.block_count)) * 100.0;
    }

    // Create allocation record
    AllocationInfo allocation;
    allocation.pointer = pointer;
    allocation.size = size;
    allocation.pool_id = pool_id;
    allocation.memory_type = pool.memory_type;
    allocation.allocation_time = std::chrono::system_clock::now();
    allocation.last_access_time = allocation.allocation_time;
    allocation.access_count = 1;
    allocation.is_active = true;
    allocation.allocation_tag = tag;
    allocation.owner_name = "";
    allocation.priority = 0;
    allocation.can_be_reclaimed = true;
    allocation.allocation_duration = std::chrono::microseconds(0);
    allocation.deallocation_duration = std::chrono::microseconds(0);

    allocations_[pointer] = allocation;

    // Update memory tracking
    total_allocated_memory_ += size;
    total_free_memory_ -= pool.block_size;

    return pointer;
}

void MemoryPoolManager::DeallocateFromPoolInternal(void* pointer, size_t pool_id) {
    auto pool_it = pools_.find(pool_id);
    if (pool_it == pools_.end()) {
        return;
    }

    PoolConfig& pool = pool_it->second;

    // Return block to pool
    free_blocks_[pool_id].push(pointer);
    pool.free_blocks++;
    pool.allocated_blocks--;
    pool.deallocation_count++;
    pool.last_access = std::chrono::system_clock::now();

    // Update utilization
    if (pool.block_count > 0) {
        pool.utilization_percentage =
            (static_cast<double>(pool.allocated_blocks) / static_cast<double>(pool.block_count)) * 100.0;
    }

    // Update memory tracking
    total_allocated_memory_ -= pool.block_size;
    total_free_memory_ += pool.block_size;

    // Check if pool should be shrunk
    if (pool.allow_shrinking &&
        pool.utilization_percentage < pool.shrink_threshold * 100.0 &&
        pool.block_count > pool.min_block_count) {
        ResizePoolIfNeeded(pool_id);
    }
}

void* MemoryPoolManager::AllocateMemory(MemoryType type, size_t size) {
    void* pointer = nullptr;
    cudaError_t result;

    switch (type) {
        case MemoryType::DEVICE_MEMORY:
            result = cudaMalloc(&pointer, size);
            break;

        case MemoryType::HOST_PINNED_MEMORY:
            result = cudaHostAlloc(&pointer, size, cudaHostAllocDefault);
            break;

        case MemoryType::UNIFIED_MEMORY:
            result = cudaMallocManaged(&pointer, size);
            break;

        case MemoryType::MANAGED_MEMORY:
            result = cudaMallocManaged(&pointer, size, cudaMemAttachGlobal);
            break;

        case MemoryType::ZERO_COPY_MEMORY:
            result = cudaHostAlloc(&pointer, size, cudaHostAllocMapped);
            break;

        default:
            result = cudaErrorMemoryAllocation;
            break;
    }

    if (result != cudaSuccess) {
        SetError(ErrorType::CUDA_ERROR, "CUDA memory allocation failed");
        return nullptr;
    }

    return pointer;
}

void MemoryPoolManager::DeallocateMemory(MemoryType type, void* pointer, size_t size) {
    if (!pointer) {
        return;
    }

    cudaError_t result;

    switch (type) {
        case MemoryType::DEVICE_MEMORY:
        case MemoryType::UNIFIED_MEMORY:
        case MemoryType::MANAGED_MEMORY:
            result = cudaFree(pointer);
            break;

        case MemoryType::HOST_PINNED_MEMORY:
        case MemoryType::ZERO_COPY_MEMORY:
            result = cudaFreeHost(pointer);
            break;

        default:
            result = cudaErrorInvalidValue;
            break;
    }

    if (result != cudaSuccess) {
        SetError(ErrorType::CUDA_ERROR, "CUDA memory deallocation failed");
    }
}

size_t MemoryPoolManager::FindBestFitPool(size_t size, MemoryType type) {
    size_t best_pool_id = 0;
    size_t min_waste = SIZE_MAX;

    auto type_it = pools_by_type_.find(type);
    if (type_it == pools_by_type_.end()) {
        return 0;
    }

    for (size_t pool_id : type_it->second) {
        auto pool_it = pools_.find(pool_id);
        if (pool_it != pools_.end() && pool_it->second.block_size >= size) {
            size_t waste = pool_it->second.block_size - size;
            if (waste < min_waste) {
                min_waste = waste;
                best_pool_id = pool_id;
            }
        }
    }

    return best_pool_id;
}

size_t MemoryPoolManager::FindFirstFitPool(size_t size, MemoryType type) {
    auto type_it = pools_by_type_.find(type);
    if (type_it == pools_by_type_.end()) {
        return 0;
    }

    for (size_t pool_id : type_it->second) {
        auto pool_it = pools_.find(pool_id);
        if (pool_it != pools_.end() && pool_it->second.block_size >= size) {
            return pool_id;
        }
    }

    return 0;
}

size_t MemoryPoolManager::FindPoolByStrategy(size_t size, MemoryType type, AllocationStrategy strategy) {
    switch (strategy) {
        case AllocationStrategy::BEST_FIT:
            return FindBestFitPool(size, type);

        case AllocationStrategy::FIRST_FIT:
            return FindFirstFitPool(size, type);

        default:
            return FindBestFitPool(size, type);
    }
}

void MemoryPoolManager::ResizePoolIfNeeded(size_t pool_id) {
    auto it = pools_.find(pool_id);
    if (it == pools_.end()) {
        return;
    }

    PoolConfig& pool = it->second;

    if (pool.utilization_percentage < pool.shrink_threshold * 100.0 &&
        pool.block_count > pool.min_block_count) {

        size_t new_block_count = std::max(
            static_cast<size_t>(pool.block_count * pool.shrink_threshold),
            pool.min_block_count
        );

        ResizePool(pool_id, new_block_count);
    }
}

void MemoryPoolManager::UpdateMemoryMetrics() {
    UpdateMemoryMetrics();
}

void MemoryPoolManager::UpdatePoolMetrics(size_t pool_id) {
    auto it = pools_.find(pool_id);
    if (it != pools_.end()) {
        PoolConfig& pool = it->second;

        if (pool.block_count > 0) {
            pool.utilization_percentage =
                (static_cast<double>(pool.allocated_blocks) / static_cast<double>(pool.block_count)) * 100.0;
        }

        pool.last_access = std::chrono::system_clock::now();
    }
}

void MemoryPoolManager::RecordAccessPattern(size_t size, std::chrono::system_clock::time_point timestamp) {
    if (!config_.enable_access_pattern_analysis) {
        return;
    }

    auto& pattern = access_patterns_[size];
    pattern.size = size;
    pattern.frequency++;
    pattern.access_times.push_back(timestamp);

    // Keep only recent access times
    if (pattern.access_times.size() > 1000) {
        pattern.access_times.erase(pattern.access_times.begin(),
                                 pattern.access_times.begin() + 100);
    }

    // Calculate average lifetime
    if (pattern.access_times.size() > 1) {
        auto time_diff = timestamp - pattern.access_times[0];
        pattern.average_lifetime = std::chrono::duration_cast<std::chrono::microseconds>(time_diff) / pattern.access_times.size();
    }
}

void MemoryPoolManager::AnalyzeAllocationPatterns() {
    // This would analyze historical allocation patterns to predict future needs
    // Implementation depends on specific requirements
}

std::vector<size_t> MemoryPoolManager::CalculateOptimalPoolSizes() const {
    std::vector<size_t> optimal_sizes;

    // Sort access patterns by frequency
    std::vector<std::pair<size_t, AccessPattern>> sorted_patterns;
    for (const auto& pair : access_patterns_) {
        sorted_patterns.push_back(pair);
    }

    std::sort(sorted_patterns.begin(), sorted_patterns.end(),
        [](const auto& a, const auto& b) {
            return a.second.frequency > b.second.frequency;
        });

    // Select top patterns as optimal pool sizes
    for (size_t i = 0; i < std::min(sorted_patterns.size(), size_t(8)); ++i) {
        optimal_sizes.push_back(sorted_patterns[i].first);
    }

    // Ensure we have at least some default sizes
    if (optimal_sizes.empty()) {
        optimal_sizes = config_.default_pool_sizes;
    }

    return optimal_sizes;
}

void MemoryPoolManager::GarbageCollectionThread() {
    while (!shutdown_requested_) {
        std::this_thread::sleep_for(gc_interval_);

        if (!shutdown_requested_) {
            CleanupIdleAllocations();
            ReclaimUnusedMemory();
            CompactPools();
        }
    }
}

void MemoryPoolManager::CleanupIdleAllocations() {
    // This would clean up allocations that have been idle for too long
    // Implementation depends on specific idle detection criteria
}

void MemoryPoolManager::ReclaimUnusedMemory() {
    // This would reclaim memory from underutilized pools
    // Implementation depends on specific utilization thresholds
}

void MemoryPoolManager::RecordAllocationTime(std::chrono::microseconds duration) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    allocation_times_.push_back(duration);
}

void MemoryPoolManager::RecordDeallocationTime(std::chrono::microseconds duration) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    deallocation_times_.push_back(duration);
}

void MemoryPoolManager::UpdatePerformanceMetrics() {
    // Update performance metrics based on recent allocations/deallocations
}

void MemoryPoolManager::SetError(ErrorType error, const std::string& message) {
    last_error_ = error;
    last_error_message_ = message;
}

bool MemoryPoolManager::RecoverFromError(ErrorType error) {
    // Implementation depends on specific error recovery strategies
    return false;
}

std::string MemoryPoolManager::GetMemoryTypeName(MemoryType type) const {
    switch (type) {
        case MemoryType::DEVICE_MEMORY: return "Device Memory";
        case MemoryType::HOST_PINNED_MEMORY: return "Host Pinned Memory";
        case MemoryType::UNIFIED_MEMORY: return "Unified Memory";
        case MemoryType::MANAGED_MEMORY: return "Managed Memory";
        case MemoryType::ZERO_COPY_MEMORY: return "Zero Copy Memory";
        default: return "Unknown";
    }
}

std::string MemoryPoolManager::GetPoolStrategyName(PoolStrategy strategy) const {
    switch (strategy) {
        case PoolStrategy::FIXED_SIZE_POOLS: return "Fixed Size Pools";
        case PoolStrategy::POWER_OF_TWO_POOLS: return "Power of Two Pools";
        case PoolStrategy::ADAPTIVE_POOLS: return "Adaptive Pools";
        case PoolStrategy::TIERED_POOLS: return "Tiered Pools";
        default: return "Unknown";
    }
}

size_t MemoryPoolManager::RoundUpToPowerOfTwo(size_t size) const {
    if (size == 0) return 0;

    size--;
    size |= size >> 1;
    size |= size >> 2;
    size |= size >> 4;
    size |= size >> 8;
    size |= size >> 16;
    size |= size >> 32;
    size++;

    return size;
}

size_t MemoryPoolManager::GetOptimalBlockSize(size_t requested_size) const {
    // Find the smallest pool block size that can accommodate the request
    for (size_t pool_size : config_.default_pool_sizes) {
        if (pool_size >= requested_size) {
            return pool_size;
        }
    }

    // If no pool is large enough, round up to next power of two
    return RoundUpToPowerOfTwo(requested_size);
}

bool MemoryPoolManager::CanFitInPool(size_t pool_id, size_t size) const {
    auto it = pools_.find(pool_id);
    return it != pools_.end() && it->second.block_size >= size;
}

// Factory function
std::unique_ptr<MemoryPoolManager> CreateMemoryPoolManager(int device_id) {
    return std::make_unique<MemoryPoolManager>(device_id);
}

} // namespace performance
} // namespace gpu
} // namespace keycuda