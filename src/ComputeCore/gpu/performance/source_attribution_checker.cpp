#include "ComputeCore/gpu/performance/source_attribution_checker.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <ctime>

namespace puzzle71::gpu::performance {

SourceAttributionChecker::SourceAttributionChecker(const std::string& project_root)
    : project_root_(project_root)
    , strict_mode_enabled_(true) {

    // Initialize required attribution types
    required_attribution_types_ = {
        AttributionType::ORIGIN,
        AttributionType::LICENSE,
        AttributionType::AUTHOR
    };

    // Load default attribution rules
    LoadDefaultAttributionRules();
    InitializeDefaultPatterns();
}

bool SourceAttributionChecker::ValidateFileAttribution(const std::string& file_path) {
    std::lock_guard<std::mutex> lock(attribution_mutex_);

    if (!std::filesystem::exists(file_path)) {
        return false;
    }

    std::string content = ReadFileContent(file_path);
    auto file_attributions = ExtractAttributionsFromContent(content, file_path);

    // Check if required attribution types are present
    std::set<AttributionType> found_types;
    for (const auto& attribution : file_attributions) {
        found_types.insert(attribution.type);
    }

    for (const auto& required_type : required_attribution_types_) {
        if (found_types.find(required_type) == found_types.end()) {
            return false;
        }
    }

    // Validate individual attributions
    for (auto& attribution : file_attributions) {
        attribution.status = ValidateAttribution(attribution);
        if (attribution.status != AttributionStatus::VALID) {
            return false;
        }
    }

    return true;
}

bool SourceAttributionChecker::ValidateProjectAttribution() {
    std::lock_guard<std::mutex> lock(attribution_mutex_);

    auto source_files = GetSourceFiles();
    size_t valid_files = 0;

    for (const auto& file_path : source_files) {
        if (ValidateFileAttribution(file_path)) {
            valid_files++;
        }
    }

    // Consider project valid if at least 90% of files have valid attributions
    return source_files.empty() ? false :
        (static_cast<double>(valid_files) / source_files.size()) >= 0.9;
}

AttributionComplianceReport SourceAttributionChecker::GenerateComplianceReport() {
    std::lock_guard<std::mutex> lock(attribution_mutex_);

    AttributionComplianceReport report;
    report.component_name = "keycuda-gpu-performance";
    report.report_timestamp = std::to_string(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));

    auto source_files = GetSourceFiles();
    report.total_files_scanned = source_files.size();

    // Scan all files for attributions
    for (const auto& file_path : source_files) {
        std::string content = ReadFileContent(file_path);
        auto file_attributions = ExtractAttributionsFromContent(content, file_path);

        if (file_attributions.empty()) {
            report.files_without_attributions.push_back(file_path);
        } else {
            report.files_with_attributions++;
        }

        // Process all attributions
        for (auto& attribution : file_attributions) {
            attribution.status = ValidateAttribution(attribution);
            report.all_attributions.push_back(attribution);
            report.total_attributions_found++;

            if (attribution.status == AttributionStatus::VALID) {
                report.valid_attributions++;
            } else {
                report.invalid_attributions++;
                report.invalid_attributions.push_back(attribution);
            }
        }
    }

    report.files_missing_attributions = report.files_without_attributions.size();
    report.missing_required_attributions = CountMissingRequiredAttributions();

    // Calculate compliance metrics
    report.overall_compliance_score = CalculateComplianceScore(report);
    report.meets_attribution_requirements = report.overall_compliance_score >= 80.0;

    // Check specific requirements
    report.has_valid_license_attribution = HasValidLicenseAttribution();
    report.has_valid_origin_attribution = HasValidOriginAttribution();

    // Generate compliance violations and recommendations
    report.compliance_violations = IdentifyComplianceViolations(report);
    report.recommendations = GenerateRecommendations(report);

    return report;
}

bool SourceAttributionChecker::AddAttribution(const SourceAttribution& attribution) {
    std::lock_guard<std::mutex> lock(attribution_mutex_);

    // Validate the attribution before adding
    if (ValidateAttribution(attribution) != AttributionStatus::VALID) {
        return false;
    }

    attributions_.push_back(attribution);
    file_line_map_[attribution.file_path].push_back(attribution.line_number);
    return true;
}

