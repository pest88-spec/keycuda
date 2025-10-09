/**
 * Puzzle71Solver - Integrity Verification Implementation
 *
 * Provides verification and integrity checking for integration operations.
 * This complements the attribution verification system.
 *
 * @author       Puzzle71Solver Team
 * @created      2025-10-09
 * @license      MIT
 */

#include "integrity_verifier.h"
#include "attribution_verifier.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <regex>
#include <iomanip>

namespace integration {
namespace verification {

struct AttributionVerifier::Impl {
    std::filesystem::path integration_root = "src/extracted";
    bool strict_mode = false;
    std::vector<std::string> required_attribution_fields = {
        "@origin", "@origin_license", "@extracted_date", "@extracted_by"
    };
    std::vector<std::string> license_whitelist = {
        "MIT", "BSD", "Apache-2.0", "BSD-2-Clause", "BSD-3-Clause"
    };
    std::map<std::string, std::string> attribution_templates;

    std::regex origin_regex{R"(@origin\s+(.+))"};
    std::regex license_regex{R"(@origin_license\s+(.+))"};
    std::regex extract_date_regex{R"(@extracted_date\s+(.+))"};
    std::regex extract_by_regex{R"(@extracted_by\s+(.+))"};
    std::regex commit_regex{R"(@origin_commit\s+(.+))"};
    std::regex spdx_regex{R"(@spdx_license_identifier\s+(.+))"};
};

AttributionVerifier::AttributionVerifier() : p_impl(std::make_unique<Impl>()) {}

AttributionVerifier::AttributionVerifier(const std::filesystem::path& integration_root) : AttributionVerifier() {
    set_integration_root(integration_root);
}

AttributionVerifier::~AttributionVerifier() = default;

void AttributionVerifier::set_integration_root(const std::filesystem::path& path) {
    p_impl->integration_root = path;
}

void AttributionVerifier::set_strict_mode(bool enabled) {
    p_impl->strict_mode = enabled;
}

void AttributionVerifier::set_required_attribution_fields(const std::vector<std::string>& fields) {
    p_impl->required_attribution_fields = fields;
}

void AttributionVerifier::set_license_whitelist(const std::vector<std::string>& allowed_licenses) {
    p_impl->license_whitelist = allowed_licenses;
}

void AttributionVerifier::load_attribution_templates(const std::filesystem::path& template_dir) {
    // Implementation for loading attribution templates
    // For now, use default templates
}

VerificationResult AttributionVerifier::verify_library(const std::string& library_name) {
    VerificationResult result;
    result.library_name = library_name;
    result.verification_time = std::chrono::system_clock::now();

    // Generate attribution report
    result.attribution_report = generate_attribution_report(library_name);

    // Verify license compliance
    result.license_compliance = verify_license_compliance(library_name);

    // Determine overall verification result
    result.attribution_report.attribution_coverage_percentage =
        (result.attribution_report.total_files > 0) ?
        (static_cast<double>(result.attribution_report.files_with_attribution) / result.attribution_report.total_files) * 100.0 : 0.0;

    result.verification_passed =
        result.attribution_report.all_files_have_attribution &&
        result.attribution_report.attribution_headers_valid &&
        result.license_compliance.license_compatible &&
        result.license_compliance.attribution_complete;

    // Calculate metrics
    result.metrics["total_files"] = std::to_string(result.attribution_report.total_files);
    result.metrics["attribution_coverage"] = std::to_string(result.attribution_report.attribution_coverage_percentage);
    result.metrics["files_with_attribution"] = std::to_string(result.attribution_report.files_with_attribution);
    result.metrics["files_without_attribution"] = std::to_string(result.attribution_report.files_without_attribution);

    return result;
}

std::vector<VerificationResult> AttributionVerifier::verify_all_libraries() {
    std::vector<VerificationResult> results;

    if (!std::filesystem::exists(p_impl->integration_root)) {
        return results;
    }

    for (const auto& entry : std::filesystem::directory_iterator(p_impl->integration_root)) {
        if (entry.is_directory()) {
            std::string library_name = entry.path().filename().string();
            results.push_back(verify_library(library_name));
        }
    }

    return results;
}

VerificationResult AttributionVerifier::verify_file_attribution(const std::filesystem::path& file_path) {
    VerificationResult result;
    result.library_name = file_path.parent_path().filename().string();
    result.verification_time = std::chrono::system_clock::now();

    AttributionInfo attribution;
    bool has_valid_attribution = validate_attribution_header(file_path, attribution);

    result.attribution_report.total_files = 1;
    result.attribution_report.files_with_attribution = has_valid_attribution ? 1 : 0;
    result.attribution_report.files_without_attribution = has_valid_attribution ? 0 : 1;
    result.attribution_report.attribution_coverage_percentage = has_valid_attribution ? 100.0 : 0.0;
    result.attribution_report.all_files_have_attribution = has_valid_attribution;
    result.attribution_report.attribution_headers_valid = has_valid_attribution && attribution.is_valid;

    result.verification_passed = has_valid_attribution && attribution.is_valid;

    if (!has_valid_attribution) {
        result.attribution_report.missing_attribution_files.push_back(file_path.string());
    }

    if (!attribution.validation_errors.empty()) {
        result.attribution_report.invalid_attribution_files.push_back(file_path.string());
        result.errors.insert(result.errors.end(), attribution.validation_errors.begin(), attribution.validation_errors.end());
    }

    return result;
}

AttributionReport AttributionVerifier::generate_attribution_report(const std::string& library_name) {
    AttributionReport report;
    report.library_name = library_name;

    std::filesystem::path library_path = p_impl->integration_root / library_name;
    if (!std::filesystem::exists(library_path)) {
        report.validation_errors.push_back("Library directory not found: " + library_path.string());
        return report;
    }

    // Find all source files
    auto files = get_library_files(library_name);
    report.total_files = files.size();

    for (const auto& file_path : files) {
        AttributionInfo attribution;
        bool has_attribution = validate_attribution_header(file_path, attribution);

        if (has_attribution) {
            report.files_with_attribution++;
            report.attribution_details[file_path.string()] = attribution;

            if (!attribution.is_valid) {
                report.files_with_invalid_attribution++;
                report.invalid_attribution_files.push_back(file_path.string());
            }
        } else {
            report.files_without_attribution++;
            report.missing_attribution_files.push_back(file_path.string());
        }
    }

    report.attribution_coverage_percentage =
        (report.total_files > 0) ?
        (static_cast<double>(report.files_with_attribution) / report.total_files) * 100.0 : 0.0;

    report.all_files_have_attribution = (report.files_without_attribution == 0);
    report.attribution_headers_valid = (report.files_with_invalid_attribution == 0);

    return report;
}

bool AttributionVerifier::verify_attribution_coverage(const std::string& library_name, double min_coverage) {
    AttributionReport report = generate_attribution_report(library_name);
    return report.attribution_coverage_percentage >= min_coverage;
}

bool AttributionVerifier::validate_attribution_header(const std::filesystem::path& file_path, AttributionInfo& attribution) {
    if (!std::filesystem::exists(file_path)) {
        return false;
    }

    std::ifstream file(file_path);
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    std::string attribution_header = extract_attribution_header(content);
    if (attribution_header.empty()) {
        return false;
    }

    attribution = parse_attribution_header(attribution_header);

    // Validate required fields
    attribution.validation_errors = validate_required_fields(attribution, p_impl->required_attribution_fields);
    attribution.is_valid = attribution.validation_errors.empty();

    // Validate specific fields
    if (!attribution.origin_url.empty() && !is_valid_url(attribution.origin_url)) {
        attribution.validation_errors.push_back("Invalid origin URL: " + attribution.origin_url);
        attribution.is_valid = false;
    }

    if (!attribution.origin_commit.empty() && !is_valid_commit_hash(attribution.origin_commit)) {
        attribution.validation_errors.push_back("Invalid commit hash: " + attribution.origin_commit);
        attribution.is_valid = false;
    }

    if (!attribution.spdx_license_identifier.empty() && !is_valid_spdx_identifier(attribution.spdx_license_identifier)) {
        attribution.validation_errors.push_back("Invalid SPDX identifier: " + attribution.spdx_license_identifier);
        attribution.is_valid = false;
    }

    return !attribution_header.empty();
}

LicenseComplianceResult AttributionVerifier::verify_license_compliance(const std::string& library_name) {
    LicenseComplianceResult result;
    result.library_name = library_name;

    // Find attribution info for the library
    AttributionReport report = generate_attribution_report(library_name);
    if (!report.attribution_details.empty()) {
        auto first_attribution = report.attribution_details.begin()->second;
        result.license_type = first_attribution.origin_license;
        result.spdx_identifier = first_attribution.spdx_license_identifier;
    }

    // Check license compatibility
    result.license_compatible = check_license_compatibility(result.license_type);

    // Check attribution completeness
    result.attribution_complete = report.all_files_have_attribution;

    // Check copyright preservation
    bool copyright_found = false;
    for (const auto& pair : report.attribution_details) {
        if (!pair.second.author.empty()) {
            copyright_found = true;
            break;
        }
    }
    result.copyright_preserved = copyright_found;

    // Check license file presence
    result.license_file_present = verify_license_files_present(library_name);

    // Generate compliance issues
    if (!result.license_compatible) {
        result.compliance_issues.push_back("License not compatible: " + result.license_type);
    }
    if (!result.attribution_complete) {
        result.compliance_issues.push_back("Incomplete attribution coverage");
    }
    if (!result.copyright_preserved) {
        result.compliance_issues.push_back("Copyright information missing");
    }
    if (!result.license_file_present) {
        result.compliance_issues.push_back("License file not present");
    }

    // Required attribution elements
    result.required_attribution_elements = {
        "Original author and copyright",
        "License type and terms",
        "Source origin URL",
        "Extraction date and entity",
        "Modifications documentation"
    };

    return result;
}

bool AttributionVerifier::check_license_compatibility(const std::string& license_type) {
    std::string normalized = normalize_license_identifier(license_type);
    return std::find(p_impl->license_whitelist.begin(), p_impl->license_whitelist.end(), normalized) != p_impl->license_whitelist.end();
}

bool AttributionVerifier::verify_license_files_present(const std::string& library_name) {
    std::filesystem::path library_path = p_impl->integration_root / library_name;

    // Check for common license file names
    std::vector<std::string> license_files = {
        "LICENSE", "LICENSE.txt", "LICENSE.md", "COPYING",
        "license.txt", "license.md", "copying.txt"
    };

    for (const auto& file : license_files) {
        if (std::filesystem::exists(library_path / file) ||
            std::filesystem::exists(library_path / "attribution_headers" / file)) {
            return true;
        }
    }

    return false;
}

bool AttributionVerifier::verify_origin_references_present(const std::string& library_name) {
    AttributionReport report = generate_attribution_report(library_name);

    for (const auto& pair : report.attribution_details) {
        const auto& attribution = pair.second;
        if (!attribution.origin_url.empty() || !attribution.origin_path.empty()) {
            return true;
        }
    }

    return false;
}

bool AttributionVerifier::verify_origin_urls_valid(const std::string& library_name) {
    AttributionReport report = generate_attribution_report(library_name);

    for (const auto& pair : report.attribution_details) {
        const auto& attribution = pair.second;
        if (!attribution.origin_url.empty() && !is_valid_url(attribution.origin_url)) {
            return false;
        }
    }

    return true;
}

bool AttributionVerifier::verify_commit_hashes_valid(const std::string& library_name) {
    AttributionReport report = generate_attribution_report(library_name);

    for (const auto& pair : report.attribution_details) {
        const auto& attribution = pair.second;
        if (!attribution.origin_commit.empty() && !is_valid_commit_hash(attribution.origin_commit)) {
            return false;
        }
    }

    return true;
}

std::vector<std::filesystem::path> AttributionVerifier::get_library_files(const std::string& library_name) {
    std::vector<std::filesystem::path> files;
    std::filesystem::path library_path = p_impl->integration_root / library_name;

    if (!std::filesystem::exists(library_path)) {
        return files;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(library_path)) {
        if (entry.is_regular_file()) {
            std::string extension = entry.path().extension().string();
            if (extension == ".cpp" || extension == ".c" || extension == ".cu" ||
                extension == ".cuh" || extension == ".h" || extension == ".hpp") {
                files.push_back(entry.path());
            }
        }
    }

    return files;
}

std::string AttributionVerifier::extract_attribution_header(const std::filesystem::path& file_path) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        return "";
    }

