/**
 * @file           digest_verifier.h
 * @brief          SHA-256 digest verification system for Puzzle71Solver
 * @author         Puzzle71Solver Team
 * @origin         https://github.com/Puzzle71Solver/Puzzle71Solver
 * @origin_path    src/integration/digest_verifier.h
 * @origin_commit  <current_commit>
 * @origin_license MIT
 * @extracted_date 2025-10-10
 * @extracted_by   Puzzle71Solver Team
 * @modifications  Created for third-party dependency integration optimization
 * @spdx_license_identifier MIT
 *
 * Provides cryptographic SHA-256 digest verification for integrated libraries
 * to ensure integrity, authenticity, and detect any modifications to source code.
 */

#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <chrono>
#include <optional>
#include <functional>
#include <nlohmann/json.hpp>

namespace puzzle71 {
namespace integration {

/**
 * @brief Digest verification algorithms
 */
enum class DigestAlgorithm {
    SHA256,         ///< SHA-256 (default)
    SHA512,         ///< SHA-512
    MD5,           ///< MD5 (legacy, not recommended)
    SHA1           ///< SHA-1 (legacy, not recommended)
};

/**
 * @brief Verification result status
 */
enum class VerificationStatus {
    Verified,       ///< Digest matches - integrity verified
    Failed,         ///< Digest mismatch - integrity compromised
    Error,          ///< Verification error occurred
    Pending,        ///< Verification pending
    NotApplicable   ///< Digest not applicable
};

/**
 * @brief File digest information
 */
struct FileDigest {
    std::string file_path;                           ///< Relative file path
    std::string digest;                              ///< Calculated digest
    std::string expected_digest;                     ///< Expected digest
    DigestAlgorithm algorithm;                       ///< Digest algorithm
    VerificationStatus status;                      ///< Verification status
    std::chrono::system_clock::time_point calculated_at; ///< When digest was calculated
    std::chrono::system_clock::time_point verified_at;   ///< When digest was verified
    size_t file_size_bytes;                         ///< File size in bytes
    std::string file_hash;                          ///< Additional file identifier

    /**
     * @brief Check if verification is successful
     * @return True if verified
     */
    bool is_verified() const { return status == VerificationStatus::Verified; }

    /**
     * @brief Convert to JSON representation
     * @return JSON object
     */
    nlohmann::json to_json() const;

    /**
     * @brief Create from JSON
     * @param j JSON object
     * @return FileDigest instance
     */
    static FileDigest from_json(const nlohmann::json& j);
};

/**
 * @brief Library digest manifest
 */
struct LibraryDigestManifest {
    std::string library_name;                        ///< Library identifier
    std::string version;                             ///< Library version
    std::string origin_commit;                       ///< Origin commit hash
    std::chrono::system_clock::time_point created_at; ///< Manifest creation time
    std::vector<FileDigest> file_digests;            ///< File digests
    std::map<std::string, std::string> metadata;      ///< Additional metadata
    DigestAlgorithm algorithm;                       ///< Default algorithm
    std::string manifest_signature;                  ///< Manifest signature
    bool is_comprehensive;                           ///< Complete library coverage

    /**
     * @brief Get verification statistics
     * @return Pair of (verified_count, total_count)
     */
    std::pair<size_t, size_t> get_verification_stats() const;

    /**
     * @brief Calculate overall verification percentage
     * @return Verification percentage (0-100)
     */
    double get_verification_percentage() const;

    /**
     * @brief Convert to JSON representation
     * @return JSON object
     */
    nlohmann::json to_json() const;

    /**
     * @brief Create from JSON
     * @param j JSON object
     * @return LibraryDigestManifest instance
     */
    static LibraryDigestManifest from_json(const nlohmann::json& j);
};

/**
 * @brief Digest verification policy
 */
struct VerificationPolicy {
    bool require_verification;                      ///< Require verification for all files
    bool allow_legacy_algorithms;                   ///< Allow MD5/SHA1 algorithms
    bool verify_on_access;                         ///< Verify when files are accessed
    bool continuous_monitoring;                    ///< Continuously monitor for changes
    std::chrono::seconds verification_interval;     ///< Re-verification interval
    std::vector<std::string> exclude_patterns;      ///< File patterns to exclude
    std::vector<std::string> critical_files;        ///< Critical files requiring strict verification
    double tolerance_threshold;                     ///< Verification tolerance threshold
    bool auto_recovery;                            ///< Attempt automatic recovery