bool SourceAttributionChecker::UpdateAttribution(const std::string& file_path, size_t line_number, const SourceAttribution& updated) {
    std::lock_guard<std::mutex> lock(attribution_mutex_);

    // Find the attribution to update
    auto it = std::find_if(attributions_.begin(), attributions_.end(),
        [&file_path, line_number](const SourceAttribution& attr) {
            return attr.file_path == file_path && attr.line_number == line_number;
        });

    if (it == attributions_.end()) {
        return false;
    }

    // Validate the updated attribution
    if (ValidateAttribution(updated) != AttributionStatus::VALID) {
        return false;
    }

    *it = updated;
    return true;
}

bool SourceAttributionChecker::RemoveAttribution(const std::string& file_path, size_t line_number) {
    std::lock_guard<std::mutex> lock(attribution_mutex_);

    auto it = std::find_if(attributions_.begin(), attributions_.end(),
        [&file_path, line_number](const SourceAttribution& attr) {
            return attr.file_path == file_path && attr.line_number == line_number;
        });

    if (it == attributions_.end()) {
        return false;
    }

    attributions_.erase(it);

    // Remove from file line map
    auto& line_numbers = file_line_map_[file_path];
    line_numbers.erase(std::remove(line_numbers.begin(), line_numbers.end(), line_number), line_numbers.end());
    if (line_numbers.empty()) {
        file_line_map_.erase(file_path);
    }

    return true;
}

std::vector<SourceAttribution> SourceAttributionChecker::GetFileAttributions(const std::string& file_path) const {
    std::lock_guard<std::mutex> lock(attribution_mutex_);

    std::vector<SourceAttribution> file_attributions;
    for (const auto& attribution : attributions_) {
        if (attribution.file_path == file_path) {
            file_attributions.push_back(attribution);
        }
    }

    return file_attributions;
}

void SourceAttributionChecker::AddAttributionRule(const AttributionRule& rule) {
    std::lock_guard<std::mutex> lock(attribution_mutex_);
    attribution_rules_.push_back(rule);
}

void SourceAttributionChecker::RemoveAttributionRule(const std::string& rule_name) {
    std::lock_guard<std::mutex> lock(attribution_mutex_);

    attribution_rules_.erase(
        std::remove_if(attribution_rules_.begin(), attribution_rules_.end(),
            [&rule_name](const AttributionRule& rule) {
                return rule.rule_name == rule_name;
            }),
        attribution_rules_.end());
}

void SourceAttributionChecker::LoadDefaultAttributionRules() {
    attribution_rules_ = {
        {
            "Required Origin Tag",
            AttributionType::ORIGIN,
            R"(^\s*\/\/\s*@origin:\s*(.+))",
            true,
            "Every source file must have an @origin tag",
            {"reference_url"},
            std::chrono::system_clock::now() - std::chrono::hours(24 * 365) // 1 year ago
        },
        {
            "Required SOT Ref Tag",
            AttributionType::SOT_REF,
            R"(^\s*\/\/\s*@sot_ref:\s*(.+))",
            true,
            "Every source file must have a @sot_ref tag",
            {"reference_url"},
            std::chrono::system_clock::now() - std::chrono::hours(24 * 365)
        },
        {
            "Required License Tag",
            AttributionType::LICENSE,
            R"(^\s*\/\/\s*@license:\s*(.+))",
            true,
            "Every source file must have a @license tag",
            {"license_info"},
            std::chrono::system_clock::now() - std::chrono::hours(24 * 365)
        },
        {
            "Required Author Tag",
            AttributionType::AUTHOR,
            R"(^\s*\/\/\s*@author:\s*(.+))",
            true,
            "Every source file must have an @author tag",
            {"author_info"},
            std::chrono::system_clock::now() - std::chrono::hours(24 * 365)
        }
    };
}

std::vector<AttributionRule> SourceAttributionChecker::GetApplicableRules(const std::string& file_path) const {
    std::lock_guard<std::mutex> lock(attribution_mutex_);

    std::vector<AttributionRule> applicable_rules;
    for (const auto& rule : attribution_rules_) {
        // For now, apply all rules to all files
        // In a more sophisticated implementation, we might filter by file type, etc.
        applicable_rules.push_back(rule);
    }

    return applicable_rules;
}

std::vector<SourceAttribution> SourceAttributionChecker::ExtractAttributionsFromContent(
    const std::string& content, const std::string& file_path) {

    std::vector<SourceAttribution> attributions;
    auto lines = SplitIntoLines(content);

    for (size_t i = 0; i < lines.size(); ++i) {
        SourceAttribution attribution;
        if (ParseAttributionTag(lines[i], attribution)) {
            attribution.file_path = file_path;
            attribution.line_number = i + 1;
            attribution.last_verified = std::chrono::system_clock::now();
            attributions.push_back(attribution);
        }
    }

    return attributions;
}

