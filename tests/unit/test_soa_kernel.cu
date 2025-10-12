/**
 * @file test_soa_kernel.cu
 * @brief Test for Structure-of-Arrays (SoA) kernel implementation (T020)
 *
 * Validates that the SoA kernel produces identical results to the AoS kernel
 * and demonstrates improved memory access patterns.
 */

#include <gtest/gtest.h>
#include <cuda_runtime.h>
#include <cstdint>
#include <vector>
#include <random>
#include <cstring>

#include "cuda_test_fixture.h"
#include "KeyhuntCore/kernels/ecc_scalar_mul.cu"
#include "KeyhuntCore/gpu/memory_manager.cuh"

class SoAKernelTest : public CudaTestFixture {
protected:
    void SetUp() override {
        CudaTestFixture::SetUp();

        // Allocate test data
        numKeys = 1000;

        // Allocate host memory
        h_privateKeys.resize(numKeys * 32);
        h_publicKeysAoS.resize(numKeys * 65);
        h_publicKeysSoA_X.resize(numKeys * 8);
        h_publicKeysSoA_Y.resize(numKeys * 8);
        h_publicKeysSoAConverted.resize(numKeys * 65);

        // Allocate device memory
        cudaMalloc(&d_privateKeys, numKeys * 32);
        cudaMalloc(&d_publicKeysAoS, numKeys * 65);
        cudaMalloc(&d_publicKeysSoA_X, numKeys * 8 * sizeof(uint32_t));
        cudaMalloc(&d_publicKeysSoA_Y, numKeys * 8 * sizeof(uint32_t));
        cudaMalloc(&d_publicKeysSoAConverted, numKeys * 65);

        // Generate random test keys
        generateRandomKeys();

        // Create precomputed table
        createPrecomputedTable();
    }

    void TearDown() override {
        // Free device memory
        cudaFree(d_privateKeys);
        cudaFree(d_publicKeysAoS);
        cudaFree(d_publicKeysSoA_X);
        cudaFree(d_publicKeysSoA_Y);
        cudaFree(d_publicKeysSoAConverted);
        deallocateCoalescedPoints(precomputedSoA);

        CudaTestFixture::TearDown();
    }

    void generateRandomKeys() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint8_t> dis(0, 255);

        for (size_t i = 0; i < h_privateKeys.size(); i++) {
            h_privateKeys[i] = dis(gen);
        }
    }

    void createPrecomputedTable() {
        // Create simple precomputed table (placeholder values)
        precomputedAoS = new PaddedECCPoint[1024];
        std::vector<uint32_t> h_x(1024 * 8);
        std::vector<uint32_t> h_y(1024 * 8);

        for (int i = 0; i < 1024; i++) {
            for (int j = 0; j < 8; j++) {
                uint32_t val = static_cast<uint32_t>(i * 8 + j);
                precomputedAoS[i].x[j] = val;
                precomputedAoS[i].y[j] = val ^ 0xFFFFFFFF;
                h_x[i * 8 + j] = val;
                h_y[i * 8 + j] = val ^ 0xFFFFFFFF;
            }
        }

        // Allocate device memory for AoS table
        cudaMalloc(&d_precomputedAoS, 1024 * sizeof(PaddedECCPoint));
        cudaMemcpy(d_precomputedAoS, precomputedAoS, 1024 * sizeof(PaddedECCPoint), cudaMemcpyHostToDevice);

        // Allocate device memory for SoA table
        precomputedSoA = allocateCoalescedPoints(1024);
        copyPointsHostToDevice(precomputedSoA, h_x.data(), h_y.data(), 1024);

        delete[] precomputedAoS;
    }

    // Test data
    size_t numKeys;
    std::vector<uint8_t> h_privateKeys;
    std::vector<uint8_t> h_publicKeysAoS;
    std::vector<uint32_t> h_publicKeysSoA_X;
    std::vector<uint32_t> h_publicKeysSoA_Y;
    std::vector<uint8_t> h_publicKeysSoAConverted;

    // Device memory
    uint8_t* d_privateKeys;
    uint8_t* d_publicKeysAoS;
    uint32_t* d_publicKeysSoA_X;
    uint32_t* d_publicKeysSoA_Y;
    uint8_t* d_publicKeysSoAConverted;

    // Precomputed tables
    PaddedECCPoint* precomputedAoS;
    ECCPointsSoA precomputedSoA;
    PaddedECCPoint* d_precomputedAoS;
};

/**
 * @brief Test that SoA kernel produces identical results to AoS kernel
 */
