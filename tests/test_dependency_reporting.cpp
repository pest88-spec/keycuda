// Test program for T049: Dependency Reporting and Documentation Generation
// Tests comprehensive reporting capabilities, documentation generation, and output formats

#include <iostream>
#include <filesystem>
#include <vector>
#include <map>
#include <string>
#include <chrono>
#include <cassert>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>
#include "src/integration/dependency_reporter.h"

namespace fs = std::filesystem;
using json = nlohmann::json;
using namespace integration;

// Mock test data and helpers
struct MockDependency {
    std::string name;
    std::string version;
    std::string source_type;
    std::string license;
    std::string status;
    std::map<std::string, std::string> metadata;
    std::vector<std::string> dependencies;
    std::vector<std::string> conflicts_with;
};

std::vector<MockDependency> createMockDependencies() {
    return {
        {
            "nlohmann_json",
            "3.11.3",
            "cmake_fetch",
            "MIT",
            "active",
            {
                {"url", "https://github.com/nlohmann/json"},
                {"description", "JSON for Modern C++"},
                {"sha256", "abc123def456"},
                {"extracted_date", "2024-01-15"},
                {"last_update", "2024-01-20"},
                "compatibility_score", "95"
            },
            {},
            {}
        },
        {
            "googletest",
            "1.14.0",
            "cmake_fetch",
            "BSD-3-Clause",
            "active",
            {
                {"url", "https://github.com/google/googletest"},
                {"description", "Google Testing and Mocking Framework"},
                {"sha256", "def789ghi012"},
                {"extracted_date", "2024-01-10"},
                {"last_update", "2024-01-18"},
                "compatibility_score", "98"
            },
            {"nlohmann_json"},
            {}
        },
        {
            "secp256k1",
            "0.3.0",
            "extracted",
            "Apache-2.0",
            "active",
            {
                {"url", "https://github.com/bitcoin-core/secp256k1"},
                {"description", "Optimized C library for EC operations on curve secp256k1"},
                {"sha256", "ghi345jkl678"},
                {"extracted_date", "2024-01-05"},
                {"last_update", "2024-01-22"},
                "compatibility_score", "92"
            },
            {"openssl"},
            {}
        },
        {
            "openssl",
            "1.1.1k",
            "system",
            "Apache-2.0",
            "active",
            {
                {"url", "https://www.openssl.org/"},
                {"description", "Cryptography and SSL/TLS Toolkit"},
                {"system_path", "/usr/lib/x86_64-linux-gnu/libssl.so"},
                "compatibility_score", "88"
            },
            {},
            {}
        },
        {
            "boost",
            "1.75.0",
            "cmake_fetch",
            "Boost-1.0",
            "outdated",
            {
                {"url", "https://www.boost.org/"},
                {"description", "Free peer-reviewed portable C++ source libraries"},
                {"sha256", "jkl901mno234"},
                {"extracted_date", "2023-12-01"},
                {"last_update", "2024-01-10"},
                "compatibility_score", "76",
                "available_version", "1.82.0"
            },
            {},
            {"1.70.0"}
        }
    };
}

// Test functions
void testDependencyReporterInitialization() {
    std::cout << "Test: Dependency Reporter Initialization" << std::endl;

    auto test_dir = fs::temp_directory_path() / ("report_test_" + std::to_string(std::time(nullptr)));
    fs::create_directories(test_dir);

    DependencyReporter reporter(test_dir.string());

    // Test basic initialization
    assert(reporter.isInitialized());

    fs::remove_all(test_dir);
    std::cout << "PASS: Dependency reporter initialization" << std::endl;
}

void testBasicDependencyReporting() {
    std::cout << "Test: Basic Dependency Reporting" << std::endl;

    auto test_dir = fs::temp_directory_path() / ("report_test_" + std::to_string(std::time(nullptr)));
    fs::create_directories(test_dir);

    DependencyReporter reporter(test_dir.string());

    // Convert mock dependencies to reporter format
    auto mock_deps = createMockDependencies();
    std::vector<DependencyInfo> deps;

    for (const auto& mock_dep : mock_deps) {
        DependencyInfo dep;
        dep.name = mock_dep.name;
        dep.version = mock_dep.version;
        dep.source_type = mock_dep.source_type;
        dep.license = mock_dep.license;
        dep.status = mock_dep.status;
        dep.metadata = mock_dep.metadata;
        dep.dependencies = mock_dep.dependencies;
        dep.conflicts_with = mock_dep.conflicts_with;
        deps.push_back(dep);
    }

    auto report = reporter.generateDependencyReport(deps);

    // Test report content
    assert(!report.report_id.empty());
    assert(!report.generated_timestamp.empty());
    assert(report.total_dependencies == deps.size());
    assert(report.active_dependencies == 4);  // All except boost
    assert(report.outdated_dependencies == 1);  // boost

    fs::remove_all(test_dir);
    std::cout << "PASS: Basic dependency reporting" << std::endl;
}

