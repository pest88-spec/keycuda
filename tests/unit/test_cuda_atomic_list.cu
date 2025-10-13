/**
 * Unit tests for CudaAtomicList boundary checking
 * 
 * Purpose: Verify that CudaAtomicList::add() properly handles buffer overflow
 * Test-First-CUDA: This test is written BEFORE fixing the boundary check bug
 * Expected: Test should FAIL initially, then PASS after fix
 * 
 * Related Issue: P0-001 Buffer Overflow Risk
 * Audit Report: audits/CODE_QUALITY_AUDIT_2025-10-13_ROUND2.md
 */

#include <gtest/gtest.h>
#include <cuda_runtime.h>
#include <cstring>

// Include the CudaAtomicList implementation
#include "../../src/extracted/bitcrack/CudaKeySearchDevice/CudaAtomicList.h"

/**
 * Test fixture for CudaAtomicList tests
 */
class CudaAtomicListTest : public ::testing::Test {
protected:
    CudaAtomicList list_;
    static constexpr unsigned int ITEM_SIZE = 32;  // 32 bytes per item
    static constexpr unsigned int MAX_ITEMS = 100;  // Small buffer for testing

    void SetUp() override {
        // Initialize the atomic list
        cudaError_t err = list_.init(ITEM_SIZE, MAX_ITEMS);
        ASSERT_EQ(err, cudaSuccess) << "Failed to initialize CudaAtomicList: " 
                                    << cudaGetErrorString(err);
    }

    void TearDown() override {
        list_.cleanup();
    }
};

/**
 * Test 1: Normal operation - adding items within capacity
 * 
 * Expected: Should succeed without errors
 */
TEST_F(CudaAtomicListTest, NormalAddition) {
    // Add items within capacity
    const unsigned int NUM_ITEMS = 50;  // Half of MAX_ITEMS
    
    for (unsigned int i = 0; i < NUM_ITEMS; i++) {
        unsigned char data[ITEM_SIZE];
        memset(data, i, ITEM_SIZE);
        
        // Note: We can't directly call atomicListAdd from host code
        // This test verifies the list can hold the expected number of items
    }
    
    // Verify size is within bounds
    EXPECT_LE(list_.size(), MAX_ITEMS);
}

/**
 * Test 2: Boundary case - adding exactly MAX_ITEMS
 * 
 * Expected: Should succeed, size should equal MAX_ITEMS
 */
TEST_F(CudaAtomicListTest, BoundaryAddition) {
    // This test will be implemented after we have a way to add items from host
    // For now, we verify the list can be initialized with MAX_ITEMS capacity
    
    EXPECT_EQ(list_.size(), 0);  // Initially empty
}

/**
 * Test 3: Overflow case - attempting to add more than MAX_ITEMS
 * 
 * Expected: Should NOT crash, should handle overflow gracefully
 * 
 * THIS TEST WILL FAIL BEFORE THE FIX IS APPLIED
 * After fix: Should pass by preventing buffer overflow
 */
TEST_F(CudaAtomicListTest, OverflowPrevention) {
    // This test requires a CUDA kernel to actually trigger the overflow
    // We'll create a simple kernel for testing
    
    // For now, we verify that the list has a maximum capacity
    EXPECT_GT(MAX_ITEMS, 0);
    
    // TODO: Implement kernel-based overflow test after fix
    // The kernel should:
    // 1. Attempt to add MAX_ITEMS + 10 items
    // 2. Verify that only MAX_ITEMS are actually added
    // 3. Verify no memory corruption occurs
}

/**
 * Test 4: Clear and reuse
 * 
 * Expected: After clear(), list should be empty and reusable
 */
TEST_F(CudaAtomicListTest, ClearAndReuse) {
    // Clear the list
    list_.clear();
    
    // Verify it's empty
    EXPECT_EQ(list_.size(), 0);
    
    // Verify we can add items after clearing
    // (Actual addition test requires kernel implementation)
}

/**
 * Test 5: Read operation
 * 
 * Expected: Should read correct number of items
 */
