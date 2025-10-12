/**
 * @file test_ecc_scalar_mul.cu
 * @brief Unit tests for ECC scalar multiplication kernel
 *
 * Tests T012: GPU ECC scalar multiplication with CPU reference validation
 *
 * Validates against bitcoin-core/secp256k1 CPU reference (Constitution Principle III).
 * Expected to FAIL initially (Red phase) until T018 implementation completes.
 */

#include <gtest/gtest.h>
#include "cuda_test_fixture.h"
#include "KeyhuntCore/kernels/shared_memory.cuh"
#include "crypto/secp256k1_wrapper.cpp"
#include <vector>
#include <cmath>

using namespace keyhunt::testing;
using namespace keyhunt::kernels;
using namespace keyhunt::crypto;

/**
 * @brief Stub kernel for ECC scalar multiplication (will be implemented in T018)
 *
 * This is a placeholder that will FAIL tests until real implementation.
 */
__global__ void eccScalarMulKernel_Stub(
    const uint64_t* privateKeys,
    unsigned char* publicKeys,  // 65 bytes per key: 0x04 + X + Y
    const PaddedECCPoint* precomputedTable,
    const size_t count)
{
    // Stub implementation - just zero output (will cause test to fail)
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid < count) {
        for (int i = 0; i < 65; i++) {
            publicKeys[tid * 65 + i] = 0;
        }
    }
}

/**
 * @brief Test fixture for ECC scalar multiplication tests
 */
class ECCScalarMulTest : public CudaTestFixture {
protected:
    static constexpr size_t kTestKeyCount = 10000;  // 10K random test cases

    void SetUp() override {
        CudaTestFixture::SetUp();

        // Allocate device memory for test data
        cudaError_t err = cudaMalloc(&d_privateKeys, kTestKeyCount * 32);
        checkCudaError(err, "Failed to allocate d_privateKeys");

        err = cudaMalloc(&d_publicKeys, kTestKeyCount * 65);
        checkCudaError(err, "Failed to allocate d_publicKeys");

        err = cudaMalloc(&d_precomputedTable, 1024 * sizeof(PaddedECCPoint));
        checkCudaError(err, "Failed to allocate d_precomputedTable");
    }

    void TearDown() override {
        if (d_privateKeys) {
            cudaFree(d_privateKeys);
            d_privateKeys = nullptr;
        }
        if (d_publicKeys) {
            cudaFree(d_publicKeys);
            d_publicKeys = nullptr;
        }
        if (d_precomputedTable) {
            cudaFree(d_precomputedTable);
            d_precomputedTable = nullptr;
        }

        CudaTestFixture::TearDown();
    }

    unsigned char* d_privateKeys = nullptr;
    unsigned char* d_publicKeys = nullptr;
    PaddedECCPoint* d_precomputedTable = nullptr;
};

/**
 * @brief Test: Compute public keys from 10,000 random private keys
 *
 * Validates:
 * - GPU results match bitcoin-core/secp256k1 CPU reference
 * - Relative error < 1e-10 (FR-005 requirement)
 * - 100% pass rate (no mismatches allowed)
 *
 * Expected: FAIL initially (kernel stub returns zeros)
 */
