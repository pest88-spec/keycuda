/**
 * @file memory_manager.cuh
 * @brief Header for GPU memory management with Structure-of-Arrays optimization
 */

#pragma once

#include <cuda_runtime.h>
#include <cstdint>
#include <vector>

namespace keyhunt {
namespace gpu {

/**
 * @brief Structure-of-Arrays layout for ECC points
 */
struct ECCPointsSoA {
    uint32_t* x;        // Array of X coordinates (8 uint32_t per point)
    uint32_t* y;        // Array of Y coordinates (8 uint32_t per point)
    size_t count;       // Number of points
    size_t pitch_x;     // Pitch in bytes for X array
    size_t pitch_y;     // Pitch in bytes for Y array

    ECCPointsSoA() : x(nullptr), y(nullptr), count(0), pitch_x(0), pitch_y(0) {}
    bool isAllocated() const { return x != nullptr && y != nullptr && count > 0; }
    size_t getMemoryFootprint() const { return count * 8 * sizeof(uint32_t) * 2; }
};

// Function declarations
ECCPointsSoA allocateCoalescedPoints(size_t count);
void deallocateCoalescedPoints(ECCPointsSoA& points);
void copyPointsHostToDevice(ECCPointsSoA& dst, const uint32_t* src_x, const uint32_t* src_y, size_t count);
void copyPointsDeviceToHost(uint32_t* dst_x, uint32_t* dst_y, const ECCPointsSoA& src, size_t count);
std::pair<uint32_t*, uint32_t*> allocatePinnedHostPoints(size_t count);
void freePinnedHostPoints(uint32_t* h_x, uint32_t* h_y);
std::vector<ECCPointsSoA> allocateBatchedPoints(size_t batchSize, size_t numBatches);
void deallocateBatchedPoints(std::vector<ECCPointsSoA>& batches);

} // namespace gpu
} // namespace keyhunt