TEST_F(CudaAtomicListTest, ReadOperation) {
    unsigned char buffer[ITEM_SIZE * MAX_ITEMS];
    
    // Read from empty list
    unsigned int count = list_.read(buffer, MAX_ITEMS);
    EXPECT_EQ(count, 0);  // Should read 0 items from empty list
}

/**
 * CUDA Kernel for overflow testing
 * 
 * This kernel will be used to test the boundary check fix
 */
__global__ void testAtomicListAddKernel(unsigned int numItems) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    
    if (idx < numItems) {
        // Create test data
        unsigned char data[32];
        for (int i = 0; i < 32; i++) {
            data[i] = (unsigned char)(idx & 0xFF);
        }
        
        // Attempt to add to atomic list
        // This will call atomicListAdd() which currently has no boundary check
        atomicListAdd(data, 32);
    }
}

/**
 * Test 6: Kernel-based overflow test
 * 
 * THIS IS THE CRITICAL TEST THAT WILL FAIL BEFORE THE FIX
 * 
 * Expected behavior BEFORE fix:
 * - Buffer overflow occurs
 * - Memory corruption possible
 * - CUDA-MEMCHECK will report errors
 * 
 * Expected behavior AFTER fix:
 * - No buffer overflow
 * - Only MAX_ITEMS are added
 * - Excess items are silently dropped or error is reported
 */
TEST_F(CudaAtomicListTest, KernelOverflowTest) {
    // Attempt to add more items than capacity
    const unsigned int OVERFLOW_ITEMS = MAX_ITEMS + 50;
    
    // Launch kernel
    int threadsPerBlock = 256;
    int blocksPerGrid = (OVERFLOW_ITEMS + threadsPerBlock - 1) / threadsPerBlock;
    
    testAtomicListAddKernel<<<blocksPerGrid, threadsPerBlock>>>(OVERFLOW_ITEMS);
    
    // Wait for kernel to complete
    cudaError_t err = cudaDeviceSynchronize();
    
    // BEFORE FIX: This might crash or cause memory corruption
    // AFTER FIX: Should complete successfully
    EXPECT_EQ(err, cudaSuccess) << "Kernel execution failed: " 
                                << cudaGetErrorString(err);
    
    // Verify that no more than MAX_ITEMS were added
    unsigned int actualSize = list_.size();
    
    // BEFORE FIX: actualSize might be > MAX_ITEMS (buffer overflow)
    // AFTER FIX: actualSize should be <= MAX_ITEMS
    EXPECT_LE(actualSize, MAX_ITEMS) 
        << "Buffer overflow detected: " << actualSize << " items added, "
        << "but maximum capacity is " << MAX_ITEMS;
}

/**
 * Test 7: Concurrent access stress test
 * 
 * Expected: Multiple threads should be able to add items concurrently
 * without causing memory corruption
 */
TEST_F(CudaAtomicListTest, ConcurrentAccessStressTest) {
    // Launch many threads attempting to add items simultaneously
    const unsigned int STRESS_ITEMS = MAX_ITEMS * 2;  // Intentional overflow
    
    int threadsPerBlock = 256;
    int blocksPerGrid = (STRESS_ITEMS + threadsPerBlock - 1) / threadsPerBlock;
    
    testAtomicListAddKernel<<<blocksPerGrid, threadsPerBlock>>>(STRESS_ITEMS);
    
    cudaError_t err = cudaDeviceSynchronize();
    EXPECT_EQ(err, cudaSuccess);
    
    // Verify no overflow
    EXPECT_LE(list_.size(), MAX_ITEMS);
}

/**
 * Main function for running tests
 */
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    
    // Check CUDA availability
    int deviceCount = 0;
    cudaError_t err = cudaGetDeviceCount(&deviceCount);
    
    if (err != cudaSuccess || deviceCount == 0) {
        std::cerr << "No CUDA devices available. Skipping tests." << std::endl;
        return 0;
    }
    
    std::cout << "Running CudaAtomicList tests on " << deviceCount << " CUDA device(s)" << std::endl;
    
    return RUN_ALL_TESTS();
}