bool SourceAttributionChecker::ParseAttributionTag(const std::string& line, SourceAttribution& attribution) {
    std::smatch match;

    // Try to match @origin tag
    static const std::regex origin_pattern(R"(^\s*\/\/\s*@origin:\s*(.+))");
    if (std::regex_search(line, match, origin_pattern) && match.size() > 1) {
        attribution.type = AttributionType::ORIGIN;
        attribution.tag_content = match[1].str();
        attribution.reference_url = attribution.tag_content;
        attribution.status = AttributionStatus::UNVERIFIED;
        return true;
    }

    // Try to match @sot_ref tag
    static const std::regex sot_ref_pattern(R"(^\s*\/\/\s*@sot_ref:\s*(.+))");
    if (std::regex_search(line, match, sot_ref_pattern) && match.size() > 1) {
        attribution.type = AttributionType::SOT_REF;
        attribution.tag_content = match[1].str();
        attribution.reference_url = attribution.tag_content;
        attribution.status = AttributionStatus::UNVERIFIED;
        return true;
    }

    // Try to match @license tag
    static const std::regex license_pattern(R"(^\s*\/\/\s*@license:\s*(.+))");
    if (std::regex_search(line, match, license_pattern) && match.size() > 1) {
        attribution.type = AttributionType::LICENSE;
        attribution.tag_content = match[1].str();
        attribution.license_info = attribution.tag_content;
        attribution.status = AttributionStatus::UNVERIFIED;
        return true;
    }

    // Try to match @author tag
    static const std::regex author_pattern(R"(^\s*\/\/\s*@author:\s*(.+))");
    if (std::regex_search(line, match, author_pattern) && match.size() > 1) {
        attribution.type = AttributionType::AUTHOR;
        attribution.tag_content = match[1].str();
        attribution.author_info = attribution.tag_content;
        attribution.status = AttributionStatus::UNVERIFIED;
        return true;
    }

    return false;
}

std::string SourceAttributionChecker::GenerateAttributionTag(const SourceAttribution& attribution) const {
    std::ostringstream oss;

    switch (attribution.type) {
        case AttributionType::ORIGIN:
            oss << "// @origin: " << attribution.tag_content;
            break;
        case AttributionType::SOT_REF:
            oss << "// @sot_ref: " << attribution.tag_content;
            break;
        case AttributionType::LICENSE:
            oss << "// @license: " << attribution.tag_content;
            break;
        case AttributionType::AUTHOR:
            oss << "// @author: " << attribution.tag_content;
            break;
        case AttributionType::DERIVED:
            oss << "// @derived: " << attribution.tag_content;
            break;
        case AttributionType::MODIFIED:
            oss << "// @modified: " << attribution.tag_content;
            break;
    }

    return oss.str();
}

bool SourceAttributionChecker::CheckComplianceWithRules(const std::string& file_path) {
    auto attributions = GetFileAttributions(file_path);
    auto rules = GetApplicableRules(file_path);

    for (const auto& rule : rules) {
        if (rule.is_required) {
            bool found_required_type = std::any_of(attributions.begin(), attributions.end(),
                [&rule](const SourceAttribution& attr) {
                    return attr.type == rule.applies_to;
                });

            if (!found_required_type) {
                return false;
            }
        }
    }

    return true;
}

bool SourceAttributionChecker::VerifyAttributionConsistency() {
    std::lock_guard<std::mutex> lock(attribution_mutex_);

    // Group attributions by file
    std::map<std::string, std::vector<SourceAttribution>> file_attributions;
    for (const auto& attribution : attributions_) {
        file_attributions[attribution.file_path].push_back(attribution);
    }

    // Check for consistency within each file
    for (const auto& [file_path, attributions] : file_attributions) {
        // Check for duplicate attribution types
        std::set<AttributionType> found_types;
        for (const auto& attribution : attributions) {
            if (found_types.count(attribution.type) > 0) {
                return false; // Duplicate type found
            }
            found_types.insert(attribution.type);
        }
    }

    return true;
}

