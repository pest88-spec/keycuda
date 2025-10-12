/**
 * @file test_cpu_gpu_parity.cpp
 * @brief Validation tests for CPU-GPU parity (Constitution Principle VI)
 *
 * Tests T015: Statistical validation with ≥10,000 random test cases
 *
 * Validates:
 * - GPU results match bitcoin-core/secp256k1 CPU reference
 * - 100% pass rate (no failures allowed)
 * - <1e-10 maximum relative error (FR-005)
 * - Validation statistics stored in ValidationResult entity
 *
 * Expected to FAIL initially (Red phase) until T024 implementation completes.
 */

#include <gtest/gtest.h>
#include "cuda_test_fixture.h"
#include "crypto/secp256k1_wrapper.cpp"
#include "KeyhuntCore/utils/json_serializer.cpp"
#include "KeyhuntCore/validation/parity_checker.cuh"
#include <vector>
#include <cmath>
#include <fstream>
#include <iostream>

using namespace keyhunt::testing;
using namespace keyhunt::crypto;
using namespace keyhunt::utils;
using namespace keyhunt::validation;

/**
 * @brief Validation result entity (from data-model.md)
 */
struct ValidationResult {
    std::string validationId;
    std::string kernelName;
    std::string cpuReferenceImplementation;
    uint32_t testCaseCount;
    uint32_t passedCount;
    uint32_t failedCount;
    double passRate;
    double maxRelativeError;
    double meanRelativeError;
    std::vector<struct FailedTest> failedTestSamples;
    uint64_t randomSeed;
    std::string timestamp;
};

/**
 * @brief Failed test case details
 */
struct FailedTest {
    uint32_t testCaseId;
    std::string inputPrivateKey;
    std::string cpuResult;
    std::string gpuResult;
    double relativeError;
};

/**
 * @brief Test fixture for CPU-GPU parity validation
 */
class CPUGPUParityTest : public CudaTestFixture {
protected:
    static constexpr size_t kValidationTestCount = 10000;  // FR-005 requirement

    /**
     * @brief Convert byte array to hex string
     */
    std::string bytesToHex(const unsigned char* bytes, size_t len) {
        std::ostringstream oss;
        for (size_t i = 0; i < len; i++) {
            oss << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<int>(bytes[i]);
        }
        return oss.str();
    }

    /**
     * @brief Compute relative error between two byte arrays (interpreted as big-endian integers)
     */
    double computeRelativeErrorBytes(const unsigned char* expected, const unsigned char* actual, size_t len) {
        // Simplified: Compare first 8 bytes as uint64_t for error estimation
        // Full implementation would need big-integer arithmetic
        uint64_t exp_val = 0, act_val = 0;
        for (size_t i = 0; i < std::min(len, size_t(8)); i++) {
            exp_val = (exp_val << 8) | expected[i];
            act_val = (act_val << 8) | actual[i];
        }

        if (exp_val == 0) {
            return (act_val == 0) ? 0.0 : 1.0;  // Exact match or complete mismatch
        }

        return std::abs(static_cast<double>(act_val) - static_cast<double>(exp_val)) /
               static_cast<double>(exp_val);
    }
};

/**
 * @brief Test: Validate GPU public key computation against CPU reference (10,000 cases)
 *
 * This is the primary validation test for GPU ECC scalar multiplication.
 * Uses seeded PRNG for reproducible test data generation.
 *
 * Validation criteria (FR-005):
 * - 100% pass rate (passedCount == testCaseCount)
 * - <1e-10 maximum relative error
 * - <1e-11 mean relative error
 *
 * Expected: FAIL initially (GPU kernel not yet implemented)
 */
