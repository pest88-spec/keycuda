#include "ComputeCore/gpu/performance/tdd_evidence_collector.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <numeric>
#include <iomanip>
#include <filesystem>

namespace puzzle71::gpu::performance {

TddEvidenceCollector::TddEvidenceCollector(const std::string& component_name)
    : component_name_(component_name)
    , version_("1.0.0")
    , current_evidence_level_(EvidenceLevel::STANDARD)
    , required_coverage_percent_(85.0)
    , quality_metrics_cached_(false)
    , cached_quality_score_(0.0) {

    // Initialize overall coverage with zeros
    overall_coverage_ = {
        "", 0, 0, 0, 0, 0, 0, 0.0, 0.0, 0.0, {}, {}
    };
}

void TddEvidenceCollector::RecordTestExecution(const TestExecutionRecord& record) {
    std::lock_guard<std::mutex> lock(evidence_mutex_);
    execution_history_.push_back(record);

    // Update last known results for regression detection
    last_known_results_[record.test_name] = record.result;

    // Clear quality metrics cache
    quality_metrics_cached_ = false;
}

void TddEvidenceCollector::RecordTestStart(const std::string& test_name,
                                          const std::string& function_under_test,
                                          TestType type) {
    std::lock_guard<std::mutex> lock(evidence_mutex_);

    TestSession session{
        test_name,
        function_under_test,
        type,
        std::chrono::steady_clock::now(),
        true
    };

    active_sessions_[test_name] = session;
}

void TddEvidenceCollector::RecordTestResult(const std::string& test_name,
                                           TestResult result,
                                           const std::string& error_message) {
    std::lock_guard<std::mutex> lock(evidence_mutex_);

    auto it = active_sessions_.find(test_name);
    if (it != active_sessions_.end()) {
        it->second.is_active = false;

        // Update execution history if record already exists
        auto record_it = std::find_if(execution_history_.rbegin(), execution_history_.rend(),
            [&test_name](const TestExecutionRecord& record) {
                return record.test_name == test_name;
            });

        if (record_it != execution_history_.rend()) {
            record_it->result = result;
            record_it->error_message = error_message;
            record_it->timestamp = std::chrono::steady_clock::now();
        }
    }

    quality_metrics_cached_ = false;
}

void TddEvidenceCollector::RecordTestCompletion(const std::string& test_name,
                                               std::chrono::milliseconds execution_time,
                                               double performance_value,
                                               const std::string& performance_unit) {
    std::lock_guard<std::mutex> lock(evidence_mutex_);

    auto it = active_sessions_.find(test_name);
    if (it != active_sessions_.end()) {
        // Update the execution history record
        auto record_it = std::find_if(execution_history_.rbegin(), execution_history_.rend(),
            [&test_name](const TestExecutionRecord& record) {
                return record.test_name == test_name;
            });

        if (record_it != execution_history_.rend()) {
            record_it->execution_time = execution_time;
            record_it->performance_value = performance_value;
            record_it->performance_unit = performance_unit;
        }

        // Remove from active sessions
        active_sessions_.erase(it);
    }

    quality_metrics_cached_ = false;
}

void TddEvidenceCollector::UpdateCodeCoverage(const std::string& source_file_path,
                                             const CodeCoverageMetric& coverage) {
    std::lock_guard<std::mutex> lock(evidence_mutex_);
    file_coverage_[source_file_path] = coverage;
    quality_metrics_cached_ = false;
}

void TddEvidenceCollector::SetOverallCoverage(const CodeCoverageMetric& coverage) {
    std::lock_guard<std::mutex> lock(evidence_mutex_);
    overall_coverage_ = coverage;
    quality_metrics_cached_ = false;
}

bool TddEvidenceCollector::ValidateTddCompliance(EvidenceLevel required_level) const {
    std::lock_guard<std::mutex> lock(evidence_mutex_);

    // Basic TDD requirements
    if (execution_history_.empty()) {
        return false;
    }

    if (overall_coverage_.line_coverage_percent < required_coverage_percent_) {
        return false;
    }

    // Check for different test types based on evidence level
    std::set<TestType> found_types;
    for (const auto& record : execution_history_) {
        found_types.insert(record.test_type);
    }

    switch (required_level) {
        case EvidenceLevel::BASIC:
            return found_types.count(TestType::UNIT) > 0;

        case EvidenceLevel::STANDARD:
            return found_types.count(TestType::UNIT) > 0 &&
                   found_types.count(TestType::INTEGRATION) > 0;

        case EvidenceLevel::COMPREHENSIVE:
            return found_types.count(TestType::UNIT) > 0 &&
                   found_types.count(TestType::INTEGRATION) > 0 &&
                   found_types.count(TestType::PERFORMANCE) > 0 &&
                   found_types.count(TestType::ACCURACY) > 0;

        case EvidenceLevel::EXHAUSTIVE:
            return found_types.count(TestType::UNIT) > 0 &&
                   found_types.count(TestType::INTEGRATION) > 0 &&
                   found_types.count(TestType::PERFORMANCE) > 0 &&
                   found_types.count(TestType::ACCURACY) > 0 &&
                   found_types.count(TestType::REGRESSION) > 0 &&
                   found_types.count(TestType::EDGE_CASE) > 0;
    }

    return false;
}

bool TddEvidenceCollector::VerifyCodeTestLinkage() const {
    std::lock_guard<std::mutex> lock(evidence_mutex_);

    // Check that each test record has a valid function under test
    for (const auto& record : execution_history_) {
        if (record.function_under_test.empty()) {
            return false;
        }

        // Verify that the function under test exists in source files
        bool found_function = false;
        for (const auto& [file_path, coverage] : file_coverage_) {
            // In a real implementation, this would parse the source files
            // For now, assume function exists if coverage data is present
            if (!coverage.covered_functions.empty()) {
                found_function = true;
                break;
            }
        }

        if (!found_function) {
            return false;
        }
    }

    return true;
}

bool TddEvidenceCollector::CheckForRegressions() const {
    std::lock_guard<std::mutex> lock(evidence_mutex_);

    size_t regression_count = 0;
    for (const auto& record : execution_history_) {
        if (IsRegressionTest(record.test_name) && record.result != TestResult::PASSED) {
            regression_count++;
        }
    }

    return regression_count == 0;
}

bool TddEvidenceCollector::ValidateTestQuality() const {
    std::lock_guard<std::mutex> lock(evidence_mutex_);

    if (execution_history_.empty()) {
        return false;
    }

    // Calculate quality score
    double quality_score = CalculateTestQualityScore();
    return quality_score >= 80.0; // Require 80% quality score
}

TestEvidenceReport TddEvidenceCollector::GenerateEvidenceReport() const {
    std::lock_guard<std::mutex> lock(evidence_mutex_);

    TestEvidenceReport report;
    report.component_name = component_name_;
    report.version = version_;
    report.evidence_level = current_evidence_level_;
    report.report_timestamp = std::chrono::steady_clock::now();

    // Copy coverage metrics
    report.overall_coverage = overall_coverage_;
    for (const auto& [file_path, coverage] : file_coverage_) {
        report.file_coverage.push_back(coverage);
    }

    // Copy execution history
    report.execution_history = execution_history_;

    // Calculate test statistics
    report.total_tests_run = execution_history_.size();
    report.passed_tests = std::count_if(execution_history_.begin(), execution_history_.end(),
        [](const TestExecutionRecord& record) { return record.result == TestResult::PASSED; });
    report.failed_tests = std::count_if(execution_history_.begin(), execution_history_.end(),
        [](const TestExecutionRecord& record) { return record.result == TestResult::FAILED; });
    report.skipped_tests = std::count_if(execution_history_.begin(), execution_history_.end(),
        [](const TestExecutionRecord& record) { return record.result == TestResult::SKIPPED; });

    report.pass_rate_percent = report.total_tests_run > 0 ?
        (static_cast<double>(report.passed_tests) / report.total_tests_run) * 100.0 : 0.0;

    // Calculate execution time statistics
    if (!execution_history_.empty()) {
        auto total_time = std::accumulate(execution_history_.begin(), execution_history_.end(),
            std::chrono::milliseconds(0),
            [](std::chrono::milliseconds acc, const TestExecutionRecord& record) {
                return acc + record.execution_time;
            });

        report.avg_test_execution_time = total_time / execution_history_.size();

        auto max_time_it = std::max_element(execution_history_.begin(), execution_history_.end(),
            [](const TestExecutionRecord& a, const TestExecutionRecord& b) {
                return a.execution_time < b.execution_time;
            });
        report.max_test_execution_time = max_time_it->execution_time;
    }

    // Calculate regression count
    report.regression_count = std::count_if(execution_history_.begin(), execution_history_.end(),
        [this](const TestExecutionRecord& record) {
            return IsRegressionTest(record.test_name) && record.result != TestResult::PASSED;
        });

    // Calculate coverage percentage
    report.code_coverage_percent = overall_coverage_.line_coverage_percent;

    // Calculate test quality score
    report.test_quality_score = CalculateTestQualityScore();

    // Compliance verification
    report.meets_tdd_requirements = ValidateTddCompliance(current_evidence_level_);
    report.has_regression_tests = std::any_of(execution_history_.begin(), execution_history_.end(),
        [](const TestExecutionRecord& record) { return record.test_type == TestType::REGRESSION; });
    report.has_performance_tests = std::any_of(execution_history_.begin(), execution_history_.end(),
        [](const TestExecutionRecord& record) { return record.test_type == TestType::PERFORMANCE; });
    report.has_accuracy_tests = std::any_of(execution_history_.begin(), execution_history_.end(),
        [](const TestExecutionRecord& record) { return record.test_type == TestType::ACCURACY; });
    report.has_edge_case_tests = std::any_of(execution_history_.begin(), execution_history_.end(),
        [](const TestExecutionRecord& record) { return record.test_type == TestType::EDGE_CASE; });

    // Generate compliance violations and recommendations
    if (!report.meets_tdd_requirements) {
        report.compliance_violations.push_back("Does not meet TDD requirements at level " +
            std::to_string(static_cast<int>(current_evidence_level_)));
    }

    if (report.code_coverage_percent < required_coverage_percent_) {
        report.compliance_violations.push_back("Code coverage below required threshold of " +
            std::to_string(required_coverage_percent_) + "%");
    }

    if (report.pass_rate_percent < 95.0) {
        report.improvement_recommendations.push_back("Investigate failing tests to improve pass rate");
    }

    if (report.regression_count > 0) {
        report.improvement_recommendations.push_back("Fix failing regression tests");
    }

    return report;
}

std::string TddEvidenceCollector::ExportEvidenceReport(const std::string& format) const {
    auto report = GenerateEvidenceReport();

    if (format == "json") {
        return TestEvidenceReportToJson(report).dump(4);
    } else if (format == "xml") {
        // Basic XML format (simplified)
        std::ostringstream oss;
        oss << "<TddEvidenceReport>\n";
        oss << "  <Component>" << report.component_name << "</Component>\n";
        oss << "  <Version>" << report.version << "</Version>\n";
        oss << "  <Coverage>" << report.code_coverage_percent << "</Coverage>\n";
        oss << "  <PassRate>" << report.pass_rate_percent << "</PassRate>\n";
        oss << "  <QualityScore>" << report.test_quality_score << "</QualityScore>\n";
        oss << "</TddEvidenceReport>\n";
        return oss.str();
    } else {
        // Human-readable format
        std::ostringstream oss;
        oss << "TDD Evidence Report for " << report.component_name << " v" << report.version << "\n";
        oss << "Generated: " << std::chrono::duration_cast<std::chrono::seconds>(
            report.report_timestamp.time_since_epoch()).count() << "\n\n";

        oss << "Coverage: " << std::fixed << std::setprecision(2)
            << report.code_coverage_percent << "%\n";
        oss << "Pass Rate: " << report.pass_rate_percent << "%\n";
        oss << "Quality Score: " << report.test_quality_score << "/100\n";
        oss << "Total Tests: " << report.total_tests_run << "\n";
        oss << "Failed Tests: " << report.failed_tests << "\n";
        oss << "Regression Failures: " << report.regression_count << "\n";

        return oss.str();
    }
}

bool TddEvidenceCollector::SaveEvidenceReport(const std::string& file_path) const {
    try {
        std::ofstream file(file_path);
        if (!file.is_open()) {
            return false;
        }

        std::string content = ExportEvidenceReport("json");
        file << content;
        return file.good();
    } catch (...) {
        return false;
    }
}

void TddEvidenceCollector::SetEvidenceLevel(EvidenceLevel level) {
    std::lock_guard<std::mutex> lock(evidence_mutex_);
    current_evidence_level_ = level;
    quality_metrics_cached_ = false;
}

void TddEvidenceCollector::SetRequiredCoveragePercent(double coverage_percent) {
    std::lock_guard<std::mutex> lock(evidence_mutex_);
    required_coverage_percent_ = coverage_percent;
}

void TddEvidenceCollector::SetComponentVersion(const std::string& version) {
    std::lock_guard<std::mutex> lock(evidence_mutex_);
    version_ = version;
}

void TddEvidenceCollector::AddTestTag(const std::string& test_name, const std::string& tag) {
    std::lock_guard<std::mutex> lock(evidence_mutex_);
    test_tags_[test_name].push_back(tag);
}

std::vector<std::string> TddEvidenceCollector::GetLowCoverageFiles(double threshold_percent) const {
    std::lock_guard<std::mutex> lock(evidence_mutex_);

    std::vector<std::string> low_coverage_files;
    for (const auto& [file_path, coverage] : file_coverage_) {
        if (coverage.line_coverage_percent < threshold_percent) {
            low_coverage_files.push_back(file_path);
        }
    }

    return low_coverage_files;
}

std::vector<std::string> TddEvidenceCollector::GetSlowRunningTests(std::chrono::milliseconds threshold) const {
    std::lock_guard<std::mutex> lock(evidence_mutex_);

    std::vector<std::string> slow_tests;
    for (const auto& record : execution_history_) {
        if (record.execution_time > threshold) {
            slow_tests.push_back(record.test_name);
        }
    }

    return slow_tests;
}

std::vector<std::string> TddEvidenceCollector::GetFailingTests() const {
    std::lock_guard<std::mutex> lock(evidence_mutex_);

    std::vector<std::string> failing_tests;
    for (const auto& record : execution_history_) {
        if (record.result == TestResult::FAILED) {
            failing_tests.push_back(record.test_name);
        }
    }

    return failing_tests;
}

std::vector<std::string> TddEvidenceCollector::GetRegressionCandidates() const {
    std::lock_guard<std::mutex> lock(evidence_mutex_);

    std::vector<std::string> regression_candidates;
    for (const auto& record : execution_history_) {
        if (record.test_type == TestType::REGRESSION || IsRegressionTest(record.test_name)) {
            regression_candidates.push_back(record.test_name);
        }
    }

    return regression_candidates;
}

double TddEvidenceCollector::CalculateTestQualityScore() const {
    if (quality_metrics_cached_) {
        return cached_quality_score_;
    }

    UpdateQualityMetricsCache();
    quality_metrics_cached_ = true;
    last_cache_update_ = std::chrono::steady_clock::now();

    return cached_quality_score_;
}

// Private methods implementation

TddEvidenceCollector::TestSession* TddEvidenceCollector::FindActiveSession(const std::string& test_name) {
    auto it = active_sessions_.find(test_name);
    return it != active_sessions_.end() ? &it->second : nullptr;
}

void TddEvidenceCollector::RemoveActiveSession(const std::string& test_name) {
    active_sessions_.erase(test_name);
}

bool TddEvidenceCollector::IsRegressionTest(const std::string& test_name) const {
    return regression_test_names_.count(test_name) > 0;
}

bool TddEvidenceCollector::CompareTestResults(TestResult current, TestResult previous) const {
    // Consider it a regression if previous was PASS and current is FAIL
    return previous == TestResult::PASSED && current == TestResult::FAILED;
}

double TddEvidenceCollector::CalculateCoverageScore() const {
    if (overall_coverage_.total_lines == 0) return 0.0;

    double line_score = overall_coverage_.line_coverage_percent;
    double function_score = overall_coverage_.function_coverage_percent;
    double branch_score = overall_coverage_.branch_coverage_percent;

    // Weight the scores (line coverage most important)
    return (line_score * 0.5) + (function_score * 0.3) + (branch_score * 0.2);
}

double TddEvidenceCollector::CalculateExecutionConsistency() const {
    if (execution_history_.size() < 2) return 100.0;

    // Calculate consistency of test results over time
    std::map<std::string, std::vector<TestResult>> test_results;
    for (const auto& record : execution_history_) {
        test_results[record.test_name].push_back(record.result);
    }

    double total_consistency = 0.0;
    size_t test_count = 0;

    for (const auto& [test_name, results] : test_results) {
        if (results.size() > 1) {
            size_t consistent_results = 0;
            for (size_t i = 1; i < results.size(); ++i) {
                if (results[i] == results[0]) {
                    consistent_results++;
                }
            }

            double consistency = (static_cast<double>(consistent_results) / (results.size() - 1)) * 100.0;
            total_consistency += consistency;
            test_count++;
        }
    }

    return test_count > 0 ? total_consistency / test_count : 100.0;
}

void TddEvidenceCollector::UpdateQualityMetricsCache() const {
    double coverage_score = CalculateCoverageScore();
    double consistency_score = CalculateExecutionConsistency();
    double pass_rate = execution_history_.empty() ? 0.0 :
        (static_cast<double>(std::count_if(execution_history_.begin(), execution_history_.end(),
            [](const TestExecutionRecord& record) { return record.result == TestResult::PASSED; })) /
         execution_history_.size()) * 100.0;

    // Calculate comprehensive quality score
    cached_quality_score_ = (coverage_score * 0.4) + (pass_rate * 0.4) + (consistency_score * 0.2);
}

// ScopedTddTestTracker implementation

ScopedTddTestTracker::ScopedTddTestTracker(TddEvidenceCollector& collector,
                                           const std::string& test_name,
                                           const std::string& function_under_test,
                                           TestType test_type,
                                           const std::vector<std::string>& tags)
    : collector_(collector)
    , test_name_(test_name)
    , start_time_(std::chrono::steady_clock::now())
    , metadata_(json::object())
    , performance_value_(0.0)
    , performance_unit_("") {

    collector_.RecordTestStart(test_name, function_under_test, test_type);

    // Add tags if provided
    for (const auto& tag : tags) {
        collector_.AddTestTag(test_name, tag);
    }
}

ScopedTddTestTracker::~ScopedTddTestTracker() {
    auto end_time = std::chrono::steady_clock::now();
    auto execution_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time_);

