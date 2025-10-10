/**
 * Library Metadata Enforcement System Implementation
 *
 * @origin       https://github.com/Puzzle71Solver/Puzzle71Solver
 * @origin_path  src/integration/metadata_enforcer.cpp
 * @origin_commit <current_commit>
 * @origin_license MIT
 * @extracted_date   2025-10-10
 * @extracted_by     Puzzle71Solver Team
 * @modifications    Created for third-party dependency integration optimization
 * @spdx_license_identifier MIT
 */

#include "metadata_enforcer.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <filesystem>
#include <regex>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

MetadataEnforcer::MetadataEnforcer(
    const std::string& metadata_schema_path,
    const std::string& compliance_report_path
) : metadata_schema_path_(metadata_schema_path),
    compliance_report_path_(compliance_report_path) {

    // Create directories if they don't exist
    std::filesystem::path schema_path(metadata_schema_path_);
    std::filesystem::create_directories(schema_path.parent_path());
    std::filesystem::path report_path(compliance_report_path_);
    std::filesystem::create_directories(report_path.parent_path());

    // Initialize validation rules
    initialize_validation_rules();

    // Load metadata schema if available
    load_metadata_schema();
}

void MetadataEnforcer::initialize_validation_rules() {
    // Library name validation
    add_validation_rule(ValidationRule(
        "library_name",
        R"(^[a-zA-Z][a-zA-Z0-9_-]*$)",
        "Library name must start with a letter and contain only letters, numbers, underscores, and hyphens"
    ));

    // Version validation (semantic versioning)
    add_validation_rule(ValidationRule(
        "version",
        R"(^\d+\.\d+\.\d+(-[a-zA-Z0-9-]+)?(\+[a-zA-Z0-9-]+)?$)",
        "Version must follow semantic versioning format (x.y.z[-pre][+build])",
        true,
        [this](const std::string& version) { return is_valid_semantic_version(version); }
    ));

    // URL validation
    add_validation_rule(ValidationRule(
        "origin_url",
        R"(^https?://[^\s/$.?#].[^\s]*$)",
        "Origin URL must be a valid HTTP or HTTPS URL",
        true,
        [this](const std::string& url) { return is_valid_url(url); }
    ));

    // SPDX license identifier validation
    add_validation_rule(ValidationRule(
        "spdx_license_identifier",
        R"(^[A-Za-z0-9.\-+]+ OR [A-Za-z0-9.\-+]+$|^[A-Za-z0-9.\-+]+$)",
        "SPDX license identifier must be a valid license identifier or expression",
        true,
        [this](const std::string& id) { return is_valid_spdx_identifier(id); }
    ));

    // Date validation
    add_validation_rule(ValidationRule(
        "extracted_date",
        R"(^\d{4}-\d{2}-\d{2}$)",
        "Date must be in YYYY-MM-DD format",
        true,
        [this](const std::string& date) { return is_valid_date_format(date); }
    ));

    // Integration path validation
    add_validation_rule(ValidationRule(
        "integration_path",
        R"(^src/|^[a-zA-Z0-9_/-]+/?$)",
        "Integration path must be a valid relative path"
    ));
}

bool MetadataEnforcer::register_library(const LibraryMetadata& metadata) {
    // Validate metadata first
    ValidationResult validation = validate_metadata(metadata);
    if (!validation.is_valid) {
        return false; // Don't register invalid metadata
    }

    registered_libraries_[metadata.library_name] = metadata;
    return true;
}

bool MetadataEnforcer::update_library(const std::string& library_name, const LibraryMetadata& metadata) {
    if (library_name != metadata.library_name) {
        return false; // Library name mismatch
    }

    // Validate metadata first
    ValidationResult validation = validate_metadata(metadata);
    if (!validation.is_valid) {
        return false;
    }

    registered_libraries_[library_name] = metadata;
    return true;
}

bool MetadataEnforcer::remove_library(const std::string& library_name) {
    auto it = registered_libraries_.find(library_name);
    if (it != registered_libraries_.end()) {
        registered_libraries_.erase(it);
        return true;
    }
    return false;
}

