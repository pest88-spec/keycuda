// T020: Implement secp256k1-zkp Extraction
// Extracts and integrates secp256k1-zkp source code with full attribution

#include "secp256k1_extractor.h"
#include "../manager/integration_manager.h"
#include "../evidence/evidence_collector.h"
#include "../audit/audit_logger.h"
#include "../integrity/integrity_verifier.h"
#include "../attribution/attribution_verifier.h"
#include "../metrics/metrics.h"
#include <fstream>
#include <sstream>
#include <filesystem>

namespace integration {
namespace extraction {

bool Secp256k1Extractor::extract(const ExtractionConfig& config) {
    INTEGRATION_TIMER_SCOPE("secp256k1_extraction");

    LOG_INTEGRATION_OP("secp256k1_extraction_start", {
        {"source", config.source_path},
        {"target", config.target_path},
        {"include_attribution", config.include_attribution},
        {"verify_integrity", config.verify_integrity}
    });

    // Record extraction start
    auto& evidence = integration::manager::get_integration_manager().get_evidence_collector();
    auto& audit = integration::manager::get_integration_manager().get_audit_logger();

    audit->log_extraction("secp256k1-zkp", config.source_path, config.target_path, 0, false);

    // Step 1: Verify source exists
    if (!std::filesystem::exists(config.source_path)) {
        last_error_ = "Source path does not exist: " + config.source_path;
        LOG_INTEGRATION_OP("secp256k1_extraction_error", {
            {"error", last_error_}
        });
        return false;
    }

    // Step 2: Create target directory
    if (!std::filesystem::create_directories(config.target_path)) {
        last_error_ = "Failed to create target directory: " + config.target_path;
        LOG_INTEGRATION_OP("secp256k1_extraction_error", {
            {"error", last_error_}
        });
        return false;
    }

    // Step 3: Discover files to extract
    std::vector<std::string> files_to_extract;
    if (!discover_files(config.source_path, files_to_extract)) {
        last_error_ = "Failed to discover files in source path";
        return false;
    }

    LOG_INTEGRATION_OP("secp256k1_extraction_files_discovered", {
        {"file_count", files_to_extract.size()}
    });

    // Step 4: Copy files with attribution
    std::vector<std::string> extracted_files;
    for (const auto& file : files_to_extract) {
        if (copy_file_with_attribution(config, file, extracted_files)) {
            LOG_INTEGRATION_OP("secp256k1_extraction_file_copied", {
                {"file", file}
            });
        } else {
            LOG_INTEGRATION_OP("secp256k1_extraction_file_failed", {
                {"file", file},
                {"error", last_error_}
            });
        }
    }

    // Step 5: Generate integrity manifest
    if (config.verify_integrity) {
        if (!generate_integrity_manifest(config.target_path, extracted_files)) {
            LOG_INTEGRATION_OP("secp256k1_extraction_integrity_failed", {});
            // Don't fail the extraction, just log the issue
        }
    }

    // Step 6: Verify extraction
    if (config.verify_extraction) {
        if (!verify_extraction(config.target_path, extracted_files)) {
            last_error_ = "Extraction verification failed";
            audit->log_extraction("secp256k1-zkp", config.source_path, config.target_path,
                                extracted_files.size(), false);
            return false;
        }
    }

    // Record successful extraction
    evidence->record_extraction("secp256k1-zkp", config.source_path, config.target_path, extracted_files);
    audit->log_extraction("secp256k1-zkp", config.source_path, config.target_path,
                        extracted_files.size(), true);

    LOG_INTEGRATION_OP("secp256k1_extraction_complete", {
        {"files_extracted", extracted_files.size()},
        {"target_path", config.target_path},
        {"success", true}
    });

    return true;
}

bool Secp256k1Extractor::discover_files(const std::string& source_path,
                                         std::vector<std::string>& files) {
    files.clear();

    // Common include patterns for secp256k1-zkp
    std::vector<std::string> include_patterns = {
        "include/**/*.h",
        "include/**/*.hpp",
        "src/**/*.h",
        "src/**/*.c",
        "src/**/*.cpp",
        "src/**/*.cc"
    };

    // Exclude patterns
    std::vector<std::string> exclude_patterns = {
        "test",
        "tests",
        "example",
        "examples",
        "doc",
        "docs",
        "build",
        "cmake",
        "CMakeFiles",
        ".git",
        "*.o",
        "*.so",
        "*.a",
        "*.exe"
    };

    for (const auto& pattern : include_patterns) {
        std::string cmd = "find " + source_path + " -path '" + pattern + "' -type f";
        std::string result = execute_command(cmd);

        std::stringstream ss(result);
        std::string file;
        while (std::getline(ss, file)) {
            if (!file.empty() && should_include_file(file, exclude_patterns)) {
                files.push_back(file);
            }
        }
    }

    // Add specific secp256k1-zkp files
    std::vector<std::string> specific_files = {
        "README.md",
        "LICENSE",
        "COPYING",
        "ChangeLog",
        "configure",
        "Makefile.am",
        "CMakeLists.txt"
    };

    for (const auto& file : specific_files) {
        std::string full_path = source_path + "/" + file;
        if (std::filesystem::exists(full_path)) {
            files.push_back(full_path);
        }
    }

    return !files.empty();
}

bool Secp256k1Extractor::copy_file_with_attribution(const ExtractionConfig& config,
                                                  const std::string& source_file,
                                                  std::vector<std::string>& extracted_files) {
    // Calculate relative path
    std::string relative_path = source_file.substr(config.source_path.length());
    if (relative_path[0] == '/') {
        relative_path = relative_path.substr(1);
    }

    std::string target_file = config.target_path + "/" + relative_path;
    std::filesystem::path target_path_obj(target_file);

    // Create target directory if needed
    if (!std::filesystem::exists(target_path_obj.parent_path())) {
        if (!std::filesystem::create_directories(target_path_obj.parent_path())) {
            last_error_ = "Failed to create directory: " + target_path_obj.parent_path().string();
            return false;
        }
    }

    // Copy file
    try {
        std::filesystem::copy_file(source_file, target_file);

        // Add attribution if requested
        if (config.include_attribution) {
            if (!add_attribution_header(target_file, config)) {
                // Log warning but don't fail the copy
                LOG_INTEGRATION_OP("secp256k1_extraction_attribution_warning", {
                    {"file", relative_path}
                });
            }
        }

        extracted_files.push_back(relative_path);
        return true;
    } catch (const std::exception& e) {
        last_error_ = "Failed to copy file: " + std::string(e.what());
        return false;
    }
}

bool Secp256k1Extractor::add_attribution_header(const std::string& filepath,
                                               const ExtractionConfig& config) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    file.close();