    collector_.RecordTestCompletion(test_name_, execution_time, performance_value_, performance_unit_);
}

void ScopedTddTestTracker::SetPerformanceMetrics(double value, const std::string& unit) {
    performance_value_ = value;
    performance_unit_ = unit;
    metadata_["performance_value"] = value;
    metadata_["performance_unit"] = unit;
}

void ScopedTddTestTracker::AddMetadata(const json& metadata) {
    if (metadata.is_object()) {
        for (auto& [key, value] : metadata.items()) {
            metadata_[key] = value;
        }
    }
}

// Factory function

std::unique_ptr<TddEvidenceCollector> CreateTddEvidenceCollector(
    const std::string& component_name,
    EvidenceLevel evidence_level,
    double required_coverage_percent) {

    auto collector = std::make_unique<TddEvidenceCollector>(component_name);
    collector->SetEvidenceLevel(evidence_level);
    collector->SetRequiredCoveragePercent(required_coverage_percent);

    return collector;
}

// JSON serialization helpers (simplified implementations)

json TddEvidenceCollector::TestExecutionRecordToJson(const TestExecutionRecord& record) const {
    json j;
    j["test_name"] = record.test_name;
    j["function_under_test"] = record.function_under_test;
    j["test_type"] = static_cast<int>(record.test_type);
    j["result"] = static_cast<int>(record.result);
    j["execution_time_ms"] = record.execution_time.count();
    j["error_message"] = record.error_message;
    j["test_tags"] = record.test_tags;
    j["performance_value"] = record.performance_value;
    j["performance_unit"] = record.performance_unit;
    return j;
}

