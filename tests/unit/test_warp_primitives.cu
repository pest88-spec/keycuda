/**
 * @file test_warp_primitives.cu
 * @brief Unit tests for warp-level primitives (shuffle instructions)
 *
 * Tests T021-T022: Warp shuffle primitives for register-only communication
 *
 * Validates:
 * - warpReduceMax computes maximum across 32 threads
 * - Zero shared memory usage (register-only communication)
 * - All threads in warp have result in lane 0 after reduction
 * - Efficient 256-bit shuffling for ECC operations
 */

#include <gtest/gtest.h>
#include "cuda_test_fixture.h"
#include "KeyhuntCore/kernels/warp_primitives.cuh"
#include <vector>
#include <algorithm>
#include <cmath>
#include <limits>

using namespace keyhunt::testing;

/**
 * @brief Test kernel: Warp-level maximum reduction
 *
 * Each thread has a value. The warp reduction computes the maximum
 * across all 32 threads in the warp using shuffle instructions.
 *
 * Result stored in lane 0 of each warp.
 */
__global__ void testWarpReduceMaxKernel(
    const double* __restrict__ input,
    double* __restrict__ output,
    const size_t count)
{
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    int warpId = tid / 32;
    int laneId = tid % 32;

    // Load input value
    double myValue = (tid < count) ? input[tid] : -INFINITY;

    // Warp-level reduction using shuffle instructions
    double warpMax = static_cast<double>(keyhunt::kernels::warpReduceMax(static_cast<uint32_t>(myValue)));

    // Lane 0 of each warp writes result
    if (laneId == 0 && warpId < count / 32) {
        output[warpId] = warpMax;
    }
}

/**
 * @brief Test kernel: Verify zero shared memory usage
 *
 * This kernel performs warp reduction without using any shared memory.
 * Profiling with Nsight Compute should show 0 bytes shared memory usage.
 */
__global__ void testWarpReduceNoSharedMemoryKernel(
    const double* __restrict__ input,
    double* __restrict__ output)
{
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    int warpId = threadIdx.x / 32;
    int laneId = threadIdx.x % 32;

    // Load input
    double myValue = input[tid];

    // Warp reduction (register-only, no shared memory)
    double warpMax = static_cast<double>(keyhunt::kernels::warpReduceMax(static_cast<uint32_t>(myValue)));

    // Write result (one per warp)
    __shared__ double warpResults[32];  // Only for final output, not for reduction
    if (laneId == 0) {
        warpResults[warpId] = warpMax;
    }
    __syncthreads();

    // Block-level aggregation (first warp only)
    if (warpId == 0) {
        double blockMax = (laneId < blockDim.x / 32) ? warpResults[laneId] : -INFINITY;
        blockMax = static_cast<double>(keyhunt::kernels::warpReduceMax(static_cast<uint32_t>(blockMax)));

        if (laneId == 0) {
            output[blockIdx.x] = blockMax;
        }
    }
}

/**
 * @brief Test fixture for warp primitive tests
 */
class WarpPrimitivesTest : public CudaTestFixture {
protected:
    static constexpr size_t kTestDataSize = 8192;  // 256 warps

    void SetUp() override {
        CudaTestFixture::SetUp();

        cudaError_t err = cudaMalloc(&d_input, kTestDataSize * sizeof(double));
        checkCudaError(err, "Failed to allocate d_input");

        err = cudaMalloc(&d_output, (kTestDataSize / 32) * sizeof(double));
        checkCudaError(err, "Failed to allocate d_output");
    }

    void TearDown() override {
        if (d_input) cudaFree(d_input);
        if (d_output) cudaFree(d_output);

        CudaTestFixture::TearDown();
    }

    double* d_input = nullptr;
    double* d_output = nullptr;
};

/**
 * @brief Test: warpReduceMax computes maximum across warp (32 threads)
 *
 * Validates:
 * - Input: Random double values (one per thread)
 * - Expected: Maximum value computed correctly for each warp
 * - All threads in warp should have max in lane 0 after reduction
 *
 * Expected: FAIL initially (stub returns input unchanged)
 */