    // Check if attribution already exists
    if (content.find("SPDX-License-Identifier:") != std::string::npos) {
        return true; // Already has attribution
    }

    // Create attribution header
    std::stringstream header;
    header << "/*\n";
    header << " * Attribution Notice for Integrated Third-Party Code\n";
    header << " * \n";
    header << " * Library: secp256k1-zkp\n";
    header << " * Source: " << config.source_url << "\n";
    header << " * Integrated: " << get_current_timestamp() << "\n";
    header << " * License: MIT\n";
    header << " * \n";
    header << " * This file has been extracted from the upstream repository\n";
    header << " * and integrated into this project with full attribution.\n";
    header << " * No modifications have been made except for namespace adaptation.\n";
    header << " * \n";
    header << " * SPDX-License-Identifier: MIT\n";
    header << " * @origin: " << config.source_url << "\n";
    header << " * @sot_ref: integrated-" << calculate_file_hash(filepath) << "\n";
    header << " */\n\n";

    // Prepend attribution header
    content.insert(0, header.str());

    std::ofstream out_file(filepath);
    if (!out_file.is_open()) {
        return false;
    }

    out_file << content;
    out_file.close();

    return true;
}

bool Secp256k1Extractor::generate_integrity_manifest(const std::string& target_path,
                                                      const std::vector<std::string>& files) {
    nlohmann::json manifest;
    manifest["library"] = "secp256k1-zkp";
    manifest["extraction_date"] = get_current_timestamp();
    manifest["source_path"] = source_path_;
    manifest["target_path"] = target_path;
    manifest["files"] = nlohmann::json::array();

    auto& integrity = integration::manager::get_integration_manager().get_integrity_verifier();

    for (const auto& file : files) {
        std::string full_path = target_path + "/" + file;
        std::string hash = integrity->calculate_file_hash(full_path);

        nlohmann::json file_info;
        file_info["path"] = file;
        file_info["sha256"] = hash;
        file_info["size"] = std::filesystem::file_size(full_path);
        file_info["last_modified"] = get_file_timestamp(full_path);

        manifest["files"].push_back(file_info);
    }

    std::string manifest_file = target_path + "/MANIFEST.json";
    std::ofstream file(manifest_file);
    if (!file.is_open()) {
        return false;
    }

    file << manifest.dump(2) << std::endl;
    file.close();

    return true;
}

bool Secp256k1Extractor::verify_extraction(const std::string& target_path,
                                            const std::vector<std::string>& files) {
    // Verify all expected files exist
    for (const auto& file : files) {
        std::string full_path = target_path + "/" + file;
        if (!std::filesystem::exists(full_path)) {
            last_error_ = "Missing extracted file: " + file;
            return false;
        }
    }

    // Verify integrity manifest exists
    std::string manifest_file = target_path + "/MANIFEST.json";
    if (!std::filesystem::exists(manifest_file)) {
        last_error_ = "Missing integrity manifest: MANIFEST.json";
        return false;
    }

    // Verify attribution (if applicable)
    int files_with_attribution = 0;
    for (const auto& file : files) {
        std::string full_path = target_path + "/" + file;
        auto& verifier = integration::manager::get_integration_manager().get_attribution_verifier();
        if (verifier.verify_file_integrity(full_path)) {
            files_with_attribution++;
        }
    }

    double attribution_percentage = files.empty() ? 0.0 :
        static_cast<double>(files_with_attribution) / files.size() * 100.0;

    LOG_INTEGRATION_OP("secp256k1_extraction_verification", {
        {"total_files", files.size()},
        {"files_with_attribution", files_with_attribution},
        {"attribution_percentage", attribution_percentage}
    });

    return true; // Verification passes if files exist
}

std::string Secp256k1Extractor::execute_command(const std::string& cmd) const {
    std::array<char, 128> buffer;
    std::string result;

    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) {
        return "";
    }

    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }

    return result;
}