void testJSONReportGeneration() {
    std::cout << "Test: JSON Report Generation" << std::endl;

    auto test_dir = fs::temp_directory_path() / ("report_test_" + std::to_string(std::time(nullptr)));
    fs::create_directories(test_dir);

    DependencyReporter reporter(test_dir.string());

    auto mock_deps = createMockDependencies();
    std::vector<DependencyInfo> deps;

    for (const auto& mock_dep : mock_deps) {
        DependencyInfo dep;
        dep.name = mock_dep.name;
        dep.version = mock_dep.version;
        dep.source_type = mock_dep.source_type;
        dep.license = mock_dep.license;
        dep.status = mock_dep.status;
        dep.metadata = mock_dep.metadata;
        dep.dependencies = mock_dep.dependencies;
        dep.conflicts_with = mock_dep.conflicts_with;
        deps.push_back(dep);
    }

    auto json_report = reporter.generateJSONReport(deps);

    // Test JSON report structure
    assert(json_report.contains("report_id"));
    assert(json_report.contains("generated_timestamp"));
    assert(json_report.contains("total_dependencies"));
    assert(json_report.contains("active_dependencies"));
    assert(json_report.contains("outdated_dependencies"));
    assert(json_report.contains("dependencies"));
    assert(json_report["dependencies"].size() == deps.size());

    // Test dependency details
    auto first_dep = json_report["dependencies"][0];
    assert(first_dep.contains("name"));
    assert(first_dep.contains("version"));
    assert(first_dep.contains("source_type"));
    assert(first_dep.contains("license"));
    assert(first_dep.contains("status"));

    fs::remove_all(test_dir);
    std::cout << "PASS: JSON report generation" << std::endl;
}

void testMarkdownReportGeneration() {
    std::cout << "Test: Markdown Report Generation" << std::endl;

    auto test_dir = fs::temp_directory_path() / ("report_test_" + std::to_string(std::time(nullptr)));
    fs::create_directories(test_dir);

    DependencyReporter reporter(test_dir.string());

    auto mock_deps = createMockDependencies();
    std::vector<DependencyInfo> deps;

    for (const auto& mock_dep : mock_deps) {
        DependencyInfo dep;
        dep.name = mock_dep.name;
        dep.version = mock_dep.version;
        dep.source_type = mock_dep.source_type;
        dep.license = mock_dep.license;
        dep.status = mock_dep.status;
        dep.metadata = mock_dep.metadata;
        dep.dependencies = mock_dep.dependencies;
        dep.conflicts_with = mock_dep.conflicts_with;
        deps.push_back(dep);
    }

    auto markdown_report = reporter.generateMarkdownReport(deps);

    // Test markdown report content
    assert(!markdown_report.empty());
    assert(markdown_report.find("# Dependency Report") != std::string::npos);
    assert(markdown_report.find("## Summary") != std::string::npos);
    assert(markdown_report.find("## Dependencies") != std::string::npos);
    assert(markdown_report.find("nlohmann_json") != std::string::npos);
    assert(markdown_report.find("googletest") != std::string::npos);
    assert(markdown_report.find("| Name | Version | Status |") != std::string::npos);

    fs::remove_all(test_dir);
    std::cout << "PASS: Markdown report generation" << std::endl;
}

void testHTMLReportGeneration() {
    std::cout << "Test: HTML Report Generation" << std::endl;

    auto test_dir = fs::temp_directory_path() / ("report_test_" + std::to_string(std::time(nullptr)));
    fs::create_directories(test_dir);

    DependencyReporter reporter(test_dir.string());

    auto mock_deps = createMockDependencies();
    std::vector<DependencyInfo> deps;

    for (const auto& mock_dep : mock_deps) {
        DependencyInfo dep;
        dep.name = mock_dep.name;
        dep.version = mock_dep.version;
        dep.source_type = mock_dep.source_type;
        dep.license = mock_dep.license;
        dep.status = mock_dep.status;
        dep.metadata = mock_dep.metadata;
        dep.dependencies = mock_dep.dependencies;
        dep.conflicts_with = mock_dep.conflicts_with;
        deps.push_back(dep);
    }

    auto html_report = reporter.generateHTMLReport(deps);

    // Test HTML report content
    assert(!html_report.empty());
    assert(html_report.find("<!DOCTYPE html>") != std::string::npos);
    assert(html_report.find("<html>") != std::string::npos);
    assert(html_report.find("<head>") != std::string::npos);
    assert(html_report.find("<title>Dependency Report</title>") != std::string::npos);
    assert(html_report.find("<body>") != std::string::npos);
    assert(html_report.find("<h1>Dependency Report</h1>") != std::string::npos);
    assert(html_report.find("<table") != std::string::npos);
    assert(html_report.find("nlohmann_json") != std::string::npos);

    fs::remove_all(test_dir);
    std::cout << "PASS: HTML report generation" << std::endl;
}

