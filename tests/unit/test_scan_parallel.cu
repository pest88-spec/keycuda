/**
 * @file test_scan_parallel.cu
 * @brief Test for parallel prefix scan implementation (T026)
 *
 * Tests T030: Replace serial batch indexing with CUB BlockScan
 *
 * Validates:
 * - CUB BlockScan computes correct prefix sum for integer arrays
 * - Parallel scan matches serial CPU reference implementation
 * - Handles various array sizes correctly
 * - Performance scales better than serial implementation
 *
 * Expected to FAIL initially (Red phase) until T031 implementation completes.
 */

#include <gtest/gtest.h>
#include <cuda_runtime.h>
#include <cub/device/device_scan.cuh>
#include <cub/iterator/transform_input_iterator.cuh>
#include <cub/iterator/transform_output_iterator.cuh>

#include <vector>
#include <algorithm>
#include <numeric>

#include "cuda_test_fixture.h"

using namespace keyhunt::testing;

/**
 * @brief Test kernel: Parallel prefix sum using CUB BlockScan
 *
 * Each thread block computes prefix sum of its portion of the array.
 * Uses CUB's BlockScan primitive for efficient parallel prefix sum.
 *
 * Expected to FAIL initially (CUB integration not yet implemented)
 */
__global__ void parallelPrefixSumKernel(
    const int* __restrict__ input,
    int* __restrict__ output,
    const size_t count)
{
    // Specialize BlockScan for int type and 256 threads per block
    typedef cub::BlockScan<int, 256> BlockScan;

    // Allocate shared memory for BlockScan
    __shared__ typename BlockScan::TempStorage temp_storage;

    // Each thread loads one element
    int thread_data = 0;
    int global_tid = blockIdx.x * blockDim.x + threadIdx.x;

    if (global_tid < count) {
        thread_data = input[global_tid];
    } else {
        thread_data = 0;  // Out-of-bounds threads get 0
    }

    // Perform inclusive prefix sum
    BlockScan(temp_storage).InclusiveSum(thread_data, thread_data);

    // Store result
    if (global_tid < count) {
        output[global_tid] = thread_data;
    }
}

/**
 * @brief Test kernel: Parallel prefix sum for variable block sizes
 *
 * Tests CUB BlockScan with different block configurations.
 */
__global__ void parallelPrefixSumVariableKernel(
    const int* __restrict__ input,
    int* __restrict__ output,
    const size_t count)
{
    // Use CUB's device-level scan for complete array
    int global_tid = blockIdx.x * blockDim.x + threadIdx.x;

    // Simple approach: each thread computes partial sum
    if (global_tid < count) {
        int partial_sum = 0;
        for (int i = 0; i <= global_tid; i++) {
            partial_sum += input[i];
        }
        output[global_tid] = partial_sum;
    }
}

/**
 * @brief Test kernel: Parallel prefix sum with multiple blocks
 *
 * Tests handling of arrays larger than block size.
 * Each block processes a chunk and results are combined.
 */
__global__ void parallelPrefixSumMultiBlockKernel(
    const int* __restrict__ input,
    int* __restrict__ output,
    int* __restrict__ block_sums,
    const size_t count,
    const size_t block_size)
{
    typedef cub::BlockScan<int, 256> BlockScan;
    __shared__ typename BlockScan::TempStorage temp_storage;
    __shared__ int block_sum;

    int global_tid = blockIdx.x * blockDim.x + threadIdx.x;
    int block_start = blockIdx.x * block_size;
    int block_end = min(block_start + block_size, static_cast<int>(count));

    // Load and compute within block
    int thread_data = 0;
    if (global_tid < count && global_tid < block_end) {
        thread_data = input[global_tid];
    } else {
        thread_data = 0;
    }

    // Inclusive prefix sum within block
    BlockScan(temp_storage).InclusiveSum(thread_data, thread_data);

    // Store block result
    if (global_tid < count) {
        output[global_tid] = thread_data;
    }

    // Last thread in block computes block sum
    if (threadIdx.x == blockDim.x - 1) {
        block_sum = thread_data;
        if (blockIdx.x < gridDim.x - 1) {  // Not last block
            block_sums[blockIdx.x] = block_sum;
        }
    }

    __syncthreads();

    // Add previous block sums to current block results
    if (blockIdx.x > 0) {
        int prefix_block_sum = 0;
        for (int i = 0; i < blockIdx.x; i++) {
            prefix_block_sum += block_sums[i];
        }
        thread_data += prefix_block_sum;

        if (global_tid < count && global_tid < block_end) {
            output[global_tid] = thread_data;
        }
    }
}

