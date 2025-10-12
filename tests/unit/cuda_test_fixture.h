/**
 * @file cuda_test_fixture.h
 * @brief Base test fixture for CUDA kernel unit tests
 *
 * Provides common setup/teardown for CUDA tests following Google Test conventions.
 * Implements Test-First CUDA Development workflow (Constitution Principle II).
 *
 * Usage:
 *   class MyKernelTest : public CudaTestFixture {
 *     // Test implementation
 *   };
 *
 *   TEST_F(MyKernelTest, TestCase) {
 *     // Use d_testInput, d_testOutput, generateRandomKeys(), checkCudaError()
 *   }
 */

#pragma once

#include <gtest/gtest.h>
#include <cuda_runtime.h>
#include <random>
#include <vector>
#include <array>
#include <stdexcept>
#include <iostream>

namespace keyhunt {
namespace testing {

/**
 * @brief Base fixture for CUDA kernel unit tests
 *
 * Handles CUDA device setup/teardown, provides reusable device memory buffers,
 * and utility functions for reproducible testing.
 */
class CudaTestFixture : public ::testing::Test {
protected:
    // Test configuration constants
    static constexpr size_t kMaxTestSize = 10000;  // Maximum test data size
    static constexpr size_t kDefaultBufferSize = kMaxTestSize * sizeof(uint64_t);

    // Device memory buffers (allocated in SetUp, freed in TearDown)
    void* d_testInput = nullptr;
    void* d_testOutput = nullptr;
    void* d_testAux = nullptr;  // Auxiliary buffer for intermediate results

    // Device properties
    cudaDeviceProp deviceProps;
    int deviceCount = 0;
    int selectedDevice = 0;

    // Seeded random number generator (for reproducible tests)
    std::mt19937_64 rng;
    static constexpr uint64_t kDefaultSeed = 0x123456789ABCDEF;

    /**
     * @brief Setup: Check CUDA availability and allocate device memory
     *
     * Runs before each test. Skips test if no CUDA device available.
     */
    void SetUp() override {
        // Check CUDA device availability
        cudaError_t err = cudaGetDeviceCount(&deviceCount);
        if (err != cudaSuccess || deviceCount == 0) {
            GTEST_SKIP() << "No CUDA devices available. Skipping CUDA test.";
            return;
        }

        // Set device and query properties
        selectedDevice = 0;  // Use first device for tests
        err = cudaSetDevice(selectedDevice);
        checkCudaError(err, "Failed to set CUDA device");

        err = cudaGetDeviceProperties(&deviceProps, selectedDevice);
        checkCudaError(err, "Failed to get device properties");

        // Print device info (only once per test suite)
        static bool deviceInfoPrinted = false;
        if (!deviceInfoPrinted) {
            std::cout << "[CUDA Device] " << deviceProps.name
                      << " (Compute " << deviceProps.major << "." << deviceProps.minor << ")"
                      << ", " << (deviceProps.totalGlobalMem / (1024.0 * 1024.0 * 1024.0)) << " GB"
                      << std::endl;
            deviceInfoPrinted = true;
        }

        // Allocate reusable device memory buffers
        err = cudaMalloc(&d_testInput, kDefaultBufferSize);
        checkCudaError(err, "Failed to allocate d_testInput");

        err = cudaMalloc(&d_testOutput, kDefaultBufferSize);
        checkCudaError(err, "Failed to allocate d_testOutput");

        err = cudaMalloc(&d_testAux, kDefaultBufferSize);
        checkCudaError(err, "Failed to allocate d_testAux");

        // Initialize seeded random generator (deterministic for reproducibility)
        rng.seed(kDefaultSeed);
    }

    /**
     * @brief Teardown: Free device memory and reset device
     *
     * Runs after each test. Ensures clean state for next test.
     */
    void TearDown() override {
        // Free device memory (safe to call even if allocation failed)
        if (d_testInput) {
            cudaFree(d_testInput);
            d_testInput = nullptr;
        }
        if (d_testOutput) {
            cudaFree(d_testOutput);
            d_testOutput = nullptr;
        }
        if (d_testAux) {
            cudaFree(d_testAux);
            d_testAux = nullptr;
        }

        // Reset device (clears error state and frees implicit allocations)
        cudaDeviceReset();
    }