void testReportToFileExport() {
    std::cout << "Test: Report to File Export" << std::endl;

    auto test_dir = fs::temp_directory_path() / ("report_test_" + std::to_string(std::time(nullptr)));
    fs::create_directories(test_dir);

    DependencyReporter reporter(test_dir.string());

    auto mock_deps = createMockDependencies();
    std::vector<DependencyInfo> deps;

    for (const auto& mock_dep : mock_deps) {
        DependencyInfo dep;
        dep.name = mock_dep.name;
        dep.version = mock_dep.version;
        dep.source_type = mock_dep.source_type;
        dep.license = mock_dep.license;
        dep.status = mock_dep.status;
        dep.metadata = mock_dep.metadata;
        dep.dependencies = mock_dep.dependencies;
        dep.conflicts_with = mock_dep.conflicts_with;
        deps.push_back(dep);
    }

    // Test JSON export
    std::string json_file = test_dir / "report.json";
    bool json_success = reporter.exportReportToFile(deps, json_file, "json");
    assert(json_success);
    assert(fs::exists(json_file));

    // Test Markdown export
    std::string markdown_file = test_dir / "report.md";
    bool markdown_success = reporter.exportReportToFile(deps, markdown_file, "markdown");
    assert(markdown_success);
    assert(fs::exists(markdown_file));

    // Test HTML export
    std::string html_file = test_dir / "report.html";
    bool html_success = reporter.exportReportToFile(deps, html_file, "html");
    assert(html_success);
    assert(fs::exists(html_file));

    fs::remove_all(test_dir);
    std::cout << "PASS: Report to file export" << std::endl;
}

void testDependencyMetricsReporting() {
    std::cout << "Test: Dependency Metrics Reporting" << std::endl;

    auto test_dir = fs::temp_directory_path() / ("report_test_" + std::to_string(std::time(nullptr)));
    fs::create_directories(test_dir);

    DependencyReporter reporter(test_dir.string());

    auto mock_deps = createMockDependencies();
    std::vector<DependencyInfo> deps;

    for (const auto& mock_dep : mock_deps) {
        DependencyInfo dep;
        dep.name = mock_dep.name;
        dep.version = mock_dep.version;
        dep.source_type = mock_dep.source_type;
        dep.license = mock_dep.dep.license;
        dep.status = mock_dep.status;
        dep.metadata = mock_dep.metadata;
        dep.dependencies = mock_dep.dependencies;
        dep.conflicts_with = mock_dep.conflicts_with;
        deps.push_back(dep);
    }

    auto metrics = reporter.generateDependencyMetrics(deps);

    // Test metrics content
    assert(metrics.total_dependencies == deps.size());
    assert(metrics.active_dependencies == 4);
    assert(metrics.outdated_dependencies == 1);
    assert(metrics.license_types.size() >= 3);  // MIT, BSD-3-Clause, Apache-2.0, Boost-1.0
    assert(metrics.source_types.size() >= 2);   // cmake_fetch, extracted, system
    assert(metrics.average_compatibility_score > 0.0);

    fs::remove_all(test_dir);
    std::cout << "PASS: Dependency metrics reporting" << std::endl;
}

