#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <ctime>
#include <chrono>
#include <filesystem>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace fs = std::filesystem;

struct VersionInfo {
    std::string name;
    std::string current_version;
    std::string available_version;
    bool update_available;
};

// Helper function to parse semantic versions
std::vector<int> parseVersion(const std::string& version) {
    std::vector<int> parts;
    std::stringstream ss(version);
    std::string part;

    while (std::getline(ss, part, '.')) {
        try {
            parts.push_back(std::stoi(part));
        } catch (...) {
            parts.push_back(0);
        }
    }

    // Ensure we have at least 3 parts (major.minor.patch)
    while (parts.size() < 3) {
        parts.push_back(0);
    }

    return parts;
}

// Helper function to compare versions
int compareVersions(const std::string& v1, const std::string& v2) {
    std::vector<int> parts1 = parseVersion(v1);
    std::vector<int> parts2 = parseVersion(v2);

    for (size_t i = 0; i < 3; ++i) {
        if (parts1[i] < parts2[i]) return -1;
        if (parts1[i] > parts2[i]) return 1;
    }

    return 0;
}

// Helper function to check if version is in range
bool isVersionInRange(const std::string& version, const std::string& min_version, const std::string& max_version) {
    return compareVersions(version, min_version) >= 0 && compareVersions(version, max_version) <= 0;
}

// Helper function to validate compatibility according to matrix rules
bool validateCompatibility(const std::string& dependency, const std::string& version, const fs::path& cache_dir) {
    std::ifstream matrix_file(cache_dir / "compatibility_matrix.json");
    json matrix;
    matrix_file >> matrix;
    matrix_file.close();

    for (const auto& rule : matrix["compatibility_rules"]) {
        if (rule["dependency"] == dependency) {
            auto compat_range = rule["compatible_versions"];
            std::string min_version = compat_range["min_version"];
            std::string max_version = compat_range["max_version"];

            // Check if version is excluded
            for (const auto& excluded : compat_range["excluded_versions"]) {
                if (excluded == version) {
                    return false;
                }
            }

            return isVersionInRange(version, min_version, max_version);
        }
    }

    return false; // Unknown dependency
}

void createMockCompatibilityMatrix(const fs::path& cache_dir) {
    json matrix = {
        {"matrix_version", "1.0"},
        {"generated_timestamp", "2025-10-10T00:00:00Z"},
        {"compatibility_rules", {
            {
                {"dependency", "nlohmann_json"},
                {"compatible_versions", {
                    {"min_version", "3.9.0"},
                    {"max_version", "3.99.99"},
                    {"excluded_versions", json::array()},
                    {"notes", "JSON library with stable API"}
                }}
            },
            {
                {"dependency", "googletest"},
                {"compatible_versions", {
                    {"min_version", "1.10.0"},
                    {"max_version", "1.99.99"},
                    {"excluded_versions", json::array({"1.12.0"})},
                    {"notes", "Testing framework with breaking changes in 1.12.0"}
                }}
            },
            {
                {"dependency", "secp256k1"},
                {"compatible_versions", {
                    {"min_version", "0.1.0"},
                    {"max_version", "0.9.99"},
                    {"excluded_versions", json::array()},
                    {"notes", "Cryptography library - conservative version range"}
                }}
            }
        }},
        {"validation_rules", {
            {"allow_minor_version_upgrades", true},
            {"allow_major_version_upgrades", false},
            {"require_security_patches", true},
            {"check_compile_time_compatibility", true},
            {"check_runtime_compatibility", true}
        }}
    };

    std::ofstream matrix_file(cache_dir / "compatibility_matrix.json");
    matrix_file << matrix.dump(4);
    matrix_file.close();
}

void createMockDependencyManifest(const fs::path& cache_dir) {
    json manifest = {
        {"manifest_version", "1.0"},
        {"generated_timestamp", "2025-10-10T00:00:00Z"},
        {"project", {
            {"name", "Puzzle71Solver"},
            {"version", "0.1.0"},
            {"build_system", "CMake"}
        }},
        {"dependencies", {
            {"cmake_fetch", {
                {"nlohmann_json", {
                    {"current_version", "3.11.3"},
                    {"source_type", "cmake_fetchcontent"},
                    {"available_version", "3.11.4"},
                    {"update_available", true}
                }},
                {"googletest", {
                    {"current_version", "1.14.0"},
                    {"source_type", "cmake_fetchcontent"},
                    {"available_version", "1.15.0"},
                    {"update_available", true}
                }}
            }},
            {"extracted", {
                {"secp256k1-zkp", {
                    {"current_version", "0.3.0"},
                    {"source_type", "extracted_library"},
                    {"available_version", "0.4.0"},
                    {"update_available", true}
                }}
            }}
        }}
    };

    std::ofstream manifest_file(cache_dir / "dependency_manifest.json");
    manifest_file << manifest.dump(4);
    manifest_file.close();
}