    /**
     * @brief Check CUDA error and fail test if error detected
     * @param err CUDA error code to check
     * @param msg Custom error message
     *
     * If error detected, test will fail with descriptive message.
     */
    void checkCudaError(cudaError_t err, const char* msg) {
        if (err != cudaSuccess) {
            FAIL() << msg << ": " << cudaGetErrorString(err)
                   << " (error code: " << err << ")";
        }
    }

    /**
     * @brief Generate random 64-bit keys (seeded for reproducibility)
     * @param count Number of keys to generate
     * @return Vector of random uint64_t values
     *
     * Uses seeded PRNG for deterministic test data generation.
     */
    std::vector<uint64_t> generateRandomKeys(size_t count) {
        std::vector<uint64_t> keys(count);
        std::uniform_int_distribution<uint64_t> dist(1, UINT64_MAX);
        for (size_t i = 0; i < count; i++) {
            keys[i] = dist(rng);
        }
        return keys;
    }

    /**
     * @brief Generate random 256-bit private keys (for ECC tests)
     * @param count Number of keys to generate
     * @return Vector of 32-byte private keys
     *
     * Uses seeded PRNG for deterministic test key generation.
     */
    std::vector<std::array<unsigned char, 32>> generateRandomPrivateKeys(size_t count) {
        std::vector<std::array<unsigned char, 32>> keys(count);
        std::uniform_int_distribution<unsigned char> byteDist(0, 255);

        for (size_t i = 0; i < count; i++) {
            for (size_t j = 0; j < 32; j++) {
                keys[i][j] = byteDist(rng);
            }
            // Ensure non-zero (simplified check)
            if (keys[i][0] == 0 && keys[i][1] == 0) {
                keys[i][1] = 1;
            }
        }
        return keys;
    }

    /**
     * @brief Compute relative error between two double values
     * @param expected Expected value (reference)
     * @param actual Actual value (test result)
     * @return Relative error magnitude
     *
     * Used for floating-point comparison with tolerance.
     */
    double computeRelativeError(double expected, double actual) {
        if (expected == 0.0) {
            return std::abs(actual);  // Absolute error if expected is zero
        }
        return std::abs((actual - expected) / expected);
    }

    /**
     * @brief Synchronize device and check for kernel execution errors
     *
     * Call after kernel launch to ensure completion and detect runtime errors.
     */
    void syncAndCheckErrors() {
        // Check for launch errors
        cudaError_t err = cudaGetLastError();
        checkCudaError(err, "Kernel launch failed");

        // Synchronize and check for execution errors
        err = cudaDeviceSynchronize();
        checkCudaError(err, "Kernel execution failed");
    }

    /**
     * @brief Copy data from host to device (with error checking)
     * @param dst Device pointer
     * @param src Host pointer
     * @param size Bytes to copy
     */
    void copyToDevice(void* dst, const void* src, size_t size) {
        cudaError_t err = cudaMemcpy(dst, src, size, cudaMemcpyHostToDevice);
        checkCudaError(err, "Failed to copy data to device");
    }

    /**
     * @brief Copy data from device to host (with error checking)
     * @param dst Host pointer
     * @param src Device pointer
     * @param size Bytes to copy
     */
    void copyFromDevice(void* dst, const void* src, size_t size) {
        cudaError_t err = cudaMemcpy(dst, src, size, cudaMemcpyDeviceToHost);
        checkCudaError(err, "Failed to copy data from device");
    }

    /**
     * @brief Get device properties (for adaptive test configuration)
     * @return Device properties structure
     */
    const cudaDeviceProp& getDeviceProperties() const {
        return deviceProps;
    }

    /**
     * @brief Check if device has minimum compute capability
     * @param major Major compute capability version
     * @param minor Minor compute capability version
     * @return true if device meets or exceeds requirement
     */
    bool hasComputeCapability(int major, int minor) const {
        return (deviceProps.major > major) ||
               (deviceProps.major == major && deviceProps.minor >= minor);
    }
};

} // namespace testing
} // namespace keyhunt