TEST_F(CPUGPUParityTest, ECCScalarMul_10000RandomKeys_100PercentPassRate) {
    // Initialize validation result
    ValidationResult result;
    result.validationId = "VAL-" + std::to_string(std::time(nullptr)) + "-eccScalarMulKernel";
    result.kernelName = "eccScalarMulKernel";
    result.cpuReferenceImplementation = "bitcoin-core/secp256k1";
    result.testCaseCount = kValidationTestCount;
    result.passedCount = 0;
    result.failedCount = 0;
    result.maxRelativeError = 0.0;
    result.meanRelativeError = 0.0;
    result.randomSeed = 0x123456789ABCDEF;
    result.timestamp = getCurrentTimestampUTC();

    // Generate random private keys (seeded PRNG for reproducibility)
    std::vector<std::array<unsigned char, 32>> h_privateKeys(kValidationTestCount);
    for (size_t i = 0; i < kValidationTestCount; i++) {
        generateRandomPrivateKey(result.randomSeed + i, h_privateKeys[i].data());
    }

    // Compute CPU reference results using bitcoin-core/secp256k1
    std::vector<std::array<unsigned char, 65>> h_cpuPublicKeys(kValidationTestCount);
    std::vector<bool> cpuSuccess(kValidationTestCount);

    for (size_t i = 0; i < kValidationTestCount; i++) {
        cpuSuccess[i] = computePublicKeyCPU(h_privateKeys[i].data(), h_cpuPublicKeys[i].data());
    }

    // TODO: Compute GPU results (will be implemented in T024)
    // For now, create dummy GPU results that will cause test to fail
    std::vector<std::array<unsigned char, 65>> h_gpuPublicKeys(kValidationTestCount);
    for (size_t i = 0; i < kValidationTestCount; i++) {
        // Stub: Zero output (will cause test failure)
        std::fill(h_gpuPublicKeys[i].begin(), h_gpuPublicKeys[i].end(), 0);
    }

    // T030: Replace serial validation loop with parallel Thrust reduce
    // Convert arrays to contiguous vectors for GPU processing
    std::vector<uint8_t> h_gpuPublicKeysFlat(kValidationTestCount * 65);
    std::vector<uint8_t> h_cpuPublicKeysFlat(kValidationTestCount * 65);

    for (size_t i = 0; i < kValidationTestCount; i++) {
        std::copy(h_gpuPublicKeys[i].begin(), h_gpuPublicKeys[i].end(),
                  &h_gpuPublicKeysFlat[i * 65]);
        std::copy(h_cpuPublicKeys[i].begin(), h_cpuPublicKeys[i].end(),
                  &h_cpuPublicKeysFlat[i * 65]);
    }

    // Use parallel validation with Thrust reduce
    ValidationMetrics metrics = validateParityParallelHost(
        h_gpuPublicKeysFlat, h_cpuPublicKeysFlat, cpuSuccess);

    // Update result with parallel validation statistics
    result.passedCount = metrics.passedComparisons;
    result.failedCount = metrics.failedComparisons;
    result.maxRelativeError = metrics.maxRelativeError;
    result.meanRelativeError = metrics.meanRelativeError;
    result.passRate = (result.passedCount * 100.0) / result.testCaseCount;

    // For debugging, still collect first 10 failure samples using serial approach
    // (This is a small overhead compared to the parallel validation)
    size_t failuresCollected = 0;
    for (size_t i = 0; i < kValidationTestCount && failuresCollected < 10; i++) {
        if (!cpuSuccess[i]) {
            continue;
        }

        bool match = true;
        for (size_t j = 0; j < 65; j++) {
            if (h_gpuPublicKeys[i][j] != h_cpuPublicKeys[i][j]) {
                match = false;
                break;
            }
        }

        if (!match) {
            FailedTest failed;
            failed.testCaseId = static_cast<uint32_t>(i);
            failed.inputPrivateKey = bytesToHex(h_privateKeys[i].data(), 32);
            failed.cpuResult = bytesToHex(h_cpuPublicKeys[i].data(), 65);
            failed.gpuResult = bytesToHex(h_gpuPublicKeys[i].data(), 65);
            failed.relativeError = computeRelativeErrorBytes(
                h_cpuPublicKeys[i].data() + 1,  // Skip 0x04 prefix
                h_gpuPublicKeys[i].data() + 1,
                64);  // X + Y coordinates
            result.failedTestSamples.push_back(failed);
            failuresCollected++;
        }
    }

    // Print validation summary
    std::cout << "\n=== CPU-GPU Parity Validation Summary ===" << std::endl;
    std::cout << "Kernel: " << result.kernelName << std::endl;
    std::cout << "Test Cases: " << result.testCaseCount << std::endl;
    std::cout << "Passed: " << result.passedCount << std::endl;
    std::cout << "Failed: " << result.failedCount << std::endl;
    std::cout << "Pass Rate: " << std::fixed << std::setprecision(2)
              << result.passRate << "%" << std::endl;
    std::cout << "Max Relative Error: " << std::scientific
              << result.maxRelativeError << std::endl;
    std::cout << "Mean Relative Error: " << std::scientific
              << result.meanRelativeError << std::endl;

    if (!result.failedTestSamples.empty()) {
        std::cout << "\nFirst " << result.failedTestSamples.size()
                  << " failed test cases:" << std::endl;
        for (const auto& failed : result.failedTestSamples) {
            std::cout << "  Test " << failed.testCaseId
                      << " (error: " << failed.relativeError << ")" << std::endl;
            std::cout << "    Input:  " << failed.inputPrivateKey.substr(0, 16) << "..." << std::endl;
            std::cout << "    CPU:    " << failed.cpuResult.substr(0, 16) << "..." << std::endl;
            std::cout << "    GPU:    " << failed.gpuResult.substr(0, 16) << "..." << std::endl;
        }
    }
    std::cout << "========================================\n" << std::endl;

    // Save validation result to JSON (for archival and analysis)
    // TODO: Implement JSON serialization for ValidationResult entity

    // FR-005 assertions
    EXPECT_EQ(result.failedCount, 0)
        << "CPU-GPU parity validation failed for " << result.failedCount << " test cases";
    EXPECT_EQ(result.passRate, 100.0)
        << "Pass rate must be 100% (actual: " << result.passRate << "%)";
    EXPECT_LT(result.maxRelativeError, 1e-10)
        << "Maximum relative error exceeds 1e-10 threshold (FR-005)";
    EXPECT_LT(result.meanRelativeError, 1e-11)
        << "Mean relative error exceeds 1e-11 threshold";
}

