// T047: Compatibility Validation Between Library Versions
// Header file for semantic version parsing, compatibility matrix validation, and version conflict detection

#ifndef INTEGRATION_COMPATIBILITY_VALIDATOR_H
#define INTEGRATION_COMPATIBILITY_VALIDATOR_H

#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <ctime>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace integration {

// Structure for version compatibility details
struct CompatibilityDetails {
    std::string dependency_name;
    std::string version;
    bool is_compatible;
    std::string reason;
    std::string min_version;
    std::string max_version;
    bool has_exclusions;
    std::string notes;
};

// Structure for dependency update proposals
struct DependencyUpdate {
    std::string name;
    std::string current_version;
    std::string proposed_version;
    std::string source_type;
};

// Structure for dependency validation results
struct DependencyValidationResult {
    std::string dependency_name;
    std::string current_version;
    std::string proposed_version;
    bool current_compatible;
    bool proposed_compatible;
    bool is_safe_to_update;
    bool validation_passed;
    std::string recommendation;
    CompatibilityDetails current_details;
    CompatibilityDetails proposed_details;
};

// Structure for validation reports
struct ValidationReport {
    std::string validation_timestamp;
    std::string validation_type;
    int total_dependencies;
    int compatible_dependencies;
    int incompatible_dependencies;
    bool validation_passed;
    std::vector<DependencyValidationResult> dependency_results;
    std::vector<std::string> recommendations;
};

class CompatibilityValidator {
public:
    explicit CompatibilityValidator(const std::string& cache_dir);
    ~CompatibilityValidator();

    // Core validation functions
    std::vector<int> parseVersion(const std::string& version) const;
    int compareVersions(const std::string& v1, const std::string& v2) const;
    bool isVersionInRange(const std::string& version, const std::string& min_version, const std::string& max_version) const;

    // Single dependency validation
    bool validateCompatibility(const std::string& dependency, const std::string& version) const;
    CompatibilityDetails getCompatibilityDetails(const std::string& dependency, const std::string& version) const;

    // Cross-dependency validation
    bool validateCrossDependencyCompatibility(const std::vector<std::pair<std::string, std::string>>& dependencies) const;
    bool areDependenciesCrossCompatible(const std::string& dep1_name, const std::string& dep1_version,
                                       const std::string& dep2_name, const std::string& dep2_version) const;

    // Update validation
    ValidationReport validateProposedUpdates(const std::vector<DependencyUpdate>& updates) const;
    bool isSecurityUpdate(const std::string& dependency, const std::string& current_version, const std::string& proposed_version) const;
    bool isMajorVersionUpgrade(const std::string& dependency, const std::string& current_version, const std::string& proposed_version) const;

    // Reporting and export
    json generateCompatibilityReport() const;
    bool exportValidationReport(const ValidationReport& report, const std::string& filename) const;

    // Matrix management
    bool loadCompatibilityMatrix(const std::string& filename);
    bool loadCompatibilityMatrix();
    bool createDefaultCompatibilityMatrix() const;

    // Status
    bool isMatrixLoaded() const { return compatibility_matrix_loaded_; }

private:
    std::string cache_dir_;
    json compatibility_matrix_;
    bool compatibility_matrix_loaded_ = false;

    std::string getCurrentTimestamp() const;
};

} // namespace integration

#endif // INTEGRATION_COMPATIBILITY_VALIDATOR_H