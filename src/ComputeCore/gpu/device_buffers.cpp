#include "ComputeCore/gpu/device_buffers.h"
#include "ComputeCore/gpu/batch_planner.h"

#include <algorithm>
#include <stdexcept>

#include <cub/cub.cuh>
#include <cuda_runtime.h>

namespace puzzle71::gpu {

namespace {

core::UInt256 Increment(const core::UInt256& value, std::uint64_t delta) {
    core::UInt256 out = value;
    out.AddUint64(delta);
    return out;
}

/**
 * @brief CUDA kernel for parallel scalar generation using CUB BlockScan (T031)
 *
 * Each thread generates a sequence of scalar values using parallel prefix sum.
 * Replaces the serial for loop with efficient parallel computation.
 */
__global__ void parallelScalarGenerationKernel(
    const core::UInt256 start_value,
    core::UInt256* scalars,
    const std::uint64_t count)
{
    // Specialize BlockScan for uint64_t prefix sums
    typedef cub::BlockScan<std::uint64_t, 256> BlockScanT;

    // Allocate shared memory for BlockScan
    __shared__ typename BlockScanT::TempStorage temp_storage;

    // Thread identification
    const std::uint64_t global_tid = blockIdx.x * blockDim.x + threadIdx.x;
    const std::uint64_t stride = blockDim.x * gridDim.x;

    // Each thread processes multiple elements if necessary
    for (std::uint64_t base_idx = global_tid; base_idx < count; base_idx += stride) {
        // This thread's contribution (always 1 for sequential increment)
        std::uint64_t thread_increment = 1;

        // Compute prefix sum to get this thread's starting offset
        std::uint64_t thread_offset;
        BlockScanT(temp_storage).ExclusiveSum(thread_increment, thread_offset);

        // Synchronize to ensure all threads have their offsets
        __syncthreads();

        // Generate the scalar value for this thread's element
        core::UInt256 current = start_value;
        current.AddUint64(base_idx + thread_offset);

        // Store result
        if (base_idx < count) {
            scalars[base_idx] = current;
        }

        // Synchronize before next iteration
        __syncthreads();
    }
}

/**
 * @brief Simplified CUDA kernel for batch scalar generation (T031)
 *
 * More efficient version that directly computes scalars without prefix scan,
 * since each scalar is just start_value + index.
 */
__global__ void batchScalarGenerationKernel(
    const core::UInt256 start_value,
    core::UInt256* scalars,
    const std::uint64_t count)
{
    const std::uint64_t global_tid = blockIdx.x * blockDim.x + threadIdx.x;
    const std::uint64_t stride = blockDim.x * gridDim.x;

    // Each thread directly computes its assigned scalars
    for (std::uint64_t idx = global_tid; idx < count; idx += stride) {
        // Direct computation: scalar = start_value + index
        core::UInt256 current = start_value;
        current.AddUint64(idx);
        scalars[idx] = current;
    }
}

}  // namespace

DeviceBuffers::DeviceBuffers() = default;

DeviceBuffers::~DeviceBuffers() {
    Release();
}

DeviceBuffers::DeviceBuffers(DeviceBuffers&& other) noexcept {
    Swap(other);
}

DeviceBuffers& DeviceBuffers::operator=(DeviceBuffers&& other) noexcept {
    if (this != &other) {
        Release();
        Swap(other);
    }
    return *this;
}

void DeviceBuffers::Release() {
    host_scalars_.clear();
}

void DeviceBuffers::Swap(DeviceBuffers& other) noexcept {
    std::swap(grid_, other.grid_);
    std::swap(block_, other.block_);
    host_scalars_.swap(other.host_scalars_);
}

void DeviceBuffers::Configure(dim3 grid, dim3 block, int points_per_thread) {
    grid_ = grid;
    block_ = block;
    points_per_thread_ = std::max(points_per_thread, 1);

    std::uint64_t threads = static_cast<std::uint64_t>(grid.x) * block.x;
    if (threads == 0) {
        threads = 1;
    }

    std::uint64_t total = threads * static_cast<std::uint64_t>(points_per_thread_);
    if (total == 0 || total > kMaxKeysPerBatch) {
        throw std::runtime_error("DeviceBuffers::Configure exceeds batch limits");
    }

    slots_ = static_cast<std::size_t>(total);
    host_scalars_.resize(slots_);
}

DeviceBatch DeviceBuffers::PrepareBatch(const core::UInt256& start, std::uint64_t batch_size) {
    if (host_scalars_.empty()) {
        Configure(dim3(1,1,1), dim3(1,1,1), 1);
    }

    if (batch_size == 0) {
        throw std::runtime_error("DeviceBuffers::PrepareBatch called with zero batch size");
    }

    if (batch_size != slots_) {
        throw std::runtime_error("Batch size mismatch with configured device buffers");
    }

    std::uint64_t span = std::min<std::uint64_t>(batch_size, slots_);

    // Replace serial loop with parallel CUDA kernel (T031)
    core::UInt256* d_scalars;
    cudaMalloc(&d_scalars, span * sizeof(core::UInt256));

    // Choose optimal launch configuration
    int blockSize = 256;
    int gridSize = (span + blockSize - 1) / blockSize;
    gridSize = std::min(gridSize, 65535); // Max grid size

    // Launch parallel scalar generation kernel
    batchScalarGenerationKernel<<<gridSize, blockSize>>>(start, d_scalars, span);

    // Check for kernel launch errors
    cudaError_t error = cudaGetLastError();
    if (error != cudaSuccess) {
        cudaFree(d_scalars);
        throw std::runtime_error("CUDA kernel launch failed: " + std::string(cudaGetErrorString(error)));
    }

    // Synchronize and copy results back to host
    cudaDeviceSynchronize();
    cudaMemcpy(host_scalars_.data(), d_scalars, span * sizeof(core::UInt256), cudaMemcpyDeviceToHost);

    // Cleanup device memory
    cudaFree(d_scalars);

    DeviceBatch batch;
    batch.scalars.assign(host_scalars_.begin(), host_scalars_.begin() + span);
    return batch;
}

void DeviceBuffers::Clear() {
    host_scalars_.clear();
    host_scalars_.shrink_to_fit();
    grid_ = dim3(0, 0, 0);
    block_ = dim3(0, 0, 0);
    points_per_thread_ = 1;
    slots_ = 0;
}

}  // namespace puzzle71::gpu
