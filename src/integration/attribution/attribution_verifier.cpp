// T005: Attribution Verification Testing Framework
// Verifies that all integrated third-party code has proper attribution

#include "attribution_verifier.h"
#include <fstream>
#include <regex>

namespace integration {
namespace attribution {

AttributionVerifier::AttributionVerifier() {
    setup_patterns();
}

VerificationResult AttributionVerifier::verify_file(const std::string& filepath) {
    VerificationResult result;
    result.file_path = filepath;
    result.passed = false;

    std::ifstream file(filepath);
    if (!file.is_open()) {
        result.error = "Cannot open file";
        return result;
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    file.close();

    // Check for SPDX license identifier
    result.has_spdx = std::regex_search(content, spdx_pattern_);
    result.spdx_text = extract_spdx_text(content);

    // Check for copyright notice
    result.has_copyright = std::regex_search(content, copyright_pattern_);
    result.copyright_text = extract_copyright_text(content);

    // Check for origin reference
    result.has_origin = std::regex_search(content, origin_pattern_);
    result.origin_text = extract_origin_text(content);

    // Check for required fields
    result.missing_fields = determine_missing_fields(result);

    // Overall verification passes if all required fields are present
    result.passed = result.missing_fields.empty();

    return result;
}

VerificationResult AttributionVerifier::verify_directory(const std::string& dirpath) {
    VerificationResult result;
    result.file_path = dirpath;
    result.passed = true;

    // Find all source files
    std::vector<std::string> source_files;
    find_source_files(dirpath, source_files);

    int total_files = source_files.size();
    int passed_files = 0;
    std::vector<std::string> failed_files;

    for (const auto& file : source_files) {
        VerificationResult file_result = verify_file(file);
        if (file_result.passed) {
            passed_files++;
        } else {
            failed_files.push_back(file);
            result.details.push_back(file + ": " +
                                    join_strings(file_result.missing_fields, ", "));
        }
    }

    result.total_files = total_files;
    result.passed_files = passed_files;
    result.compliance_percentage = total_files > 0 ?
        static_cast<double>(passed_files) / total_files * 100.0 : 0.0;

    // Pass if 100% compliance
    result.passed = (result.compliance_percentage >= 100.0);

    if (!result.passed) {
        result.error = std::to_string(failed_files.size()) +
                     " files failed attribution verification";
    }

    return result;
}

void AttributionVerifier::generate_attribution_report(const std::string& output_file) {
    nlohmann::json report;
    report["generated"] = get_current_timestamp();
    report["verifier_version"] = "1.0.0";
    report["libraries"] = nlohmann::json::array();

    // Verify extracted libraries
    std::vector<std::string> libraries = {"secp256k1-zkp", "bitcrack"};

    for (const auto& lib : libraries) {
        std::string lib_path = "src/extracted/" + lib;
        VerificationResult result = verify_directory(lib_path);

        nlohmann::json lib_report;
        lib_report["name"] = lib;
        lib_report["path"] = lib_path;
        lib_report["compliance_percentage"] = result.compliance_percentage;
        lib_report["total_files"] = result.total_files;
        lib_report["passed_files"] = result.passed_files;
        lib_report["passed"] = result.passed;

        if (!result.details.empty()) {
            lib_report["issues"] = result.details;
        }

        report["libraries"].push_back(lib_report);
    }

    // Save report
    std::ofstream file(output_file);
    if (file.is_open()) {
        file << report.dump(2) << std::endl;
        file.close();
    }
}

std::string AttributionVerifier::extract_spdx_text(const std::string& content) {
    std::smatch match;
    if (std::regex_search(content, match, spdx_pattern_)) {
        return match[0].str();
    }
    return "";
}

std::string AttributionVerifier::extract_copyright_text(const std::string& content) {
    std::smatch match;
    if (std::regex_search(content, match, copyright_pattern_)) {
        return match[0].str();
    }
    return "";
}

std::string AttributionVerifier::extract_origin_text(const std::string& content) {
    std::smatch match;
    if (std::regex_search(content, match, origin_pattern_)) {
        return match[0].str();
    }
    return "";
}

std::vector<std::string> AttributionVerifier::determine_missing_fields(
    const VerificationResult& result) {
    std::vector<std::string> missing;

    if (!result.has_spdx) {
        missing.push_back("SPDX license identifier");
    }
    if (!result.has_copyright) {
        missing.push_back("Copyright notice");
    }
    if (!result.has_origin) {
        missing.push_back("Origin reference");
    }

    return missing;
}

void AttributionVerifier::find_source_files(const std::string& dir,
                                           std::vector<std::string>& files) {
    // This is a simplified implementation
    // In production, use filesystem library for recursive search
    std::string cmd = "find " + dir + " -type f \\( -name \"*.h\" -o -name \"*.hpp\" -o -name \"*.c\" -o -name \"*.cpp\" -o -name \"*.cc\" \\)";
    std::string result = execute_command(cmd);

    std::stringstream ss(result);
    std::string file;
    while (std::getline(ss, file)) {
        if (!file.empty()) {
            files.push_back(file);
        }
    }
}

std::string AttributionVerifier::execute_command(const std::string& cmd) {
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

std::string AttributionVerifier::join_strings(const std::vector<std::string>& strings,
                                             const std::string& delimiter) {
    if (strings.empty()) return "";

    std::string result = strings[0];
    for (size_t i = 1; i < strings.size(); ++i) {
        result += delimiter + strings[i];
    }
    return result;
}

void AttributionVerifier::setup_patterns() {
    // SPDX pattern: SPDX-License-Identifier: MIT OR Apache-2.0
    spdx_pattern_ = std::regex(R"(SPDX-License-Identifier:\s*[^\s\n]+)");

    // Copyright pattern: Copyright (c) YYYY Name
    copyright_pattern_ = std::regex(R"(Copyright\s+\(c\)\s+\d{4}.*|©\s+\d{4}.*)",
                                 std::regex_constants::icase);

    // Origin pattern: @origin: or @sot_ref:
    origin_pattern_ = std::regex(R"(@origin:\s*\S+|@sot_ref:\s*\S+)");
}

std::string AttributionVerifier::get_current_timestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

} // namespace attribution
} // namespace integration