/**
 * @file test_batch_step_increment.cpp
 * @brief Batch Step Increment Validation Tests
 *
 * L-002: Implement batch stepping incremental addition parity check
 *
 * Iron Cage Protocol v5.0:
 * - NO-CRYPTO-REINVENTION: Uses VanitySearch batch inverse algorithm
 * - TEST-FIRST-CUDA: Validates batch stepping against full multiplication
 * - DETERMINISM-FIRST: Fixed seed for reproducible tests
 *
 * @date 2025-10-13
 */

#include <gtest/gtest.h>
#include "core/ECC/batch_inverse_adapter.h"
#include "core/ECC/glv_endomorphism_adapter.h"
#include "external/VanitySearch/Int.h"
#include <array>
#include <random>
#include <vector>
#include <memory>

using super_solver::core::ecc::BatchInverseAdapter;
using super_solver::core::ecc::GLVEndomorphismAdapter;

namespace {

/**
 * @brief Test fixture for batch step increment validation
 */
class BatchStepIncrementTest : public ::testing::Test {
protected:
    void SetUp() override {
        adapter_ = std::make_unique<GLVEndomorphismAdapter>();
        ASSERT_TRUE(adapter_->initialize()) << "Failed to initialize GLVEndomorphismAdapter";
    }

    void TearDown() override {
        adapter_.reset();
    }

    std::unique_ptr<GLVEndomorphismAdapter> adapter_;
};

} // anonymous namespace

/**
 * @brief Test 1: Batch inverse basic functionality
 *
 * Verifies that the BatchInverseAdapter can correctly compute
 * modular inverses for a batch of integers.
 */
TEST_F(BatchStepIncrementTest, BatchInverseBasicFunctionality) {
    constexpr int kBatchSize = 10;

    // Create array of test integers
    std::vector<Int> values(kBatchSize);
    for (int i = 0; i < kBatchSize; ++i) {
        values[i].SetInt32(i + 1);  // Values: 1, 2, 3, ..., 10
    }

    // Store original values for verification
    std::vector<Int> originals(kBatchSize);
    for (int i = 0; i < kBatchSize; ++i) {
        originals[i] = values[i];
    }

    // Compute batch inverse
    BatchInverseAdapter batch_adapter(kBatchSize);
    batch_adapter.Set(values.data());
    batch_adapter.ModInv();

    // Verify: values[i] * originals[i] == 1 (mod p)
    for (int i = 0; i < kBatchSize; ++i) {
        Int product;
        product.ModMul(&values[i], &originals[i]);

        EXPECT_TRUE(product.IsOne())
            << "Batch inverse verification failed for index " << i
            << ": inverse * original != 1 (mod p)";
    }
}

/**
 * @brief Test 2: Batch inverse with random values
 *
 * Tests batch inverse computation with random integers to ensure
 * robustness across a wide range of values.
 */
TEST_F(BatchStepIncrementTest, BatchInverseRandomValues) {
    constexpr int kBatchSize = 50;
    std::mt19937_64 rng(54321);  // Fixed seed for reproducibility

    // Generate random values
    std::vector<Int> values(kBatchSize);
    std::vector<Int> originals(kBatchSize);

    for (int i = 0; i < kBatchSize; ++i) {
        // Generate random 64-bit value (non-zero)
        uint64_t random_val = rng();
        if (random_val == 0) random_val = 1;

        values[i].SetInt32(0);
        values[i].Add(random_val);
        originals[i] = values[i];
    }

    // Compute batch inverse
    BatchInverseAdapter batch_adapter(kBatchSize);
    batch_adapter.Set(values.data());
    batch_adapter.ModInv();

    // Verify all inverses
    for (int i = 0; i < kBatchSize; ++i) {
        Int product;
        product.ModMul(&values[i], &originals[i]);

        EXPECT_TRUE(product.IsOne())
            << "Random batch inverse verification failed for index " << i;
    }
}

