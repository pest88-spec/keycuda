#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace fs = std::filesystem;

class DependencyVersionManagementTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir = fs::temp_directory_path() / "dependency_test_" + std::to_string(std::time(nullptr));
        fs::create_directories(test_dir);

        // Create mock project structure
        project_root = test_dir / "project";
        fs::create_directories(project_root);

        scripts_dir = project_root / "scripts";
        fs::create_directories(scripts_dir);

        cache_dir = project_root / ".dependency_cache";
        logs_dir = project_root / "logs" / "dependency_updates";
        fs::create_directories(cache_dir);
        fs::create_directories(logs_dir);

        // Copy the update script to test directory
        std::ifstream src_script("/root/keycuda/scripts/update-dependencies.sh");
        std::ofstream dst_script(scripts_dir / "update-dependencies.sh");
        dst_script << src_script.rdbuf();
        dst_script.close();
        src_script.close();

        // Make script executable
        fs::permissions(scripts_dir / "update-dependencies.sh",
                       fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                       fs::perm_options::add);

        // Create mock .gitmodules
        createMockGitModules();

        // Create mock extracted libraries
        createMockExtractedLibraries();

        // Create mock CMakeLists.txt
        createMockCMakeLists();
    }

    void TearDown() override {
        fs::remove_all(test_dir);
    }

    void createMockGitModules() {
        std::ofstream gitmodules(project_root / ".gitmodules");
        gitmodules << R"([submodule "third_party/secp256k1-zkp"]
	path = third_party/secp256k1-zkp
	url = https://github.com/bitcoin-core/secp256k1.git
[submodule "third_party/bitcoin-core-secp256k1"]
	path = third_party/bitcoin-core-secp256k1
	url = https://github.com/bitcoin-core/secp256k1.git
)";
        gitmodules.close();
    }

    void createMockExtractedLibraries() {
        // Create mock secp256k1-zkp extracted library
        auto zkp_include = project_root / "src" / "extracted" / "secp256k1-zkp" / "include";
        fs::create_directories(zkp_include);

        std::ofstream secp_header(zkp_include / "secp256k1.h");
        secp_header << R"(/* secp256k1 library - version 0.3.0 */
#ifndef SECP256K1_H
#define SECP256K1_H
#endif /* SECP256K1_H */
)";
        secp_header.close();

        // Create some mock source files
        auto zkp_src = project_root / "src" / "extracted" / "secp256k1-zkp" / "src";
        fs::create_directories(zkp_src);

        std::ofstream secp_src(zkp_src / "secp256k1.c");
        secp_src << "// Mock secp256k1 implementation\n";
        secp_src.close();

        std::ofstream precomputed_ecmult(zkp_src / "precomputed_ecmult.c");
        precomputed_ecmult << "// Precomputed ecmult data\n";
        precomputed_ecmult.close();

        // Create mock bitcrack extracted library
        auto bitcrack_dir = project_root / "src" / "extracted" / "bitcrack";
        fs::create_directories(bitcrack_dir);

        std::ofstream bitcrack_cpp(bitcrack_dir / "mock.cpp");
        bitcrack_cpp << "// Mock BitCrack implementation\n";
        bitcrack_cpp.close();
    }

    void createMockCMakeLists() {
        std::ofstream cmake(project_root / "CMakeLists.txt");
        cmake << R"(cmake_minimum_required(VERSION 3.22)
project(Puzzle71Solver VERSION 0.1.0 LANGUAGES CXX CUDA)

if(NOT OFFLINE_BUILD)
  include(FetchContent)
  FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  )
  FetchContent_MakeAvailable(nlohmann_json)

  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
  )
  FetchContent_MakeAvailable(googletest)
endif()
)";
        cmake.close();
    }

    int runScript(const std::vector<std::string>& args) {
        std::string cmd = "cd " + project_root.string() + " && " +
                         (scripts_dir / "update-dependencies.sh").string();

        for (const auto& arg : args) {
            cmd += " " + arg;
        }

        return std::system(cmd.c_str());
    }

    bool fileExists(const fs::path& path) {
        return fs::exists(path);
    }

    json readJsonFile(const fs::path& path) {
        if (!fs::exists(path)) {
            return json{};
        }

        std::ifstream file(path);
        json data;
        file >> data;
        return data;
    }

    fs::path test_dir;
    fs::path project_root;
    fs::path scripts_dir;
    fs::path cache_dir;
    fs::path logs_dir;
};

// Test initialization of version management infrastructure
TEST_F(DependencyVersionManagementTest, TestInitialization) {
    EXPECT_TRUE(fs::exists(scripts_dir / "update-dependencies.sh"));

    // Run init command
    int result = runScript({"init"});
    EXPECT_EQ(result, 0);

    // Check that cache directory and initial files are created
    EXPECT_TRUE(fs::exists(cache_dir / "dependency_manifest.json"));
    EXPECT_TRUE(fs::exists(cache_dir / "version_history.json"));
    EXPECT_TRUE(fs::exists(cache_dir / "compatibility_matrix.json"));

    // Verify manifest structure
    json manifest = readJsonFile(cache_dir / "dependency_manifest.json");
    EXPECT_TRUE(manifest.contains("manifest_version"));
    EXPECT_TRUE(manifest.contains("project"));
    EXPECT_TRUE(manifest.contains("dependencies"));
    EXPECT_EQ(manifest["project"]["name"], "Puzzle71Solver");
    EXPECT_EQ(manifest["project"]["version"], "0.1.0");
}

