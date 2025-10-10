/**
 * Library Metadata Enforcement System for Puzzle71Solver
 *
 * Enforces consistent library metadata across all integrated third-party libraries
 * including name, version, origin, attribution, and licensing information.
 * Validates metadata completeness and compliance with integration standards.
 *
 * @origin       https://github.com/Puzzle71Solver/Puzzle71Solver
 * @origin_path  src/integration/metadata_enforcer.h
 * @origin_commit <current_commit>
 * @origin_license MIT
 * @extracted_date   2025-10-10
 * @extracted_by     Puzzle71Solver Team
 * @modifications    Created for third-party dependency integration optimization
 * @spdx_license_identifier MIT
 */

#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <regex>
#include <fstream>
#include <filesystem>

/**
 * Library Metadata Enforcer
 *
 * Enforces metadata standards and validation for all integrated libraries
 * to ensure consistency, completeness, and regulatory compliance.
 */
class MetadataEnforcer {
public:
    /**
     * Required metadata fields for each library
     */
    struct LibraryMetadata {
        std::string library_name;
        std::string version;
        std::string origin_url;
        std::string origin_commit;
        std::string origin_license;
        std::string spdx_license_identifier;
        std::string extracted_date;
        std::string extracted_by;
        std::string integration_path;
        std::vector<std::string> source_files;
        std::vector<std::string> include_files;
        std::vector<std::string> exclude_patterns;
        std::map<std::string, std::string> custom_metadata;
        bool is_compliant;

        LibraryMetadata() : is_compliant(false) {}
    };

    /**
     * Validation rule for metadata fields
     */
    struct ValidationRule {
        std::string field_name;
        std::regex pattern;
        std::string error_message;
        bool required;
        std::function<bool(const std::string&)> custom_validator;

        ValidationRule(const std::string& name,
                      const std::string& pattern_str,
                      const std::string& error,
                      bool req = true,
                      std::function<bool(const std::string&)> custom_val = nullptr)
            : field_name(name), pattern(pattern_str), error_message(error), required(req), custom_validator(custom_val) {}
    };

    /**
     * Metadata validation result
     */
    struct ValidationResult {
        bool is_valid;
        std::vector<std::string> missing_fields;
        std::vector<std::string> invalid_fields;
        std::vector<std::string> warnings;
        std::map<std::string, std::string> field_errors;
        double completeness_score;

        ValidationResult() : is_valid(false), completeness_score(0.0) {}
    };

    /**
     * Library integration status
     */
    struct IntegrationStatus {
        std::string library_name;
        bool metadata_complete;
        bool source_integrated;
        bool attribution_compliant;
        bool build_verified;
        std::string last_updated;
        std::vector<std::string> pending_actions;

        IntegrationStatus() : metadata_complete(false), source_integrated(false),
                            attribution_compliant(false), build_verified(false) {}
    };

private:
    std::vector<ValidationRule> validation_rules_;
    std::map<std::string, LibraryMetadata> registered_libraries_;
    std::string metadata_schema_path_;
    std::string compliance_report_path_;

    /**
     * Initialize default validation rules
     */
    void initialize_validation_rules();

    /**
     * Validate field against its rule
     */
    bool validate_field(const std::string& field_name,
                       const std::string& field_value,
                       std::string& error_message) const;

    /**
     * Check semantic version compliance
     */
    bool is_valid_semantic_version(const std::string& version) const;

    /**
     * Check URL validity
     */
    bool is_valid_url(const std::string& url) const;

    /**
     * Check SPDX license identifier format
     */
    bool is_valid_spdx_identifier(const std::string& identifier) const;

    /**
     * Check date format compliance
     */
    bool is_valid_date_format(const std::string& date) const;

    /**
     * Calculate metadata completeness score
     */
    double calculate_completeness_score(const LibraryMetadata& metadata) const;

    /**
     * Load metadata schema
     */
    bool load_metadata_schema();

    /**
     * Save compliance report
     */
    bool save_compliance_report(const std::map<std::string, ValidationResult>& results) const;

public:
    /**
     * Constructor
     *
     * @param metadata_schema_path Path to metadata schema file
     * @param compliance_report_path Path for compliance reports
     */
    explicit MetadataEnforcer(
        const std::string& metadata_schema_path = "config/metadata_schema.json",
        const std::string& compliance_report_path = "reports/metadata_compliance.json"
    );

    /**
     * Register library metadata
     *
     * @param metadata Library metadata to register
     * @return True if registration successful
     */
    bool register_library(const LibraryMetadata& metadata);

    /**
     * Update library metadata
     *
     * @param library_name Library name
     * @param metadata Updated metadata
     * @return True if update successful
     */
    bool update_library(const std::string& library_name, const LibraryMetadata& metadata);

