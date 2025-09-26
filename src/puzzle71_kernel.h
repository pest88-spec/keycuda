#pragma once

#include <array>
#include <cuda_runtime.h>

#include "core/uint256.h"

namespace puzzle71::kernel {

struct KernelLaunchConfig {
    dim3 grid;
    dim3 block;
    std::uint64_t batch_size;
};

KernelLaunchConfig ChooseLaunchConfig(std::uint64_t desired_threads);

}  // namespace puzzle71::kernel
