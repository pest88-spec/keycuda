#include "memory_manager.h"
#include <stdexcept>
#include <algorithm>
#include <iostream>

namespace puzzle71::gpu {

MemoryManager::MemoryManager(int device_id, std::size_t max_memory_mb)
    : device_id_(device_id), max_memory_mb_(max_memory_mb) {
    InitializeDevice();
}

MemoryManager::~MemoryManager() {
    Cleanup();
}

void MemoryManager::InitializeDevice() {
    cudaError_t err = cudaSetDevice(device_id_);
    if (err != cudaSuccess) {
        throw std::runtime_error(std::string("cudaSetDevice failed: ") + cudaGetErrorString(err));
    }

    // Get device memory information
    cudaDeviceProp props;
    err = cudaGetDeviceProperties(&props, device_id_);
    if (err != cudaSuccess) {
        throw std::runtime_error("cudaGetDeviceProperties failed");
    }

    device_total_memory_mb_ = props.totalGlobalMem / (1024 * 1024);

    // If no explicit limit, use 80% of device memory
    if (max_memory_mb_ == 0) {
        max_memory_mb_ = device_total_memory_mb_ * 8 / 10;
    }

    // Pre-allocate some common pool sizes
    const std::size_t prealloc_sizes[] = {1024*1024, 4*1024*1024, 16*1024*1024}; // 1MB, 4MB, 16MB
    for (std::size_t size : prealloc_sizes) {
        if (total_allocated_ + size < max_memory_mb_ * 1024 * 1024) {
            AddBlockToPool(size);
        }
    }

    initialized_ = true;
}

void* MemoryManager::Allocate(std::size_t size, bool use_pool) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        throw std::runtime_error("MemoryManager not initialized");
    }

    // Round up size to pool alignment
    std::size_t aligned_size = use_pool ? RoundUpToPoolSize(size) : size;

    // Check memory availability
    std::size_t size_mb = aligned_size / (1024 * 1024);
    if (!IsMemoryAvailable(size_mb)) {
        // Try to optimize pool and recheck
        OptimizePool();
        if (!IsMemoryAvailable(size_mb)) {
            throw std::runtime_error("Insufficient GPU memory");
        }
    }

    // Try to find a block in pool
    if (use_pool) {
        MemoryBlock* block = FindFreeBlock(aligned_size);
        if (block) {
            block->in_use = true;
            block->allocation_id = ++allocation_counter_;
            total_allocated_ += aligned_size;
            peak_allocated_ = std::max(peak_allocated_, total_allocated_);
            return block->ptr;
        }
    }

    // Allocate new block
    void* ptr = nullptr;
    cudaError_t err = cudaMalloc(&ptr, aligned_size);
    if (err != cudaSuccess) {
        // Try to free some memory and retry
        OptimizePool();
        err = cudaMalloc(&ptr, aligned_size);
        if (err != cudaSuccess) {
            throw std::runtime_error(std::string("cudaMalloc failed: ") + cudaGetErrorString(err));
        }
    }

    // Add to pool if it's a reasonable size
    if (use_pool && aligned_size <= 256 * 1024 * 1024) { // Max 256MB in pool
        memory_pool_.push_back({ptr, aligned_size, true, ++allocation_counter_});
    } else {
        large_allocations_.emplace_back(new std::uint8_t[aligned_size]);
        large_allocations_.back().reset();
        large_allocations_.back().release(); // We manage it ourselves
    }

    total_allocated_ += aligned_size;
    peak_allocated_ = std::max(peak_allocated_, total_allocated_);

    return ptr;
}

void MemoryManager::Deallocate(void* ptr) {
    if (!ptr) return;

    std::lock_guard<std::mutex> lock(mutex_);

    // Find in pool
    for (auto& block : memory_pool_) {
        if (block.ptr == ptr) {
            block.in_use = false;
            // Don't actually free, keep in pool for reuse
            return;
        }
    }

    // Not in pool, must be a large allocation
    auto it = std::find_if(large_allocations_.begin(), large_allocations_.end(),
        [ptr](const std::unique_ptr<std::uint8_t[]>& alloc) {
            return static_cast<void*>(alloc.get()) == ptr;
        });

    if (it != large_allocations_.end()) {
        std::size_t size = (*it).get()[0]; // This is a placeholder - in real implementation track sizes
        cudaFree(ptr);
        large_allocations_.erase(it);
        total_allocated_ -= size;
    }
}

