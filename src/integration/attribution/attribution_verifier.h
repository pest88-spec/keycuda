// T005: Attribution Verification Testing Framework
// Verifies that all integrated third-party code has proper attribution

#pragma once

#include <string>
#include <vector>
#include <regex>
#include <nlohmann/json.hpp>

namespace integration {
namespace attribution {

struct VerificationResult {
    std::string file_path;
    bool passed = false;
    bool has_spdx = false;
    bool has_copyright = false;
    bool has_origin = false;
    std::string spdx_text;
    std::string copyright_text;
    std::string origin_text;
    std::vector<std::string> missing_fields;
    std::string error;
    std::vector<std::string> details;

    // For directory verification
    int total_files = 0;
    int passed_files = 0;
    double compliance_percentage = 0.0;
};

class AttributionVerifier {
public:
    AttributionVerifier();

    // Single file verification
    VerificationResult verify_file(const std::string& filepath);

    // Directory verification (recursive)
    VerificationResult verify_directory(const std::string& dirpath);

    // Report generation
    void generate_attribution_report(const std::string& output_file);

    // Configuration
    void set_source_extensions(const std::vector<std::string>& extensions) {
        source_extensions_ = extensions;
    }

private:
    std::string extract_spdx_text(const std::string& content);
    std::string extract_copyright_text(const std::string& content);
    std::string extract_origin_text(const std::string& content);
    std::vector<std::string> determine_missing_fields(const VerificationResult& result);

    void find_source_files(const std::string& dir, std::vector<std::string>& files);
    std::string execute_command(const std::string& cmd);
    std::string join_strings(const std::vector<std::string>& strings,
                           const std::string& delimiter);

    void setup_patterns();
    std::string get_current_timestamp() const;

    std::regex spdx_pattern_;
    std::regex copyright_pattern_;
    std::regex origin_pattern_;
    std::vector<std::string> source_extensions_ = {".h", ".hpp", ".c", ".cpp", ".cc"};
};

} // namespace attribution
} // namespace integration