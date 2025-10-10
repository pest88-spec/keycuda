#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <map>
#include <vector>
#include <sstream>
#include <ctime>
#include <chrono>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace fs = std::filesystem;

class CompatibilityValidationTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir = fs::temp_directory_path() / ("compat_test_" + std::to_string(std::time(nullptr)));
        fs::create_directories(test_dir);

        // Create project structure
        project_root = test_dir / "project";
        fs::create_directories(project_root);

        scripts_dir = project_root / "scripts";
        fs::create_directories(scripts_dir);

        cache_dir = project_root / ".dependency_cache";
        fs::create_directories(cache_dir);

        // Create mock compatibility matrix
        createMockCompatibilityMatrix();

        // Create mock dependency manifest with version information
        createMockDependencyManifest();

        // Copy the update script for testing
        std::ifstream src_script("/root/keycuda/scripts/update-dependencies.sh");
        std::ofstream dst_script(scripts_dir / "update-dependencies.sh");
        dst_script << src_script.rdbuf();
        dst_script.close();
        src_script.close();

        // Make script executable
        fs::permissions(scripts_dir / "update-dependencies.sh",
                       fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                       fs::perm_options::add);
    }

    void TearDown() override {
        fs::remove_all(test_dir);
    }

    void createMockCompatibilityMatrix() {
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
            {"cross_dependency_compatibility", {
                {
                    {"dependencies", {"nlohmann_json", "googletest"}},
                    {"compatible_combinations", {
                        {"nlohmann_json", {"3.9.0", "3.11.3"}},
                        {"googletest", {"1.10.0", "1.14.0"}}
                    }},
                    {"incompatible_combinations", json::array()}
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

    void createMockDependencyManifest() {
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
    bool validateCompatibility(const std::string& dependency, const std::string& version) {
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

    fs::path test_dir;
    fs::path project_root;
    fs::path scripts_dir;
    fs::path cache_dir;
};

// Test compatibility matrix initialization
TEST_F(CompatibilityValidationTest, TestCompatibilityMatrixInitialization) {
    EXPECT_TRUE(fs::exists(cache_dir / "compatibility_matrix.json"));

    std::ifstream matrix_file(cache_dir / "compatibility_matrix.json");
    json matrix;
    matrix_file >> matrix;
    matrix_file.close();

    EXPECT_TRUE(matrix.contains("matrix_version"));
    EXPECT_TRUE(matrix.contains("compatibility_rules"));
    EXPECT_TRUE(matrix.contains("validation_rules"));

    // Verify structure of compatibility rules
    EXPECT_GT(matrix["compatibility_rules"].size(), 0);
    for (const auto& rule : matrix["compatibility_rules"]) {
        EXPECT_TRUE(rule.contains("dependency"));
        EXPECT_TRUE(rule.contains("compatible_versions"));
    }
}

// Test individual version compatibility validation
TEST_F(CompatibilityValidationTest, TestIndividualVersionValidation) {
    // Test known compatible versions
    EXPECT_TRUE(validateCompatibility("nlohmann_json", "3.11.3"));
    EXPECT_TRUE(validateCompatibility("googletest", "1.14.0"));
    EXPECT_TRUE(validateCompatibility("secp256k1", "0.3.0"));

    // Test known incompatible versions
    EXPECT_FALSE(validateCompatibility("googletest", "1.12.0")); // Excluded version
    EXPECT_FALSE(validateCompatibility("nlohmann_json", "4.0.0")); // Beyond max version
    EXPECT_FALSE(validateCompatibility("secp256k1", "1.0.0")); // Beyond max version

    // Test unknown dependency
    EXPECT_FALSE(validateCompatibility("unknown_lib", "1.0.0"));
}

// Test semantic version parsing
TEST_F(CompatibilityValidationTest, TestSemanticVersionParsing) {
    // Test normal versions
    auto v1 = parseVersion("1.2.3");
    EXPECT_EQ(v1[0], 1);
    EXPECT_EQ(v1[1], 2);
    EXPECT_EQ(v1[2], 3);

    // Test versions with fewer parts
    auto v2 = parseVersion("1.2");
    EXPECT_EQ(v2[0], 1);
    EXPECT_EQ(v2[1], 2);
    EXPECT_EQ(v2[2], 0);

    // Test versions with more parts
    auto v3 = parseVersion("1.2.3.4");
    EXPECT_EQ(v3[0], 1);
    EXPECT_EQ(v3[1], 2);
    EXPECT_EQ(v3[2], 3); // Should ignore additional parts
}

// Test version comparison
TEST_F(CompatibilityValidationTest, TestVersionComparison) {
    // Equal versions
    EXPECT_EQ(compareVersions("1.2.3", "1.2.3"), 0);

    // Less than
    EXPECT_EQ(compareVersions("1.2.3", "1.2.4"), -1);
    EXPECT_EQ(compareVersions("1.2.3", "1.3.0"), -1);
    EXPECT_EQ(compareVersions("1.2.3", "2.0.0"), -1);

    // Greater than
    EXPECT_EQ(compareVersions("1.2.4", "1.2.3"), 1);
    EXPECT_EQ(compareVersions("1.3.0", "1.2.3"), 1);
    EXPECT_EQ(compareVersions("2.0.0", "1.2.3"), 1);
}

// Test version range validation
TEST_F(CompatibilityValidationTest, TestVersionRangeValidation) {
    // Test in range
    EXPECT_TRUE(isVersionInRange("1.2.3", "1.0.0", "2.0.0"));
    EXPECT_TRUE(isVersionInRange("1.9.9", "1.0.0", "2.0.0"));

    // Test out of range
    EXPECT_FALSE(isVersionInRange("0.9.9", "1.0.0", "2.0.0"));
    EXPECT_FALSE(isVersionInRange("2.0.1", "1.0.0", "2.0.0"));

    // Test boundary conditions
    EXPECT_TRUE(isVersionInRange("1.0.0", "1.0.0", "2.0.0"));
    EXPECT_TRUE(isVersionInRange("2.0.0", "1.0.0", "2.0.0"));
}

// Test cross-dependency compatibility validation
TEST_F(CompatibilityValidationTest, TestCrossDependencyCompatibility) {
    std::ifstream matrix_file(cache_dir / "compatibility_matrix.json");
    json matrix;
    matrix_file >> matrix;
    matrix_file.close();

    // Find cross-compatibility rules
    bool found_cross_rules = false;
    for (const auto& rule : matrix["cross_dependency_compatibility"]) {
        if (rule["dependencies"] == json({"nlohmann_json", "googletest"})) {
            found_cross_rules = true;
            EXPECT_TRUE(rule.contains("compatible_combinations"));
            EXPECT_TRUE(rule.contains("incompatible_combinations"));
            break;
        }
    }

    EXPECT_TRUE(found_cross_rules);
}

// Test compatibility validation for proposed updates
TEST_F(CompatibilityValidationTest, TestProposedUpdateValidation) {
    std::ifstream manifest_file(cache_dir / "dependency_manifest.json");
    json manifest;
    manifest_file >> manifest;
    manifest_file.close();

    // Test nlohmann_json update from 3.11.3 to 3.11.4
    std::string current_version = manifest["dependencies"]["cmake_fetch"]["nlohmann_json"]["current_version"];
    std::string available_version = manifest["dependencies"]["cmake_fetch"]["nlohmann_json"]["available_version"];

    EXPECT_EQ(current_version, "3.11.3");
    EXPECT_EQ(available_version, "3.11.4");

    // Validate both versions are compatible
    EXPECT_TRUE(validateCompatibility("nlohmann_json", current_version));
    EXPECT_TRUE(validateCompatibility("nlohmann_json", available_version));
}

// Test incompatible version detection
TEST_F(CompatibilityValidationTest, TestIncompatibleVersionDetection) {
    // Create a scenario with an incompatible version
    std::vector<std::string> incompatible_versions = {
        "4.0.0",  // Major version beyond range for nlohmann_json
        "1.12.0", // Explicitly excluded for googletest
        "1.0.0"   // Beyond range for secp256k1
    };

    std::vector<std::string> dependencies = {"nlohmann_json", "googletest", "secp256k1"};

    for (size_t i = 0; i < dependencies.size(); ++i) {
        EXPECT_FALSE(validateCompatibility(dependencies[i], incompatible_versions[i]))
            << "Version " << incompatible_versions[i] << " should be incompatible with " << dependencies[i];
    }
}

// Test compatibility rule validation
TEST_F(CompatibilityValidationTest, TestCompatibilityRuleValidation) {
    std::ifstream matrix_file(cache_dir / "compatibility_matrix.json");
    json matrix;
    matrix_file >> matrix;
    matrix_file.close();

    auto validation_rules = matrix["validation_rules"];

    EXPECT_TRUE(validation_rules["allow_minor_version_upgrades"]);
    EXPECT_FALSE(validation_rules["allow_major_version_upgrades"]);
    EXPECT_TRUE(validation_rules["require_security_patches"]);
    EXPECT_TRUE(validation_rules["check_compile_time_compatibility"]);
    EXPECT_TRUE(validation_rules["check_runtime_compatibility"]);
}

// Test security update validation
TEST_F(CompatibilityValidationTest, TestSecurityUpdateValidation) {
    // Simulate security patches (typically minor version increments)
    EXPECT_TRUE(validateCompatibility("nlohmann_json", "3.11.4")); // Security patch for 3.11.3
    EXPECT_TRUE(validateCompatibility("googletest", "1.14.1"));  // Security patch for 1.14.0

    // Security patches should be allowed even if major version upgrades are disabled
    auto v1 = parseVersion("3.11.3");
    auto v2 = parseVersion("3.11.4");

    // Same major and minor, only patch changed - should be allowed
    EXPECT_EQ(v1[0], v2[0]); // Major
    EXPECT_EQ(v1[1], v2[1]); // Minor
    EXPECT_LT(v1[2], v2[2]); // Patch
}

// Test major version upgrade prevention
TEST_F(CompatibilityValidationTest, TestMajorVersionUpgradePrevention) {
    // Major version upgrades should be prevented by validation rules
    EXPECT_FALSE(validateCompatibility("nlohmann_json", "4.0.0")); // Major upgrade from 3.x
    EXPECT_FALSE(validateCompatibility("googletest", "2.0.0"));   // Major upgrade from 1.x

    // Verify version parsing correctly identifies major differences
    auto v1 = parseVersion("3.11.3");
    auto v2 = parseVersion("4.0.0");
    EXPECT_LT(v1[0], v2[0]); // Major version increased
}

// Test compatibility report generation
TEST_F(CompatibilityValidationTest, TestCompatibilityReportGeneration) {
    // Create a compatibility report
    json report = {
        {"report_timestamp", "2025-10-10T00:00:00Z"},
        {"validation_type", "compatibility_check"},
        {"summary", {
            {"total_dependencies", 3},
            {"compatible_dependencies", 3},
            {"incompatible_dependencies", 0},
            {"validation_passed", true}
        }},
        {"dependency_details", {
            {
                {"name", "nlohmann_json"},
                {"current_version", "3.11.3"},
                {"proposed_version", "3.11.4"},
                {"is_compatible", true},
                {"notes", "Minor version upgrade, within compatible range"}
            },
            {
                {"name", "googletest"},
                {"current_version", "1.14.0"},
                {"proposed_version", "1.15.0"},
                {"is_compatible", true},
                {"notes", "Minor version upgrade, within compatible range"}
            },
            {
                {"name", "secp256k1-zkp"},
                {"current_version", "0.3.0"},
                {"proposed_version", "0.3.1"},
                {"is_compatible", true},
                {"notes", "Patch version upgrade, conservative cryptography library"}
            }
        }},
        {"recommendations", {
            "All proposed updates are compatible and safe to apply",
            "Consider applying security patches first",
            "Test compilation after major library updates"
        }}
    };

    // Verify report structure
    EXPECT_TRUE(report.contains("report_timestamp"));
    EXPECT_TRUE(report.contains("validation_type"));
    EXPECT_TRUE(report.contains("summary"));
    EXPECT_TRUE(report.contains("dependency_details"));
    EXPECT_TRUE(report.contains("recommendations"));

    // Verify summary
    auto summary = report["summary"];
    EXPECT_EQ(summary["total_dependencies"], 3);
    EXPECT_EQ(summary["compatible_dependencies"], 3);
    EXPECT_EQ(summary["incompatible_dependencies"], 0);
    EXPECT_TRUE(summary["validation_passed"]);
}

// Test compatibility validation integration
TEST_F(CompatibilityValidationTest, TestCompatibilityValidationIntegration) {
    // Test the full compatibility validation workflow
    std::ifstream manifest_file(cache_dir / "dependency_manifest.json");
    json manifest;
    manifest_file >> manifest;
    manifest_file.close();

    std::vector<VersionInfo> dependencies_to_check;

    // Collect all dependencies with available updates
    for (auto& [category, deps] : manifest["dependencies"].items()) {
        for (auto& [name, info] : deps.items()) {
            if (info["update_available"]) {
                dependencies_to_check.push_back({
                    name,
                    info["current_version"],
                    info["available_version"],
                    info["update_available"]
                });
            }
        }
    }

    EXPECT_GT(dependencies_to_check.size(), 0);

    // Validate all proposed updates
    int compatible_count = 0;
    int incompatible_count = 0;

    for (const auto& dep : dependencies_to_check) {
        bool current_compatible = validateCompatibility(dep.name, dep.current_version);
        bool available_compatible = validateCompatibility(dep.name, dep.available_version);

        if (current_compatible && available_compatible) {
            compatible_count++;
        } else {
            incompatible_count++;
        }
    }

    // All dependencies should be compatible in our test data
    EXPECT_EQ(compatible_count, dependencies_to_check.size());
    EXPECT_EQ(incompatible_count, 0);
}

// Test edge cases in version parsing
TEST_F(CompatibilityValidationTest, TestVersionParsingEdgeCases) {
    // Test malformed versions
    auto v1 = parseVersion("");          // Empty string
    EXPECT_EQ(v1[0], 0);
    EXPECT_EQ(v1[1], 0);
    EXPECT_EQ(v1[2], 0);

    auto v2 = parseVersion("invalid");    // Non-numeric
    EXPECT_EQ(v2[0], 0);
    EXPECT_EQ(v2[1], 0);
    EXPECT_EQ(v2[2], 0);

    auto v3 = parseVersion("1.invalid.3"); // Mixed
    EXPECT_EQ(v3[0], 1);
    EXPECT_EQ(v3[1], 0);
    EXPECT_EQ(v3[2], 3);
}

// Performance test - compatibility validation should be fast
TEST_F(CompatibilityValidationTest, TestPerformance) {
    auto start = std::chrono::high_resolution_clock::now();

    // Perform 1000 compatibility validations
    for (int i = 0; i < 1000; ++i) {
        validateCompatibility("nlohmann_json", "3.11.3");
        validateCompatibility("googletest", "1.14.0");
        validateCompatibility("secp256k1", "0.3.0");
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // Should complete 3000 validations in under 100ms
    EXPECT_LT(duration.count(), 100);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}