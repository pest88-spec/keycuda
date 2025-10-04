#pragma once

#include <array>
#include <cuda_runtime.h>

#include "KeyhuntCore/gpu/device_results.h"
#include "core/uint256.h"

namespace puzzle71::kernel {

struct KernelLaunchConfig {
    dim3 grid;
    dim3 block;
    std::uint64_t batch_size;
    int points_per_thread{1};
};

KernelLaunchConfig ChooseLaunchConfig(std::uint64_t desired_threads);
void SetDeterministicLaunchConfig(const KernelLaunchConfig& config);
void ClearDeterministicLaunchConfig();
bool HasDeterministicLaunchConfig();

cudaError_t SetResultBuffer(const puzzle71::gpu::DeviceResultBuffer& buffer);

cudaError_t LaunchFusedKernel(dim3 grid,
                              dim3 block,
                              int points_per_thread,
                              int compression);

void EnableRegisterAudit(bool enabled);
bool IsRegisterAuditEnabled();

}  // namespace puzzle71::kernel
