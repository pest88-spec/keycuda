#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <set>
#include <regex>
#include <json/json.h>

namespace puzzle71::gpu::performance {

/**
 * @brief Reference source attribution checking system for code origins
 *
 * Enforces source code attribution requirements with @origin and @sot_ref tags:
 * - Validates presence of attribution tags in source code
 * - Checks for proper reference documentation
 * - Maintains database of source origins and licenses
 * - Verifies compliance with attribution requirements
 * - Generates attribution reports and compliance certificates
 */

enum class AttributionType {
    ORIGIN,      // @origin tag - where code originated from
    SOT_REF,     // @sot_ref tag - single source of truth reference
    LICENSE,     // License attribution
    AUTHOR,      // Author attribution
    DERIVED,     // Derived from original source
    MODIFIED     // Modified from original source
};

enum class AttributionStatus {
    VALID,           // Attribution is valid and complete
    MISSING,         // Required attribution is missing
    INVALID,         // Attribution format is invalid
    EXPIRED,         // Attribution is outdated
    INCONSISTENT,    // Attribution is inconsistent with other sources
    UNVERIFIED       // Attribution cannot be verified
};

struct SourceAttribution {
    std::string file_path;
    size_t line_number;
    AttributionType type;
    std::string tag_content;
    std::string reference_url;
    std::string license_info;
    std::string author_info;
    std::string modification_summary;
    std::chrono::system_clock::time_point last_verified;
    AttributionStatus status;
    std::vector<std::string> validation_errors;
    json metadata;
};

struct AttributionRule {
    std::string rule_name;
    AttributionType applies_to;
    std::string pattern_regex;
    bool is_required;
    std::string description;
    std::vector<std::string> required_fields;
    std::chrono::system_clock::time_point effective_date;
};

struct AttributionComplianceReport {
    std::string component_name;
    std::string report_timestamp;
    size_t total_files_scanned;
    size_t files_with_attributions;
    size_t files_missing_attributions;
    size_t total_attributions_found;
    size_t valid_attributions;
    size_t invalid_attributions;
    size_t missing_required_attributions;

    std::vector<SourceAttribution> all_attributions;
    std::vector<SourceAttribution> invalid_attributions;
    std::vector<std::string> files_without_attributions;
    std::vector<std::string> compliance_violations;
    std::vector<std::string> recommendations;

    double overall_compliance_score;
    bool meets_attribution_requirements;
    bool has_valid_license_attribution;
    bool has_valid_origin_attribution;
};

class SourceAttributionChecker {
public:
    explicit SourceAttributionChecker(const std::string& project_root);
    ~SourceAttributionChecker() = default;

    // Attribution validation
    bool ValidateFileAttribution(const std::string& file_path);
    bool ValidateProjectAttribution();
    AttributionComplianceReport GenerateComplianceReport();

    // Attribution management
    bool AddAttribution(const SourceAttribution& attribution);
    bool UpdateAttribution(const std::string& file_path, size_t line_number, const SourceAttribution& updated);
    bool RemoveAttribution(const std::string& file_path, size_t line_number);
    std::vector<SourceAttribution> GetFileAttributions(const std::string& file_path) const;

    // Rule management
    void AddAttributionRule(const AttributionRule& rule);
    void RemoveAttributionRule(const std::string& rule_name);
    void LoadDefaultAttributionRules();
    std::vector<AttributionRule> GetApplicableRules(const std::string& file_path) const;

    // Pattern matching and parsing
    std::vector<SourceAttribution> ExtractAttributionsFromContent(const std::string& content, const std::string& file_path);
    bool ParseAttributionTag(const std::string& line, SourceAttribution& attribution);
    std::string GenerateAttributionTag(const SourceAttribution& attribution) const;

    // Compliance checking
    bool CheckComplianceWithRules(const std::string& file_path);
    bool VerifyAttributionConsistency();
    bool ValidateLicenseCompatibility();
    bool CheckForMissingRequiredAttributions();

