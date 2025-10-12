/**
 * @file test_shared_memory.cu
 * @brief Unit tests for shared memory optimization (bank conflict elimination)
 *
 * Tests T011: Shared memory ECC table loading with zero bank conflicts
 *
 * Expected to FAIL initially (Red phase) until T017 implementation completes.
 */

#include <gtest/gtest.h>
#include "cuda_test_fixture.h"
#include "KeyhuntCore/kernels/shared_memory.cuh"
#include <vector>
#include <random>

using namespace keyhunt::testing;
using namespace keyhunt::kernels;

/**
 * @brief Test kernel: Load precomputed table and verify in shared memory
 *
 * This kernel loads a precomputed ECC table into shared memory and verifies
 * that all threads can access it without bank conflicts.
 */
__global__ void testSharedMemoryLoadingKernel(
    const PaddedECCPoint* __restrict__ d_globalTable,
    PaddedECCPoint* __restrict__ d_output,
    const size_t tableSize)
{
    // Declare shared memory for precomputed table
    __shared__ PaddedECCPoint sharedTable[1024];

    // Load table from global to shared memory (coalesced pattern)
    loadPrecomputedTableToSharedMemory(d_globalTable, sharedTable, tableSize);

    // Each thread reads from shared memory and writes to global memory
    // This verifies that loading worked correctly
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid < tableSize) {
        // Read from shared memory (should have zero bank conflicts due to padding)
        d_output[tid] = sharedTable[tid];
    }
}

/**
 * @brief Test fixture for shared memory tests
 */
class SharedMemoryTest : public CudaTestFixture {
protected:
    static constexpr size_t kTableSize = 1024;  // Precomputed table size

    void SetUp() override {
        CudaTestFixture::SetUp();

        // Allocate additional memory for ECC point arrays
        cudaError_t err = cudaMalloc(&d_globalTable, kTableSize * sizeof(PaddedECCPoint));
        checkCudaError(err, "Failed to allocate d_globalTable");

        err = cudaMalloc(&d_outputTable, kTableSize * sizeof(PaddedECCPoint));
        checkCudaError(err, "Failed to allocate d_outputTable");
    }

    void TearDown() override {
        if (d_globalTable) {
            cudaFree(d_globalTable);
            d_globalTable = nullptr;
        }
        if (d_outputTable) {
            cudaFree(d_outputTable);
            d_outputTable = nullptr;
        }

        CudaTestFixture::TearDown();
    }

    PaddedECCPoint* d_globalTable = nullptr;
    PaddedECCPoint* d_outputTable = nullptr;
};

/**
 * @brief Test: Load 1024 precomputed ECC points from global to shared memory
 *
 * Validates:
 * - All threads can load table cooperatively (coalesced pattern)
 * - Data integrity: copied points match original points
 * - Zero bank conflicts (manual verification via struct padding)
 *
 * Expected: FAIL initially (kernel not yet implemented)
 */
TEST_F(SharedMemoryTest, LoadPrecomputedTable_1024Points_AllThreadsAccessWithoutBankConflicts) {
    // Generate random ECC points for testing
    std::vector<PaddedECCPoint> h_inputTable(kTableSize);
    std::mt19937_64 rng(0x123456789ABCDEF);
    std::uniform_int_distribution<uint32_t> dist(0, UINT32_MAX);

    for (size_t i = 0; i < kTableSize; i++) {
        for (int j = 0; j < 8; j++) {
            h_inputTable[i].x[j] = dist(rng);
            h_inputTable[i].y[j] = dist(rng);
        }
        h_inputTable[i].pad[0] = 0;  // Padding always zero
    }

    // Copy input table to device
    copyToDevice(d_globalTable, h_inputTable.data(), kTableSize * sizeof(PaddedECCPoint));

    // Launch kernel to load table into shared memory and copy back
    dim3 blockSize(256);
    dim3 gridSize((kTableSize + blockSize.x - 1) / blockSize.x);

    testSharedMemoryLoadingKernel<<<gridSize, blockSize>>>(
        d_globalTable, d_outputTable, kTableSize);

    syncAndCheckErrors();

    // Copy results back to host
    std::vector<PaddedECCPoint> h_outputTable(kTableSize);
    copyFromDevice(h_outputTable.data(), d_outputTable, kTableSize * sizeof(PaddedECCPoint));

    // Validate: Output matches input (data integrity)
    int mismatchCount = 0;
    for (size_t i = 0; i < kTableSize; i++) {
        for (int j = 0; j < 8; j++) {
            if (h_outputTable[i].x[j] != h_inputTable[i].x[j] ||
                h_outputTable[i].y[j] != h_inputTable[i].y[j]) {
                mismatchCount++;
                if (mismatchCount <= 10) {  // Print first 10 mismatches
                    ADD_FAILURE() << "Mismatch at index " << i << ", coordinate " << j;
                }
            }
        }
    }

    EXPECT_EQ(mismatchCount, 0) << "Found " << mismatchCount << " mismatches in loaded table";

    // Manual bank conflict verification (via struct size)
    EXPECT_EQ(sizeof(PaddedECCPoint), 68)
        << "PaddedECCPoint must be 68 bytes (17 words) to eliminate bank conflicts";

    // Verify stride is coprime with 32 (bank count)
    constexpr int stride = sizeof(PaddedECCPoint) / 4;  // Stride in words (68 / 4 = 17)
    constexpr int bankCount = 32;

    // GCD(17, 32) should be 1 (coprime)
    auto gcd = [](int a, int b) -> int {
        while (b != 0) {
            int temp = b;
            b = a % b;
            a = temp;
        }
        return a;
    };

    EXPECT_EQ(gcd(stride, bankCount), 1)
        << "Stride (" << stride << ") must be coprime with bank count (" << bankCount
        << ") to eliminate bank conflicts";
}

/**
 * @brief Test: Verify padding does not affect ECC point data
 *
 * Ensures that padding field is always zero and doesn't interfere with coordinates.
 */
TEST_F(SharedMemoryTest, PaddedECCPoint_PaddingIsZero_DoesNotAffectData) {
    PaddedECCPoint point;

    // Default constructor should zero-initialize
    EXPECT_EQ(point.pad[0], 0) << "Padding must be zero-initialized";

    // Test with custom coordinates
    uint32_t testX[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint32_t testY[8] = {9, 10, 11, 12, 13, 14, 15, 16};

    PaddedECCPoint point2(testX, testY);

    // Verify coordinates copied correctly
    for (int i = 0; i < 8; i++) {
        EXPECT_EQ(point2.x[i], testX[i]);
        EXPECT_EQ(point2.y[i], testY[i]);
    }

    // Verify padding is still zero
    EXPECT_EQ(point2.pad[0], 0) << "Padding must remain zero after initialization";
}
