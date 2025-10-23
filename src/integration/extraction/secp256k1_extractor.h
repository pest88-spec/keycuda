// T020: Implement secp256k1-zkp Extraction
// Extracts and integrates secp256k1-zkp source code with full attribution

#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace integration {
namespace extraction {

struct ExtractionConfig {
    std::string source_path;
    std::string target_path;
    std::string source_url;
    bool include_attribution = true;
    bool verify_integrity = true;
    bool verify_extraction = true;
    std::string version = "";
    std::string license = "MIT";
    std::map<std::string, std::string> metadata;
};

class Secp256k1Extractor {
public:
    Secp256k1Extractor() = default;
    ~Secp256k1Extractor() = default;

    // Main extraction method
    bool extract(const ExtractionConfig& config);

    // Error handling
    const std::string& get_last_error() const { return last_error_; }

private:
    // Core extraction steps
    bool discover_files(const std::string& source_path, std::vector<std::string>& files);
    bool copy_file_with_attribution(const ExtractionConfig& config,
                                   const std::string& source_file,
                                   std::vector<std::string>& extracted_files);
    bool add_attribution_header(const std::string& filepath, const ExtractionConfig& config);
    bool generate_integrity_manifest(const std::string& target_path,
                                    const std::vector<std::string>& files);
    bool verify_extraction(const std::string& target_path,
                          const std::vector<std::string>& files);

    // Helper methods
    bool should_include_file(const std::string& file, const std::vector<std::string>& exclude_patterns);
    std::string execute_command(const std::string& cmd) const;
    std::string calculate_file_hash(const std::string& filepath) const;
    std::string get_file_timestamp(const std::string& filepath) const;
    std::string get_current_timestamp() const;

    // Member variables
    std::string source_path_;
    std::string last_error_;
};

// Global accessor
Secp256k1Extractor& get_secp256k1_extractor();

} // namespace extraction
} // namespace integration