MetadataEnforcer::ValidationResult MetadataEnforcer::validate_metadata(const LibraryMetadata& metadata) const {
    ValidationResult result;
    result.is_valid = true;

    // Required fields validation
    std::vector<std::string> required_fields = {
        "library_name", "version", "origin_url", "origin_commit",
        "origin_license", "spdx_license_identifier", "extracted_date",
        "extracted_by", "integration_path"
    };

    for (const auto& field : required_fields) {
        bool field_found = false;
        std::string field_value;

        if (field == "library_name") field_value = metadata.library_name;
        else if (field == "version") field_value = metadata.version;
        else if (field == "origin_url") field_value = metadata.origin_url;
        else if (field == "origin_commit") field_value = metadata.origin_commit;
        else if (field == "origin_license") field_value = metadata.origin_license;
        else if (field == "spdx_license_identifier") field_value = metadata.spdx_license_identifier;
        else if (field == "extracted_date") field_value = metadata.extracted_date;
        else if (field == "extracted_by") field_value = metadata.extracted_by;
        else if (field == "integration_path") field_value = metadata.integration_path;

        if (field_value.empty()) {
            result.missing_fields.push_back(field);
            result.is_valid = false;
        } else {
            field_found = true;
            // Validate field against rules
            std::string error_message;
            if (!validate_field(field, field_value, error_message)) {
                result.invalid_fields.push_back(field);
                result.field_errors[field] = error_message;
                result.is_valid = false;
            }
        }
    }

    // Calculate completeness score
    result.completeness_score = calculate_completeness_score(metadata);

    // Add warnings for optional but recommended fields
    if (metadata.source_files.empty()) {
        result.warnings.push_back("No source files specified");
    }
    if (metadata.custom_metadata.empty()) {
        result.warnings.push_back("No custom metadata provided");
    }

    return result;
}

std::map<std::string, MetadataEnforcer::ValidationResult> MetadataEnforcer::validate_all_libraries() const {
    std::map<std::string, ValidationResult> results;

    for (const auto& [library_name, metadata] : registered_libraries_) {
        results[library_name] = validate_metadata(metadata);
    }

    // Save compliance report
    save_compliance_report(results);

    return results;
}

MetadataEnforcer::LibraryMetadata MetadataEnforcer::get_library_metadata(const std::string& library_name) const {
    auto it = registered_libraries_.find(library_name);
    if (it != registered_libraries_.end()) {
        return it->second;
    }
    return LibraryMetadata();
}

std::map<std::string, MetadataEnforcer::LibraryMetadata> MetadataEnforcer::get_all_libraries() const {
    return registered_libraries_;
}

bool MetadataEnforcer::is_library_registered(const std::string& library_name) const {
    return registered_libraries_.find(library_name) != registered_libraries_.end();
}

void MetadataEnforcer::add_validation_rule(const ValidationRule& rule) {
    // Remove existing rule for the same field
    remove_validation_rule(rule.field_name);
    validation_rules_.push_back(rule);
}

void MetadataEnforcer::remove_validation_rule(const std::string& field_name) {
    validation_rules_.erase(
        std::remove_if(validation_rules_.begin(), validation_rules_.end(),
            [&field_name](const ValidationRule& rule) {
                return rule.field_name == field_name;
            }),
        validation_rules_.end()
    );
}

std::vector<MetadataEnforcer::IntegrationStatus> MetadataEnforcer::get_integration_status() const {
    std::vector<IntegrationStatus> status_list;

    for (const auto& [library_name, metadata] : registered_libraries_) {
        IntegrationStatus status;
        status.library_name = library_name;

        // Check metadata completeness
        ValidationResult validation = validate_metadata(metadata);
        status.metadata_complete = validation.is_valid;

        // Check source integration
        status.source_integrated = !metadata.integration_path.empty() &&
                                  std::filesystem::exists(metadata.integration_path);

        // Check attribution compliance
        status.attribution_compliant = validation.completeness_score >= 0.95;

        // Build verification (placeholder - would need actual build system integration)
        status.build_verified = status.source_integrated && status.metadata_complete;

        // Set last updated timestamp
        status.last_updated = metadata.extracted_date;

        // Add pending actions if needed
        if (!status.metadata_complete) {
            status.pending_actions.push_back("Complete metadata validation");
        }
        if (!status.source_integrated) {
            status.pending_actions.push_back("Integrate source files");
        }
        if (!status.attribution_compliant) {
            status.pending_actions.push_back("Improve attribution coverage");
        }
        if (!status.build_verified) {
            status.pending_actions.push_back("Verify build integration");
        }

        status_list.push_back(status);
    }

    return status_list;
}