    /**
     * @brief Default verification policy
     */
    static VerificationPolicy default_policy();

    /**
     * @brief Strict verification policy
     */
    static VerificationPolicy strict_policy();

    /**
     * @brief Development verification policy
     */
    static VerificationPolicy development_policy();

    /**
     * @brief Convert to JSON representation
     * @return JSON object
     */
    nlohmann::json to_json() const;

    * @brief Create from JSON
     * @param j JSON object
     * @return VerificationPolicy instance
     */
    static VerificationPolicy from_json(const nlohmann::json& j);
};

/**
 * @brief Digest verification result
 */
struct VerificationResult {
    bool overall_success;                          ///< Overall verification success
    size_t total_files;                           ///< Total files checked
    size_t verified_files;                        ///< Successfully verified files
    size_t failed_files;                          ///< Failed verification files
    size_t error_files;                           ///< Error files
    std::vector<FileDigest> failed_verifications;  ///< Detailed failure information
    std::vector<std::string> warning_messages;     ///< Warning messages
    std::chrono::milliseconds verification_time;   ///< Time taken for verification
    std::chrono::system_clock::time_point verified_at; ///< Verification timestamp

    /**
     * @brief Get success rate
     * @return Success percentage (0-100)
     */
    double get_success_rate() const;

    /**
     * @brief Check if verification is acceptable
     * @param tolerance Acceptable failure tolerance (0-1)
     * @return True if acceptable
     */
    bool is_acceptable(double tolerance = 0.0) const;

    /**
     * @brief Convert to JSON representation
     * @return JSON object
     */
    nlohmann::json to_json() const;

    /**
     * @brief Create from JSON
     * @param j JSON object
     * @return VerificationResult instance
     */
    static VerificationResult from_json(const nlohmann::json& j);
};

/**
 * @brief SHA-256 Digest Verifier
 *
 * Provides comprehensive digest verification capabilities for integrated libraries
 * with support for multiple algorithms, continuous monitoring, and integrity validation.
 */
class DigestVerifier {
public:
    /**
     * @brief Verification callback type
     */
    using VerificationCallback = std::function<void(const VerificationResult&)>;

    /**
     * @brief Get singleton instance
     * @return DigestVerifier instance
     */
    static DigestVerifier& instance();

    /**
     * @brief Initialize digest verifier
     * @param storage_path Path for digest storage
     * @param policy Verification policy
     * @return True if initialization successful
     */
    bool initialize(const std::string& storage_path,
                   const VerificationPolicy& policy = VerificationPolicy::default_policy());

    /**
     * @brief Set verification policy
     * @param policy New verification policy
     */
    void set_policy(const VerificationPolicy& policy);

    /**
     * @brief Get current verification policy
     * @return Current policy
     */
    const VerificationPolicy& get_policy() const;

    /**
     * @brief Calculate file digest
     * @param file_path Path to file
     * @param algorithm Digest algorithm
     * @return Calculated digest or empty if error
     */
    std::string calculate_digest(const std::string& file_path,
                                DigestAlgorithm algorithm = DigestAlgorithm::SHA256) const;

    /**
     * @brief Calculate directory digest manifest
     * @param directory_path Path to directory
     * @param library_name Library name
     * @param version Library version
     * @param origin_commit Origin commit hash
     * @param algorithm Digest algorithm
     * @param exclude_patterns File patterns to exclude
     * @return Digest manifest
     */
    LibraryDigestManifest calculate_directory_manifest(
        const std::string& directory_path,
        const std::string& library_name,
        const std::string& version,
        const std::string& origin_commit,
        DigestAlgorithm algorithm = DigestAlgorithm::SHA256,
        const std::vector<std::string>& exclude_patterns = {});

