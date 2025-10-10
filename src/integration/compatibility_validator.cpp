// T047: Compatibility Validation Between Library Versions
// Implements semantic version parsing, compatibility matrix validation, and version conflict detection

#include "compatibility_validator.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace integration {

CompatibilityValidator::CompatibilityValidator(const std::string& cache_dir)
    : cache_dir_(cache_dir) {
    loadCompatibilityMatrix();
}

CompatibilityValidator::~CompatibilityValidator() = default;

// Parse semantic version strings into numeric components
std::vector<int> CompatibilityValidator::parseVersion(const std::string& version) const {
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

    // Truncate to exactly 3 parts
    if (parts.size() > 3) {
        parts.resize(3);
    }

    return parts;
}

// Compare two version strings
int CompatibilityValidator::compareVersions(const std::string& v1, const std::string& v2) const {
    std::vector<int> parts1 = parseVersion(v1);
    std::vector<int> parts2 = parseVersion(v2);

    for (size_t i = 0; i < 3; ++i) {
        if (parts1[i] < parts2[i]) return -1;
        if (parts1[i] > parts2[i]) return 1;
    }

    return 0;
}

// Check if version falls within specified range
bool CompatibilityValidator::isVersionInRange(const std::string& version,
                                            const std::string& min_version,
                                            const std::string& max_version) const {
    return compareVersions(version, min_version) >= 0 && compareVersions(version, max_version) <= 0;
}