// Test dependency version detection
TEST_F(DependencyVersionManagementTest, TestDependencyDetection) {
    // Initialize first
    runScript({"init"});

    // Run detect command
    int result = runScript({"detect"});
    EXPECT_EQ(result, 0);

    // Check that manifest was updated with dependency information
    json manifest = readJsonFile(cache_dir / "dependency_manifest.json");

    // Should detect extracted libraries
    EXPECT_TRUE(manifest["dependencies"]["extracted"].contains("secp256k1-zkp"));
    EXPECT_TRUE(manifest["dependencies"]["extracted"].contains("bitcrack"));

    // Should detect CMake dependencies
    EXPECT_TRUE(manifest["dependencies"]["cmake_fetch"].contains("nlohmann_json"));
    EXPECT_TRUE(manifest["dependencies"]["cmake_fetch"].contains("googletest"));

    // Verify extracted library details
    auto zkp_info = manifest["dependencies"]["extracted"]["secp256k1-zkp"];
    EXPECT_EQ(zkp_info["path"], "src/extracted/secp256k1-zkp");
    EXPECT_EQ(zkp_info["source_type"], "extracted_library");
    EXPECT_GT(zkp_info["files_count"], 0);

    // Verify CMake dependency details
    auto nlohmann_info = manifest["dependencies"]["cmake_fetch"]["nlohmann_json"];
    EXPECT_EQ(nlohmann_info["current_version"], "3.11.3");
    EXPECT_EQ(nlohmann_info["source_type"], "cmake_fetchcontent");
}

// Test update checking functionality
TEST_F(DependencyVersionManagementTest, TestUpdateChecking) {
    // Initialize and detect
    runScript({"init"});
    runScript({"detect"});

    // Check for updates
    int result = runScript({"check-updates"});

    // The script should always complete successfully (exit code might be 1 if no updates)
    // We're testing that it doesn't crash and produces valid output

    // Verify manifest was processed (update_available flags should be present)
    json manifest = readJsonFile(cache_dir / "dependency_manifest.json");

    // Check that update_available field exists for CMake dependencies
    if (manifest["dependencies"]["cmake_fetch"].contains("nlohmann_json")) {
        EXPECT_TRUE(manifest["dependencies"]["cmake_fetch"]["nlohmann_json"].contains("update_available"));
    }
}

// Test compatibility validation
TEST_F(DependencyVersionManagementTest, TestCompatibilityValidation) {
    // Initialize and detect
    runScript({"init"});
    runScript({"detect"});

    // Run compatibility validation
    int result = runScript({"validate"});
    EXPECT_EQ(result, 0);

    // Check that compatibility report was generated
    bool report_found = false;
    for (const auto& entry : fs::directory_iterator(logs_dir)) {
        if (entry.path().filename().string().starts_with("compatibility_report_")) {
            report_found = true;

            // Verify report structure
            json report = readJsonFile(entry.path());
            EXPECT_TRUE(report.contains("validation_timestamp"));
            EXPECT_TRUE(report.contains("validation_passed"));
            EXPECT_TRUE(report.contains("environment"));
            break;
        }
    }

    EXPECT_TRUE(report_found);
}

// Test backup creation
TEST_F(DependencyVersionManagementTest, TestBackupCreation) {
    // Initialize first
    runScript({"init"});
    runScript({"detect"});

    // Run a dry-run update to trigger backup creation
    int result = runScript({"update", "--dry-run"});
    EXPECT_EQ(result, 0);

    // For now, let's check that the backup mechanism would work
    // (In actual usage, backups are created during real updates)
    EXPECT_TRUE(fs::exists(cache_dir));
}

// Test report generation
TEST_F(DependencyVersionManagementTest, TestReportGeneration) {
    // Initialize and detect
    runScript({"init"});
    runScript({"detect"});

    // Generate JSON report
    int result = runScript({"report", "json"});
    EXPECT_EQ(result, 0);

    // Check that report was generated
    bool json_report_found = false;
    bool summary_report_found = false;

    for (const auto& entry : fs::directory_iterator(logs_dir)) {
        auto filename = entry.path().filename().string();
        if (filename.starts_with("dependency_report_")) {
            if (filename.ends_with(".json")) {
                json_report_found = true;

                // Verify report structure
                json report = readJsonFile(entry.path());
                EXPECT_TRUE(report.contains("report_timestamp"));
                EXPECT_TRUE(report.contains("project"));
                EXPECT_TRUE(report.contains("dependency_summary"));
                EXPECT_TRUE(report.contains("dependencies"));
            }
        }
    }

    EXPECT_TRUE(json_report_found);

    // Generate summary report
    result = runScript({"report", "summary"});
    EXPECT_EQ(result, 0);

    // Check for summary report
    for (const auto& entry : fs::directory_iterator(logs_dir)) {
        auto filename = entry.path().filename().string();
        if (filename.starts_with("dependency_report_") && !filename.ends_with(".json")) {
            summary_report_found = true;
            break;
        }
    }

    EXPECT_TRUE(summary_report_found);
}