bool SourceAttributionChecker::ValidateLicenseCompatibility() {
    std::lock_guard<std::mutex> lock(attribution_mutex_);

    // Collect all license types found
    std::set<std::string> license_types;
    for (const auto& attribution : attributions_) {
        if (attribution.type == AttributionType::LICENSE && !attribution.license_info.empty()) {
            license_types.insert(attribution.license_info);
        }
    }

    // Check for incompatible license combinations
    // This is a simplified check - in practice, you'd need a comprehensive license compatibility matrix
    if (license_types.count("GPL") > 0 && license_types.count("proprietary") > 0) {
        return false; // GPL and proprietary are incompatible
    }

    return true;
}

bool SourceAttributionChecker::CheckForMissingRequiredAttributions() {
    auto source_files = GetSourceFiles();

    for (const auto& file_path : source_files) {
        auto attributions = GetFileAttributions(file_path);
        std::set<AttributionType> found_types;

        for (const auto& attribution : attributions) {
            found_types.insert(attribution.type);
        }

        for (const auto& required_type : required_attribution_types_) {
            if (found_types.find(required_type) == found_types.end()) {
                return false;
            }
        }
    }

    return true;
}

std::string SourceAttributionChecker::ExportComplianceReport(const std::string& format) const {
    auto report = GenerateComplianceReport();

    if (format == "json") {
        return AttributionComplianceReportToJson(report).dump(4);
    } else {
        // Human-readable format
        std::ostringstream oss;
        oss << "Source Attribution Compliance Report\n";
        oss << "===================================\n";
        oss << "Files Scanned: " << report.total_files_scanned << "\n";
        oss << "Files with Attributions: " << report.files_with_attributions << "\n";
        oss << "Files Missing Attributions: " << report.files_missing_attributions << "\n";
        oss << "Overall Compliance Score: " << std::fixed << std::setprecision(2)
            << report.overall_compliance_score << "%\n";
        oss << "Meets Requirements: " << (report.meets_attribution_requirements ? "Yes" : "No") << "\n";

        return oss.str();
    }
}

bool SourceAttributionChecker::SaveComplianceReport(const std::string& file_path) const {
    try {
        std::ofstream file(file_path);
        if (!file.is_open()) {
            return false;
        }

        std::string content = ExportComplianceReport("json");
        file << content;
        return file.good();
    } catch (...) {
        return false;
    }
}

// Configuration methods

void SourceAttributionChecker::SetProjectRoot(const std::string& project_root) {
    std::lock_guard<std::mutex> lock(attribution_mutex_);
    project_root_ = project_root;
}

void SourceAttributionChecker::SetRequiredAttributionTypes(const std::set<AttributionType>& required_types) {
    std::lock_guard<std::mutex> lock(attribution_mutex_);
    required_attribution_types_ = required_types;
}

void SourceAttributionChecker::SetLicenseRequirements(const std::map<std::string, std::vector<std::string>>& license_requirements) {
    std::lock_guard<std::mutex> lock(attribution_mutex_);
    license_requirements_ = license_requirements;
}

void SourceAttributionChecker::EnableStrictMode(bool enabled) {
    std::lock_guard<std::mutex> lock(attribution_mutex_);
    strict_mode_enabled_ = enabled;
}

// Private methods

void SourceAttributionChecker::InitializeDefaultPatterns() {
    // Initialize regex patterns for common attribution formats
    compiled_patterns_["origin"] = std::regex(R"(^\s*\/\/\s*@origin:\s*(.+))");
    compiled_patterns_["sot_ref"] = std::regex(R"(^\s*\/\/\s*@sot_ref:\s*(.+))");
    compiled_patterns_["license"] = std::regex(R"(^\s*\/\/\s*@license:\s*(.+))");
    compiled_patterns_["author"] = std::regex(R"(^\s*\/\/\s*@author:\s*(.+))");
}

AttributionStatus SourceAttributionChecker::ValidateAttribution(const SourceAttribution& attribution) {
    if (!IsValidAttributionFormat(attribution)) {
        return AttributionStatus::INVALID;
    }

    if (!CheckAttributionCompleteness(attribution)) {
        return AttributionStatus::MISSING;
    }

    if (IsAttributionExpired(attribution)) {
        return AttributionStatus::EXPIRED;
    }

    if (!VerifyReferenceURL(attribution.reference_url)) {
        return AttributionStatus::UNVERIFIED;
    }

    return AttributionStatus::VALID;
}

