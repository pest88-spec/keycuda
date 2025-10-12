/**
 * @file test_baseline_serialization.cpp
 * @brief Unit test for baseline JSON serialization with SHA-256 digest protection
 *
 * Test T037 [US3]: Write test for baseline JSON serialization
 * - Create PerformanceBaseline entity with sample data
 * - Serialize to JSON using T009 helper
 * - Compute SHA-256 digest, verify matches
 * - Deserialize JSON, verify SHA-256 verification detects tampering (modify one field, expect load failure)
 *
 * Expected to FAIL initially (baseline management not yet implemented - Red phase)
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <fstream>
#include <filesystem>
#include <memory>
#include <string>
#include <stdexcept>

// Include the entities and serialization utilities
#include "KeyhuntCore/utils/json_serializer.h"

using namespace keyhunt::utils;
using ::testing::HasSubstr;
using ::testing::Throws;
using ::testing::Ne;

class BaselineSerializationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create test directory
        test_dir_ = std::filesystem::temp_directory_path() / "baseline_test";
        std::filesystem::create_directories(test_dir_);

        // Sample baseline file path
        baseline_file_ = test_dir_ / "test_baseline.json";
        tampered_file_ = test_dir_ / "tampered_baseline.json";
    }

    void TearDown() override {
        // Cleanup test files
        std::error_code ec;
        std::filesystem::remove_all(test_dir_, ec);
    }

    std::filesystem::path test_dir_;
    std::filesystem::path baseline_file_;
    std::filesystem::path tampered_file_;
};

// Test T037.1: Create PerformanceBaseline entity with sample data
TEST_F(BaselineSerializationTest, CreatePerformanceBaseline) {
    // Arrange & Act
    PerformanceBaseline baseline = createSamplePerformanceBaseline();

    // Assert - Verify baseline has expected fields
    EXPECT_EQ(baseline.baselineId, "rtx3090_v1");
    EXPECT_EQ(baseline.gpuModel, "RTX 3090");
    EXPECT_EQ(baseline.computeCapability, "8.6");
    EXPECT_EQ(baseline.targetThroughput, 2.0);
    EXPECT_EQ(baseline.medianThroughput, 2.15);
    EXPECT_EQ(baseline.status, BaselineStatus::Active);

    // Verify kernel configuration
    EXPECT_EQ(baseline.kernelConfiguration.kernelName, "eccScalarMulKernel");
    EXPECT_EQ(baseline.kernelConfiguration.gpuArchitecture, GPUArchitecture::Ampere);
    EXPECT_EQ(baseline.kernelConfiguration.pointsPerThread, 1024);
}

// Test T037.2: Serialize PerformanceBaseline to JSON
TEST_F(BaselineSerializationTest, SerializeBaselineToJSON) {
    // Arrange
    PerformanceBaseline baseline = createSamplePerformanceBaseline();

    // Act
    json j = serializePerformanceBaseline(baseline);

    // Assert - Verify JSON contains expected fields
    EXPECT_TRUE(j.contains("baselineId"));
    EXPECT_TRUE(j.contains("gpuModel"));
    EXPECT_TRUE(j.contains("kernelConfiguration"));
    EXPECT_TRUE(j.contains("targetThroughput"));
    EXPECT_TRUE(j.contains("medianThroughput"));
    EXPECT_TRUE(j.contains("status"));

    EXPECT_EQ(j["baselineId"], "rtx3090_v1");
    EXPECT_EQ(j["gpuModel"], "RTX 3090");
    EXPECT_EQ(j["targetThroughput"], 2.0);
    EXPECT_EQ(j["medianThroughput"], 2.15);
    EXPECT_EQ(j["status"], "Active");

    // Verify kernel configuration serialization
    EXPECT_TRUE(j["kernelConfiguration"].contains("kernelName"));
    EXPECT_EQ(j["kernelConfiguration"]["kernelName"], "eccScalarMulKernel");
}

// Test T037.3: Serialize JSON with SHA-256 digest protection
TEST_F(BaselineSerializationTest, SerializeWithSHA256Digest) {
    // Arrange
    PerformanceBaseline baseline = createSamplePerformanceBaseline();
    json baselineJson = serializePerformanceBaseline(baseline);

    // Act
    json protectedJson = addSHA256Digest(baselineJson);

    // Assert
    EXPECT_TRUE(protectedJson.contains("sha256Digest"));
    EXPECT_EQ(protectedJson["sha256Digest"].get<std::string>().length(), 64); // SHA-256 hex length

    // Verify digest is computed correctly
    EXPECT_TRUE(verifySHA256Digest(protectedJson));
}

// Test T037.4: Save and load protected JSON file
TEST_F(BaselineSerializationTest, SaveAndLoadProtectedJSON) {
    // Arrange
    PerformanceBaseline baseline = createSamplePerformanceBaseline();
    json baselineJson = serializePerformanceBaseline(baseline);

    // Act - Save protected JSON
    saveProtectedJSONToFile(baselineJson, baseline_file_.string());

    // Assert - File was created
    EXPECT_TRUE(std::filesystem::exists(baseline_file_));

    // Act - Load and verify protected JSON
    json loadedJson = loadProtectedJSONFromFile(baseline_file_.string());

    // Assert - Loaded JSON matches original
    EXPECT_EQ(loadedJson["baselineId"], baseline.baselineId);
    EXPECT_EQ(loadedJson["gpuModel"], baseline.gpuModel);
    EXPECT_EQ(loadedJson["targetThroughput"], baseline.targetThroughput);
    EXPECT_TRUE(loadedJson.contains("sha256Digest"));
    EXPECT_TRUE(verifySHA256Digest(loadedJson));
}

// Test T037.5: Detect tampering through SHA-256 verification failure
TEST_F(BaselineSerializationTest, DetectTampering) {
    // Arrange
    PerformanceBaseline baseline = createSamplePerformanceBaseline();
    json baselineJson = serializePerformanceBaseline(baseline);
    saveProtectedJSONToFile(baselineJson, baseline_file_.string());

    // Act - Tamper with the file by modifying a field
    std::ifstream file(baseline_file_);
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    // Replace target throughput from 2.0 to 1.9 (simulate performance regression)
    size_t pos = content.find("\"targetThroughput\": 2.0");
    ASSERT_NE(pos, std::string::npos); // Make sure we found the field
    content.replace(pos, 23, "\"targetThroughput\": 1.9");

    std::ofstream tamperedFile(tampered_file_);
    tamperedFile << content;
    tamperedFile.close();

    // Assert - Loading tampered file should throw exception
    EXPECT_THROW(
        loadProtectedJSONFromFile(tampered_file_.string()),
        std::runtime_error
    );

    // Verify error message mentions tampering
    try {
        loadProtectedJSONFromFile(tampered_file_.string());
        FAIL() << "Expected runtime_error for tampered file";
    } catch (const std::runtime_error& e) {
        EXPECT_THAT(e.what(), HasSubstr("SHA-256 digest verification failed"));
        EXPECT_THAT(e.what(), HasSubstr("tampering"));
    }
}

// Test T037.6: Round-trip serialization and deserialization
TEST_F(BaselineSerializationTest, RoundTripSerialization) {
    // Arrange
    PerformanceBaseline originalBaseline = createSamplePerformanceBaseline();

    // Act - Serialize to protected JSON and load back
    json baselineJson = serializePerformanceBaseline(originalBaseline);
    saveProtectedJSONToFile(baselineJson, baseline_file_.string());

    json loadedJson = loadProtectedJSONFromFile(baseline_file_.string());
    PerformanceBaseline deserializedBaseline = deserializePerformanceBaseline(loadedJson);

    // Assert - All fields match
    EXPECT_EQ(deserializedBaseline.baselineId, originalBaseline.baselineId);
    EXPECT_EQ(deserializedBaseline.gpuModel, originalBaseline.gpuModel);
    EXPECT_EQ(deserializedBaseline.computeCapability, originalBaseline.computeCapability);
    EXPECT_EQ(deserializedBaseline.targetThroughput, originalBaseline.targetThroughput);
    EXPECT_EQ(deserializedBaseline.medianThroughput, originalBaseline.medianThroughput);
    EXPECT_EQ(deserializedBaseline.status, originalBaseline.status);

    // Verify kernel configuration
    EXPECT_EQ(deserializedBaseline.kernelConfiguration.kernelName,
              originalBaseline.kernelConfiguration.kernelName);
    EXPECT_EQ(deserializedBaseline.kernelConfiguration.gpuArchitecture,
              originalBaseline.kernelConfiguration.gpuArchitecture);
    EXPECT_EQ(deserializedBaseline.kernelConfiguration.pointsPerThread,
              originalBaseline.kernelConfiguration.pointsPerThread);
}

// Test T037.7: SHA-256 digest consistency
TEST_F(BaselineSerializationTest, SHA256DigestConsistency) {
    // Arrange
    PerformanceBaseline baseline = createSamplePerformanceBaseline();
    json baselineJson = serializePerformanceBaseline(baseline);

    // Act - Compute digest multiple times
    json protectedJson1 = addSHA256Digest(baselineJson);
    json protectedJson2 = addSHA256Digest(baselineJson);

    // Assert - Digests should be identical
    std::string digest1 = protectedJson1["sha256Digest"];
    std::string digest2 = protectedJson2["sha256Digest"];
    EXPECT_EQ(digest1, digest2);
    EXPECT_EQ(digest1.length(), 64); // SHA-256 hex length

    // Verify both are valid
    EXPECT_TRUE(verifySHA256Digest(protectedJson1));
    EXPECT_TRUE(verifySHA256Digest(protectedJson2));
}

// Test T037.8: Handle missing digest field gracefully
TEST_F(BaselineSerializationTest, HandleMissingDigest) {
    // Arrange
    PerformanceBaseline baseline = createSamplePerformanceBaseline();
    json baselineJson = serializePerformanceBaseline(baseline);

    // Act & Assert - JSON without digest should fail verification
    EXPECT_FALSE(verifySHA256Digest(baselineJson));

    // But adding digest should make it pass
    json protectedJson = addSHA256Digest(baselineJson);
    EXPECT_TRUE(verifySHA256Digest(protectedJson));
}

// Test T037.9: JSON serialization preserves numeric precision
TEST_F(BaselineSerializationTest, PreserveNumericPrecision) {
    // Arrange
    PerformanceBaseline baseline = createSamplePerformanceBaseline();
    baseline.targetThroughput = 2.123456789;  // High precision value
    baseline.medianThroughput = 2.234567890;
    baseline.gpuUtilizationPercent = 92.3456789;

    // Act
    json baselineJson = serializePerformanceBaseline(baseline);
    saveProtectedJSONToFile(baselineJson, baseline_file_.string());

    json loadedJson = loadProtectedJSONFromFile(baseline_file_.string());
    PerformanceBaseline deserializedBaseline = deserializePerformanceBaseline(loadedJson);

    // Assert - High precision values should be preserved
    EXPECT_NEAR(deserializedBaseline.targetThroughput, baseline.targetThroughput, 1e-9);
    EXPECT_NEAR(deserializedBaseline.medianThroughput, baseline.medianThroughput, 1e-9);
    EXPECT_NEAR(deserializedBaseline.gpuUtilizationPercent, baseline.gpuUtilizationPercent, 1e-6);
}

// Test T037.10: Enum serialization and deserialization
TEST_F(BaselineSerializationTest, EnumSerialization) {
    // Arrange - Test all GPU architecture enums
    std::vector<GPUArchitecture> architectures = {
        GPUArchitecture::Turing,
        GPUArchitecture::Ampere,
        GPUArchitecture::Hopper
    };

    std::vector<std::string> expectedStrings = {"Turing", "Ampere", "Hopper"};

    for (size_t i = 0; i < architectures.size(); ++i) {
        // Act
        GPUKernelConfiguration config = createSampleGPUKernelConfiguration();
        config.gpuArchitecture = architectures[i];

        json configJson = serializeGPUKernelConfiguration(config);
        saveProtectedJSONToFile(configJson, baseline_file_.string());

        json loadedJson = loadProtectedJSONFromFile(baseline_file_.string());
        GPUKernelConfiguration deserializedConfig = deserializeGPUKernelConfiguration(loadedJson);

        // Assert
        EXPECT_EQ(configJson["gpuArchitecture"], expectedStrings[i]);
        EXPECT_EQ(deserializedConfig.gpuArchitecture, architectures[i]);
    }
}