void testDependencyTrendAnalysis() {
    std::cout << "Test: Dependency Trend Analysis" << std::endl;

    auto test_dir = fs::temp_directory_path() / ("report_test_" + std::to_string(std::time(nullptr)));
    fs::create_directories(test_dir);

    DependencyReporter reporter(test_dir.string());

    // Create historical data
    std::vector<DependencySnapshot> snapshots;

    // Snapshot 1 (old)
    DependencySnapshot snapshot1;
    snapshot1.timestamp = "2024-01-01T00:00:00Z";
    snapshot1.total_dependencies = 3;
    snapshot1.active_dependencies = 3;
    snapshot1.outdated_dependencies = 0;
    snapshots.push_back(snapshot1);

    // Snapshot 2 (middle)
    DependencySnapshot snapshot2;
    snapshot2.timestamp = "2024-01-15T00:00:00Z";
    snapshot2.total_dependencies = 4;
    snapshot2.active_dependencies = 4;
    snapshot2.outdated_dependencies = 1;
    snapshots.push_back(snapshot2);

    // Snapshot 3 (current)
    DependencySnapshot snapshot3;
    snapshot3.timestamp = "2024-02-01T00:00:00Z";
    snapshot3.total_dependencies = 5;
    snapshot3.active_dependencies = 4;
    snapshot3.outdated_dependencies = 1;
    snapshots.push_back(snapshot3);

    auto trends = reporter.analyzeDependencyTrends(snapshots);

    // Test trend analysis
    assert(trends.size() >= 2);  // At least 2 trends
    assert(!trends[0].metric_name.empty());
    assert(trends[0].trend_direction == "increasing" ||
           trends[0].trend_direction == "decreasing" ||
           trends[0].trend_direction == "stable");

    fs::remove_all(test_dir);
    std::cout << "PASS: Dependency trend analysis" << std::endl;
}

void testDependencyDocumentationGeneration() {
    std::cout << "Test: Dependency Documentation Generation" << std::endl;

    auto test_dir = fs::temp_directory_path() / ("report_test_" + std::to_string(std::time(nullptr)));
    fs::create_directories(test_dir);

    DependencyReporter reporter(test_dir.string());

    auto mock_deps = createMockDependencies();
    std::vector<DependencyInfo> deps;

    for (const auto& mock_dep : mock_deps) {
        DependencyInfo dep;
        dep.name = mock_dep.name;
        dep.version = mock_dep.version;
        dep.source_type = mock_dep.source_type;
        dep.license = mock_dep.license;
        dep.status = mock_dep.status;
        dep.metadata = mock_dep.metadata;
        dep.dependencies = mock_dep.dependencies;
        dep.conflicts_with = mock_dep.conflicts_with;
        deps.push_back(dep);
    }

    auto documentation = reporter.generateDependencyDocumentation(deps);

    // Test documentation content
    assert(!documentation.content.empty());
    assert(documentation.content.find("# Dependency Documentation") != std::string::npos);
    assert(documentation.content.find("## Overview") != std::string::npos);
    assert(documentation.content.find("## Library Details") != std::string::npos);
    assert(documentation.content.find("## Integration Guide") != std::string::npos);
    assert(documentation.content.find("## Maintenance") != std::string::npos);
    assert(documentation.content.find("nlohmann_json") != std::string::npos);

    fs::remove_all(test_dir);
    std::cout << "PASS: Dependency documentation generation" << std::endl;
}

void testPerformanceBenchmark() {
    std::cout << "Test: Performance Benchmark" << std::endl;

    auto test_dir = fs::temp_directory_path() / ("report_test_" + std::to_string(std::time(nullptr)));
    fs::create_directories(test_dir);

    DependencyReporter reporter(test_dir.string());

    // Create large dependency set for performance testing
    std::vector<DependencyInfo> large_deps;
    for (int i = 0; i < 200; ++i) {
        DependencyInfo dep;
        dep.name = "dep_" + std::to_string(i);
        dep.version = "1.0." + std::to_string(i);
        dep.source_type = "cmake_fetch";
        dep.license = "MIT";
        dep.status = "active";
        dep.metadata["description"] = "Test dependency " + std::to_string(i);
        large_deps.push_back(dep);
    }

    auto start = std::chrono::high_resolution_clock::now();
    auto report = reporter.generateDependencyReport(large_deps);
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // Should complete within reasonable time (under 500ms for 200 dependencies)
    assert(duration.count() < 500);
    assert(report.total_dependencies == 200);

    fs::remove_all(test_dir);
    std::cout << "PASS: Performance benchmark (" << duration.count() << "ms)" << std::endl;
}

