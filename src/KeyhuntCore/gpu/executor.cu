/**
 * @file executor.cu
 * @brief Implementation of GPU executor with parallel batch indexing
 *
 * Implements T031: Replace serial batch indexing with CUB BlockScan
 * Provides CUDA kernels and device functions for parallel operations.
 */

#include "executor.cuh"
#include <thrust/device_vector.h>
#include <thrust/transform.h>
#include <thrust/scan.h>
#include <thrust/iterator/counting_iterator.h>
#include <stdexcept>

namespace keyhunt {
namespace gpu {

// Implementation is in the header file for template functions
// This file contains non-template implementations and specializations

cudaError_t computeBlockOffsets(
    const size_t batchSize,
    const uint32_t gridSize,
    uint32_t* d_blockOffsets)
{
    // Items per block calculation kernel
    uint32_t blockSize = 256;

    // Use thrust transform to compute items per block
    thrust::device_vector<uint32_t> d_itemsPerBlock(gridSize);

    thrust::transform(thrust::device,
        thrust::counting_iterator<uint32_t>(0),
        thrust::counting_iterator<uint32_t>(gridSize),
        d_itemsPerBlock.begin(),
        [batchSize, blockSize] __device__ (uint32_t blockId) -> uint32_t {
            uint32_t startIdx = blockId * blockSize;
            uint32_t endIdx = min(startIdx + blockSize, static_cast<uint32_t>(batchSize));
            return max(0, static_cast<int>(endIdx) - static_cast<int>(startIdx));
        });

    // Compute exclusive prefix sum
    thrust::exclusive_scan(thrust::device,
                         d_itemsPerBlock.begin(),
                         d_itemsPerBlock.end(),
                         thrust::device_ptr<uint32_t>(d_blockOffsets));

    return cudaGetLastError();
}

} // namespace gpu
} // namespace keyhunt