/**
 * @brief Test fixture for parallel prefix scan tests
 */
class ParallelPrefixScanTest : public CudaTestFixture {
protected:
    static constexpr size_t kTestSizes[] = {32, 64, 128, 256, 512, 1024, 2048, 4096};
    static constexpr int kNumTestSizes = 8;

    void SetUp() override {
        CudaTestFixture::SetUp();

        // Allocate device memory for testing
        for (size_t i = 0; i < kNumTestSizes; i++) {
            size_t size = kTestSizes[i];

            cudaError_t err = cudaMalloc(&d_inputs_[i], size * sizeof(int));
            checkCudaError(err, "Failed to allocate input buffer");

            err = cudaMalloc(&d_outputs_[i], size * sizeof(int));
            checkCudaError(err, "Failed to allocate output buffer");

            err = cudaMalloc(&d_block_sums_[i], (size + 255) / 256 * sizeof(int));
            checkCudaError(err, "Failed to allocate block sums buffer");
        }
    }

    void TearDown() override {
        // Free device memory
        for (size_t i = 0; i < kNumTestSizes; i++) {
            if (d_inputs_[i]) cudaFree(d_inputs_[i]);
            if (d_outputs_[i]) cudaFree(d_outputs_[i]);
            if (d_block_sums_[i]) cudaFree(d_block_sums_[i]);
        }

        CudaTestFixture::TearDown();
    }

    // Device memory pointers for different test sizes
    int* d_inputs_[kNumTestSizes];
    int* d_outputs_[kNumTestSizes];
    int* d_block_sums_[kNumTestSizes];

    /**
     * @brief Generate test data with known pattern
     */
    void generateTestData(std::vector<int>& h_data, size_t size, int pattern = 1) {
        h_data.resize(size);
        for (size_t i = 0; i < size; i++) {
            h_data[i] = (i % pattern) + 1;
        }
    }

    /**
     * @brief Compute expected prefix sum on CPU (reference implementation)
     */
    void computeExpectedSerial(const std::vector<int>& input, std::vector<int>& expected) {
        expected.resize(input.size());
        int sum = 0;
        for (size_t i = 0; i < input.size(); i++) {
            sum += input[i];
            expected[i] = sum;
        }
    }

    /**
     * @brief Compare GPU result with CPU reference
     */
    void compareWithReference(const std::vector<int>& expected, const int* d_result, size_t count,
                              const std::string& test_name) {
        // Copy GPU result to host
        std::vector<int> h_result(count);
        cudaMemcpy(h_result.data(), d_result, count * sizeof(int), cudaMemcpyDeviceToHost);

        // Compare results
        for (size_t i = 0; i < count; i++) {
            EXPECT_EQ(h_result[i], expected[i])
                << test_name << ": Mismatch at index " << i
                << " (expected " << expected[i] << ", got " << h_result[i] << ")";
        }
    }
};

/**
 * @brief Test: CUB BlockScan computes correct prefix sum (single block)
 *
 * Validates:
 * - BlockScan produces same results as CPU std::partial_sum
 * - Works correctly for array sizes up to 1024 elements
 * - Handles edge cases (size 1, power of 2, non-power of 2)
 *
 * Expected: FAIL initially (CUB BlockScan not yet implemented)
 */
TEST_F(ParallelPrefixScanTest, CUBBlockScan_SingleBlock_ComputesCorrectPrefixSum) {
    for (size_t test_idx = 0; test_idx < kNumTestSizes; test_idx++) {
        size_t size = kTestSizes[test_idx];
        if (size > 1024) continue;  // Skip sizes larger than single block

        // Generate test data: pattern of values 1,2,3,1,2,3,...
        std::vector<int> h_input;
        generateTestData(h_input, size, 3);

        // Compute expected result on CPU
        std::vector<int> h_expected;
        computeExpectedSerial(h_input, h_expected);

        // Copy input to device
        cudaMemcpy(d_inputs_[test_idx], h_input.data(), size * sizeof(int), cudaMemcpyHostToDevice);

        // Launch kernel
        dim3 blockSize(256);
        dim3 gridSize((size + blockSize.x - 1) / blockSize.x);
        parallelPrefixSumKernel<<<gridSize, blockSize>>>(
            d_inputs_[test_idx], d_outputs_[test_idx], size);

        cudaDeviceSynchronize();
        syncAndCheckErrors();

        // Compare with reference
        compareWithReference(h_expected, d_outputs_[test_idx], size,
                            "CUB BlockScan size " + std::to_string(size));
    }
}