    /**
     * @brief Verify file digest
     * @param file_path Path to file
     * @param expected_digest Expected digest value
     * @param algorithm Digest algorithm
     * @return Verification result
     */
    VerificationStatus verify_file(const std::string& file_path,
                                  const std::string& expected_digest,
                                  DigestAlgorithm algorithm = DigestAlgorithm::SHA256);

    /**
     * @brief Verify library manifest
     * @param library_name Library name
     * @param manifest Digest manifest to verify
     * @return Verification result
     */
    VerificationResult verify_library(const std::string& library_name,
                                     const LibraryDigestManifest& manifest);

    /**
     * @brief Verify all registered libraries
     * @return Map of library names to verification results
     */
    std::map<std::string, VerificationResult> verify_all_libraries();

    /**
     * @brief Register library manifest
     * @param manifest Library digest manifest
     * @return True if registration successful
     */
    bool register_library(const LibraryDigestManifest& manifest);

    /**
     * @brief Unregister library
     * @param library_name Library name to unregister
     * @return True if unregistration successful
     */
    bool unregister_library(const std::string& library_name);

    /**
     * @brief Get library manifest
     * @param library_name Library name
     * @return Library manifest or empty if not found
     */
    std::optional<LibraryDigestManifest> get_library_manifest(const std::string& library_name) const;

    /**
     * @brief Get all registered libraries
     * @return Map of library names to manifests
     */
    std::map<std::string, LibraryDigestManifest> get_all_libraries() const;

    /**
     * @brief Check if library is registered
     * @param library_name Library name
     * @return True if registered
     */
    bool is_library_registered(const std::string& library_name) const;

    /**
     * @brief Update file digest in manifest
     * @param library_name Library name
     * @param file_path File path
     * @param new_digest New digest value
     * @return True if update successful
     */
    bool update_file_digest(const std::string& library_name,
                           const std::string& file_path,
                           const std::string& new_digest);

    /**
     * @brief Export manifest to file
     * @param library_name Library name
     * @param export_path Export file path
     * @param include_signature Include digital signature
     * @return True if export successful
     */
    bool export_manifest(const std::string& library_name,
                        const std::string& export_path,
                        bool include_signature = false);

    /**
     * @brief Import manifest from file
     * @param import_path Import file path
     * @param verify_signature Verify digital signature
     * @param library_name Library name (optional, extracted from manifest)
     * @return True if import successful
     */
    bool import_manifest(const std::string& import_path,
                        bool verify_signature = false,
                        const std::string& library_name = "");

    /**
     * @brief Get verification statistics
     * @return JSON statistics
     */
    nlohmann::json get_statistics() const;

    /**
     * @brief Generate integrity report
     * @param library_names Libraries to include (empty for all)
     * @param include_failed_details Include detailed failure information
     * @return Formatted report
     */
    std::string generate_integrity_report(const std::vector<std::string>& library_names = {},
                                         bool include_failed_details = true) const;

    /**
     * @brief Search for suspicious modifications
     * @param library_name Library to check (empty for all)
     * @param sensitivity Detection sensitivity (0.0-1.0)
     * @return Vector of suspicious files
     */
    std::vector<std::string> detect_suspicious_modifications(
        const std::string& library_name = "",
        double sensitivity = 0.8) const;

    /**
     * @brief Monitor file changes
     * @param library_name Library to monitor
     * @param callback Callback for change notifications
     * @return Monitor ID
     */
    size_t start_monitoring(const std::string& library_name,
                           std::function<void(const std::string&, VerificationStatus)> callback);

    /**
     * @brief Stop monitoring
     * @param monitor_id Monitor ID
     */
    void stop_monitoring(size_t monitor_id);

    /**
     * @brief Register verification callback
     * @param callback Callback function
     * @return Callback ID
     */
    size_t register_callback(VerificationCallback callback);

    /**
     * @brief Unregister verification callback
     * @param callback_id Callback ID
     */
    void unregister_callback(size_t callback_id);

    /**
     * @brief Perform integrity audit
     * @param audit_type Type of audit (full, incremental, targeted)
     * @param target_libraries Libraries to audit (empty for all)
     * @return Audit results
     */
    VerificationResult perform_integrity_audit(
        const std::string& audit_type = "full",
        const std::vector<std::string>& target_libraries = {});

