// Test program for T047: Compatibility Validation Between Library Versions
// Validates the implementation of the CompatibilityValidator class

#include <iostream>
#include <filesystem>
#include <vector>
#include "src/integration/compatibility_validator.h"

namespace fs = std::filesystem;
using namespace integration;

int main() {
    std::cout << "=== Compatibility Validator Implementation Test ===" << std::endl;

    // Create test directory
    auto test_dir = fs::temp_directory_path() / ("compat_validator_test_" + std::to_string(std::time(nullptr)));
    fs::create_directories(test_dir);
    std::string cache_dir = test_dir.string();

    int test_count = 0;
    int passed_count = 0;

    // Test 1: Constructor and initialization
    test_count++;
    {
        CompatibilityValidator validator(cache_dir);
        bool passed = true;
        if (passed) passed_count++;
        std::cout << "Test " << test_count << ": Constructor and initialization - " << (passed ? "PASS" : "FAIL") << std::endl;
    }

    // Test 2: Create default compatibility matrix
    test_count++;
    {
        CompatibilityValidator validator(cache_dir);
        bool passed = validator.createDefaultCompatibilityMatrix();
        if (passed) passed_count++;
        std::cout << "Test " << test_count << ": Create default compatibility matrix - " << (passed ? "PASS" : "FAIL") << std::endl;
    }

    // Test 3: Load compatibility matrix
    test_count++;
    {
        CompatibilityValidator validator(cache_dir);
        validator.createDefaultCompatibilityMatrix();
        bool passed = validator.loadCompatibilityMatrix();
        if (passed) passed_count++;
        std::cout << "Test " << test_count << ": Load compatibility matrix - " << (passed ? "PASS" : "FAIL") << std::endl;
    }

    // Test 4: Semantic version parsing
    test_count++;
    {
        CompatibilityValidator validator(cache_dir);
        validator.createDefaultCompatibilityMatrix();
        validator.loadCompatibilityMatrix();

        auto v1 = validator.parseVersion("1.2.3");
        auto v2 = validator.parseVersion("1.2");
        auto v3 = validator.parseVersion("invalid");

        bool passed = (v1[0] == 1 && v1[1] == 2 && v1[2] == 3 &&
                      v2[0] == 1 && v2[1] == 2 && v2[2] == 0 &&
                      v3[0] == 0 && v3[1] == 0 && v3[2] == 0);
        if (passed) passed_count++;
        std::cout << "Test " << test_count << ": Semantic version parsing - " << (passed ? "PASS" : "FAIL") << std::endl;
    }

    // Test 5: Version comparison
    test_count++;
    {
        CompatibilityValidator validator(cache_dir);
        validator.createDefaultCompatibilityMatrix();
        validator.loadCompatibilityMatrix();

        bool passed = (validator.compareVersions("1.2.3", "1.2.3") == 0 &&
                      validator.compareVersions("1.2.3", "1.2.4") == -1 &&
                      validator.compareVersions("1.2.4", "1.2.3") == 1 &&
                      validator.compareVersions("2.0.0", "1.9.9") == 1);
        if (passed) passed_count++;
        std::cout << "Test " << test_count << ": Version comparison - " << (passed ? "PASS" : "FAIL") << std::endl;
    }

    // Test 6: Version range validation
    test_count++;
    {
        CompatibilityValidator validator(cache_dir);
        validator.createDefaultCompatibilityMatrix();
        validator.loadCompatibilityMatrix();

        bool passed = (validator.isVersionInRange("1.2.3", "1.0.0", "2.0.0") &&
                      validator.isVersionInRange("1.0.0", "1.0.0", "2.0.0") &&
                      validator.isVersionInRange("2.0.0", "1.0.0", "2.0.0") &&
                      !validator.isVersionInRange("0.9.9", "1.0.0", "2.0.0") &&
                      !validator.isVersionInRange("2.0.1", "1.0.0", "2.0.0"));
        if (passed) passed_count++;
        std::cout << "Test " << test_count << ": Version range validation - " << (passed ? "PASS" : "FAIL") << std::endl;
    }

    // Test 7: Basic compatibility validation
    test_count++;
    {
        CompatibilityValidator validator(cache_dir);
        validator.createDefaultCompatibilityMatrix();
        validator.loadCompatibilityMatrix();

        bool passed = (validator.validateCompatibility("nlohmann_json", "3.11.3") &&
                      validator.validateCompatibility("googletest", "1.14.0") &&
                      validator.validateCompatibility("secp256k1", "0.3.0") &&
                      !validator.validateCompatibility("googletest", "1.12.0")); // Excluded version
        if (passed) passed_count++;
        std::cout << "Test " << test_count << ": Basic compatibility validation - " << (passed ? "PASS" : "FAIL") << std::endl;
    }

    // Test 8: Compatibility details
    test_count++;
    {
        CompatibilityValidator validator(cache_dir);
        validator.createDefaultCompatibilityMatrix();
        validator.loadCompatibilityMatrix();

        auto details = validator.getCompatibilityDetails("nlohmann_json", "3.11.3");
        bool passed = (details.dependency_name == "nlohmann_json" &&
                      details.version == "3.11.3" &&
                      details.is_compatible &&
                      details.min_version == "3.9.0" &&
                      details.max_version == "3.99.99");
        if (passed) passed_count++;
        std::cout << "Test " << test_count << ": Compatibility details - " << (passed ? "PASS" : "FAIL") << std::endl;
    }

    // Test 9: Proposed updates validation
    test_count++;
    {
        CompatibilityValidator validator(cache_dir);
        validator.createDefaultCompatibilityMatrix();
        validator.loadCompatibilityMatrix();

        std::vector<DependencyUpdate> updates = {
            {"nlohmann_json", "3.11.3", "3.11.4", "cmake_fetchcontent"},
            {"googletest", "1.14.0", "1.15.0", "cmake_fetchcontent"},
            {"secp256k1", "0.3.0", "0.4.0", "extracted_library"}
        };

        auto report = validator.validateProposedUpdates(updates);
        bool passed = (report.total_dependencies == 3 &&
                      report.compatible_dependencies >= 2 && // At least 2 should be compatible
                      report.dependency_results.size() == 3);
        if (passed) passed_count++;
        std::cout << "Test " << test_count << ": Proposed updates validation - " << (passed ? "PASS" : "FAIL") << std::endl;
    }

    // Test 10: Security update detection
    test_count++;
    {
        CompatibilityValidator validator(cache_dir);
        validator.createDefaultCompatibilityMatrix();
        validator.loadCompatibilityMatrix();

        bool passed = (validator.isSecurityUpdate("nlohmann_json", "3.11.3", "3.11.4") &&
                      !validator.isSecurityUpdate("nlohmann_json", "3.11.3", "3.12.0") &&
                      !validator.isSecurityUpdate("nlohmann_json", "3.11.3", "4.0.0"));
        if (passed) passed_count++;
        std::cout << "Test " << test_count << ": Security update detection - " << (passed ? "PASS" : "FAIL") << std::endl;
    }

    // Test 11: Major version upgrade detection
    test_count++;
    {
        CompatibilityValidator validator(cache_dir);
        validator.createDefaultCompatibilityMatrix();
        validator.loadCompatibilityMatrix();

        bool passed = (validator.isMajorVersionUpgrade("nlohmann_json", "3.11.3", "4.0.0") &&
                      !validator.isMajorVersionUpgrade("nlohmann_json", "3.11.3", "3.12.0") &&
                      !validator.isMajorVersionUpgrade("nlohmann_json", "3.11.3", "3.11.4"));
        if (passed) passed_count++;
        std::cout << "Test " << test_count << ": Major version upgrade detection - " << (passed ? "PASS" : "FAIL") << std::endl;
    }

    // Test 12: Cross-dependency compatibility
    test_count++;
    {
        CompatibilityValidator validator(cache_dir);
        validator.createDefaultCompatibilityMatrix();
        validator.loadCompatibilityMatrix();

        std::vector<std::pair<std::string, std::string>> dependencies = {
            {"nlohmann_json", "3.11.3"},
            {"googletest", "1.14.0"}
        };

        bool passed = validator.validateCrossDependencyCompatibility(dependencies);
        if (passed) passed_count++;
        std::cout << "Test " << test_count << ": Cross-dependency compatibility - " << (passed ? "PASS" : "FAIL") << std::endl;
    }

    // Test 13: Compatibility report generation
    test_count++;
    {
        CompatibilityValidator validator(cache_dir);
        validator.createDefaultCompatibilityMatrix();
        validator.loadCompatibilityMatrix();

        auto report = validator.generateCompatibilityReport();
        bool passed = (report.contains("matrix_version") &&
                      report.contains("compatibility_rules") &&
                      report.contains("validation_rules") &&
                      report["summary"]["total_dependencies"] > 0);
        if (passed) passed_count++;
        std::cout << "Test " << test_count << ": Compatibility report generation - " << (passed ? "PASS" : "FAIL") << std::endl;
    }

    // Test 14: Export validation report
    test_count++;
    {
        CompatibilityValidator validator(cache_dir);
        validator.createDefaultCompatibilityMatrix();
        validator.loadCompatibilityMatrix();

        std::vector<DependencyUpdate> updates = {
            {"nlohmann_json", "3.11.3", "3.11.4", "cmake_fetchcontent"}
        };

        auto report = validator.validateProposedUpdates(updates);
        std::string report_file = test_dir / "validation_report.json";
        bool passed = validator.exportValidationReport(report, report_file) &&
                      fs::exists(report_file);
        if (passed) passed_count++;
        std::cout << "Test " << test_count << ": Export validation report - " << (passed ? "PASS" : "FAIL") << std::endl;
    }

    // Test 15: Performance test
    test_count++;
    {
        CompatibilityValidator validator(cache_dir);
        validator.createDefaultCompatibilityMatrix();
        validator.loadCompatibilityMatrix();

        auto start = std::chrono::high_resolution_clock::now();

        // Perform 1000 compatibility validations
        for (int i = 0; i < 1000; ++i) {
            validator.validateCompatibility("nlohmann_json", "3.11.3");
            validator.validateCompatibility("googletest", "1.14.0");
            validator.validateCompatibility("secp256k1", "0.3.0");
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        bool passed = (duration.count() < 200); // Should complete in under 200ms
        if (passed) passed_count++;
        std::cout << "Test " << test_count << ": Performance test - " << (passed ? "PASS" : "FAIL")
                  << " (" << duration.count() << "ms)" << std::endl;
    }

    // Clean up
    fs::remove_all(test_dir);

    std::cout << "\n=== Test Summary ===" << std::endl;
    std::cout << "Total tests: " << test_count << std::endl;
    std::cout << "Passed: " << passed_count << std::endl;
    std::cout << "Failed: " << (test_count - passed_count) << std::endl;
    std::cout << "Success rate: " << (passed_count * 100 / test_count) << "%" << std::endl;

    if (passed_count == test_count) {
        std::cout << "\n✅ T047 Implementation: ALL TESTS PASSED!" << std::endl;
        std::cout << "Compatibility validation between library versions is working correctly." << std::endl;
    } else {
        std::cout << "\n❌ T047 Implementation: Some tests failed." << std::endl;
        std::cout << "Please review the implementation and fix the failing tests." << std::endl;
    }

    return (passed_count == test_count) ? 0 : 1;
}