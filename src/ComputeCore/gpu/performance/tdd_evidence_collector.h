#pragma once

#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <json/json.h>

namespace puzzle71::gpu::performance {

/**
 * @brief Test-Driven Development evidence collection and verification system
 *
 * Provides comprehensive tracking of TDD artifacts including:
 * - Unit test coverage metrics
 * - Test execution history and results
 * - Code-test linkage verification
 * - Regression detection and prevention
 * - Test quality assessment and reporting
 */

enum class TestType {
    UNIT,
    INTEGRATION,
    PERFORMANCE,
    ACCURACY,
    REGRESSION,
    EDGE_CASE
};

enum class TestResult {
    PASSED,
    FAILED,
    SKIPPED,
    TIMEOUT,
    CRASHED
};

enum class EvidenceLevel {
    BASIC,      // Basic test coverage
    STANDARD,   // Standard TDD practices
    COMPREHENSIVE, // Comprehensive evidence
    EXHAUSTIVE  // Complete verification
};

struct TestExecutionRecord {
    std::string test_name;
    std::string test_file_path;
    std::string function_under_test;
    TestType test_type;
    TestResult result;
    std::chrono::steady_clock::time_point timestamp;
    std::chrono::milliseconds execution_time;
    std::string error_message;
    std::vector<std::string> test_tags;
    json test_metadata;
    size_t iteration_count;
    double performance_value; // e.g., throughput, latency
    std::string performance_unit;
};

struct CodeCoverageMetric {
    std::string source_file_path;
    size_t total_lines;
    size_t covered_lines;
    size_t total_functions;
    size_t covered_functions;
    size_t total_branches;
    size_t covered_branches;
    double line_coverage_percent;
    double function_coverage_percent;
    double branch_coverage_percent;
    std::vector<std::string> uncovered_lines;
    std::vector<std::string> uncovered_functions;
};

struct TestEvidenceReport {
    std::string component_name;
    std::string version;
    EvidenceLevel evidence_level;
    std::chrono::steady_clock::time_point report_timestamp;

    // Coverage metrics
    CodeCoverageMetric overall_coverage;
    std::vector<CodeCoverageMetric> file_coverage;

    // Test execution statistics
    std::vector<TestExecutionRecord> execution_history;
    size_t total_tests_run;
    size_t passed_tests;
    size_t failed_tests;
    size_t skipped_tests;
    double pass_rate_percent;

    // Quality metrics
    std::chrono::milliseconds avg_test_execution_time;
    std::chrono::milliseconds max_test_execution_time;
    size_t regression_count;
    double code_coverage_percent;
    double test_quality_score;

    // Compliance verification
    bool meets_tdd_requirements;
    bool has_regression_tests;
    bool has_performance_tests;
    bool has_accuracy_tests;
    bool has_edge_case_tests;

    std::vector<std::string> compliance_violations;
    std::vector<std::string> improvement_recommendations;
};

class TddEvidenceCollector {
public:
    explicit TddEvidenceCollector(const std::string& component_name);
    ~TddEvidenceCollector() = default;

    // Test execution tracking
    void RecordTestExecution(const TestExecutionRecord& record);
    void RecordTestStart(const std::string& test_name, const std::string& function_under_test, TestType type);
    void RecordTestResult(const std::string& test_name, TestResult result,
                         const std::string& error_message = "");
    void RecordTestCompletion(const std::string& test_name,
                            std::chrono::milliseconds execution_time,
                            double performance_value = 0.0,
                            const std::string& performance_unit = "");

    // Coverage tracking
    void UpdateCodeCoverage(const std::string& source_file_path,
                           const CodeCoverageMetric& coverage);
    void SetOverallCoverage(const CodeCoverageMetric& coverage);

    // Evidence validation
    bool ValidateTddCompliance(EvidenceLevel required_level = EvidenceLevel::STANDARD) const;
    bool VerifyCodeTestLinkage() const;
    bool CheckForRegressions() const;
    bool ValidateTestQuality() const;