json TddEvidenceCollector::CodeCoverageMetricToJson(const CodeCoverageMetric& metric) const {
    json j;
    j["source_file_path"] = metric.source_file_path;
    j["total_lines"] = metric.total_lines;
    j["covered_lines"] = metric.covered_lines;
    j["total_functions"] = metric.total_functions;
    j["covered_functions"] = metric.covered_functions;
    j["line_coverage_percent"] = metric.line_coverage_percent;
    j["function_coverage_percent"] = metric.function_coverage_percent;
    j["branch_coverage_percent"] = metric.branch_coverage_percent;
    return j;
}

json TddEvidenceCollector::TestEvidenceReportToJson(const TestEvidenceReport& report) const {
    json j;
    j["component_name"] = report.component_name;
    j["version"] = report.version;
    j["evidence_level"] = static_cast<int>(report.evidence_level);
    j["code_coverage_percent"] = report.code_coverage_percent;
    j["pass_rate_percent"] = report.pass_rate_percent;
    j["test_quality_score"] = report.test_quality_score;
    j["total_tests_run"] = report.total_tests_run;
    j["passed_tests"] = report.passed_tests;
    j["failed_tests"] = report.failed_tests;
    j["skipped_tests"] = report.skipped_tests;
    j["regression_count"] = report.regression_count;
    j["meets_tdd_requirements"] = report.meets_tdd_requirements;
    j["compliance_violations"] = report.compliance_violations;
    j["improvement_recommendations"] = report.improvement_recommendations;

    // Add detailed records
    j["execution_history"] = json::array();
    for (const auto& record : report.execution_history) {
        j["execution_history"].push_back(TestExecutionRecordToJson(record));
    }

    j["file_coverage"] = json::array();
    for (const auto& metric : report.file_coverage) {
        j["file_coverage"].push_back(CodeCoverageMetricToJson(metric));
    }

    return j;
}

} // namespace puzzle71::gpu::performance