std::string MetadataEnforcer::generate_compliance_report(const std::string& format) const {
    auto validation_results = validate_all_libraries();

    if (format == "json") {
        json report;
        report["report_generated"] = "2025-10-10T00:00:00Z"; // Current timestamp
        report["total_libraries"] = registered_libraries_.size();

        size_t compliant_count = 0;
        json libraries = json::object();

        for (const auto& [library_name, result] : validation_results) {
            if (result.is_valid) compliant_count++;

            json lib_data;
            lib_data["is_valid"] = result.is_valid;
            lib_data["completeness_score"] = result.completeness_score;
            lib_data["missing_fields"] = result.missing_fields;
            lib_data["invalid_fields"] = result.invalid_fields;
            lib_data["warnings"] = result.warnings;

            libraries[library_name] = lib_data;
        }

        report["compliant_libraries"] = compliant_count;
        report["non_compliant_libraries"] = registered_libraries_.size() - compliant_count;
        report["libraries"] = libraries;

        return report.dump(4);
    } else {
        // Text format
        std::stringstream ss;
        ss << "Metadata Compliance Report\n";
        ss << "==========================\n\n";
        ss << "Total Libraries: " << registered_libraries_.size() << "\n";

        size_t compliant_count = 0;
        for (const auto& [library_name, result] : validation_results) {
            if (result.is_valid) compliant_count++;
        }

        ss << "Compliant Libraries: " << compliant_count << "\n";
        ss << "Non-Compliant Libraries: " << (registered_libraries_.size() - compliant_count) << "\n\n";

        ss << "Library Details:\n";
        for (const auto& [library_name, result] : validation_results) {
            ss << "  " << library_name << ": " << (result.is_valid ? "COMPLIANT" : "NON-COMPLIANT");
            ss << " (Completeness: " << std::fixed << std::setprecision(1) << (result.completeness_score * 100) << "%)\n";

            if (!result.missing_fields.empty()) {
                ss << "    Missing: ";
                for (size_t i = 0; i < result.missing_fields.size(); ++i) {
                    if (i > 0) ss << ", ";
                    ss << result.missing_fields[i];
                }
                ss << "\n";
            }

            if (!result.invalid_fields.empty()) {
                ss << "    Invalid: ";
                for (size_t i = 0; i < result.invalid_fields.size(); ++i) {
                    if (i > 0) ss << ", ";
                    ss << result.invalid_fields[i];
                }
                ss << "\n";
            }
        }

        return ss.str();
    }
}

bool MetadataEnforcer::export_metadata(const std::vector<std::string>& library_names,
                                       const std::string& export_path,
                                       const std::string& format) const {
    std::ofstream file(export_path);
    if (!file.is_open()) {
        return false;
    }

    json export_data;
    export_data["export_timestamp"] = "2025-10-10T00:00:00Z";
    export_data["export_format"] = format;
    export_data["libraries"] = json::object();

    for (const auto& [library_name, metadata] : registered_libraries_) {
        // Filter by library names if specified
        if (!library_names.empty() &&
            std::find(library_names.begin(), library_names.end(), library_name) == library_names.end()) {
            continue;
        }

        json lib_data;
        lib_data["library_name"] = metadata.library_name;
        lib_data["version"] = metadata.version;
        lib_data["origin_url"] = metadata.origin_url;
        lib_data["origin_commit"] = metadata.origin_commit;
        lib_data["origin_license"] = metadata.origin_license;
        lib_data["spdx_license_identifier"] = metadata.spdx_license_identifier;
        lib_data["extracted_date"] = metadata.extracted_date;
        lib_data["extracted_by"] = metadata.extracted_by;
        lib_data["integration_path"] = metadata.integration_path;
        lib_data["source_files"] = metadata.source_files;
        lib_data["include_files"] = metadata.include_files;
        lib_data["exclude_patterns"] = metadata.exclude_patterns;
        lib_data["custom_metadata"] = metadata.custom_metadata;
        lib_data["is_compliant"] = metadata.is_compliant;

        export_data["libraries"][library_name] = lib_data;
    }

    file << std::setw(4) << export_data << std::endl;
    return true;
}

