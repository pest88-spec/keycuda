/**
 * @file auto_tuner.cuh
 * @brief Header for GPU kernel configuration auto-tuner
 */

#pragma once

#include <cuda_runtime.h>
#include <vector>
#include <map>

namespace keyhunt {
namespace gpu {

struct KernelConfig;
struct DeviceProfile;
struct PerformanceMetrics;
class AutoTuner;

// Public API functions
KernelConfig getOptimalKernelConfig(size_t numKeys, int deviceId = 0);
KernelConfig benchmarkBestKernelConfig(size_t numKeys, int deviceId = 0);
void initializeAutoTuner(int deviceId = 0);
DeviceProfile getDeviceProfile(int deviceId = 0);

} // namespace gpu
} // namespace keyhunt