// Validate single dependency version against compatibility matrix
bool CompatibilityValidator::validateCompatibility(const std::string& dependency,
                                                  const std::string& version) const {
    if (!compatibility_matrix_loaded_) {
        return false; // Matrix not loaded
    }

    for (const auto& rule : compatibility_matrix_["compatibility_rules"]) {
        if (rule["dependency"] == dependency) {
            auto compat_range = rule["compatible_versions"];
            std::string min_version = compat_range["min_version"];
            std::string max_version = compat_range["max_version"];

            // Check if version is explicitly excluded
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

// Get compatibility details for a dependency
CompatibilityDetails CompatibilityValidator::getCompatibilityDetails(const std::string& dependency,
                                                                   const std::string& version) const {
    CompatibilityDetails details;
    details.dependency_name = dependency;
    details.version = version;
    details.is_compatible = false;
    details.reason = "Unknown dependency";
    details.min_version = "";
    details.max_version = "";
    details.has_exclusions = false;

    if (!compatibility_matrix_loaded_) {
        details.reason = "Compatibility matrix not loaded";
        return details;
    }

    for (const auto& rule : compatibility_matrix_["compatibility_rules"]) {
        if (rule["dependency"] == dependency) {
            auto compat_range = rule["compatible_versions"];
            details.min_version = compat_range["min_version"];
            details.max_version = compat_range["max_version"];
            details.notes = compat_range["notes"];

            // Check exclusions
            details.has_exclusions = !compat_range["excluded_versions"].empty();
            for (const auto& excluded : compat_range["excluded_versions"]) {
                if (excluded == version) {
                    details.is_compatible = false;
                    details.reason = "Version explicitly excluded: " + excluded.get<std::string>();
                    return details;
                }
            }

            // Check version range
            if (isVersionInRange(version, details.min_version, details.max_version)) {
                details.is_compatible = true;
                details.reason = "Within compatible range";
            } else {
                details.reason = "Version outside compatible range";
            }

            return details;
        }
    }

    return details;
}

// Validate cross-dependency compatibility
bool CompatibilityValidator::validateCrossDependencyCompatibility(
    const std::vector<std::pair<std::string, std::string>>& dependencies) const {

    if (!compatibility_matrix_loaded_ || dependencies.size() < 2) {
        return true; // Cannot validate without matrix or single dependency
    }

    // Check each pair of dependencies for cross-compatibility rules
    for (size_t i = 0; i < dependencies.size(); ++i) {
        for (size_t j = i + 1; j < dependencies.size(); ++j) {
            const auto& dep1 = dependencies[i];
            const auto& dep2 = dependencies[j];

            if (!areDependenciesCrossCompatible(dep1.first, dep1.second, dep2.first, dep2.second)) {
                return false;
            }
        }
    }

    return true;
}

// Check if two specific dependency versions are cross-compatible
bool CompatibilityValidator::areDependenciesCrossCompatible(
    const std::string& dep1_name, const std::string& dep1_version,
    const std::string& dep2_name, const std::string& dep2_version) const {

    if (!compatibility_matrix_loaded_) {
        return true;
    }

    for (const auto& rule : compatibility_matrix_["cross_dependency_compatibility"]) {
        auto rule_deps = rule["dependencies"];
        if ((rule_deps[0] == dep1_name && rule_deps[1] == dep2_name) ||
            (rule_deps[0] == dep2_name && rule_deps[1] == dep1_name)) {

            // Check if this combination is explicitly compatible
            for (const auto& combo : rule["compatible_combinations"]) {
                bool dep1_match = false;
                bool dep2_match = false;

                for (const auto& version : combo[dep1_name]) {
                    if (version == dep1_version) {
                        dep1_match = true;
                        break;
                    }
                }

                for (const auto& version : combo[dep2_name]) {
                    if (version == dep2_version) {
                        dep2_match = true;
                        break;
                    }
                }

                if (dep1_match && dep2_match) {
                    return true;
                }
            }

            // Check if this combination is explicitly incompatible
            for (const auto& combo : rule["incompatible_combinations"]) {
                bool dep1_match = false;
                bool dep2_match = false;

                for (const auto& version : combo[dep1_name]) {
                    if (version == dep1_version) {
                        dep1_match = true;
                        break;
                    }
                }

                for (const auto& version : combo[dep2_name]) {
                    if (version == dep2_version) {
                        dep2_match = true;
                        break;
                    }
                }

                if (dep1_match && dep2_match) {
                    return false;
                }
            }
        }
    }

    // No specific cross-compatibility rules found, assume compatible
    return true;
}

// Validate proposed updates for compatibility
ValidationReport CompatibilityValidator::validateProposedUpdates(
    const std::vector<DependencyUpdate>& updates) const {

    ValidationReport report;
    report.validation_type = "proposed_updates";
    report.total_dependencies = updates.size();
    report.compatible_dependencies = 0;
    report.incompatible_dependencies = 0;
    report.validation_passed = true;

    auto timestamp = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(timestamp);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");
    report.validation_timestamp = ss.str();

    for (const auto& update : updates) {
        DependencyValidationResult result;
        result.dependency_name = update.name;
        result.current_version = update.current_version;
        result.proposed_version = update.proposed_version;
        result.current_compatible = validateCompatibility(update.name, update.current_version);
        result.proposed_compatible = validateCompatibility(update.name, update.proposed_version);

        if (result.current_compatible && result.proposed_compatible) {
            result.is_safe_to_update = true;
            result.recommendation = "Safe to update";
            report.compatible_dependencies++;
        } else {
            result.is_safe_to_update = false;
            result.validation_passed = false;
            report.validation_passed = false;
            report.incompatible_dependencies++;

            if (!result.current_compatible && !result.proposed_compatible) {
                result.recommendation = "Both current and proposed versions are incompatible";
            } else if (!result.proposed_compatible) {
                result.recommendation = "Proposed version is incompatible";
            } else {
                result.recommendation = "Current version is incompatible but proposed is compatible";
            }
        }

        // Get detailed compatibility information
        result.current_details = getCompatibilityDetails(update.name, update.current_version);
        result.proposed_details = getCompatibilityDetails(update.name, update.proposed_version);

        report.dependency_results.push_back(result);
    }

    // Generate summary recommendations
    if (report.validation_passed) {
        report.recommendations.push_back("All proposed updates are compatible and safe to apply");
        report.recommendations.push_back("Consider applying security patches first");
        report.recommendations.push_back("Test compilation after major library updates");
    } else {
        report.recommendations.push_back("Some proposed updates are incompatible");
        report.recommendations.push_back("Review incompatible dependencies before proceeding");
        report.recommendations.push_back("Consider alternative versions or compatibility matrix updates");
    }

    return report;
}

// Check if version is a security update
bool CompatibilityValidator::isSecurityUpdate(const std::string& dependency,
                                            const std::string& current_version,
                                            const std::string& proposed_version) const {
    auto current_parts = parseVersion(current_version);
    auto proposed_parts = parseVersion(proposed_version);

    // Security updates typically only change patch version
    return (current_parts[0] == proposed_parts[0] &&  // Major version same
            current_parts[1] == proposed_parts[1] &&  // Minor version same
            current_parts[2] < proposed_parts[2]);    // Patch version increased
}

// Check if version is a major version upgrade
bool CompatibilityValidator::isMajorVersionUpgrade(const std::string& dependency,
                                                  const std::string& current_version,
                                                  const std::string& proposed_version) const {
    auto current_parts = parseVersion(current_version);
    auto proposed_parts = parseVersion(proposed_version);

    return proposed_parts[0] > current_parts[0];
}

// Generate compatibility report
json CompatibilityValidator::generateCompatibilityReport() const {
    json report = {
        {"report_timestamp", getCurrentTimestamp()},
        {"report_type", "compatibility_matrix"},
        {"matrix_version", compatibility_matrix_["matrix_version"]},
        {"validation_rules", compatibility_matrix_["validation_rules"]},
        {"compatibility_rules", compatibility_matrix_["compatibility_rules"]},
        {"cross_dependency_compatibility", compatibility_matrix_["cross_dependency_compatibility"]},
        {"summary", {
            {"total_dependencies", compatibility_matrix_["compatibility_rules"].size()},
            {"cross_dependency_rules", compatibility_matrix_["cross_dependency_compatibility"].size()},
            {"validation_rules", compatibility_matrix_["validation_rules"].size()}
        }}
    };

    return report;
}

// Export validation report to file
bool CompatibilityValidator::exportValidationReport(const ValidationReport& report,
                                                  const std::string& filename) const {
    try {
        json json_report;
        json_report["report_timestamp"] = report.validation_timestamp;
        json_report["validation_type"] = report.validation_type;
        json_report["summary"] = {
            {"total_dependencies", report.total_dependencies},
            {"compatible_dependencies", report.compatible_dependencies},
            {"incompatible_dependencies", report.incompatible_dependencies},
            {"validation_passed", report.validation_passed}
        };

        for (const auto& result : report.dependency_results) {
            json dep_result = {
                {"name", result.dependency_name},
                {"current_version", result.current_version},
                {"proposed_version", result.proposed_version},
                {"current_compatible", result.current_compatible},
                {"proposed_compatible", result.proposed_compatible},
                {"is_safe_to_update", result.is_safe_to_update},
                {"recommendation", result.recommendation}
            };
            json_report["dependency_results"].push_back(dep_result);
        }

        json_report["recommendations"] = report.recommendations;

        std::ofstream report_file(filename);
        report_file << json_report.dump(4);
        report_file.close();

        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

// Load compatibility matrix from file
bool CompatibilityValidator::loadCompatibilityMatrix(const std::string& filename) {
    try {
        std::ifstream matrix_file(filename);
        if (!matrix_file.is_open()) {
            compatibility_matrix_loaded_ = false;
            return false;
        }

        matrix_file >> compatibility_matrix_;
        matrix_file.close();

        compatibility_matrix_loaded_ = true;
        return true;
    } catch (const std::exception& e) {
        compatibility_matrix_loaded_ = false;
        return false;
    }
}

// Load compatibility matrix from default location
bool CompatibilityValidator::loadCompatibilityMatrix() {
    std::string matrix_file = cache_dir_ + "/compatibility_matrix.json";
    return loadCompatibilityMatrix(matrix_file);
}

// Create default compatibility matrix
bool CompatibilityValidator::createDefaultCompatibilityMatrix() const {
    json default_matrix = {
        {"matrix_version", "1.0"},
        {"generated_timestamp", getCurrentTimestamp()},
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
        {"cross_dependency_compatibility", json::array()},
        {"validation_rules", {
            {"allow_minor_version_upgrades", true},
            {"allow_major_version_upgrades", false},
            {"require_security_patches", true},
            {"check_compile_time_compatibility", true},
            {"check_runtime_compatibility", true}
        }}
    };

    try {
        std::string matrix_file = cache_dir_ + "/compatibility_matrix.json";
        std::ofstream file(matrix_file);
        file << default_matrix.dump(4);
        file.close();
        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

// Get current timestamp in ISO 8601 format
std::string CompatibilityValidator::getCurrentTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

} // namespace integration