bool MetadataEnforcer::import_metadata(const std::string& import_path,
                                       bool merge_mode,
                                       bool validate_after_import) {
    std::ifstream file(import_path);
    if (!file.is_open()) {
        return false;
    }

    try {
        json import_data;
        file >> import_data;

        if (!merge_mode) {
            registered_libraries_.clear();
        }

        for (auto& [library_name, lib_data] : import_data["libraries"].items()) {
            LibraryMetadata metadata;
            metadata.library_name = lib_data.value("library_name", "");
            metadata.version = lib_data.value("version", "");
            metadata.origin_url = lib_data.value("origin_url", "");
            metadata.origin_commit = lib_data.value("origin_commit", "");
            metadata.origin_license = lib_data.value("origin_license", "");
            metadata.spdx_license_identifier = lib_data.value("spdx_license_identifier", "");
            metadata.extracted_date = lib_data.value("extracted_date", "");
            metadata.extracted_by = lib_data.value("extracted_by", "");
            metadata.integration_path = lib_data.value("integration_path", "");
            metadata.source_files = lib_data.value("source_files", std::vector<std::string>{});
            metadata.include_files = lib_data.value("include_files", std::vector<std::string>{});
            metadata.exclude_patterns = lib_data.value("exclude_patterns", std::vector<std::string>{});
            metadata.custom_metadata = lib_data.value("custom_metadata", std::map<std::string, std::string>{});
            metadata.is_compliant = lib_data.value("is_compliant", false);

            if (validate_after_import) {
                ValidationResult validation = validate_metadata(metadata);
                metadata.is_compliant = validation.is_valid;
            }

            registered_libraries_[library_name] = metadata;
        }

        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

std::vector<std::string> MetadataEnforcer::search_libraries(
    const std::map<std::string, std::string>& search_criteria,
    bool exact_match) const {

    std::vector<std::string> results;

    for (const auto& [library_name, metadata] : registered_libraries_) {
        bool matches = true;

        for (const auto& [field, value] : search_criteria) {
            std::string metadata_value;

            if (field == "library_name") metadata_value = metadata.library_name;
            else if (field == "version") metadata_value = metadata.version;
            else if (field == "origin_license") metadata_value = metadata.origin_license;
            else if (field == "spdx_license_identifier") metadata_value = metadata.spdx_license_identifier;
            else if (field == "extracted_by") metadata_value = metadata.extracted_by;
            else continue; // Skip unknown fields

            if (exact_match) {
                if (metadata_value != value) {
                    matches = false;
                    break;
                }
            } else {
                if (metadata_value.find(value) == std::string::npos) {
                    matches = false;
                    break;
                }
            }
        }

        if (matches) {
            results.push_back(library_name);
        }
    }

    return results;
}

MetadataEnforcer::MetadataStats MetadataEnforcer::get_statistics() const {
    MetadataStats stats;
    stats.total_libraries = registered_libraries_.size();

    double total_completeness = 0.0;
    for (const auto& [library_name, metadata] : registered_libraries_) {
        ValidationResult validation = validate_metadata(metadata);
        total_completeness += validation.completeness_score;

        if (validation.is_valid) {
            stats.compliant_libraries++;
        } else {
            stats.non_compliant_libraries++;
        }

        // License distribution
        stats.license_distribution[metadata.spdx_license_identifier]++;

        // Integration status distribution
        if (!metadata.integration_path.empty() && std::filesystem::exists(metadata.integration_path)) {
            stats.integration_status_distribution["integrated"]++;
        } else {
            stats.integration_status_distribution["not_integrated"]++;
        }
    }

    if (stats.total_libraries > 0) {
        stats.average_completeness_score = total_completeness / stats.total_libraries;
    }

    stats.last_validation_timestamp = "2025-10-10T00:00:00Z"; // Current timestamp

    return stats;
}

bool MetadataEnforcer::auto_fix_metadata_issues(const std::string& library_name) {
    auto it = registered_libraries_.find(library_name);
    if (it == registered_libraries_.end()) {
        return false;
    }

    LibraryMetadata metadata = it->second;
    bool fixed_anything = false;

    // Auto-fix extracted date if missing
    if (metadata.extracted_date.empty() || !is_valid_date_format(metadata.extracted_date)) {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%d");
        metadata.extracted_date = ss.str();
        fixed_anything = true;
    }

    // Auto-fix extracted_by if missing
    if (metadata.extracted_by.empty()) {
        metadata.extracted_by = "system";
        fixed_anything = true;
    }

    // Re-validate after fixes
    ValidationResult validation = validate_metadata(metadata);
    metadata.is_compliant = validation.is_valid;

    registered_libraries_[library_name] = metadata;
    return fixed_anything;
}

MetadataEnforcer::ValidationResult MetadataEnforcer::validate_source_files(const std::string& library_name) const {
    ValidationResult result;
    result.is_valid = true;

    auto it = registered_libraries_.find(library_name);
    if (it == registered_libraries_.end()) {
        result.is_valid = false;
        result.missing_fields.push_back("library_not_found");
        return result;
    }

    const auto& metadata = it->second;

    // Check integration path exists
    if (!std::filesystem::exists(metadata.integration_path)) {
        result.is_valid = false;
        result.invalid_fields.push_back("integration_path");
        result.field_errors["integration_path"] = "Integration path does not exist";
    }

    // Check source files exist
    size_t missing_files = 0;
    for (const auto& source_file : metadata.source_files) {
        std::string full_path = metadata.integration_path + "/" + source_file;
        if (!std::filesystem::exists(full_path)) {
            missing_files++;
        }
    }

    if (missing_files > 0) {
        result.warnings.push_back(std::to_string(missing_files) + " source files not found");
    }

    return result;
}

std::vector<std::vector<std::string>> MetadataEnforcer::find_duplicate_libraries() const {
    std::vector<std::vector<std::string>> duplicates;

    // Check for duplicates by origin URL
    std::map<std::string, std::vector<std::string>> url_to_libraries;
    for (const auto& [library_name, metadata] : registered_libraries_) {
        url_to_libraries[metadata.origin_url].push_back(library_name);
    }

    for (const auto& [url, libraries] : url_to_libraries) {
        if (libraries.size() > 1) {
            duplicates.push_back(libraries);
        }
    }

    return duplicates;
}

std::map<std::string, double> MetadataEnforcer::get_completeness_trends(size_t days) const {
    // This is a placeholder implementation
    // In a real system, you would store historical completeness data
    std::map<std::string, double> trends;

    auto now = std::chrono::system_clock::now();
    for (size_t i = 0; i < days; ++i) {
        auto date = now - std::chrono::hours(24 * i);
        auto time_t = std::chrono::system_clock::to_time_t(date);
        std::stringstream ss;
        ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%d");

        // Mock trend data - in reality this would come from historical records
        trends[ss.str()] = 85.0 + (i % 10) * 1.5; // Varying completeness between 85-100%
    }

    return trends;
}

// Private methods implementation
bool MetadataEnforcer::validate_field(const std::string& field_name,
                                     const std::string& field_value,
                                     std::string& error_message) const {
    for (const auto& rule : validation_rules_) {
        if (rule.field_name == field_name) {
            if (!std::regex_match(field_value, rule.pattern)) {
                error_message = rule.error_message;
                return false;
            }

            if (rule.custom_validator && !rule.custom_validator(field_value)) {
                error_message = rule.error_message;
                return false;
            }

            return true;
        }
    }

    // No validation rule found - consider valid
    return true;
}

bool MetadataEnforcer::is_valid_semantic_version(const std::string& version) const {
    std::regex semver_regex(R"(^(\d+)\.(\d+)\.(\d+)(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?(?:\+([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?$)");
    return std::regex_match(version, semver_regex);
}

bool MetadataEnforcer::is_valid_url(const std::string& url) const {
    std::regex url_regex(R"(^https?://[^\s/$.?#].[^\s]*$)");
    return std::regex_match(url, url_regex);
}

bool MetadataEnforcer::is_valid_spdx_identifier(const std::string& identifier) const {
    // Basic SPDX identifier validation
    std::regex spdx_regex(R"(^([A-Za-z0-9.\-+]+(?: OR [A-Za-z0-9.\-+]+)*)$)");
    return std::regex_match(identifier, spdx_regex);
}

bool MetadataEnforcer::is_valid_date_format(const std::string& date) const {
    std::regex date_regex(R"(^\d{4}-\d{2}-\d{2}$)");
    if (!std::regex_match(date, date_regex)) {
        return false;
    }

    // Additional validation: check if it's a valid date
    std::tm tm = {};
    std::istringstream ss(date);
    ss >> std::get_time(&tm, "%Y-%m-%d");
    return !ss.fail();
}

double MetadataEnforcer::calculate_completeness_score(const LibraryMetadata& metadata) const {
    std::vector<std::string> required_fields = {
        "library_name", "version", "origin_url", "origin_commit",
        "origin_license", "spdx_license_identifier", "extracted_date",
        "extracted_by", "integration_path"
    };

    std::vector<std::string> optional_fields = {
        "source_files", "include_files", "exclude_patterns", "custom_metadata"
    };

    size_t present_required = 0;
    for (const auto& field : required_fields) {
        bool field_present = false;

        if (field == "library_name" && !metadata.library_name.empty()) field_present = true;
        else if (field == "version" && !metadata.version.empty()) field_present = true;
        else if (field == "origin_url" && !metadata.origin_url.empty()) field_present = true;
        else if (field == "origin_commit" && !metadata.origin_commit.empty()) field_present = true;
        else if (field == "origin_license" && !metadata.origin_license.empty()) field_present = true;
        else if (field == "spdx_license_identifier" && !metadata.spdx_license_identifier.empty()) field_present = true;
        else if (field == "extracted_date" && !metadata.extracted_date.empty()) field_present = true;
        else if (field == "extracted_by" && !metadata.extracted_by.empty()) field_present = true;
        else if (field == "integration_path" && !metadata.integration_path.empty()) field_present = true;

        if (field_present) present_required++;
    }

    size_t present_optional = 0;
    for (const auto& field : optional_fields) {
        bool field_present = false;

        if (field == "source_files" && !metadata.source_files.empty()) field_present = true;
        else if (field == "include_files" && !metadata.include_files.empty()) field_present = true;
        else if (field == "exclude_patterns" && !metadata.exclude_patterns.empty()) field_present = true;
        else if (field == "custom_metadata" && !metadata.custom_metadata.empty()) field_present = true;

        if (field_present) present_optional++;
    }

    // Weight required fields more heavily (70% weight) and optional fields (30% weight)
    double required_score = (double)present_required / required_fields.size() * 0.7;
    double optional_score = (double)present_optional / optional_fields.size() * 0.3;

    return required_score + optional_score;
}

bool MetadataEnforcer::load_metadata_schema() {
    if (!std::filesystem::exists(metadata_schema_path_)) {
        return false;
    }

    // In a real implementation, this would load and parse a JSON schema
    // For now, just return true if the file exists
    return true;
}

bool MetadataEnforcer::save_compliance_report(const std::map<std::string, ValidationResult>& results) const {
    std::ofstream file(compliance_report_path_);
    if (!file.is_open()) {
        return false;
    }

    json report;
    report["report_timestamp"] = "2025-10-10T00:00:00Z";
    report["total_libraries"] = registered_libraries_.size();

    for (const auto& [library_name, result] : results) {
        json lib_result;
        lib_result["is_valid"] = result.is_valid;
        lib_result["completeness_score"] = result.completeness_score;
        lib_result["missing_fields"] = result.missing_fields;
        lib_result["invalid_fields"] = result.invalid_fields;
        lib_result["warnings"] = result.warnings;

        report["libraries"][library_name] = lib_result;
    }

    file << std::setw(4) << report << std::endl;
    return true;
}

// MetadataValidator implementation
MetadataValidator::MetadataValidator(const MetadataEnforcer& enforcer)
    : enforcer_(enforcer), validation_passed_(true) {
}

MetadataValidator::~MetadataValidator() {
    // Auto-validation cleanup if needed
}

bool MetadataValidator::validate_library(const std::string& library_name) {
    if (!enforcer_.is_library_registered(library_name)) {
        validation_passed_ = false;
        return false;
    }

    auto metadata = enforcer_.get_library_metadata(library_name);
    auto result = enforcer_.validate_metadata(metadata);

    validated_libraries_.push_back(library_name);

    if (!result.is_valid) {
        validation_passed_ = false;
    }

    return result.is_valid;
}