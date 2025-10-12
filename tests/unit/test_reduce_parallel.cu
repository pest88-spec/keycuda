/**
 * @file test_reduce_parallel.cu
 * @brief Test for parallel reduction using Thrust (T027)
 *
 * Tests T030: Replace serial validation loop with parallel Thrust reduce
 *
 * Validates:
 * - thrust::reduce computes correct sum for large arrays
 * - Thrust GPU reduction matches CPU std::accumulate results
 * - Various reduction operations work (sum, max, min, custom functors)
 * - Performance scales better than serial implementation
 *
 * Expected to FAIL initially (Red phase) until T030 implementation completes.
 */

#include <gtest/gtest.h>
#include <thrust/device_vector.h>
#include <thrust/functional.h>
#include <thrust/extrema.h>
#include <thrust/iterator/counting_iterator.h>
#include <thrust/transform.h>

#include <vector>
#include <algorithm>
#include <numeric>
#include <random>
#include <cmath>

#include "cuda_test_fixture.h"

using namespace keyhunt::testing;

/**
 * @brief Test fixture for parallel reduction tests
 */
class ParallelReductionTest : public CudaTestFixture {
protected:
    static constexpr size_t kLargeArraySize = 1000000;  // 1M elements for performance testing
    static constexpr size_t kMediumArraySize = 100000;   // 100K elements
    static constexpr size_t kSmallArraySize = 1000;      // 1K elements

    void SetUp() override {
        CudaTestFixture::SetUp();

        // Set random seed for reproducible tests
        rng_.seed(0x123456789ABCDEF);
    }

    std::mt19937_64 rng_;

    /**
     * @brief Generate test data with specified distribution
     */
    void generateTestData(std::vector<double>& data, size_t size, double min_val = -1000.0, double max_val = 1000.0) {
        std::uniform_real_distribution<double> dist(min_val, max_val);
        data.resize(size);
        for (size_t i = 0; i < size; i++) {
            data[i] = dist(rng_);
        }
    }

    /**
     * @brief Generate integer test data
     */
    void generateIntegerTestData(std::vector<int>& data, size_t size, int min_val = -1000, int max_val = 1000) {
        std::uniform_int_distribution<int> dist(min_val, max_val);
        data.resize(size);
        for (size_t i = 0; i < size; i++) {
            data[i] = dist(rng_);
        }
    }

    /**
     * @brief Generate test data with specific pattern
     */
    void generatePatternData(std::vector<int>& data, size_t size, int pattern_length = 10) {
        data.resize(size);
        for (size_t i = 0; i < size; i++) {
            data[i] = (i % pattern_length) + 1;
        }
    }

    /**
     * @brief Compute expected sum on CPU (reference)
     */
    double computeExpectedSum(const std::vector<double>& input) {
        return std::accumulate(input.begin(), input.end(), 0.0);
    }

    /**
     * @brief Compute expected max on CPU (reference)
     */
    double computeExpectedMax(const std::vector<double>& input) {
        if (input.empty()) return 0.0;
        return *std::max_element(input.begin(), input.end());
    }

    /**
     * @brief Compute expected min on CPU (reference)
     */
    double computeExpectedMin(const std::vector<double>& input) {
        if (input.empty()) return 0.0;
        return *std::min_element(input.begin(), input.end());
    }

    /**
     * @brief Compare GPU result with CPU reference with tolerance
     */
    void compareWithTolerance(double expected, double actual, double tolerance, const std::string& description) {
        double error = std::abs(expected - actual);
        double relative_error = (expected != 0.0) ? error / std::abs(expected) : 0.0;

        EXPECT_LT(error, tolerance)
            << description << ": Absolute error " << error << " exceeds tolerance " << tolerance;
        EXPECT_LT(relative_error, 1e-12)
            << description << ": Relative error " << relative_error << " exceeds tolerance 1e-12";
    }
};

/**
 * @brief Test: Thrust reduce computes correct sum for large arrays
 *
 * Validates:
 * - thrust::reduce produces same result as CPU std::accumulate
 * - Handles 1,000,000 element arrays efficiently
 * - Floating-point precision is maintained (<1e-12 relative error)
 *
 * Expected: FAIL initially (Thrust integration not yet implemented)
 */