TEST_F(SoAKernelTest, CompareAoSvsSoA) {
    // Copy private keys to device
    cudaMemcpy(d_privateKeys, h_privateKeys.data(), numKeys * 32, cudaMemcpyHostToDevice);

    // Run AoS kernel
    dim3 block(256);
    dim3 grid((numKeys + 255) / 256);

    keyhunt::kernels::eccScalarMulKernel<<<grid, block>>>(
        d_privateKeys, d_publicKeysAoS, d_precomputedAoS, numKeys);
    cudaDeviceSynchronize();

    // Run SoA kernel
    keyhunt::kernels::eccScalarMulKernelSoA<<<grid, block>>>(
        d_privateKeys, precomputedSoA, d_publicKeysSoA_X, d_publicKeysSoA_Y, numKeys);
    cudaDeviceSynchronize();

    // Convert SoA results to public key format
    keyhunt::kernels::convertSoAToPublicKey<<<grid, block>>>(
        d_publicKeysSoA_X, d_publicKeysSoA_Y, d_publicKeysSoAConverted, numKeys);
    cudaDeviceSynchronize();

    // Copy results back to host
    cudaMemcpy(h_publicKeysAoS.data(), d_publicKeysAoS, numKeys * 65, cudaMemcpyDeviceToHost);
    cudaMemcpy(h_publicKeysSoA_X.data(), d_publicKeysSoA_X, numKeys * 8 * sizeof(uint32_t), cudaMemcpyDeviceToHost);
    cudaMemcpy(h_publicKeysSoA_Y.data(), d_publicKeysSoA_Y, numKeys * 8 * sizeof(uint32_t), cudaMemcpyDeviceToHost);
    cudaMemcpy(h_publicKeysSoAConverted.data(), d_publicKeysSoAConverted, numKeys * 65, cudaMemcpyDeviceToHost);

    // Compare results
    for (size_t i = 0; i < numKeys; i++) {
        // Compare uncompressed prefix
        EXPECT_EQ(h_publicKeysAoS[i * 65], h_publicKeysSoAConverted[i * 65])
            << "Mismatch in prefix for key " << i;

        // Compare X coordinates
        for (int j = 0; j < 32; j++) {
            EXPECT_EQ(h_publicKeysAoS[i * 65 + 1 + j], h_publicKeysSoAConverted[i * 65 + 1 + j])
                << "Mismatch in X coordinate byte " << j << " for key " << i;
        }

        // Compare Y coordinates
        for (int j = 0; j < 32; j++) {
            EXPECT_EQ(h_publicKeysAoS[i * 65 + 33 + j], h_publicKeysSoAConverted[i * 65 + 33 + j])
                << "Mismatch in Y coordinate byte " << j << " for key " << i;
        }
    }
}

/**
 * @brief Test SoA memory layout coalescing properties
 */
TEST_F(SoAKernelTest, TestCoalescedAccess) {
    // This test validates that memory access patterns are properly aligned
    // for coalescing. In a real implementation, you would use Nsight Compute
    // to measure actual memory bandwidth utilization.

    // Verify SoA layout properties
    EXPECT_TRUE(precomputedSoA.isAllocated());
    EXPECT_EQ(precomputedSoA.count, 1024);
    EXPECT_EQ(precomputedSoA.getMemoryFootprint(), 1024 * 8 * sizeof(uint32_t) * 2);

    // Check alignment (cudaMalloc guarantees 256-byte alignment)
    EXPECT_EQ(reinterpret_cast<uintptr_t>(precomputedSoA.x) % 256, 0);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(precomputedSoA.y) % 256, 0);

    // Test memory access patterns
    dim3 block(256);
    dim3 grid((numKeys + 255) / 256);

    // Launch kernel and measure time (simplified)
    cudaEvent_t start, stop;
    cudaEventCreate(&start);
    cudaEventCreate(&stop);

    // Test SoA kernel performance
    cudaEventRecord(start);
    keyhunt::kernels::eccScalarMulKernelSoA<<<grid, block>>>(
        d_privateKeys, precomputedSoA, d_publicKeysSoA_X, d_publicKeysSoA_Y, numKeys);
    cudaEventRecord(stop);
    cudaEventSynchronize(stop);

    float soaTime;
    cudaEventElapsedTime(&soaTime, start, stop);

    // Test AoS kernel performance for comparison
    cudaEventRecord(start);
    keyhunt::kernels::eccScalarMulKernel<<<grid, block>>>(
        d_privateKeys, d_publicKeysAoS, d_precomputedAoS, numKeys);
    cudaEventRecord(stop);
    cudaEventSynchronize(stop);

    float aosTime;
    cudaEventElapsedTime(&aosTime, start, stop);

    // Log performance comparison
    std::cout << "AoS kernel time: " << aosTime << " ms" << std::endl;
    std::cout << "SoA kernel time: " << soaTime << " ms" << std::endl;
    std::cout << "Performance improvement: " << (aosTime / soaTime) << "x" << std::endl;

    // Note: In a real implementation, SoA should show significant improvement
    // in memory bandwidth utilization due to coalesced access patterns

    cudaEventDestroy(start);
    cudaEventDestroy(stop);
}

/**
 * @brief Test SoA kernel with edge cases
 */
TEST_F(SoAKernelTest, TestEdgeCases) {
    // Test with single key
    size_t singleKey = 1;
    keyhunt::kernels::eccScalarMulKernelSoA<<<1, 1>>>(
        d_privateKeys, precomputedSoA, d_publicKeysSoA_X, d_publicKeysSoA_Y, singleKey);
    cudaDeviceSynchronize();

    // Test with number of keys not a multiple of block size
    size_t oddKeys = 123;
    keyhunt::kernels::eccScalarMulKernelSoA<<<1, 256>>>(
        d_privateKeys, precomputedSoA, d_publicKeysSoA_X, d_publicKeysSoA_Y, oddKeys);
    cudaDeviceSynchronize();

    // Test with zero keys (should do nothing)
    keyhunt::kernels::eccScalarMulKernelSoA<<<1, 256>>>(
        d_privateKeys, precomputedSoA, d_publicKeysSoA_X, d_publicKeysSoA_Y, 0);
    cudaDeviceSynchronize();

    // All should complete without errors
    cudaError_t err = cudaGetLastError();
    EXPECT_EQ(err, cudaSuccess);
}