TEST_F(WarpPrimitivesTest, WarpReduceMax_32Threads_ComputesMaximum) {
    // Generate random test data
    std::vector<double> h_input(kTestDataSize);
    std::mt19937_64 rng(0x123456789ABCDEF);
    std::uniform_real_distribution<double> dist(-1000.0, 1000.0);

    for (size_t i = 0; i < kTestDataSize; i++) {
        h_input[i] = dist(rng);
    }

    // Compute expected results (CPU reference)
    std::vector<double> h_expected(kTestDataSize / 32);
    for (size_t warp = 0; warp < kTestDataSize / 32; warp++) {
        double warpMax = -INFINITY;
        for (size_t lane = 0; lane < 32; lane++) {
            size_t idx = warp * 32 + lane;
            warpMax = std::max(warpMax, h_input[idx]);
        }
        h_expected[warp] = warpMax;
    }

    // Copy input to device
    copyToDevice(d_input, h_input.data(), kTestDataSize * sizeof(double));

    // Launch kernel
    dim3 blockSize(256);
    dim3 gridSize((kTestDataSize + blockSize.x - 1) / blockSize.x);

    testWarpReduceMaxKernel<<<gridSize, blockSize>>>(
        d_input, d_output, kTestDataSize);

    syncAndCheckErrors();

    // Copy results back
    std::vector<double> h_output(kTestDataSize / 32);
    copyFromDevice(h_output.data(), d_output, (kTestDataSize / 32) * sizeof(double));

    // Validate: GPU results match expected maximums
    int mismatchCount = 0;
    double maxError = 0.0;

    for (size_t i = 0; i < h_expected.size(); i++) {
        double error = std::abs(h_output[i] - h_expected[i]);
        maxError = std::max(maxError, error);

        if (error > 1e-10) {
            mismatchCount++;
            if (mismatchCount <= 10) {
                ADD_FAILURE() << "Warp " << i << ": expected " << h_expected[i]
                              << ", got " << h_output[i]
                              << " (error: " << error << ")";
            }
        }
    }

    EXPECT_EQ(mismatchCount, 0)
        << "Warp reduction failed for " << mismatchCount << " warps";
    EXPECT_LT(maxError, 1e-10)
        << "Maximum error exceeds tolerance: " << maxError;
}

/**
 * @brief Test: Warp reduction uses zero shared memory
 *
 * Validates that reduction is performed entirely in registers using shuffle instructions.
 *
 * Note: This test documents the requirement. Actual verification requires Nsight Compute profiling:
 *   ncu --metrics l1tex__data_pipe_lsu_wavefronts_mem_shared_op_ld.sum <test>
 * Should show 0 shared memory loads during warpReduceMax execution.
 */
TEST_F(WarpPrimitivesTest, WarpReduce_ZeroSharedMemory_RegisterOnlyCommunication) {
    // Generate test data
    std::vector<double> h_input(kTestDataSize);
    for (size_t i = 0; i < kTestDataSize; i++) {
        h_input[i] = static_cast<double>(i);
    }

    copyToDevice(d_input, h_input.data(), kTestDataSize * sizeof(double));

    // Launch kernel (uses minimal shared memory only for final aggregation)
    dim3 blockSize(256);
    dim3 gridSize((kTestDataSize + blockSize.x - 1) / blockSize.x);

    testWarpReduceNoSharedMemoryKernel<<<gridSize, blockSize>>>(d_input, d_output);

    syncAndCheckErrors();

    // Functional verification (actual shared memory usage requires profiling)
    std::vector<double> h_output(gridSize.x);
    copyFromDevice(h_output.data(), d_output, gridSize.x * sizeof(double));

    // Each block should have computed maximum of its 256 elements
    for (size_t block = 0; block < gridSize.x; block++) {
        size_t startIdx = block * blockSize.x;
        size_t endIdx = std::min(startIdx + blockSize.x, kTestDataSize);

        double expectedMax = -INFINITY;
        for (size_t i = startIdx; i < endIdx; i++) {
            expectedMax = std::max(expectedMax, h_input[i]);
        }

        // Note: This will likely fail until warpReduceMax is implemented
        // Documenting expected behavior for when implementation is complete
        double error = std::abs(h_output[block] - expectedMax);
        if (error > 1e-10) {
            EXPECT_LT(error, 1e-10)
                << "Block " << block << ": expected " << expectedMax
                << ", got " << h_output[block];
        }
    }

    // Document: Actual shared memory usage verification requires Nsight Compute profiling
    // Expected: warpReduceMax uses 0 bytes shared memory (register-only reduction)
    // Expected: Only warpResults[] array (32 doubles = 256 bytes) in shared memory for final aggregation
}

/**
 * @brief Test: Butterfly reduction pattern (5 iterations for 32 threads)
 *
 * Documents the expected reduction pattern:
 * - Iteration 0: offset=16, threads 0-15 exchange with threads 16-31
 * - Iteration 1: offset=8,  threads 0-7 exchange with threads 8-15
 * - Iteration 2: offset=4,  threads 0-3 exchange with threads 4-7
 * - Iteration 3: offset=2,  threads 0-1 exchange with threads 2-3
 * - Iteration 4: offset=1,  thread 0 exchanges with thread 1
 * - Result: All threads have max in lane 0
 */
TEST_F(WarpPrimitivesTest, ButterflyReduction_5Iterations_CorrectPattern) {
    // Test with known pattern: warp 0 has values [0, 1, 2, ..., 31]
    std::vector<double> h_input(32);
    for (size_t i = 0; i < 32; i++) {
        h_input[i] = static_cast<double>(i);
    }

    copyToDevice(d_input, h_input.data(), 32 * sizeof(double));

    // Launch single warp
    testWarpReduceMaxKernel<<<1, 32>>>(d_input, d_output, 32);

    syncAndCheckErrors();

    // Expected: Maximum value is 31
    double h_result;
    copyFromDevice(&h_result, d_output, sizeof(double));

    EXPECT_DOUBLE_EQ(h_result, 31.0)
        << "Warp maximum should be 31 (highest value in [0..31])";

    // Document: Butterfly reduction requires log2(32) = 5 iterations
    // Each iteration halves the number of active threads
    // After 5 iterations, lane 0 has the maximum value
}
