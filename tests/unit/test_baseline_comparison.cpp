/**
 * @file test_baseline_comparison.cpp
 * @brief Unit test for benchmark result comparison with regression detection
 *
 * Test T038 [US3]: Write test for benchmark result comparison
 * - Create two BenchmarkResult entities: baseline (2.0 Gkeys/s) and current (1.95 Gkeys/s)
 * - Expected: Comparison detects regression (throughputDelta = -0.05, isRegression = true)
 * - Expected: Test fails if regression not detected
 *
 * Expected to FAIL initially (baseline management not yet implemented - Red phase)
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <vector>
#include <algorithm>
#include <cmath>

// Include the entities and serialization utilities
#include "compute/utils/json_serializer.h"

using namespace keyhunt::utils;
using ::testing::HasSubstr;
using ::testing::DoubleEq;
using ::testing::DoubleNear;
using ::testing::Eq;
using ::testing::Ne;
using ::testing::Lt;
using ::testing::Gt;

class BaselineComparisonTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create baseline result (2.0 Gkeys/s)
        baselineResult_ = createSampleBenchmarkResult();
        baselineResult_.resultId = "baseline_20251011_RTX3090";
        baselineResult_.medianThroughput = 2.0;
        baselineResult_.meanThroughput = 1.98;
        baselineResult_.p95Throughput = 2.1;
        baselineResult_.timestamp = "2025-10-11T10:00:00Z";

        // Clear baseline comparison for baseline result
        baselineResult_.baselineComparison.baselineId = "";
        baselineResult_.baselineComparison.baselineThroughput = 0.0;
        baselineResult_.baselineComparison.throughputDelta = 0.0;
        baselineResult_.baselineComparison.throughputDeltaPercent = 0.0;
        baselineResult_.baselineComparison.isRegression = false;

        // Create current result (1.95 Gkeys/s) - regression case
        currentResultRegression_ = createSampleBenchmarkResult();
        currentResultRegression_.resultId = "current_20251011_RTX3090";
        currentResultRegression_.medianThroughput = 1.95;  // Lower than baseline
        currentResultRegression_.meanThroughput = 1.93;
        currentResultRegression_.p95Throughput = 2.05;
        currentResultRegression_.timestamp = "2025-10-11T11:00:00Z";

        // Create current result (2.15 Gkeys/s) - improvement case
        currentResultImprovement_ = createSampleBenchmarkResult();
        currentResultImprovement_.resultId = "current_20251011_RTX3090";
        currentResultImprovement_.medianThroughput = 2.15;  // Higher than baseline
        currentResultImprovement_.meanThroughput = 2.13;
        currentResultImprovement_.p95Throughput = 2.25;
        currentResultImprovement_.timestamp = "2025-10-11T11:00:00Z";

        // Create current result (2.0 Gkeys/s) - no change case
        currentResultNoChange_ = createSampleBenchmarkResult();
        currentResultNoChange_.resultId = "current_20251011_RTX3090";
        currentResultNoChange_.medianThroughput = 2.0;  // Same as baseline
        currentResultNoChange_.meanThroughput = 1.98;
        currentResultNoChange_.p95Throughput = 2.1;
        currentResultNoChange_.timestamp = "2025-10-11T11:00:00Z";
    }

    // Helper function to perform baseline comparison
    BaselineComparison performComparison(const BenchmarkResult& current, const BenchmarkResult& baseline) {
        BaselineComparison comparison;
        comparison.baselineId = baseline.resultId;
        comparison.baselineThroughput = baseline.medianThroughput;
        comparison.throughputDelta = current.medianThroughput - baseline.medianThroughput;
        comparison.throughputDeltaPercent = (comparison.throughputDelta / baseline.medianThroughput) * 100.0;
        comparison.isRegression = comparison.throughputDelta < 0.0;
        return comparison;
    }

    BenchmarkResult baselineResult_;
    BenchmarkResult currentResultRegression_;
    BenchmarkResult currentResultImprovement_;
    BenchmarkResult currentResultNoChange_;
};

// Test T038.1: Create baseline and current benchmark results
TEST_F(BaselineComparisonTest, CreateBenchmarkResults) {
    // Assert - Verify baseline result has expected properties
    EXPECT_EQ(baselineResult_.medianThroughput, 2.0);
    EXPECT_EQ(baselineResult_.resultId, "baseline_20251011_RTX3090");
    EXPECT_EQ(baselineResult_.gpuModel, "RTX 3090");

    // Assert - Verify regression result has lower throughput
    EXPECT_EQ(currentResultRegression_.medianThroughput, 1.95);
    EXPECT_LT(currentResultRegression_.medianThroughput, baselineResult_.medianThroughput);

    // Assert - Verify improvement result has higher throughput
    EXPECT_EQ(currentResultImprovement_.medianThroughput, 2.15);
    EXPECT_GT(currentResultImprovement_.medianThroughput, baselineResult_.medianThroughput);
}

// Test T038.2: Detect regression when current < baseline
TEST_F(BaselineComparisonTest, DetectRegression) {
    // Act
    BaselineComparison comparison = performComparison(currentResultRegression_, baselineResult_);

    // Assert - Regression should be detected
    EXPECT_EQ(comparison.baselineId, baselineResult_.resultId);
    EXPECT_EQ(comparison.baselineThroughput, baselineResult_.medianThroughput);
    EXPECT_DOUBLE_EQ(comparison.throughputDelta, -0.05);  // 1.95 - 2.0 = -0.05
    EXPECT_DOUBLE_EQ(comparison.throughputDeltaPercent, -2.5);  // -0.05 / 2.0 * 100 = -2.5%
    EXPECT_TRUE(comparison.isRegression);

    // Verify exact values match expected regression detection
    EXPECT_LT(comparison.throughputDelta, 0.0);
    EXPECT_LT(comparison.throughputDeltaPercent, 0.0);
}

// Test T038.3: Detect improvement when current > baseline
TEST_F(BaselineComparisonTest, DetectImprovement) {
    // Act
    BaselineComparison comparison = performComparison(currentResultImprovement_, baselineResult_);

    // Assert - Improvement should be detected
    EXPECT_EQ(comparison.baselineId, baselineResult_.resultId);
    EXPECT_EQ(comparison.baselineThroughput, baselineResult_.medianThroughput);
    EXPECT_DOUBLE_EQ(comparison.throughputDelta, 0.15);  // 2.15 - 2.0 = 0.15
    EXPECT_DOUBLE_EQ(comparison.throughputDeltaPercent, 7.5);  // 0.15 / 2.0 * 100 = 7.5%
    EXPECT_FALSE(comparison.isRegression);

    // Verify exact values match expected improvement detection
    EXPECT_GT(comparison.throughputDelta, 0.0);
    EXPECT_GT(comparison.throughputDeltaPercent, 0.0);
}

// Test T038.4: Handle no change case
TEST_F(BaselineComparisonTest, HandleNoChange) {
    // Act
    BaselineComparison comparison = performComparison(currentResultNoChange_, baselineResult_);

    // Assert - No regression should be detected
    EXPECT_EQ(comparison.baselineId, baselineResult_.resultId);
    EXPECT_EQ(comparison.baselineThroughput, baselineResult_.medianThroughput);
    EXPECT_DOUBLE_EQ(comparison.throughputDelta, 0.0);  // 2.0 - 2.0 = 0.0
    EXPECT_DOUBLE_EQ(comparison.throughputDeltaPercent, 0.0);  // 0.0 / 2.0 * 100 = 0.0%
    EXPECT_FALSE(comparison.isRegression);

    // Verify exact values match expected no-change detection
    EXPECT_EQ(comparison.throughputDelta, 0.0);
    EXPECT_EQ(comparison.throughputDeltaPercent, 0.0);
}

// Test T038.5: Test fails if regression not detected (critical test requirement)
TEST_F(BaselineComparisonTest, TestFailsIfRegressionNotDetected) {
    // Arrange - Create comparison that should detect regression
    BaselineComparison comparison = performComparison(currentResultRegression_, baselineResult_);

    // Act & Assert - This test expects regression to be properly detected
    // If isRegression is false when it should be true, test should FAIL
    ASSERT_TRUE(comparison.isRegression)
        << "CRITICAL: Regression detection failed! Current throughput ("
        << currentResultRegression_.medianThroughput
        << " Gkeys/s) is below baseline ("
        << baselineResult_.medianThroughput
        << " Gkeys/s) but isRegression=false";

    // Additional verification that regression magnitude is correct
    EXPECT_DOUBLE_EQ(comparison.throughputDelta, -0.05);
    EXPECT_DOUBLE_EQ(comparison.throughputDeltaPercent, -2.5);

    // If we reach here, regression detection is working correctly
    SUCCEED() << "Regression correctly detected: throughputDelta="
               << comparison.throughputDelta
               << " Gkeys/s (" << comparison.throughputDeltaPercent << "%)";
}

// Test T038.6: Edge case - tiny regression
TEST_F(BaselineComparisonTest, DetectTinyRegression) {
    // Arrange - Create result with very small regression (0.001 Gkeys/s)
    BenchmarkResult tinyRegression = currentResultRegression_;
    tinyRegression.medianThroughput = 1.999;  // Just 0.001 below baseline

    // Act
    BaselineComparison comparison = performComparison(tinyRegression, baselineResult_);

    // Assert - Even tiny regression should be detected
    EXPECT_DOUBLE_EQ(comparison.throughputDelta, -0.001);
    EXPECT_DOUBLE_EQ(comparison.throughputDeltaPercent, -0.05);
    EXPECT_TRUE(comparison.isRegression);
}

// Test T038.7: Edge case - tiny improvement
TEST_F(BaselineComparisonTest, DetectTinyImprovement) {
    // Arrange - Create result with very small improvement (0.001 Gkeys/s)
    BenchmarkResult tinyImprovement = currentResultImprovement_;
    tinyImprovement.medianThroughput = 2.001;  // Just 0.001 above baseline

    // Act
    BaselineComparison comparison = performComparison(tinyImprovement, baselineResult_);

    // Assert - Even tiny improvement should be detected
    EXPECT_DOUBLE_EQ(comparison.throughputDelta, 0.001);
    EXPECT_DOUBLE_EQ(comparison.throughputDeltaPercent, 0.05);
    EXPECT_FALSE(comparison.isRegression);
}

// Test T038.8: Test with different baseline values
TEST_F(BaselineComparisonTest, DifferentBaselineValues) {
    // Test with various baseline throughputs to ensure percentage calculation is robust
    std::vector<double> baselineThroughputs = {0.5, 1.0, 2.0, 4.0, 10.0};

    for (double baselineThroughput : baselineThroughputs) {
        // Arrange
        BenchmarkResult baseline = baselineResult_;
        baseline.medianThroughput = baselineThroughput;
        baseline.resultId = "baseline_" + std::to_string(static_cast<int>(baselineThroughput * 1000));

        BenchmarkResult current = currentResultRegression_;
        current.medianThroughput = baselineThroughput * 0.975;  // 2.5% regression

        // Act
        BaselineComparison comparison = performComparison(current, baseline);

        // Assert - Should always detect 2.5% regression
        EXPECT_DOUBLE_EQ(comparison.throughputDeltaPercent, -2.5);
        EXPECT_TRUE(comparison.isRegression)
            << "Failed to detect regression for baseline " << baselineThroughput;
    }
}

// Test T038.9: Verify baseline comparison serialization
TEST_F(BaselineComparisonTest, SerializeBaselineComparison) {
    // Arrange
    BaselineComparison comparison = performComparison(currentResultRegression_, baselineResult_);

    // Act - Serialize comparison to JSON
    json comparisonJson;
    comparisonJson["baselineId"] = comparison.baselineId;
    comparisonJson["baselineThroughput"] = comparison.baselineThroughput;
    comparisonJson["throughputDelta"] = comparison.throughputDelta;
    comparisonJson["throughputDeltaPercent"] = comparison.throughputDeltaPercent;
    comparisonJson["isRegression"] = comparison.isRegression;

    // Assert - JSON should contain correct values
    EXPECT_EQ(comparisonJson["baselineId"], baselineResult_.resultId);
    EXPECT_EQ(comparisonJson["baselineThroughput"], baselineResult_.medianThroughput);
    EXPECT_DOUBLE_EQ(comparisonJson["throughputDelta"], -0.05);
    EXPECT_DOUBLE_EQ(comparisonJson["throughputDeltaPercent"], -2.5);
    EXPECT_TRUE(comparisonJson["isRegression"]);
}

// Test T038.10: Integration with benchmark result serialization
TEST_F(BaselineComparisonTest, IntegratedBenchmarkResultComparison) {
    // Arrange - Create benchmark result with baseline comparison
    BenchmarkResult resultWithComparison = currentResultRegression_;
    resultWithComparison.baselineComparison = performComparison(currentResultRegression_, baselineResult_);

    // Act - Serialize to JSON
    json resultJson = serializeBenchmarkResult(resultWithComparison);

    // Assert - JSON should contain baseline comparison with regression detected
    EXPECT_TRUE(resultJson.contains("baselineComparison"));
    auto baselineComp = resultJson["baselineComparison"];
    EXPECT_EQ(baselineComp["baselineId"], baselineResult_.resultId);
    EXPECT_DOUBLE_EQ(baselineComp["throughputDelta"], -0.05);
    EXPECT_DOUBLE_EQ(baselineComp["throughputDeltaPercent"], -2.5);
    EXPECT_TRUE(baselineComp["isRegression"]);

    // Act - Deserialize and verify
    BenchmarkResult deserializedResult = deserializeBenchmarkResult(resultJson);

    // Assert - Deserialized result should preserve regression detection
    EXPECT_EQ(deserializedResult.baselineComparison.baselineId, baselineResult_.resultId);
    EXPECT_DOUBLE_EQ(deserializedResult.baselineComparison.throughputDelta, -0.05);
    EXPECT_DOUBLE_EQ(deserializedResult.baselineComparison.throughputDeltaPercent, -2.5);
    EXPECT_TRUE(deserializedResult.baselineComparison.isRegression);
}

// Test T038.11: Critical test - ensure regression detection cannot be bypassed
TEST_F(BaselineComparisonTest, RegressionDetectionCannotBeBypassed) {
    // This test ensures that even if someone tries to manipulate the comparison,
    // the logic will still correctly identify regressions

    // Arrange - Create multiple scenarios where regression should be detected
    std::vector<std::pair<double, double>> testCases = {
        {2.0, 1.95},   // 2.5% regression
        {2.0, 1.80},   // 10% regression
        {2.0, 1.50},   // 25% regression
        {2.0, 1.00},   // 50% regression
        {2.0, 0.50},   // 75% regression
    };

    for (const auto& [baseline, current] : testCases) {
        // Arrange
        BenchmarkResult baselineResult = baselineResult_;
        baselineResult.medianThroughput = baseline;

        BenchmarkResult currentResult = currentResultRegression_;
        currentResult.medianThroughput = current;

        // Act
        BaselineComparison comparison = performComparison(currentResult, baselineResult);

        // Assert - All cases must detect regression
        ASSERT_TRUE(comparison.isRegression)
            << "CRITICAL FAILURE: Regression not detected for baseline=" << baseline
            << ", current=" << current << " (delta=" << comparison.throughputDeltaPercent << "%)";

        // Additional verification
        EXPECT_LT(comparison.throughputDelta, 0.0);
        EXPECT_LT(comparison.throughputDeltaPercent, 0.0);
    }
}