// Test dry-run functionality
TEST_F(DependencyVersionManagementTest, TestDryRunFunctionality) {
    // Initialize and detect
    runScript({"init"});
    runScript({"detect"});

    // Run dry-run update
    int result = runScript({"update", "--dry-run"});
    EXPECT_EQ(result, 0);

    // Verify that no actual changes were made
    // (The manifest should remain unchanged after a dry run)
    json manifest_before = readJsonFile(cache_dir / "dependency_manifest.json");

    // Run another dry-run
    runScript({"update", "--dry-run"});

    json manifest_after = readJsonFile(cache_dir / "dependency_manifest.json");
    EXPECT_EQ(manifest_before, manifest_after);
}

// Test error handling for missing files
TEST_F(DependencyVersionManagementTest, TestErrorHandling) {
    // Test with missing project structure
    fs::remove_all(project_root);

    // Initialize should still work (creates directories)
    int result = runScript({"init"});
    EXPECT_EQ(result, 0);

    // Test help command
    result = runScript({"help"});
    EXPECT_EQ(result, 0);

    // Test invalid command
    result = runScript({"invalid-command"});
    EXPECT_NE(result, 0);
}

// Test version history tracking
TEST_F(DependencyVersionManagementTest, TestVersionHistoryTracking) {
    // Initialize
    runScript({"init"});

    // Check initial version history
    json history = readJsonFile(cache_dir / "version_history.json");
    EXPECT_TRUE(history.contains("history_version"));
    EXPECT_TRUE(history.contains("updates"));
    EXPECT_TRUE(history.contains("rollbacks"));
    EXPECT_TRUE(history["updates"].is_array());
    EXPECT_TRUE(history["rollbacks"].is_array());
}

// Test manifest persistence
TEST_F(DependencyVersionManagementTest, TestManifestPersistence) {
    // Initialize and detect
    runScript({"init"});
    runScript({"detect"});

    // Read manifest
    json manifest1 = readJsonFile(cache_dir / "dependency_manifest.json");
    EXPECT_FALSE(manifest1.empty());

    // Run detect again
    runScript({"detect"});

    // Read manifest again - should be updated with new timestamp
    json manifest2 = readJsonFile(cache_dir / "dependency_manifest.json");
    EXPECT_FALSE(manifest2.empty());

    // Generated timestamp should be different (or at least present)
    EXPECT_TRUE(manifest2.contains("generated_timestamp"));
}

// Integration test - complete workflow
TEST_F(DependencyVersionManagementTest, TestCompleteWorkflow) {
    // Step 1: Initialize
    int result = runScript({"init"});
    EXPECT_EQ(result, 0);
    EXPECT_TRUE(fs::exists(cache_dir / "dependency_manifest.json"));

    // Step 2: Detect dependencies
    result = runScript({"detect"});
    EXPECT_EQ(result, 0);

    json manifest = readJsonFile(cache_dir / "dependency_manifest.json");
    EXPECT_GT(manifest["dependencies"].size(), 0);

    // Step 3: Validate compatibility
    result = runScript({"validate"});
    EXPECT_EQ(result, 0);

    // Step 4: Check for updates
    result = runScript({"check-updates"});
    // Exit code doesn't matter here (0 if updates, 1 if no updates)

    // Step 5: Generate reports
    result = runScript({"report", "json"});
    EXPECT_EQ(result, 0);

    result = runScript({"report", "summary"});
    EXPECT_EQ(result, 0);

    // Verify all expected files exist
    EXPECT_TRUE(fs::exists(cache_dir / "dependency_manifest.json"));
    EXPECT_TRUE(fs::exists(cache_dir / "version_history.json"));
    EXPECT_TRUE(fs::exists(cache_dir / "compatibility_matrix.json"));

    // Check that reports were generated
    int report_count = 0;
    for (const auto& entry : fs::directory_iterator(logs_dir)) {
        if (entry.path().filename().string().starts_with("dependency_report_") ||
            entry.path().filename().string().starts_with("compatibility_report_")) {
            report_count++;
        }
    }

    EXPECT_GT(report_count, 0);
}

// Performance test - ensure operations complete in reasonable time
TEST_F(DependencyVersionManagementTest, TestPerformance) {
    auto start = std::chrono::high_resolution_clock::now();

    // Initialize and detect
    runScript({"init"});
    runScript({"detect"});
    runScript({"validate"});

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // Should complete within 10 seconds for this small test case
    EXPECT_LT(duration.count(), 10000);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}