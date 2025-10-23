/**
 * @file types.h
 * @brief GPU types and structures for puzzle71 kernel operations
 *
 * T029: Compatibility layer for extracted BitCrack integration
 *
 * This file provides the GPU types that were referenced in the original
 * puzzle71_kernel.h but missing from the extracted sources. Created to
 * maintain build compatibility during integration optimization.
 *
 * @author T029 Implementation Team
 * @date 2025-10-22
 * @origin BitCrack - CudaKeySearchDevice (extracted)
 * @license MIT
 */

#pragma once

#include <cuda_runtime.h>
#include <cstdint>
#include <cstring>
#include "core/uint256.h"

namespace puzzle71::gpu {

/**
 * @brief Device candidate structure for potential private keys
 */
struct DeviceCandidate {
    uint64_t x[4];                // Public key X coordinate (4 x 64-bit)
    uint64_t y[4];                // Public key Y coordinate (4 x 64-bit)
    uint32_t digest[5];           // RIPEMD160 digest of public key (5 x 32-bit)
    uint32_t block;               // Block that found this candidate
    uint32_t thread;              // Thread that found this candidate
    uint32_t idx;                 // Index within thread
    uint32_t compressed;          // Compression flag

    __device__ __host__
    DeviceCandidate() : block(0), thread(0), idx(0), compressed(0) {
        memset(x, 0, sizeof(x));
        memset(y, 0, sizeof(y));
        memset(digest, 0, sizeof(digest));
    }
};

/**
 * @brief Device result buffer for managing GPU search results
 */
struct DeviceResultBuffer {
    DeviceCandidate* candidates;  // Array of candidates
    uint32_t capacity;            // Maximum number of candidates
    uint32_t* count;              // Pointer to count of found candidates
    uint32_t* dropped;            // Pointer to dropped candidates count
    uint64_t total_keys_checked;  // Total keys processed

    __device__ __host__ constexpr
    DeviceResultBuffer() : candidates(nullptr), capacity(0),
                          count(nullptr), dropped(nullptr), total_keys_checked(0) {}
};

// Forward declaration - BatchConfig is defined in ComputeCore/gpu/batch_planner.h
struct BatchConfig;

/**
 * @brief GPU memory buffer management
 */
class DeviceBuffer {
public:
    DeviceBuffer();
    ~DeviceBuffer();

    bool allocate(size_t size);
    void deallocate();
    void* get_device_ptr() const { return device_ptr_; }
    size_t get_size() const { return size_; }
    bool is_allocated() const { return device_ptr_ != nullptr; }

    template<typename T>
    bool copy_to_device(const T* host_data, size_t count) {
        if (!is_allocated() || size_ < count * sizeof(T)) {
            return false;
        }
        cudaError_t result = cudaMemcpy(device_ptr_, host_data,
                                       count * sizeof(T), cudaMemcpyHostToDevice);
        return result == cudaSuccess;
    }

    template<typename T>
    bool copy_to_host(T* host_data, size_t count) {
        if (!is_allocated() || size_ < count * sizeof(T)) {
            return false;
        }
        cudaError_t result = cudaMemcpy(host_data, device_ptr_,
                                       count * sizeof(T), cudaMemcpyDeviceToHost);
        return result == cudaSuccess;
    }

private:
    void* device_ptr_;
    size_t size_;
};

} // namespace puzzle71::gpu