/**
 * @brief Test: Determinism - same input produces same output across runs
 *
 * Validates that GPU kernels are deterministic (Constitution Principle I).
 * Run the same input twice and verify bit-identical outputs.
 */
TEST_F(CPUGPUParityTest, Determinism_SameInputTwice_BitIdenticalOutput) {
    constexpr size_t kTestCount = 1000;

    // Generate test keys
    std::vector<std::array<unsigned char, 32>> h_testKeys(kTestCount);
    for (size_t i = 0; i < kTestCount; i++) {
        generateRandomPrivateKey(0xDEADBEEF + i, h_testKeys[i].data());
    }

    // TODO: Run GPU kernel twice and compare outputs
    // Expected: Bit-identical results both times

    // For now, just document the requirement
    GTEST_SKIP() << "Determinism test requires GPU kernel implementation (T024)";
}

/**
 * @brief Test: Edge cases - handle invalid/boundary private keys correctly
 *
 * Tests:
 * - Private key = 0 (invalid, should be rejected or produce error)
 * - Private key = 1 (minimum valid value)
 * - Private key = n-1 (maximum valid value, where n is secp256k1 curve order)
 * - Private key = n (invalid, should be rejected)
 */
TEST_F(CPUGPUParityTest, EdgeCases_InvalidAndBoundaryKeys_HandledCorrectly) {
    // Test vectors for edge cases
    std::vector<std::array<unsigned char, 32>> testKeys = {
        // Zero (invalid)
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        // One (minimum valid)
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
        // Two
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2},
    };

    // Compute CPU reference for valid keys
    for (const auto& key : testKeys) {
        unsigned char cpuPubKey[65];
        bool success = computePublicKeyCPU(key.data(), cpuPubKey);

        if (key[31] == 0) {
            // Zero key should fail
            EXPECT_FALSE(success) << "Private key = 0 should be rejected";
        } else {
            // Valid keys should succeed
            EXPECT_TRUE(success) << "Valid private key should be accepted";
        }
    }

    // TODO: GPU validation (will be implemented in T024)
    GTEST_SKIP() << "Edge case GPU validation requires kernel implementation (T024)";
}