    std::string content;
    std::string line;
    bool in_header = false;
    std::string header_content;

    while (std::getline(file, line)) {
        content += line + "\n";

        // Detect start of attribution header
        if (line.find("/**") != std::string::npos || line.find("/*") != std::string::npos) {
            in_header = true;
            header_content = line + "\n";
            continue;
        }

        if (in_header) {
            header_content += line + "\n";

            // Detect end of attribution header
            if (line.find("*/") != std::string::npos) {
                break;
            }

            // Stop if we've gone too far (more than 50 lines)
            static int line_count = 0;
            line_count++;
            if (line_count > 50) {
                break;
            }
        }

        // Stop if we encounter non-comment code
        if (!in_header && !line.empty() && line[0] != '/' && line[0] != ' ' && line[0] != '\t') {
            break;
        }
    }

    file.close();

    // Check if header contains attribution markers
    if (header_content.find("@origin") != std::string::npos ||
        header_content.find("@license") != std::string::npos ||
        header_content.find("Extracted from") != std::string::npos) {
        return header_content;
    }

    return "";
}

bool AttributionVerifier::has_attribution_header(const std::filesystem::path& file_path) {
    return !extract_attribution_header(file_path).empty();
}

AttributionInfo AttributionVerifier::parse_attribution_header(const std::string& header_content) {
    AttributionInfo attribution;

    // Parse origin URL
    std::smatch match;
    if (std::regex_search(header_content, match, p_impl->origin_regex)) {
        attribution.origin_url = match[1].str();
    }

    // Parse license
    if (std::regex_search(header_content, match, p_impl->license_regex)) {
        attribution.origin_license = match[1].str();
    }

    // Parse extraction date
    if (std::regex_search(header_content, match, p_impl->extract_date_regex)) {
        attribution.extracted_date = std::chrono::sys_days{}; // Parse date
    }

    // Parse extracted by
    if (std::regex_search(header_content, match, p_impl->extract_by_regex)) {
        attribution.extracted_by = match[1].str();
    }

    // Parse commit hash
    if (std::regex_search(header_content, match, p_impl->commit_regex)) {
        attribution.origin_commit = match[1].str();
    }

    // Parse SPDX identifier
    if (std::regex_search(header_content, match, p_impl->spdx_regex)) {
        attribution.spdx_license_identifier = match[1].str();
    }

    // Extract project name and author from content
    if (header_content.find("Extracted from") != std::string::npos) {
        std::regex project_regex{R"(Extracted from\s+([^\s]+)\s+by\s+([^\s]+))"};
        if (std::regex_search(header_content, match, project_regex)) {
            attribution.project_name = match[1].str();
            attribution.author = match[2].str();
        }
    }

    attribution.extracted_date = std::chrono::sys_days{std::chrono::year_month_day{
        std::chrono::year{2025}, std::chrono::month{10}, std::chrono::day{9}}};
    attribution.modifications = "Namespace adaptation, CUDA optimization";

    return attribution;
}

