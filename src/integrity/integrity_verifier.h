// T017: Comprehensive Integrity Verification Framework
// Core integrity verification utilities for integrated dependencies

#pragma once

#include <string>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>

namespace integration {
namespace integrity {

enum class HashAlgorithm {
    SHA256
};

struct HashInfo {
    std::string name;
    int hex_length;
    std::string description;
};

class IntegrityVerifier {
public:
    IntegrityVerifier();

    // Core verification methods
    bool verify_file_integrity(const std::string& filepath,
                              const std::string& expected_hash,
                              HashAlgorithm algorithm = HashAlgorithm::SHA256);

    bool verify_library_integrity(const std::string& library_path,
                                  const nlohmann::json& integrity_manifest);

    bool verify_cross_system_consistency(const std::vector<std::string>& paths);

    bool verify_attribution_integrity(const std::string& file_path);

    // Report generation
    nlohmann::json generate_integrity_report(const std::string& directory);

    // Accessors
    const std::string& get_last_error() const { return last_error_; }
    const std::vector<std::string>& get_verification_log() const { return verification_log_; }

private:
    std::string calculate_file_hash(const std::string& filepath,
                                     HashAlgorithm algorithm);
    std::string calculate_sha256(const std::string& filepath);

    std::vector<std::string> get_all_files(const std::string& directory);
    long long get_file_size(const std::string& filepath);
    std::string get_file_timestamp(const std::string& filepath);
    std::string execute_command(const std::string& cmd);

    void setup_hash_algorithms();
    std::string get_current_timestamp() const;

    std::map<std::string, HashInfo> hash_algorithms_;
    std::string last_error_;
    std::vector<std::string> verification_log_;
};

} // namespace integrity
} // namespace integration