bool SourceAttributionChecker::IsValidAttributionFormat(const SourceAttribution& attribution) const {
    switch (attribution.type) {
        case AttributionType::ORIGIN:
        case AttributionType::SOT_REF:
            return !attribution.reference_url.empty() &&
                   (attribution.reference_url.find("http://") == 0 ||
                    attribution.reference_url.find("https://") == 0 ||
                    attribution.reference_url.find("file://") == 0);

        case AttributionType::LICENSE:
            return !attribution.license_info.empty();

        case AttributionType::AUTHOR:
            return !attribution.author_info.empty();

        default:
            return true;
    }
}

bool SourceAttributionChecker::VerifyReferenceURL(const std::string& url) const {
    // Basic URL validation - in practice, you might want to make actual HTTP requests
    if (url.empty()) return false;

    return url.find("http://") == 0 ||
           url.find("https://") == 0 ||
           url.find("file://") == 0 ||
           url.find("git://") == 0;
}

bool SourceAttributionChecker::ValidateLicenseInfo(const std::string& license_info) const {
    // Basic license validation - check for common license names
    static const std::vector<std::string> common_licenses = {
        "MIT", "Apache-2.0", "GPL-2.0", "GPL-3.0", "BSD-3-Clause", "proprietary"
    };

    for (const auto& license : common_licenses) {
        if (license_info.find(license) != std::string::npos) {
            return true;
        }
    }

    return false;
}

std::vector<std::string> SourceAttributionChecker::GetSourceFiles() const {
    std::vector<std::string> source_files;

    try {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(project_root_)) {
            if (entry.is_regular_file()) {
                auto path = entry.path();
                if (path.extension() == ".cpp" || path.extension() == ".cu" ||
                    path.extension() == ".h" || path.extension() == ".cuh") {
                    source_files.push_back(path.string());
                }
            }
        }
    } catch (const std::exception& e) {
        // Handle filesystem errors
    }

    return source_files;
}

std::string SourceAttributionChecker::ReadFileContent(const std::string& file_path) const {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        return "";
    }

    std::ostringstream content;
    content << file.rdbuf();
    return content.str();
}

std::vector<std::string> SourceAttributionChecker::SplitIntoLines(const std::string& content) const {
    std::vector<std::string> lines;
    std::istringstream stream(content);
    std::string line;

    while (std::getline(stream, line)) {
        lines.push_back(line);
    }

    return lines;
}

size_t SourceAttributionChecker::CountMissingRequiredAttributions() const {
    auto source_files = GetSourceFiles();
    size_t missing_count = 0;

    for (const auto& file_path : source_files) {
        auto attributions = GetFileAttributions(file_path);
        std::set<AttributionType> found_types;

        for (const auto& attribution : attributions) {
            found_types.insert(attribution.type);
        }

        for (const auto& required_type : required_attribution_types_) {
            if (found_types.find(required_type) == found_types.end()) {
                missing_count++;
                break; // Count each file only once, even if multiple attributions are missing
            }
        }
    }

    return missing_count;
}

bool SourceAttributionChecker::HasValidLicenseAttribution() const {
    return std::any_of(attributions_.begin(), attributions_.end(),
        [](const SourceAttribution& attr) {
            return attr.type == AttributionType::LICENSE &&
                   attr.status == AttributionStatus::VALID;
        });
}

bool SourceAttributionChecker::HasValidOriginAttribution() const {
    return std::any_of(attributions_.begin(), attributions_.end(),
        [](const SourceAttribution& attr) {
            return attr.type == AttributionType::ORIGIN &&
                   attr.status == AttributionStatus::VALID;
        });
}

double SourceAttributionChecker::CalculateComplianceScore(const AttributionComplianceReport& report) const {
    if (report.total_files_scanned == 0) return 0.0;

    double file_coverage_score = (static_cast<double>(report.files_with_attributions) / report.total_files_scanned) * 100.0;
    double attribution_quality_score = (static_cast<double>(report.valid_attributions) / report.total_attributions_found) * 100.0;

    return (file_coverage_score * 0.6) + (attribution_quality_score * 0.4);
}

std::vector<std::string> SourceAttributionChecker::IdentifyComplianceViolations(const AttributionComplianceReport& report) const {
    std::vector<std::string> violations;

    if (report.files_missing_attributions > 0) {
        violations.push_back(std::to_string(report.files_missing_attributions) + " files missing required attributions");
    }

    if (report.invalid_attributions > 0) {
        violations.push_back(std::to_string(report.invalid_attributions) + " invalid attributions found");
    }

    if (!report.has_valid_license_attribution) {
        violations.push_back("No valid license attributions found");
    }

    if (!report.has_valid_origin_attribution) {
        violations.push_back("No valid origin attributions found");
    }

    return violations;
}