int main() {
    // Create test directory structure
    auto test_dir = fs::temp_directory_path() / ("compat_test_" + std::to_string(std::time(nullptr)));
    fs::create_directories(test_dir);
    auto cache_dir = test_dir / ".dependency_cache";
    fs::create_directories(cache_dir);

    // Create mock data
    createMockCompatibilityMatrix(cache_dir);
    createMockDependencyManifest(cache_dir);

    int test_count = 0;
    int passed_count = 0;

    std::cout << "=== Compatibility Validation Tests ===" << std::endl;

    // Test 1: Semantic version parsing
    test_count++;
    {
        auto v1 = parseVersion("1.2.3");
        bool passed = (v1[0] == 1 && v1[1] == 2 && v1[2] == 3);
        if (passed) passed_count++;
        std::cout << "Test " << test_count << ": Semantic version parsing - " << (passed ? "PASS" : "FAIL") << std::endl;
    }

    // Test 2: Version comparison
    test_count++;
    {
        bool passed = (compareVersions("1.2.3", "1.2.3") == 0 &&
                      compareVersions("1.2.3", "1.2.4") == -1 &&
                      compareVersions("1.2.4", "1.2.3") == 1);
        if (passed) passed_count++;
        std::cout << "Test " << test_count << ": Version comparison - " << (passed ? "PASS" : "FAIL") << std::endl;
    }

    // Test 3: Version range validation
    test_count++;
    {
        bool passed = (isVersionInRange("1.2.3", "1.0.0", "2.0.0") &&
                      !isVersionInRange("0.9.9", "1.0.0", "2.0.0") &&
                      isVersionInRange("1.0.0", "1.0.0", "2.0.0"));
        if (passed) passed_count++;
        std::cout << "Test " << test_count << ": Version range validation - " << (passed ? "PASS" : "FAIL") << std::endl;
    }

    // Test 4: Compatibility validation
    test_count++;
    {
        bool passed = (validateCompatibility("nlohmann_json", "3.11.3", cache_dir) &&
                      validateCompatibility("googletest", "1.14.0", cache_dir) &&
                      !validateCompatibility("googletest", "1.12.0", cache_dir)); // Excluded version
        if (passed) passed_count++;
        std::cout << "Test " << test_count << ": Compatibility validation - " << (passed ? "PASS" : "FAIL") << std::endl;
    }

    // Test 5: Matrix file creation
    test_count++;
    {
        bool passed = fs::exists(cache_dir / "compatibility_matrix.json");
        if (passed) passed_count++;
        std::cout << "Test " << test_count << ": Compatibility matrix creation - " << (passed ? "PASS" : "FAIL") << std::endl;
    }

    // Test 6: Performance test
    test_count++;
    {
        auto start = std::chrono::high_resolution_clock::now();

        // Perform 1000 compatibility validations
        for (int i = 0; i < 1000; ++i) {
            validateCompatibility("nlohmann_json", "3.11.3", cache_dir);
            validateCompatibility("googletest", "1.14.0", cache_dir);
            validateCompatibility("secp256k1", "0.3.0", cache_dir);
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        bool passed = (duration.count() < 100); // Should complete in under 100ms
        if (passed) passed_count++;
        std::cout << "Test " << test_count << ": Performance test - " << (passed ? "PASS" : "FAIL")
                  << " (" << duration.count() << "ms)" << std::endl;
    }

    // Test 7: Edge cases
    test_count++;
    {
        auto v1 = parseVersion("");          // Empty string
        auto v2 = parseVersion("invalid");    // Non-numeric
        auto v3 = parseVersion("1.invalid.3"); // Mixed

        bool passed = (v1[0] == 0 && v1[1] == 0 && v1[2] == 0 &&
                      v2[0] == 0 && v2[1] == 0 && v2[2] == 0 &&
                      v3[0] == 1 && v3[1] == 0 && v3[2] == 3);
        if (passed) passed_count++;
        std::cout << "Test " << test_count << ": Edge cases - " << (passed ? "PASS" : "FAIL") << std::endl;
    }

    // Clean up
    fs::remove_all(test_dir);

    std::cout << "\n=== Test Summary ===" << std::endl;
    std::cout << "Total tests: " << test_count << std::endl;
    std::cout << "Passed: " << passed_count << std::endl;
    std::cout << "Failed: " << (test_count - passed_count) << std::endl;
    std::cout << "Success rate: " << (passed_count * 100 / test_count) << "%" << std::endl;

    return (passed_count == test_count) ? 0 : 1;
}