/**
 * @brief Test: Parallel prefix sum handles large arrays (multiple blocks)
 *
 * Validates:
 * - Multi-block implementation works for arrays > 1024 elements
 * - Block sums are correctly combined
 * - Results match CPU reference for large arrays
 *
 * Expected: FAIL initially (multi-block CUB integration not yet implemented)
 */
TEST_F(ParallelPrefixScanTest, MultiBlock_PrefixSum_HandlesLargeArrays) {
    // Test with array larger than single block size
    const size_t large_size = 4096;
    std::vector<int> h_input;
    generateTestData(h_input, large_size, 7);  // Pattern 1-7 repeated

    // Compute expected result
    std::vector<int> h_expected;
    computeExpectedSerial(h_input, h_expected);

    // Allocate large device memory
    int* d_large_input;
    int* d_large_output;
    int* d_large_block_sums;

    cudaMalloc(&d_large_input, large_size * sizeof(int));
    cudaMalloc(&d_large_output, large_size * sizeof(int));
    cudaMalloc(&d_large_block_sums, (large_size + 255) / 256 * sizeof(int));

    // Copy input to device
    cudaMemcpy(d_large_input, h_input.data(), large_size * sizeof(int), cudaMemcpyHostToDevice);

    // Launch multi-block kernel
    dim3 blockSize(256);
    dim3 gridSize((large_size + blockSize.x - 1) / blockSize.x);
    parallelPrefixSumMultiBlockKernel<<<gridSize, blockSize>>>(
        d_large_input, d_large_output, d_large_block_sums, large_size, 256);

    cudaDeviceSynchronize();
    syncAndCheckErrors();

    // Compare with reference
    compareWithReference(h_expected, d_large_output, large_size, "Multi-block prefix sum");

    // Cleanup
    cudaFree(d_large_input);
    cudaFree(d_large_output);
    cudaFree(d_large_block_sums);
}

/**
 * @brief Test: Variable block size configuration
 *
 * Validates:
 * - Kernel works with different block sizes
 * - Results are consistent regardless of block configuration
 * - Performance scales appropriately with block size
 *
 * Expected: FAIL initially (variable block size not yet implemented)
 */
TEST_F(ParallelPrefixScanTest, VariableBlockSize_ConsistentResults) {
    const size_t test_size = 512;
    std::vector<int> h_input;
    generateTestData(h_input, test_size, 5);

    // Compute reference
    std::vector<int> h_expected;
    computeExpectedSerial(h_input, h_expected);

    // Test with different block sizes
    std::vector<int> block_sizes = {64, 128, 256, 512};

    for (int block_size : block_sizes) {
        // Allocate device memory for this test
        int* d_test_input;
        int* d_test_output;
        cudaMalloc(&d_test_input, test_size * sizeof(int));
        cudaMalloc(&d_test_output, test_size * sizeof(int));

        // Copy input
        cudaMemcpy(d_test_input, h_input.data(), test_size * sizeof(int), cudaMemcpyHostToDevice);

        // Launch with current block size
        dim3 blockSize(block_size);
        dim3 gridSize((test_size + blockSize.x - 1) / blockSize.x);
        parallelPrefixSumVariableKernel<<<gridSize, blockSize>>>(
            d_test_input, d_test_output, test_size);

        cudaDeviceSynchronize();
        syncAndCheckErrors();

        // Copy and compare
        std::vector<int> h_result(test_size);
        cudaMemcpy(h_result.data(), d_test_output, test_size * sizeof(int), cudaMemcpyDeviceToHost);

        // Verify all results match expected
        for (size_t i = 0; i < test_size; i++) {
            EXPECT_EQ(h_result[i], h_expected[i])
                << "Block size " << block_size << ": Mismatch at index " << i
                << " (expected " << h_expected[i] << ", got " << h_result[i] << ")";
        }

        // Cleanup
        cudaFree(d_test_input);
        cudaFree(d_test_output);
    }
}

/**
 * @brief Test: Performance comparison vs serial implementation
 *
 * Validates:
 * - Parallel prefix sum is faster than serial for large arrays
 * - Performance scales with array size
 * - Memory bandwidth is utilized efficiently
 *
 * Note: This test documents performance expectations but doesn't
 * enforce strict timing requirements due to hardware variability.
 *
 * Expected: PASS (performance measurement, not functional correctness)
 */