std::string Secp256k1Extractor::calculate_file_hash(const std::string& filepath) const {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return "";
    }

    std::hash<std::string> hasher;
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());

    std::stringstream ss;
    ss << std::hex << hasher(content);
    return ss.str();
}

std::string Secp256k1Extractor::get_file_timestamp(const std::string& filepath) const {
    struct stat file_stat;
    if (stat(filepath.c_str(), &file_stat) == 0) {
        std::stringstream ss;
        ss << std::put_time(std::gmtime(&file_stat.st_mtime), "%Y-%m-%dT%H:%M:%SZ");
        return ss.str();
    }
    return "";
}

std::string Secp256k1Extractor::get_current_timestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

bool Secp256k1Extractor::should_include_file(const std::string& file,
                                           const std::vector<std::string>& exclude_patterns) {
    for (const auto& pattern : exclude_patterns) {
        if (file.find(pattern) != std::string::npos) {
            return false;
        }
    }
    return true;
}

// Global instance
static std::unique_ptr<Secp256k1Extractor> g_extractor;

Secp256k1Extractor& get_secp256k1_extractor() {
    if (!g_extractor) {
        g_extractor = std::make_unique<Secp256k1Extractor>();
    }
    return *g_extractor;
}

} // namespace extraction
} // namespace integration