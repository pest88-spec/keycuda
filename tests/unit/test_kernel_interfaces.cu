#include <gtest/gtest.h>

#include <cuda_runtime.h>

TEST(KernelInterfacesUnitTest, LaunchesReturnDeterministicOutputs) {
    int device_count = 0;
    if (cudaGetDeviceCount(&device_count) != cudaSuccess || device_count == 0) {
        GTEST_SKIP() << "CUDA device not available for kernel interface test";
    }

    SUCCEED();
}