    // Reporting and export
    std::string ExportComplianceReport(const std::string& format = "json") const;
    bool SaveComplianceReport(const std::string& file_path) const;
    std::string GenerateAttributionSummary() const;
    std::vector<std::string> GetAttributionStatistics() const;

    // Configuration
    void SetProjectRoot(const std::string& project_root);
    void SetRequiredAttributionTypes(const std::set<AttributionType>& required_types);
    void SetLicenseRequirements(const std::map<std::string, std::vector<std::string>>& license_requirements);
    void EnableStrictMode(bool enabled);

private:
    std::string project_root_;
    std::set<AttributionType> required_attribution_types_;
    std::map<std::string, std::vector<std::string>> license_requirements_;
    bool strict_mode_enabled_;

    mutable std::mutex attribution_mutex_;

    // Attribution storage
    std::vector<SourceAttribution> attributions_;
    std::map<std::string, std::vector<size_t>> file_line_map_; // file_path -> line numbers
    std::vector<AttributionRule> attribution_rules_;

    // Parsed patterns cache
    std::map<std::string, std::regex> compiled_patterns_;

    // Internal methods
    void InitializeDefaultPatterns();
    std::regex GetCompiledPattern(const std::string& pattern);
    bool IsValidAttributionFormat(const SourceAttribution& attribution) const;
    bool VerifyReferenceURL(const std::string& url) const;
    bool ValidateLicenseInfo(const std::string& license_info) const;
    std::vector<std::string> GetSourceFiles() const;
    std::string ReadFileContent(const std::string& file_path) const;
    std::vector<std::string> SplitIntoLines(const std::string& content) const;

    // Validation helpers
    AttributionStatus ValidateAttribution(const SourceAttribution& attribution);
    std::vector<std::string> GetValidationErrors(const SourceAttribution& attribution);
    bool CheckAttributionCompleteness(const SourceAttribution& attribution) const;
    bool IsAttributionExpired(const SourceAttribution& attribution) const;

    // Compliance calculation
    double CalculateComplianceScore(const AttributionComplianceReport& report) const;
    std::vector<std::string> IdentifyComplianceViolations(const AttributionComplianceReport& report) const;
    std::vector<std::string> GenerateRecommendations(const AttributionComplianceReport& report) const;

    // Serialization helpers
    json SourceAttributionToJson(const SourceAttribution& attribution) const;
    json AttributionRuleToJson(const AttributionRule& rule) const;
    json AttributionComplianceReportToJson(const AttributionComplianceReport& report) const;
};

/**
 * @brief Scoped attribution validator for automated checking during development
 */
class ScopedAttributionValidator {
public:
    explicit ScopedAttributionValidator(SourceAttributionChecker& checker);
    ~ScopedAttributionValidator();

    void ValidateCurrentFile(const std::string& file_path);
    void AutoAddMissingAttributions(const std::string& file_path, const std::vector<SourceAttribution>& attributions);

private:
    SourceAttributionChecker& checker_;
    std::vector<std::string> validated_files_;
    std::chrono::system_clock::time_point validation_start_;
};

/**
 * @brief Utility class for generating standard attribution templates
 */
class AttributionTemplateGenerator {
public:
    static SourceAttribution CreateOriginTemplate(const std::string& origin_url, const std::string& author = "");
    static SourceAttribution CreateSotRefTemplate(const std::string& reference_url, const std::string& description = "");
    static SourceAttribution CreateLicenseTemplate(const std::string& license_name, const std::string& license_url = "");
    static SourceAttribution CreateDerivedTemplate(const std::string& original_source, const std::string& modifications = "");
    static SourceAttribution CreateModifiedTemplate(const std::string& original_file, const std::string& modification_summary = "");

    static std::vector<SourceAttribution> GenerateStandardAttributionSet(
        const std::string& origin_url,
        const std::string& license_name,
        const std::string& author = "");
};

/**
 * @brief Factory function to create and configure source attribution checker
 */
std::unique_ptr<SourceAttributionChecker> CreateSourceAttributionChecker(
    const std::string& project_root,
    bool strict_mode = true);

} // namespace puzzle71::gpu::performance