void testCustomReportTemplates() {
    std::cout << "Test: Custom Report Templates" << std::endl;

    auto test_dir = fs::temp_directory_path() / ("report_test_" + std::to_string(std::time(nullptr)));
    fs::create_directories(test_dir);

    DependencyReporter reporter(test_dir.string());

    // Create custom template
    std::string custom_template = R"(
# Custom Dependency Report

## Summary
Total: {{total_dependencies}}
Active: {{active_dependencies}}
Outdated: {{outdated_dependencies}}

## Dependency List
{{#dependencies}}
- {{name}} ({{version}}) - {{status}}
{{/dependencies}}
)";

    auto mock_deps = createMockDependencies();
    std::vector<DependencyInfo> deps;

    for (const auto& mock_dep : mock_deps) {
        DependencyInfo dep;
        dep.name = mock_dep.name;
        dep.version = mock_dep.version;
        dep.source_type = mock_dep.source_type;
        dep.license = mock_dep.license;
        dep.status = mock_dep.status;
        dep.metadata = mock_dep.metadata;
        dep.dependencies = mock_dep.dependencies;
        dep.conflicts_with = mock_dep.conflicts_with;
        deps.push_back(dep);
    }

    auto custom_report = reporter.generateCustomReport(deps, custom_template);

    // Test custom template rendering
    assert(!custom_report.empty());
    assert(custom_report.find("Custom Dependency Report") != std::string::npos);
    assert(custom_report.find("Total: 5") != std::string::npos);
    assert(custom_report.find("Active: 4") != std::string::npos);
    assert(custom_report.find("Outdated: 1") != std::string::npos);
    assert(custom_report.find("- nlohmann_json (3.11.3) - active") != std::string::npos);

    fs::remove_all(test_dir);
    std::cout << "PASS: Custom report templates" << std::endl;
}

void testReportFilteringAndSorting() {
    std::cout << "Test: Report Filtering and Sorting" << std::endl;

    auto test_dir = fs::temp_directory_path() / ("report_test_" + std::to_string(std::time(nullptr)));
    fs::create_directories(test_dir);

    DependencyReporter reporter(test_dir.string());

    auto mock_deps = createMockDependencies();
    std::vector<DependencyInfo> deps;

    for (const auto& mock_dep : mock_deps) {
        DependencyInfo dep;
        dep.name = mock_dep.name;
        dep.version = mock_dep.version;
        dep.source_type = mock_dep.source_type;
        dep.license = mock_dep.license;
        dep.status = mock_dep.status;
        dep.metadata = mock_dep.metadata;
        dep.dependencies = mock_dep.dependencies;
        dep.conflicts_with = mock_dep.conflicts_with;
        deps.push_back(dep);
    }

    // Test filtering by status
    auto active_deps = reporter.filterDependencies(deps, "status", "active");
    assert(active_deps.size() == 4);  // All except boost

    auto outdated_deps = reporter.filterDependencies(deps, "status", "outdated");
    assert(outdated_deps.size() == 1);  // Only boost

    // Test sorting
    auto sorted_by_name = reporter.sortDependencies(deps, "name");
    assert(sorted_by_name[0].name == "boost");  // Alphabetical first

    auto sorted_by_version = reporter.sortDependencies(deps, "version");
    // Should be sorted by version semantic order

    fs::remove_all(test_dir);
    std::cout << "PASS: Report filtering and sorting" << std::endl;
}

int main() {
    std::cout << "=== T049 Dependency Reporting and Documentation Generation Tests ===" << std::endl;

    int test_count = 0;
    int passed_count = 0;

    // Run all tests
    std::vector<void(*)()> tests = {
        testDependencyReporterInitialization,
        testBasicDependencyReporting,
        testJSONReportGeneration,
        testMarkdownReportGeneration,
        testHTMLReportGeneration,
        testReportToFileExport,
        testDependencyMetricsReporting,
        testDependencyTrendAnalysis,
        testDependencyDocumentationGeneration,
        testPerformanceBenchmark,
        testCustomReportTemplates,
        testReportFilteringAndSorting
    };

    for (auto test : tests) {
        test_count++;
        try {
            test();
            passed_count++;
        } catch (const std::exception& e) {
            std::cout << "FAIL: Exception - " << e.what() << std::endl;
        }
    }

    std::cout << "\n=== Test Summary ===" << std::endl;
    std::cout << "Total tests: " << test_count << std::endl;
    std::cout << "Passed: " << passed_count << std::endl;
    std::cout << "Failed: " << (test_count - passed_count) << std::endl;
    std::cout << "Success rate: " << (passed_count * 100 / test_count) << "%" << std::endl;

    if (passed_count == test_count) {
        std::cout << "\n✅ T049 Tests: ALL TESTS PASSED!" << std::endl;
        std::cout << "Dependency reporting and documentation generation system is working correctly." << std::endl;
    } else {
        std::cout << "\n❌ T049 Tests: Some tests failed." << std::endl;
        std::cout << "Please review the implementation and fix the failing tests." << std::endl;
    }

    return (passed_count == test_count) ? 0 : 1;
}