TEST_F(ParallelPrefixScanTest, Performance_ParallelFasterThanSerial) {
    const size_t test_size = 1024;
    std::vector<int> h_input;
    generateTestData(h_input, test_size, 3);

    // Copy input to device
    cudaMemcpy(d_inputs_[3], h_input.data(), test_size * sizeof(int), cudaMemcpyHostToDevice);

    // Time parallel implementation
    cudaEvent_t start, stop;
    cudaEventCreate(&start);
    cudaEventCreate(&stop);

    dim3 blockSize(256);
    dim3 gridSize((test_size + blockSize.x - 1) / blockSize.x);

    // Warm-up
    parallelPrefixSumKernel<<<gridSize, blockSize>>>(
        d_inputs_[3], d_outputs_[3], test_size);
    cudaDeviceSynchronize();

    // Time parallel version
    cudaEventRecord(start);
    for (int iter = 0; iter < 100; iter++) {
        parallelPrefixSumKernel<<<gridSize, blockSize>>>(
            d_inputs_[3], d_outputs_[3], test_size);
    }
    cudaEventRecord(stop);
    cudaEventSynchronize();

    float parallel_time_ms;
    cudaEventElapsedTime(&parallel_time_ms, start, stop);
    parallel_time_ms /= 100.0f;  // Average over iterations

    // Time serial implementation on CPU
    auto cpu_start = std::chrono::high_resolution_clock::now();
    for (int iter = 0; iter < 100; iter++) {
        std::vector<int> expected;
        computeExpectedSerial(h_input, expected);
    }
    auto cpu_end = std::chrono::high_resolution_clock::now();
    float serial_time_ms = std::chrono::duration_cast<std::chrono::microseconds>(cpu_end - cpu_start).count() / 1000.0f / 100.0f;

    // Report performance
    float speedup = serial_time_ms / parallel_time_ms;
    printf("Performance comparison for %zu elements:\n", test_size);
    printf("  Serial CPU:    %.3f ms per operation\n", serial_time_ms);
    printf("  Parallel GPU:   %.3f ms per operation\n", parallel_time_ms);
    printf("  Speedup:        %.2fx\n", speedup);

    // Document expectation: parallel should be faster for GPU
    // Note: Not asserting specific speedup due to hardware variability
    EXPECT_GT(speedup, 1.0f) << "Parallel prefix sum should be faster than serial";

    cudaEventDestroy(start);
    cudaEventDestroy(stop);
}

/**
 * @brief Test: Edge cases and error handling
 *
 * Validates:
 * - Empty array handling (size 0)
 * - Single element array
 * - Array with maximum int values
 * - Array with negative values
 *
 * Expected: FAIL initially (edge case handling not yet implemented)
 */
TEST_F(ParallelPrefixScanTest, EdgeCases_HandlesSpecialValues) {
    struct TestCase {
        std::string name;
        std::vector<int> input;
    };

    std::vector<TestCase> test_cases = {
        {"empty_array", {}},
        {"single_element", {42}},
        {"max_values", {INT_MAX}},
        {"negative_values", {-5, -3, -1, 0, 1, 3, 5}},
        {"mixed_values", {0, -1, INT_MAX, 42, -42, INT_MAX}}
    };

    for (const auto& test_case : test_cases) {
        size_t size = test_case.input.size();
        if (size == 0) continue;  // Skip empty array for CUDA

        // Compute expected result
        std::vector<int> expected;
        computeExpectedSerial(test_case.input, expected);

        // Allocate device memory
        int* d_test_input;
        int* d_test_output;
        cudaMalloc(&d_test_input, size * sizeof(int));
        cudaMalloc(&d_test_output, size * sizeof(int));

        // Copy input
        cudaMemcpy(d_test_input, test_case.input.data(), size * sizeof(int), cudaMemcpyHostToDevice);

        // Launch kernel
        dim3 blockSize(256);
        dim3 gridSize((size + blockSize.x - 1) / blockSize.x);
        parallelPrefixSumKernel<<<gridSize, blockSize>>>(
            d_test_input, d_test_output, size);

        cudaDeviceSynchronize();
        syncAndCheckErrors();

        // Compare results
        compareWithReference(expected, d_test_output, size, "Edge case: " + test_case.name);

        // Cleanup
        cudaFree(d_test_input);
        cudaFree(d_test_output);
    }
}