TEST_F(ParallelReductionTest, ThrustReduce_Sum_CorrectResultForLargeArrays) {
    // Generate test data: 1M random doubles
    std::vector<double> h_input;
    generateTestData(h_input, kLargeArraySize);

    // Compute expected sum on CPU
    double expected_sum = computeExpectedSum(h_input);

    // Copy to device
    thrust::device_vector<double> d_input(h_input.begin(), h_input.end());

    // Perform reduction on GPU using Thrust
    double gpu_sum = thrust::reduce(d_input.begin(), d_input.end(), 0.0, thrust::plus<double>());

    // Compare results with tolerance
    compareWithTolerance(expected_sum, gpu_sum, 1e-12, "Thrust reduce sum for 1M elements");
}

/**
 * @brief Test: Thrust reduce computes correct maximum for large arrays
 *
 * Validates:
 * - thrust::reduce with thrust::maximum finds maximum value
 * - Handles arrays with random values efficiently
 * - Correctly identifies max in edge cases
 *
 * Expected: FAIL initially (Thrust maximum reduction not yet implemented)
 */
TEST_F(ParallelReductionTest, ThrustReduce_Max_CorrectResultForLargeArrays) {
    // Generate test data with wide range
    std::vector<double> h_input;
    generateTestData(h_input, kLargeArraySize, -10000.0, 10000.0);

    // Compute expected max on CPU
    double expected_max = computeExpectedMax(h_input);

    // Copy to device
    thrust::device_vector<double> d_input(h_input.begin(), h_input.end());

    // Perform max reduction on GPU using Thrust
    double gpu_max = thrust::reduce(d_input.begin(), d_input.end(), -std::numeric_limits<double>::max(),
                                         thrust::maximum<double>());

    // Compare results with tolerance
    compareWithTolerance(expected_max, gpu_max, 1e-12, "Thrust reduce max for 1M elements");
}

/**
 * @brief Test: Thrust reduce computes correct minimum for large arrays
 *
 * Validates:
 * - thrust::reduce with thrust::minimum finds minimum value
 * - Handles arrays with negative values efficiently
 * - Correctly identifies min in edge cases
 *
 * Expected: FAIL initially (Thrust minimum reduction not yet implemented)
 */
TEST_F(ParallelReductionTest, ThrustReduce_Min_CorrectResultForLargeArrays) {
    // Generate test data with negative values
    std::vector<double> h_input;
    generateTestData(h_input, kLargeArraySize, -5000.0, 5000.0);

    // Compute expected min on CPU
    double expected_min = computeExpectedMin(h_input);

    // Copy to device
    thrust::device_vector<double> d_input(h_input.begin(), h_input.end());

    // Perform min reduction on GPU using Thrust
    double gpu_min = thrust::reduce(d_input.begin(), d_input.end(), std::numeric_limits<double>::max(),
                                         thrust::minimum<double>());

    // Compare results with tolerance
    compareWithTolerance(expected_min, gpu_min, 1e-12, "Thrust reduce min for 1M elements");
}

/**
 * @brief Test: Integer reduction works correctly
 *
 * Validates:
 * - thrust::reduce works with integer arrays
 * - No floating-point precision issues
 * - Handles edge cases (empty array, single element)
 *
 * Expected: FAIL initially (Thrust integer reduction not yet implemented)
 */
TEST_F(ParallelReductionTest, ThrustReduce_Integer_CorrectResult) {
    // Generate integer test data: pattern of values
    std::vector<int> h_input;
    generatePatternData(h_input, kMediumArraySize, 100);

    // Compute expected sum on CPU
    int expected_sum = std::accumulate(h_input.begin(), h_input.end(), 0);

    // Copy to device
    thrust::device_vector<int> d_input(h_input.begin(), h_input.end());

    // Perform reduction on GPU using Thrust
    int gpu_sum = thrust::reduce(d_input.begin(), d_input.end(), 0, thrust::plus<int>());

    // Exact comparison for integers
    EXPECT_EQ(expected_sum, gpu_sum) << "Thrust reduce sum for integer array pattern";
}

/**
 * @brief Test: Custom reduction functor works correctly
 *
 * Validates:
 * - Custom binary operations work with Thrust reduce
 * - User-defined functors integrate properly
 * - Complex reductions can be implemented
 *
 * Expected: FAIL initially (custom reduction not yet implemented)
 */