    // Report generation
    TestEvidenceReport GenerateEvidenceReport() const;
    std::string ExportEvidenceReport(const std::string& format = "json") const;
    bool SaveEvidenceReport(const std::string& file_path) const;

    // Configuration
    void SetEvidenceLevel(EvidenceLevel level);
    void SetRequiredCoveragePercent(double coverage_percent);
    void SetComponentVersion(const std::string& version);
    void AddTestTag(const std::string& test_name, const std::string& tag);

    // Analysis and insights
    std::vector<std::string> GetLowCoverageFiles(double threshold_percent = 80.0) const;
    std::vector<std::string> GetSlowRunningTests(std::chrono::milliseconds threshold) const;
    std::vector<std::string> GetFailingTests() const;
    std::vector<std::string> GetRegressionCandidates() const;
    double CalculateTestQualityScore() const;

    // Continuous integration support
    bool UpdateFromTestResults(const std::string& test_output_file);
    bool CompareWithBaseline(const TestEvidenceReport& baseline) const;
    void MarkRegressionTests(const std::vector<std::string>& regression_test_names);

private:
    struct TestSession {
        std::string test_name;
        std::string function_under_test;
        TestType test_type;
        std::chrono::steady_clock::time_point start_time;
        bool is_active;
    };

    std::string component_name_;
    std::string version_;
    EvidenceLevel current_evidence_level_;
    double required_coverage_percent_;

    mutable std::mutex evidence_mutex_;

    // Test execution tracking
    std::vector<TestExecutionRecord> execution_history_;
    std::map<std::string, TestSession> active_sessions_;
    std::map<std::string, std::vector<std::string>> test_tags_;

    // Coverage tracking
    CodeCoverageMetric overall_coverage_;
    std::map<std::string, CodeCoverageMetric> file_coverage_;

    // Regression tracking
    std::set<std::string> regression_test_names_;
    std::map<std::string, TestResult> last_known_results_;

    // Quality metrics cache
    mutable bool quality_metrics_cached_;
    mutable double cached_quality_score_;
    mutable std::chrono::steady_clock::time_point last_cache_update_;

    // Internal methods
    TestSession* FindActiveSession(const std::string& test_name);
    void RemoveActiveSession(const std::string& test_name);
    bool IsRegressionTest(const std::string& test_name) const;
    bool CompareTestResults(TestResult current, TestResult previous) const;
    double CalculateCoverageScore() const;
    double CalculateExecutionConsistency() const;
    void UpdateQualityMetricsCache() const;
    std::string GenerateComplianceReport() const;
    std::vector<std::string> IdentifyMissingTestTypes() const;

    // Serialization helpers
    json TestExecutionRecordToJson(const TestExecutionRecord& record) const;
    json CodeCoverageMetricToJson(const CodeCoverageMetric& metric) const;
    json TestEvidenceReportToJson(const TestEvidenceReport& report) const;

    TestExecutionRecord JsonToTestExecutionRecord(const json& j) const;
    CodeCoverageMetric JsonToCodeCoverageMetric(const json& j) const;
};

/**
 * @brief Scoped test execution tracker for automatic TDD evidence collection
 */
class ScopedTddTestTracker {
public:
    ScopedTddTestTracker(TddEvidenceCollector& collector,
                        const std::string& test_name,
                        const std::string& function_under_test,
                        TestType test_type,
                        const std::vector<std::string>& tags = {});

    ~ScopedTddTestTracker();

    void SetPerformanceMetrics(double value, const std::string& unit);
    void AddMetadata(const json& metadata);

private:
    TddEvidenceCollector& collector_;
    std::string test_name_;
    std::chrono::steady_clock::time_point start_time_;
    json metadata_;
    double performance_value_;
    std::string performance_unit_;
};

/**
 * @brief Factory function to create and configure TDD evidence collector
 */
std::unique_ptr<TddEvidenceCollector> CreateTddEvidenceCollector(
    const std::string& component_name,
    EvidenceLevel evidence_level = EvidenceLevel::STANDARD,
    double required_coverage_percent = 85.0);

} // namespace puzzle71::gpu::performance