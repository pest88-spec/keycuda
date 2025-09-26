#include "puzzle71_kernel.h"

#include <cuda_runtime.h>

#include <algorithm>

#include "KeyhuntCore/adapters/bitcrack/keyfinder_adapter.h"

extern __global__ void keyFinderKernel(int points, int compression);

namespace puzzle71::kernel {

KernelLaunchConfig ChooseLaunchConfig(std::uint64_t desired_threads) {
    KernelLaunchConfig config{};

    // Get GPU device properties for optimal configuration
    cudaDeviceProp device_props;
    cudaError_t err = cudaGetDeviceProperties(&device_props, 0);
    if (err != cudaSuccess) {
        // Fallback to conservative defaults if device query fails
        device_props.multiProcessorCount = 28;
        device_props.maxThreadsPerBlock = 1024;
        device_props.maxThreadsPerMultiProcessor = 2048;
        device_props.maxGridSize[0] = 2147483647;
    }

    // Use occupancy API to get optimal block size
    int min_grid = 0;
    int block_size = 0;
    cudaError_t occ_status = cudaOccupancyMaxPotentialBlockSize(&min_grid, &block_size, keyFinderKernel, 0, 0);
    if (occ_status != cudaSuccess || block_size <= 0) {
        block_size = std::min(static_cast<int>(device_props.maxThreadsPerBlock), 1024);
    }

    // Calculate optimal grid size to fully utilize all SMs
    unsigned int sm_count = device_props.multiProcessorCount;
    unsigned int max_blocks_per_sm = device_props.maxThreadsPerMultiProcessor / block_size;
    unsigned int optimal_blocks = sm_count * std::max<unsigned int>(max_blocks_per_sm, 1);

    // Don't exceed device limits but maximize utilization
    std::uint64_t blocks = std::min<std::uint64_t>(optimal_blocks,
                                                  static_cast<std::uint64_t>(device_props.maxGridSize[0]));

    // Adjust blocks based on desired threads
    if (desired_threads > 0) {
        std::uint64_t needed_blocks = (desired_threads + block_size - 1) / block_size;
        blocks = std::min(blocks, needed_blocks);
    }

    // Ensure at least minimum grid from occupancy analysis
    blocks = std::max<std::uint64_t>(blocks, static_cast<std::uint64_t>(min_grid > 0 ? min_grid : 1));

    config.block = dim3(static_cast<unsigned int>(block_size), 1, 1);
    config.grid = dim3(static_cast<unsigned int>(blocks), 1, 1);
    config.batch_size = config.block.x * config.grid.x;

    return config;
}

}  // namespace puzzle71::kernel
