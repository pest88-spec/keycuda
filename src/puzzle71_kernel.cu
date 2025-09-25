#include <cuda_runtime.h>

#include <cstdint>
#include <stdexcept>
#include <string>

namespace puzzle71::kernel {

namespace {
__global__ void Puzzle71SearchKernelStub() {
    // TODO(T029): Implement fused batch stepping + HASH160 comparison.
}
}  // namespace

cudaError_t LaunchPuzzle71SearchKernel(std::uint64_t /*grid_size*/, std::uint64_t /*block_size*/,
                                       void* /*device_state*/,
                                       cudaStream_t stream) {
    // Placeholder launch to keep build passing while tests fail per TDD.
    Puzzle71SearchKernelStub<<<1, 1, 0, stream>>>();
    return cudaGetLastError();
}

}  // namespace puzzle71::kernel