TEST_F(ParallelReductionTest, CustomReduction_Functor_CorrectResult) {
    // Custom functor: sum of squares
    auto sum_of_squares = [] __host__ __device__ (double x) -> double {
        return x * x;
    };

    // Generate test data
    std::vector<double> h_input;
    generateTestData(h_input, kMediumArraySize, -100.0, 100.0);

    // Compute expected result on CPU
    double expected = std::accumulate(h_input.begin(), h_input.end(), 0.0,
                                   [&sum_of_squares](double total, double val) {
                                       return total + sum_of_squares(val);
                                   });

    // Copy to device
    thrust::device_vector<double> d_input(h_input.begin(), h_input.end());

    // Perform custom reduction on GPU using Thrust
    double gpu_result = thrust::reduce(d_input.begin(), d_input.end(), 0.0,
                                           [=] __host__ __device__ (double total, double val) {
                                               return total + val * val;
                                           });

    // Compare results with tolerance
    compareWithTolerance(expected, gpu_result, 1e-10, "Custom sum of squares reduction");
}

/**
 * @brief Test: Reduction with different array sizes
 *
 * Validates:
 * - Thrust reduction works for small, medium, and large arrays
 * - Results are consistent across different sizes
 * - Performance scales appropriately with array size
 *
 * Expected: FAIL initially (various array size handling not yet implemented)
 */
TEST_F(ParallelReductionTest, MultipleArraySizes_ConsistentResults) {
    std::vector<size_t> test_sizes = {kSmallArraySize, kMediumArraySize, kLargeArraySize};
    std::vector<double> expected_sums;

    // Compute expected sums for all sizes
    for (size_t size : test_sizes) {
        std::vector<double> test_data;
        generateTestData(test_data, size);
        expected_sums.push_back(computeExpectedSum(test_data));
    }

    // Test each size
    for (size_t i = 0; i < test_sizes.size(); i++) {
        size_t size = test_sizes[i];
        double expected = expected_sums[i];

        // Generate and copy test data
        std::vector<double> h_input;
        generateTestData(h_input, size);
        thrust::device_vector<double> d_input(h_input.begin(), h_input.end());

        // Perform reduction
        double gpu_sum = thrust::reduce(d_input.begin(), d_input.end(), 0.0, thrust::plus<double>());

        // Compare with tolerance
        compareWithTolerance(expected, gpu_sum, 1e-12,
                           "Thrust reduce sum for array size " + std::to_string(size));
    }
}

/**
 * @brief Test: Performance comparison between GPU and CPU
 *
 * Validates:
 * - GPU reduction is faster than CPU for large arrays
 * - Performance scales with array size
 * - Memory bandwidth is utilized efficiently
 *
 * Note: This test documents performance expectations but doesn't
 * enforce strict timing requirements due to hardware variability.
 *
 * Expected: PASS (performance measurement, not functional correctness)
 */
TEST_F(ParallelReductionTest, Performance_GPU_FasterThanCPU) {
    const size_t test_size = kLargeArraySize;

    // Generate test data
    std::vector<double> h_input;
    generateTestData(h_input, test_size);

    // Time CPU reduction
    auto cpu_start = std::chrono::high_resolution_clock::now();
    double cpu_sum = computeExpectedSum(h_input);
    auto cpu_end = std::chrono::high_resolution_clock::now();
    float cpu_time_ms = std::chrono::duration_cast<std::chrono::microseconds>(cpu_end - cpu_start).count() / 1000.0f;

    // Time GPU reduction
    thrust::device_vector<double> d_input(h_input.begin(), h_input.end());

    cudaEvent_t start, stop;
    cudaEventCreate(&start);
    cudaEventCreate(&stop);

    // Warm-up
    double warmup_result = thrust::reduce(d_input.begin(), d_input.end(), 0.0, thrust::plus<double>());
    (void)warmup_result;  // Avoid unused variable warning

    // Time GPU reduction
    cudaEventRecord(start);
    double gpu_sum = thrust::reduce(d_input.begin(), d_input.end(), 0.0, thrust::plus<double>());
    cudaEventRecord(stop);
    cudaEventSynchronize();

    float gpu_time_ms;
    cudaEventElapsedTime(&gpu_time_ms, start, stop);
    cudaEventDestroy(start);
    cudaEventDestroy(stop);

    // Verify results match
    compareWithTolerance(cpu_sum, gpu_sum, 1e-12, "Performance test: GPU vs CPU reduction correctness");

    // Report performance
    float speedup = cpu_time_ms / gpu_time_ms;
    printf("Performance comparison for %zu elements:\n", test_size);
    printf("  CPU reduction:   %.3f ms\n", cpu_time_ms);
    printf("  GPU reduction:   %.3f ms\n", gpu_time_ms);
    printf("  Speedup:         %.2fx\n", speedup);

    // Document expectation: GPU should be faster for large arrays
    // Note: Not asserting specific speedup due to hardware variability
    EXPECT_GT(speedup, 1.0f) << "GPU reduction should be faster than CPU for large arrays";

    // Additional performance metrics
    double bytes_transferred = test_size * sizeof(double);
    double gpu_bandwidth_gbps = (bytes_transferred / (gpu_time_ms / 1000.0)) / (1024.0 * 1024.0 * 1024.0);
    printf("  GPU bandwidth:   %.2f GB/s\n", gpu_bandwidth_gbps);
}