std::vector<std::string> SourceAttributionChecker::GenerateRecommendations(const AttributionComplianceReport& report) const {
    std::vector<std::string> recommendations;

    if (report.files_missing_attributions > 0) {
        recommendations.push_back("Add required attribution tags to files missing them (@origin, @license, @author)");
    }

    if (report.invalid_attributions > 0) {
        recommendations.push_back("Fix format and validation issues in invalid attributions");
    }

    if (report.overall_compliance_score < 100.0) {
        recommendations.push_back("Consider adding additional attribution information to improve compliance score");
    }

    return recommendations;
}

// ScopedAttributionValidator implementation

ScopedAttributionValidator::ScopedAttributionValidator(SourceAttributionChecker& checker)
    : checker_(checker), validation_start_(std::chrono::system_clock::now()) {
}

ScopedAttributionValidator::~ScopedAttributionValidator() {
    // Generate and save compliance report
    checker_.SaveComplianceReport("attribution_compliance_report.json");
}

void ScopedAttributionValidator::ValidateCurrentFile(const std::string& file_path) {
    checker_.ValidateFileAttribution(file_path);
    validated_files_.push_back(file_path);
}

void ScopedAttributionValidator::AutoAddMissingAttributions(const std::string& file_path, const std::vector<SourceAttribution>& attributions) {
    for (const auto& attribution : attributions) {
        checker_.AddAttribution(attribution);
    }
}

// AttributionTemplateGenerator implementation

SourceAttribution AttributionTemplateGenerator::CreateOriginTemplate(const std::string& origin_url, const std::string& author) {
    SourceAttribution attribution;
    attribution.type = AttributionType::ORIGIN;
    attribution.reference_url = origin_url;
    attribution.author_info = author;
    attribution.status = AttributionStatus::UNVERIFIED;
    return attribution;
}

SourceAttribution AttributionTemplateGenerator::CreateSotRefTemplate(const std::string& reference_url, const std::string& description) {
    SourceAttribution attribution;
    attribution.type = AttributionType::SOT_REF;
    attribution.reference_url = reference_url;
    attribution.tag_content = description;
    attribution.status = AttributionStatus::UNVERIFIED;
    return attribution;
}

SourceAttribution AttributionTemplateGenerator::CreateLicenseTemplate(const std::string& license_name, const std::string& license_url) {
    SourceAttribution attribution;
    attribution.type = AttributionType::LICENSE;
    attribution.license_info = license_name;
    attribution.reference_url = license_url;
    attribution.status = AttributionStatus::UNVERIFIED;
    return attribution;
}

SourceAttribution AttributionTemplateGenerator::CreateDerivedTemplate(const std::string& original_source, const std::string& modifications) {
    SourceAttribution attribution;
    attribution.type = AttributionType::DERIVED;
    attribution.reference_url = original_source;
    attribution.modification_summary = modifications;
    attribution.status = AttributionStatus::UNVERIFIED;
    return attribution;
}

SourceAttribution AttributionTemplateGenerator::CreateModifiedTemplate(const std::string& original_file, const std::string& modification_summary) {
    SourceAttribution attribution;
    attribution.type = AttributionType::MODIFIED;
    attribution.reference_url = original_file;
    attribution.modification_summary = modification_summary;
    attribution.status = AttributionStatus::UNVERIFIED;
    return attribution;
}

std::vector<SourceAttribution> AttributionTemplateGenerator::GenerateStandardAttributionSet(
    const std::string& origin_url,
    const std::string& license_name,
    const std::string& author) {

    std::vector<SourceAttribution> attributions;
    attributions.push_back(CreateOriginTemplate(origin_url, author));
    attributions.push_back(CreateLicenseTemplate(license_name));
    attributions.push_back(CreateAuthorTemplate(author)); // Assuming this exists
    return attributions;
}

// Factory function

std::unique_ptr<SourceAttributionChecker> CreateSourceAttributionChecker(
    const std::string& project_root,
    bool strict_mode) {

    auto checker = std::make_unique<SourceAttributionChecker>(project_root);
    checker->EnableStrictMode(strict_mode);
    return checker;
}

} // namespace puzzle71::gpu::performance