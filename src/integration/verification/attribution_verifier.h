/**
 * Puzzle71Solver - Attribution Verification Testing Framework
 *
 * Provides comprehensive verification and testing infrastructure for attribution
 * compliance across all integrated third-party libraries.
 *
 * @author       Puzzle71Solver Team
 * @created      2025-10-09
 * @license      MIT
 */

#pragma once

#include <string>
#include <vector>
#include <map>
#include <filesystem>
#include <memory>

namespace integration {
namespace verification {

struct AttributionInfo {
    std::string project_name;
    std::string author;
    std::string origin_url;
    std::string origin_path;
    std::string origin_commit;
    std::string origin_license;
    std::chrono::sys_days extracted_date;
    std::string extracted_by;
    std::string modifications;
    std::string spdx_license_identifier;
    bool is_valid = false;
    std::vector<std::string> validation_errors;
};

struct AttributionReport {
    std::string library_name;
    size_t total_files = 0;
    size_t files_with_attribution = 0;
    size_t files_without_attribution = 0;
    size_t files_with_invalid_attribution = 0;
    double attribution_coverage_percentage = 0.0;
    bool all_files_have_attribution = false;
    bool attribution_headers_valid = false;
    bool license_identifiers_present = false;
    bool origin_references_present = false;
    std::vector<std::string> missing_attribution_files;
    std::vector<std::string> invalid_attribution_files;
    std::vector<std::string> validation_errors;
    std::map<std::string, AttributionInfo> attribution_details;
};

struct LicenseComplianceResult {
    std::string library_name;
    std::string license_type;
    std::string spdx_identifier;
    bool license_compatible = false;
    bool attribution_complete = false;
    bool copyright_preserved = false;
    bool license_file_present = false;
    std::vector<std::string> compliance_issues;
    std::vector<std::string> required_attribution_elements;
};

struct VerificationResult {
    std::string library_name;
    std::chrono::system_clock::time_point verification_time;
    bool verification_passed = false;
    AttributionReport attribution_report;
    LicenseComplianceResult license_compliance;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    std::map<std::string, std::string> metrics;
};

class AttributionVerifier {
public:
    AttributionVerifier();
    explicit AttributionVerifier(const std::filesystem::path& integration_root);
    ~AttributionVerifier();

    // Configuration
    void set_integration_root(const std::filesystem::path& path);
    void set_strict_mode(bool enabled);
    void set_required_attribution_fields(const std::vector<std::string>& fields);
    void set_license_whitelist(const std::vector<std::string>& allowed_licenses);
    void load_attribution_templates(const std::filesystem::path& template_dir);

    // Core verification methods
    VerificationResult verify_library(const std::string& library_name);
    std::vector<VerificationResult> verify_all_libraries();
    VerificationResult verify_file_attribution(const std::filesystem::path& file_path);

    // Attribution-specific verification
    AttributionReport generate_attribution_report(const std::string& library_name);
    bool verify_attribution_coverage(const std::string& library_name, double min_coverage = 100.0);
    bool validate_attribution_header(const std::filesystem::path& file_path, AttributionInfo& attribution);

    // License compliance verification
    LicenseComplianceResult verify_license_compliance(const std::string& library_name);
    bool check_license_compatibility(const std::string& license_type);
    bool verify_license_files_present(const std::string& library_name);

    // Origin reference verification
    bool verify_origin_references_present(const std::string& library_name);
    bool verify_origin_urls_valid(const std::string& library_name);
    bool verify_commit_hashes_valid(const std::string& library_name);

    // Template matching
    bool matches_attribution_template(const std::string& content, const std::string& template_name);
    std::vector<std::string> get_missing_template_fields(const std::string& content, const std::string& template_name);

    // Testing and validation
    bool run_attribution_tests(const std::string& library_name);
    std::vector<std::string> generate_test_cases(const std::string& library_name);
    bool validate_against_golden_master(const std::string& library_name, const std::filesystem::path& golden_master);

    // Batch operations
    std::map<std::string, VerificationResult> batch_verify_libraries(const std::vector<std::string>& library_names);
    bool generate_compliance_report(const std::filesystem::path& output_path);
    bool export_verification_results_json(const std::filesystem::path& output_path);

    // Statistics and analysis
    double get_overall_attribution_coverage() const;
    std::map<std::string, double> get_library_coverage_stats() const;
    std::vector<std::string> get_non_compliant_libraries() const;
    std::map<std::string, size_t> get_error_statistics() const;

    // Automated fixing
    bool add_missing_attribution(const std::filesystem::path& file_path, const AttributionInfo& attribution);
    bool fix_invalid_attribution(const std::filesystem::path& file_path);
    bool update_license_identifiers(const std::string& library_name, const std::string& correct_identifier);

    // Utility methods
    std::vector<std::filesystem::path> get_library_files(const std::string& library_name);
    std::string extract_attribution_header(const std::filesystem::path& file_path);
    bool has_attribution_header(const std::filesystem::path& file_path);
    AttributionInfo parse_attribution_header(const std::string& header_content);

    // Compliance checking
    bool meets_attribution_requirements(const std::string& library_name);
    bool meets_license_requirements(const std::string& library_name);
    bool meets_audit_requirements(const std::string& library_name);

private:
    struct Impl;
    std::unique_ptr<Impl> p_impl;

    // Helper methods
    std::vector<std::string> parse_attribution_tags(const std::string& content);
    bool validate_required_fields(const AttributionInfo& attribution, const std::vector<std::string>& required_fields);
    bool is_valid_url(const std::string& url);
    bool is_valid_commit_hash(const std::string& hash);
    bool is_valid_spdx_identifier(const std::string& identifier);
    std::string normalize_license_identifier(const std::string& license);
    std::map<std::string, std::string> extract_attribution_metadata(const std::string& content);
    bool content_contains_required_tags(const std::string& content, const std::vector<std::string>& required_tags);
};

// Global verifier instance
AttributionVerifier& get_attribution_verifier();

} // namespace verification
} // namespace integration