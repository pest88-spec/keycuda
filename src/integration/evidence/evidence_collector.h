// T007: Integration Evidence Collection and Verification System
// Collects and maintains evidence of integration operations

#pragma once

#include <string>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>

namespace integration {
namespace evidence {

class EvidenceCollector {
public:
    explicit EvidenceCollector(const std::string& output_dir = "build/evidence");

    // Recording methods
    void record_extraction(const std::string& library_name,
                         const std::string& source_path,
                         const std::string& target_path,
                         const std::vector<std::string>& files_copied);

    void record_attribution_added(const std::string& file_path,
                                 const std::string& attribution_type,
                                 const std::string& content);

    void record_integrity_check(const std::string& file_path,
                               const std::string& expected_hash,
                               const std::string& actual_hash,
                               bool passed);

    void record_build_integration(const std::string& library_name,
                                 const std::string& build_command,
                                 bool success);

    // Verification methods
    bool verify_integration_evidence(const std::string& library_name) const;

    // Report generation
    void generate_evidence_report(const std::string& output_file) const;

    // Configuration
    void set_output_directory(const std::string& dir) { output_dir_ = dir; }

private:
    nlohmann::json generate_summary() const;
    std::string calculate_file_hash(const std::string& filepath) const;
    std::string calculate_string_hash(const std::string& str) const;

    void create_evidence_log();
    void save_evidence(const nlohmann::json& evidence);
    std::string get_current_timestamp() const;

    std::string output_dir_;
};

} // namespace evidence
} // namespace integration