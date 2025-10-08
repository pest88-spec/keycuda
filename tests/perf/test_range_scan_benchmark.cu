#include <gtest/gtest.h>

#include <cuda_runtime.h>

TEST(RangeScanBenchmark, ReportsGpuAvailability) {
    int device_count = 0;
    cudaError_t status = cudaGetDeviceCount(&device_count);
    if (status != cudaSuccess || device_count == 0) {
        GTEST_SKIP() << "No CUDA devices available for throughput benchmark";
    }

    SUCCEED();
}