    /**
     * @brief Recover from verification failures
     * @param library_name Library to recover
     * @param recovery_strategy Recovery approach
     * @return Recovery result
     */
    bool recover_integrity(const std::string& library_name,
                          const std::string& recovery_strategy = "auto");

private:
    DigestVerifier() = default;
    ~DigestVerifier() = default;
    DigestVerifier(const DigestVerifier&) = delete;
    DigestVerifier& operator=(const DigestVerifier&) = delete;

    mutable std::mutex mutex_;
    std::string storage_path_;
    VerificationPolicy policy_;
    std::map<std::string, LibraryDigestManifest> libraries_;
    std::map<size_t, VerificationCallback> callbacks_;
    std::map<size_t, std::function<void(const std::string&, VerificationStatus)>> monitors_;
    size_t next_callback_id_ = 1;
    size_t next_monitor_id_ = 1;

    std::string calculate_file_digest(const std::string& file_path,
                                    DigestAlgorithm algorithm) const;

    std::vector<std::string> find_files(const std::string& directory_path,
                                       const std::vector<std::string>& exclude_patterns) const;

    bool matches_exclude_pattern(const std::string& file_path,
                               const std::vector<std::string>& patterns) const;

    void notify_callbacks(const VerificationResult& result);
    void notify_monitors(const std::string& library_name, const std::string& file_path, VerificationStatus status);

    std::string algorithm_to_string(DigestAlgorithm algorithm) const;
    DigestAlgorithm string_to_algorithm(const std::string& algorithm_str) const;
    std::string status_to_string(VerificationStatus status) const;
    VerificationStatus string_to_status(const std::string& status_str) const;

    bool save_manifest(const LibraryDigestManifest& manifest) const;
    std::optional<LibraryDigestManifest> load_manifest(const std::string& library_name) const;

    VerificationResult verify_manifest_integrity(const LibraryDigestManifest& manifest) const;
    std::vector<std::string> get_manifest_files(const std::string& library_name) const;
};

/**
 * @brief RAII Digest Verification Scope
 */
class DigestVerificationScope {
private:
    DigestVerifier& verifier_;
    std::string library_name_;
    bool verification_passed_;
    std::vector<std::string> modified_files_;

public:
    explicit DigestVerificationScope(DigestVerifier& verifier,
                                   const std::string& library_name);
    ~DigestVerificationScope();

    /**
     * @brief Verify current state
     * @return True if verification passed
     */
    bool verify_current_state();

    /**
     * @brief Get modified files
     * @return Vector of modified file paths
     */
    const std::vector<std::string>& get_modified_files() const { return modified_files_; }

    /**
     * @brief Check if verification passed
     * @return True if passed
     */
    bool verification_passed() const { return verification_passed_; }

    /**
     * @brief Commit verification results
     */
    void commit();

    /**
     * @brief Rollback verification results
     */
    void rollback();
};

/**
 * @brief Digest verification utility functions
 */
namespace DigestUtils {
    /**
     * @brief Calculate digest from string data
     * @param data String data
     * @param algorithm Digest algorithm
     * @return Calculated digest
     */
    std::string calculate_string_digest(const std::string& data,
                                      DigestAlgorithm algorithm = DigestAlgorithm::SHA256);

    /**
     * @brief Compare two digests
     * @param digest1 First digest
     * @param digest2 Second digest
     * @return True if digests match
     */
    bool compare_digests(const std::string& digest1, const std::string& digest2);

    /**
     * @brief Format digest for display
     * @param digest Digest value
     * @param format Format type (hex, base64)
     * @return Formatted digest
     */
    std::string format_digest(const std::string& digest, const std::string& format = "hex");

    /**
     * @brief Validate digest format
     * @param digest Digest to validate
     * @param algorithm Expected algorithm
     * @return True if format is valid
     */
    bool validate_digest_format(const std::string& digest,
                               DigestAlgorithm algorithm = DigestAlgorithm::SHA256);

    /**
     * @brief Generate random digest for testing
     * @param algorithm Digest algorithm
     * @return Random digest
     */
    std::string generate_test_digest(DigestAlgorithm algorithm = DigestAlgorithm::SHA256);
}

} // namespace integration
} // namespace puzzle71