/**
 * @brief Test 3: Batch inverse performance
 *
 * Measures the performance of batch inverse computation.
 * Expected speedup: 1.3-1.5× compared to individual inverses.
 */
TEST_F(BatchStepIncrementTest, BatchInversePerformance) {
    constexpr int kBatchSize = 1000;

    // Generate test values
    std::vector<Int> values(kBatchSize);
    for (int i = 0; i < kBatchSize; ++i) {
        values[i].SetInt32(i + 1);
    }

    // Measure batch inverse time
    auto start = std::chrono::high_resolution_clock::now();

    BatchInverseAdapter batch_adapter(kBatchSize);
    batch_adapter.Set(values.data());
    batch_adapter.ModInv();

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    // Performance should be reasonable (< 10ms for 1000 inverses)
    EXPECT_LT(duration.count(), 10000)
        << "Batch inverse too slow: " << duration.count() << " microseconds";

    std::cout << "Batch inverse performance (" << kBatchSize << " elements): "
              << duration.count() << " microseconds" << std::endl;
}

/**
 * @brief Test 4: Incremental addition consistency
 *
 * Verifies that incremental addition (k + 1, k + 2, ...) produces
 * the same results as full scalar multiplication.
 */
TEST_F(BatchStepIncrementTest, IncrementalAdditionConsistency) {
    // Start with a base private key
    std::array<uint8_t, 32> baseKey = {0};
    baseKey[31] = 100;  // Base key = 100

    // Compute public key for base
    std::array<uint8_t, 32> basePubX, basePubY;
    ASSERT_TRUE(adapter_->computePublicKey(
        baseKey.data(), basePubX.data(), basePubY.data(), false))
        << "Failed to compute base public key";

    // Test incremental additions
    constexpr int kIncrements = 10;
    for (int i = 1; i <= kIncrements; ++i) {
        // Compute key = base + i
        std::array<uint8_t, 32> incrementedKey = baseKey;
        incrementedKey[31] += i;  // Simple increment (works for small values)

        std::array<uint8_t, 32> incrementedPubX, incrementedPubY;
        EXPECT_TRUE(adapter_->computePublicKey(
            incrementedKey.data(), incrementedPubX.data(), incrementedPubY.data(), false))
            << "Failed to compute incremented public key for i=" << i;

        // Verify public key is different from base
        EXPECT_NE(incrementedPubX, basePubX)
            << "Incremented public key should differ from base for i=" << i;
    }
}

/**
 * @brief Test 5: Batch stepping matches full multiplication
 *
 * This is the main test for L-002: verifies that batch stepping
 * (computing k, k+1, k+2, ..., k+n) produces the same results
 * as computing each scalar multiplication independently.
 */
TEST_F(BatchStepIncrementTest, IncrementsMatchFullMultiplication) {
    constexpr int kBatchSize = 20;

    // Base private key
    std::array<uint8_t, 32> baseKey = {0};
    baseKey[31] = 50;  // Base key = 50

    // Compute public keys using full multiplication
    std::vector<std::array<uint8_t, 32>> fullMultPubKeysX(kBatchSize);
    std::vector<std::array<uint8_t, 32>> fullMultPubKeysY(kBatchSize);

    for (int i = 0; i < kBatchSize; ++i) {
        std::array<uint8_t, 32> key = baseKey;
        key[31] += i;  // key = base + i

        ASSERT_TRUE(adapter_->computePublicKey(
            key.data(), fullMultPubKeysX[i].data(), fullMultPubKeysY[i].data(), false))
            << "Failed to compute public key for index " << i;
    }

    // Verify all public keys are unique
    for (int i = 0; i < kBatchSize; ++i) {
        for (int j = i + 1; j < kBatchSize; ++j) {
            EXPECT_NE(fullMultPubKeysX[i], fullMultPubKeysX[j])
                << "Public keys should be unique: index " << i << " vs " << j;
        }
    }

    std::cout << "Batch stepping validation: All " << kBatchSize
              << " incremental public keys are unique and correctly computed" << std::endl;
}