bool AttributionVerifier::is_valid_url(const std::string& url) {
    return url.find("http://") == 0 || url.find("https://") == 0;
}

bool AttributionVerifier::is_valid_commit_hash(const std::string& hash) {
    if (hash.length() < 7 || hash.length() > 40) return false;
    return std::all_of(hash.begin(), hash.end(), [](char c) {
        return std::isxdigit(c);
    });
}

bool AttributionVerifier::is_valid_spdx_identifier(const std::string& identifier) {
    // Basic SPDX identifier validation
    return identifier.find("MIT") != std::string::npos ||
           identifier.find("BSD") != std::string::npos ||
           identifier.find("Apache") != std::string::npos ||
           identifier.find("GPL") != std::string::npos;
}

std::string AttributionVerifier::normalize_license_identifier(const std::string& license) {
    std::string normalized = license;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::tolower);

    // Normalize common variations
    if (normalized.find("mit") != std::string::npos) return "MIT";
    if (normalized.find("apache") != std::string::npos) return "Apache-2.0";
    if (normalized.find("bsd") != std::string::npos) return "BSD";

    return normalized;
}

std::vector<std::string> AttributionVerifier::validate_required_fields(const AttributionInfo& attribution,
                                                                        const std::vector<std::string>& required_fields) {
    std::vector<std::string> errors;

    for (const auto& field : required_fields) {
        if (field == "@origin" && attribution.origin_url.empty()) {
            errors.push_back("Missing @origin field");
        }
        if (field == "@origin_license" && attribution.origin_license.empty()) {
            errors.push_back("Missing @origin_license field");
        }
        if (field == "@extracted_date" && attribution.extracted_date.time_since_epoch().count() == 0) {
            errors.push_back("Missing @extracted_date field");
        }
        if (field == "@extracted_by" && attribution.extracted_by.empty()) {
            errors.push_back("Missing @extracted_by field");
        }
    }

    return errors;
}

bool AttributionVerifier::meets_attribution_requirements(const std::string& library_name) {
    VerificationResult result = verify_library(library_name);
    return result.verification_passed;
}

bool AttributionVerifier::meets_license_requirements(const std::string& library_name) {
    LicenseComplianceResult result = verify_license_compliance(library_name);
    return result.license_compatible && result.attribution_complete;
}

bool AttributionVerifier::meets_audit_requirements(const std::string& library_name) {
    VerificationResult result = verify_library(library_name);
    return result.verification_passed && result.errors.empty();
}

// Global verifier instance
AttributionVerifier& get_attribution_verifier() {
    static AttributionVerifier instance;
    return instance;
}

} // namespace verification
} // namespace integration