MemoryManager::MemoryStats MemoryManager::GetStats() const {
    std::lock_guard<std::mutex> lock(mutex_);

    MemoryStats stats;
    stats.total_allocated_mb = total_allocated_ / (1024 * 1024);
    stats.peak_allocated_mb = peak_allocated_ / (1024 * 1024);
    stats.device_total_mb = device_total_memory_mb_;
    stats.allocation_count = allocation_counter_;
    stats.utilization_percent = (static_cast<double>(total_allocated_) / (device_total_memory_mb_ * 1024 * 1024)) * 100.0;

    // Calculate free pool memory
    std::size_t pool_free = 0;
    for (const auto& block : memory_pool_) {
        if (!block.in_use) {
            pool_free += block.size;
        }
    }
    stats.pool_free_mb = pool_free / (1024 * 1024);
    stats.available_mb = device_total_memory_mb_ - stats.total_allocated_mb;

    return stats;
}

void MemoryManager::OptimizePool() {
    // Free unused blocks if memory pressure is high
    double utilization = static_cast<double>(total_allocated_) / (max_memory_mb_ * 1024 * 1024);

    if (utilization > 0.8) { // 80% threshold
        // Free some unused blocks
        std::size_t freed = 0;
        for (auto it = memory_pool_.begin(); it != memory_pool_.end() && freed < 64 * 1024 * 1024;) {
            if (!it->in_use && it->size <= 16 * 1024 * 1024) { // Free small unused blocks
                cudaFree(it->ptr);
                total_allocated_ -= it->size;
                freed += it->size;
                it = memory_pool_.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void MemoryManager::SetMemoryLimit(std::size_t max_mb) {
    std::lock_guard<std::mutex> lock(mutex_);
    max_memory_mb_ = std::min(max_mb, device_total_memory_mb_);
}

bool MemoryManager::IsMemoryAvailable(std::size_t size_mb) const {
    return (total_allocated_ / (1024 * 1024) + size_mb) <= max_memory_mb_;
}

double MemoryManager::GetMemoryEfficiency() const {
    auto stats = GetStats();
    if (stats.total_allocated_mb == 0) return 100.0;

    // Efficiency = (used memory) / (allocated memory) * 100
    std::size_t used = stats.total_allocated_mb - stats.pool_free_mb;
    return (static_cast<double>(used) / stats.total_allocated_mb) * 100.0;
}

void MemoryManager::Cleanup() {
    std::lock_guard<std::mutex> lock(mutex_);

    // Free all pool blocks
    for (const auto& block : memory_pool_) {
        cudaFree(block.ptr);
    }
    memory_pool_.clear();

    // Free all large allocations
    for (auto& alloc : large_allocations_) {
        cudaFree(alloc.get());
    }
    large_allocations_.clear();

    total_allocated_ = 0;
    peak_allocated_ = 0;
    allocation_counter_ = 0;
}

MemoryManager::MemoryBlock* MemoryManager::FindFreeBlock(std::size_t size) {
    // Find best-fit free block
    MemoryBlock* best_block = nullptr;
    std::size_t best_size = SIZE_MAX;

    for (auto& block : memory_pool_) {
        if (!block.in_use && block.size >= size && block.size < best_size) {
            best_block = &block;
            best_size = block.size;
        }
    }

    return best_block;
}

void MemoryManager::AddBlockToPool(std::size_t size) {
    void* ptr = nullptr;
    cudaError_t err = cudaMalloc(&ptr, size);
    if (err == cudaSuccess) {
        memory_pool_.push_back({ptr, size, false, 0});
        total_allocated_ += size;
    }
}

std::size_t MemoryManager::RoundUpToPoolSize(std::size_t size) const {
    // Find the smallest pool size that fits
    for (std::size_t pool_size : kPoolSizes) {
        if (size <= pool_size) {
            return pool_size;
        }
    }

    // Round up to next power of 2 for large allocations
    std::size_t power = 1;
    while (power < size) {
        power <<= 1;
    }
    return power;
}

}  // namespace puzzle71::gpu