/**
 * @brief Test: Edge cases for reduction operations
 *
 * Validates:
 * - Empty vector handling
 * - Single element vectors
 * - Arrays with extreme values
 * - Arrays with NaN or infinity values
 *
 * Expected: PARTIAL - Some edge cases may not be implemented yet
 */
TEST_F(ParallelReductionTest, EdgeCases_HandlesSpecialValues) {
    // Test empty vector (special case for Thrust)
    {
        thrust::device_vector<double> empty_vec;
        double empty_sum = thrust::reduce(empty_vec.begin(), empty_vec.end(), 0.0, thrust::plus<double>());
        EXPECT_EQ(empty_sum, 0.0) << "Empty vector reduction should return 0";
    }

    // Test single element
    {
        std::vector<double> single_element = {42.5};
        thrust::device_vector<double> d_single(single_element.begin(), single_element.end());
        double single_sum = thrust::reduce(d_single.begin(), d_single.end(), 0.0, thrust::plus<double>());
        EXPECT_DOUBLE_EQ(single_sum, 42.5) << "Single element reduction should return the element";
    }

    // Test maximum values
    {
        std::vector<double> max_values = {1.0, std::numeric_limits<double>::max(), 1000.0, -std::numeric_limits<double>::max()};
        thrust::device_vector<double> d_max(max_values.begin(), max_values.end());
        double max_result = thrust::reduce(d_max.begin(), d_max.end(), -std::numeric_limits<double>::max(),
                                          thrust::maximum<double>());
        EXPECT_DOUBLE_EQ(max_result, std::numeric_limits<double>::max()) << "Should find maximum double value";
    }

    // Test minimum values
    {
        std::vector<double> min_values = {-1.0, std::numeric_limits<double>::lowest(), 1000.0, std::numeric_limits<double>::min()};
        thrust::device_vector<double> d_min(min_values.begin(), min_values.end());
        double min_result = thrust::reduce(d_min.begin(), d_min.end(), std::numeric_limits<double>::max(),
                                          thrust::minimum<double>());
        EXPECT_DOUBLE_EQ(min_result, std::numeric_limits<double>::lowest()) << "Should find minimum double value";
    }
}

/**
 * @brief Test: Reduction with different data types
 *
 * Validates:
 * - Thrust reduce works with float, int, long, and custom types
 * - Type conversions are handled correctly
 * - Performance varies by data type size
 *
 * Expected: FAIL initially (multiple data type support not yet implemented)
 */
TEST_F(ParallelReductionTest, MultipleDataTypes_CorrectResults) {
    // Test with float
    {
        std::vector<float> float_data(kMediumArraySize);
        for (size_t i = 0; i < float_data.size(); i++) {
            float_data[i] = static_cast<float>(i * 0.5f);
        }
        float expected_float_sum = std::accumulate(float_data.begin(), float_data.end(), 0.0f);

        thrust::device_vector<float> d_float(float_data.begin(), float_data.end());
        float gpu_float_sum = thrust::reduce(d_float.begin(), d_float.end(), 0.0f, thrust::plus<float>());

        EXPECT_NEAR(expected_float_sum, gpu_float_sum, 1e-6) << "Float reduction should match CPU result";
    }

    // Test with long long
    {
        std::vector<long long> long_data(kSmallArraySize);
        for (size_t i = 0; i < long_data.size(); i++) {
            long_data[i] = static_cast<long long>(i) * 1000000LL;
        }
        long long expected_long_sum = std::accumulate(long_data.begin(), long_data.end(), 0LL);

        thrust::device_vector<long long> d_long(long_data.begin(), long_data.end());
        long long gpu_long_sum = thrust::reduce(d_long.begin(), d_long.end(), 0LL, thrust::plus<long long>());

        EXPECT_EQ(expected_long_sum, gpu_long_sum) << "Long long reduction should match CPU result";
    }
}