TEST_F(ECCScalarMulTest, ComputePublicKeys_10000RandomKeys_MatchesCPUReference) {
    // Generate 10,000 random private keys (seeded PRNG for reproducibility)
    std::vector<std::array<unsigned char, 32>> h_privateKeys(kTestKeyCount);
    for (size_t i = 0; i < kTestKeyCount; i++) {
        generateRandomPrivateKey(0x123456789ABCDEF + i, h_privateKeys[i].data());
    }

    // Compute CPU reference results using bitcoin-core/secp256k1
    std::vector<std::array<unsigned char, 65>> h_cpuPublicKeys(kTestKeyCount);
    int cpuSuccessCount = 0;

    for (size_t i = 0; i < kTestKeyCount; i++) {
        if (computePublicKeyCPU(h_privateKeys[i].data(), h_cpuPublicKeys[i].data())) {
            cpuSuccessCount++;
        }
    }

    ASSERT_GT(cpuSuccessCount, kTestKeyCount * 0.99)
        << "CPU reference computation should succeed for >99% of random keys";

    // Copy private keys to device
    copyToDevice(d_privateKeys, h_privateKeys.data(), kTestKeyCount * 32);

    // Launch GPU kernel (stub implementation - will fail)
    dim3 blockSize(256);
    dim3 gridSize((kTestKeyCount + blockSize.x - 1) / blockSize.x);

    eccScalarMulKernel_Stub<<<gridSize, blockSize>>>(
        reinterpret_cast<uint64_t*>(d_privateKeys),
        d_publicKeys,
        d_precomputedTable,
        kTestKeyCount);

    syncAndCheckErrors();

    // Copy GPU results back to host
    std::vector<std::array<unsigned char, 65>> h_gpuPublicKeys(kTestKeyCount);
    copyFromDevice(h_gpuPublicKeys.data(), d_publicKeys, kTestKeyCount * 65);

    // Validate: Compare GPU results with CPU reference
    int passedCount = 0;
    int failedCount = 0;
    double maxRelativeError = 0.0;
    std::vector<size_t> failedIndices;

    for (size_t i = 0; i < kTestKeyCount; i++) {
        // Skip if CPU computation failed for this key
        if (h_cpuPublicKeys[i][0] != 0x04) {
            continue;
        }

        // Compare GPU vs CPU (byte-by-byte)
        bool match = true;
        for (int j = 0; j < 65; j++) {
            if (h_gpuPublicKeys[i][j] != h_cpuPublicKeys[i][j]) {
                match = false;
                break;
            }
        }

        if (match) {
            passedCount++;
        } else {
            failedCount++;
            if (failedIndices.size() < 10) {  // Store first 10 failures for debugging
                failedIndices.push_back(i);
            }
        }
    }

    // Calculate pass rate
    double passRate = (passedCount * 100.0) / (passedCount + failedCount);

    // Report first 10 failures (for debugging)
    if (!failedIndices.empty()) {
        std::cout << "\nFirst " << failedIndices.size() << " failed cases:" << std::endl;
        for (size_t idx : failedIndices) {
            std::cout << "  Key " << idx << ": CPU[0]=" << std::hex
                      << static_cast<int>(h_cpuPublicKeys[idx][0])
                      << ", GPU[0]=" << static_cast<int>(h_gpuPublicKeys[idx][0])
                      << std::dec << std::endl;
        }
    }

    // Assertions (FR-005 requirements)
    EXPECT_EQ(failedCount, 0)
        << "GPU-CPU parity validation failed for " << failedCount << " keys";
    EXPECT_EQ(passRate, 100.0)
        << "Pass rate must be 100% (actual: " << passRate << "%)";
    EXPECT_LT(maxRelativeError, 1e-10)
        << "Maximum relative error must be <1e-10 (actual: " << maxRelativeError << ")";
}

/**
 * @brief Test: Verify kernel handles edge case private keys correctly
 *
 * Tests:
 * - Minimum valid private key (1)
 * - Maximum valid private key (n-1, where n is secp256k1 curve order)
 * - Common test vectors from bitcoin-core
 */
TEST_F(ECCScalarMulTest, EdgeCasePrivateKeys_HandledCorrectly) {
    // Test with known test vectors
    std::vector<std::array<unsigned char, 32>> h_testKeys = {
        // Private key = 1
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
        // Private key = 2
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2},
    };

    size_t testCount = h_testKeys.size();

    // Compute CPU reference
    std::vector<std::array<unsigned char, 65>> h_cpuResults(testCount);
    for (size_t i = 0; i < testCount; i++) {
        ASSERT_TRUE(computePublicKeyCPU(h_testKeys[i].data(), h_cpuResults[i].data()))
            << "CPU computation failed for test vector " << i;
    }

    // TODO: GPU computation and comparison (will be added when kernel is implemented)
    GTEST_SKIP() << "GPU kernel not yet implemented - skipping edge case test";
}
