#pragma once

#include <cuda_runtime.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include <mutex>

namespace puzzle71::gpu {

/**
 * GPU Memory Manager - Optimized memory allocation and monitoring
 *
 * Features:
 * - Memory pool for fast allocation/deallocation
 * - Real-time memory usage tracking
 * - Automatic memory cleanup
 * - Configurable memory limits
 * - Memory fragmentation prevention
 */
class MemoryManager {
public:
    struct MemoryStats {
        std::size_t total_allocated_mb{0};
        std::size_t peak_allocated_mb{0};
        std::size_t pool_free_mb{0};
        std::size_t device_total_mb{0};
        std::size_t available_mb{0};
        std::size_t allocation_count{0};
        double utilization_percent{0.0};
    };

    struct MemoryBlock {
        void* ptr{nullptr};
        std::size_t size{0};
        bool in_use{false};
        std::uint64_t allocation_id{0};
    };

    explicit MemoryManager(int device_id, std::size_t max_memory_mb = 0);
    ~MemoryManager();

    // Disable copying
    MemoryManager(const MemoryManager&) = delete;
    MemoryManager& operator=(const MemoryManager&) = delete;

    // Enable moving
    MemoryManager(MemoryManager&&) noexcept = default;
    MemoryManager& operator=(MemoryManager&&) noexcept = default;

    /**
     * Allocate memory with pool optimization
     */
    void* Allocate(std::size_t size, bool use_pool = true);

    /**
     * Deallocate memory back to pool
     */
    void Deallocate(void* ptr);

    /**
     * Get current memory statistics
     */
    MemoryStats GetStats() const;

    /**
     * Optimize memory pool - defragment and release unused memory
     */
    void OptimizePool();

    /**
     * Set memory usage limits
     */
    void SetMemoryLimit(std::size_t max_mb);

    /**
     * Check if memory is available for allocation
     */
    bool IsMemoryAvailable(std::size_t size_mb) const;

    /**
     * Get memory utilization efficiency (0-100%)
     */
    double GetMemoryEfficiency() const;

    /**
     * Force cleanup of all allocated memory
     */
    void Cleanup();

private:
    int device_id_{0};
    std::size_t max_memory_mb_{0};
    std::size_t device_total_memory_mb_{0};

    mutable std::mutex mutex_;
    std::vector<MemoryBlock> memory_pool_;
    std::vector<std::unique_ptr<std::uint8_t[]>> large_allocations_;

    std::size_t total_allocated_{0};
    std::size_t peak_allocated_{0};
    std::size_t allocation_counter_{0};
    bool initialized_{false};

    void InitializeDevice();
    MemoryBlock* FindFreeBlock(std::size_t size);
    void AddBlockToPool(std::size_t size);
    void UpdateStats();
    std::size_t RoundUpToPoolSize(std::size_t size) const;

    // Pool sizes for efficient memory management (powers of 2)
    static constexpr std::size_t kPoolSizes[] = {
        1024,        // 1KB
        4096,        // 4KB
        16384,       // 16KB
        65536,       // 64KB
        262144,      // 256KB
        1048576,     // 1MB
        4194304,     // 4MB
        16777216,    // 16MB
        67108864,    // 64MB
        268435456,   // 256MB
        1073741824   // 1GB
    };
};

/**
 * RAII Memory Guard for automatic memory management
 */
template<typename T>
class DeviceMemoryGuard {
public:
    DeviceMemoryGuard(MemoryManager& manager, std::size_t count = 1)
        : manager_(manager), count_(count) {
        ptr_ = static_cast<T*>(manager_.Allocate(sizeof(T) * count));
    }

    ~DeviceMemoryGuard() {
        if (ptr_) {
            manager_.Deallocate(ptr_);
        }
    }

    // Disable copying
    DeviceMemoryGuard(const DeviceMemoryGuard&) = delete;
    DeviceMemoryGuard& operator=(const DeviceMemoryGuard&) = delete;

    // Enable moving
    DeviceMemoryGuard(DeviceMemoryGuard&& other) noexcept
        : manager_(other.manager_), ptr_(other.ptr_), count_(other.count_) {
        other.ptr_ = nullptr;
    }

    DeviceMemoryGuard& operator=(DeviceMemoryGuard&& other) noexcept {
        if (this != &other) {
            if (ptr_) {
                manager_.Deallocate(ptr_);
            }
            manager_ = other.manager_;
            ptr_ = other.ptr_;
            count_ = other.count_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    T* get() const { return ptr_; }
    T* operator->() const { return ptr_; }
    T& operator*() const { return *ptr_; }
    std::size_t size() const { return count_; }
    operator bool() const { return ptr_ != nullptr; }

private:
    MemoryManager& manager_;
    T* ptr_{nullptr};
    std::size_t count_{0};
};

}  // namespace puzzle71::gpu