    /**
     * Remove library from registry
     *
     * @param library_name Library name to remove
     * @return True if removal successful
     */
    bool remove_library(const std::string& library_name);

    /**
     * Validate library metadata
     *
     * @param metadata Metadata to validate
     * @return Validation result
     */
    ValidationResult validate_metadata(const LibraryMetadata& metadata) const;

    /**
     * Validate all registered libraries
     *
     * @return Map of library names to validation results
     */
    std::map<std::string, ValidationResult> validate_all_libraries() const;

    /**
     * Get library metadata
     *
     * @param library_name Library name
     * @return Library metadata (empty if not found)
     */
    LibraryMetadata get_library_metadata(const std::string& library_name) const;

    /**
     * Get all registered libraries
     *
     * @return Map of library names to metadata
     */
    std::map<std::string, LibraryMetadata> get_all_libraries() const;

    /**
     * Check if library is registered
     *
     * @param library_name Library name
     * @return True if library is registered
     */
    bool is_library_registered(const std::string& library_name) const;

    /**
     * Add custom validation rule
     *
     * @param rule Validation rule to add
     */
    void add_validation_rule(const ValidationRule& rule);

    /**
     * Remove validation rule
     *
     * @param field_name Field name for rule to remove
     */
    void remove_validation_rule(const std::string& field_name);

    /**
     * Get integration status for all libraries
     *
     * @return Vector of integration status information
     */
    std::vector<IntegrationStatus> get_integration_status() const;

    /**
     * Generate metadata compliance report
     *
     * @param format Report format (json, csv, text)
     * @return Formatted compliance report
     */
    std::string generate_compliance_report(const std::string& format = "json") const;

    /**
     * Export library metadata
     *
     * @param library_names Libraries to export (empty for all)
     * @param export_path Export file path
     * @param format Export format (json, yaml, csv)
     * @return True if export successful
     */
    bool export_metadata(const std::vector<std::string>& library_names,
                        const std::string& export_path,
                        const std::string& format = "json") const;

    /**
     * Import library metadata
     *
     * @param import_path Import file path
     * @param merge_mode true=merge with existing, false=replace all
     * @param validate_after_import Validate metadata after import
     * @return True if import successful
     */
    bool import_metadata(const std::string& import_path,
                        bool merge_mode = true,
                        bool validate_after_import = true);

    /**
     * Search libraries by metadata
     *
     * @param search_criteria Map of field names to search values
     * @param exact_match Require exact matches
     * @return Vector of matching library names
     */
    std::vector<std::string> search_libraries(
        const std::map<std::string, std::string>& search_criteria,
        bool exact_match = false) const;

    /**
     * Get metadata statistics
     */
    struct MetadataStats {
        size_t total_libraries;
        size_t compliant_libraries;
        size_t non_compliant_libraries;
        double average_completeness_score;
        std::map<std::string, size_t> license_distribution;
        std::map<std::string, size_t> integration_status_distribution;
        std::string last_validation_timestamp;

        MetadataStats() : total_libraries(0), compliant_libraries(0),
                         non_compliant_libraries(0), average_completeness_score(0.0) {}
    };

    MetadataStats get_statistics() const;

    /**
     * Auto-fix common metadata issues
     *
     * @param library_name Library to fix
     * @return True if any issues were fixed
     */
    bool auto_fix_metadata_issues(const std::string& library_name);

    /**
     * Validate library source files against metadata
     *
     * @param library_name Library name
     * @return Validation result for source files
     */
    ValidationResult validate_source_files(const std::string& library_name) const;

    /**
     * Check for duplicate library registrations
     *
     * @return Vector of potential duplicate libraries
     */
    std::vector<std::vector<std::string>> find_duplicate_libraries() const;

    /**
     * Get metadata completeness trends over time
     *
     * @param days Number of days to analyze
     * @return Map of dates to completeness scores
     */
    std::map<std::string, double> get_completeness_trends(size_t days = 30) const;
};

/**
 * RAII Metadata Validator for temporary validation operations
 */
class MetadataValidator {
private:
    const MetadataEnforcer& enforcer_;
    std::vector<std::string> validated_libraries_;
    bool validation_passed_;

public:
    explicit MetadataValidator(const MetadataEnforcer& enforcer);
    ~MetadataValidator();

    /**
     * Validate library and add to results
     */
    bool validate_library(const std::string& library_name);

    /**
     * Get validation results
     */
    bool all_validations_passed() const { return validation_passed_; }

    /**
     * Get validated library names
     */
    const std::vector<std::string>& get_validated_libraries() const { return validated_libraries_; }
};