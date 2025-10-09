/**
 * Puzzle71Solver - SHA-256 Digest Verification System
 *
 * Provides cryptographic integrity verification for all integrated artifacts
 * using SHA-256 digests to ensure source code integrity and detect unauthorized modifications.
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
#include <openssl/sha.h>

namespace integration {
namespace verification {

struct FileDigest {
    std::string file_path;
    std::string sha256_digest;
    size_t file_size = 0;
    std::chrono::system_clock::time_point last_modified;
    bool is_verified = false;
    std::string verification_status;
};

struct LibraryDigestManifest {
    std::string library_name;
    std::string library_version;
    std::string origin_commit;
    std::vector<FileDigest> file_digests;
    std::map<std::string, std::string> metadata;
    std::chrono::system_clock::time_point created_at;
    std::string manifest_signature;
};

struct DigestVerificationReport {
    std::string library_name;
    size_t total_files = 0;
    size_t verified_files = 0;
    size_t failed_files = 0;
    size_t missing_files = 0;
    size_t modified_files = 0;
    bool all_files_verified = false;
    std::vector<FileDigest> verification_results;
    std::vector<std::string> integrity_violations;
    std::chrono::milliseconds verification_time{0};
    std::map<std::string, std::string> verification_metadata;
};

struct DigestCache {
    std::map<std::string, std::string> digest_cache;
    std::chrono::system_clock::time_point cache_timestamp;
    size_t cache_hits = 0;
    size_t cache_misses = 0;
};

class DigestVerifier {
public:
    DigestVerifier();
    explicit DigestVerifier(const std::filesystem::path& digest_store_path);
    ~DigestVerifier();

    // Configuration
    void set_digest_store_path(const std::filesystem::path& store_path);
    void enable_caching(bool enabled);
    void set_cache_expiry_duration(std::chrono::hours duration);
    void enable_strict_verification(bool enabled);

    // Core digest operations
    std::string calculate_file_digest(const std::filesystem::path& file_path);
    std::string calculate_content_digest(const std::string& content);
    std::string calculate_directory_digest(const std::filesystem::path& directory_path);

    // Manifest management
    bool create_library_manifest(const std::string& library_name, const std::filesystem::path& library_path,
                                 const std::string& library_version = "", const std::string& origin_commit = "");
    bool load_library_manifest(const std::string& library_name, LibraryDigestManifest& manifest);
    bool save_library_manifest(const LibraryDigestManifest& manifest);
    bool update_library_manifest(const std::string& library_name, const std::filesystem::path& library_path);

    // Verification operations
    DigestVerificationReport verify_library_integrity(const std::string& library_name);
    bool verify_file_integrity(const std::filesystem::path& file_path, const std::string& expected_digest);
    bool verify_content_integrity(const std::string& content, const std::string& expected_digest);
    std::vector<std::string> verify_multiple_libraries(const std::vector<std::string>& library_names);

    // Batch operations
    bool create_all_library_manifests(const std::filesystem::path& integration_root);
    DigestVerificationReport verify_all_libraries();
    bool update_digest_database();

    // Cache management
    void clear_cache();
    size_t get_cache_size() const;
    std::map<std::string, std::string> get_cache_statistics() const;

    // Integrity analysis
    std::vector<std::string> detect_modifications(const std::string& library_name);
    std::vector<std::string> detect_added_files(const std::string& library_name);
    std::vector<std::string> detect_removed_files(const std::string& library_name);
    bool is_file_modified_since_verification(const std::filesystem::path& file_path);

    // Export and import
    bool export_digest_database(const std::filesystem::path& export_path);
    bool import_digest_database(const std::filesystem::path& import_path);
    std::string export_verification_report_json(const DigestVerificationReport& report);

    // Audit and compliance
    bool generate_audit_trail(const std::string& library_name, const std::filesystem::path& output_path);
    std::vector<std::string> get_verification_history(const std::string& library_name);
    bool meets_integrity_requirements(const std::string& library_name);

    // Performance monitoring
    std::chrono::milliseconds get_average_verification_time() const;
    size_t get_total_files_verified() const;
    double get_verification_success_rate() const;

    // Security features
    bool sign_manifest(LibraryDigestManifest& manifest, const std::string& private_key_path);
    bool verify_manifest_signature(const LibraryDigestManifest& manifest, const std::string& public_key_path);
    std::string generate_fingerprint(const std::string& data);

    // Utility methods
    std::vector<std::filesystem::path> get_library_files(const std::string& library_name);
    std::map<std::string, LibraryDigestManifest> get_all_manifests() const;
    bool has_manifest(const std::string& library_name) const;
    std::string get_digest_algorithm() const { return "SHA-256"; }

private:
    struct Impl;
    std::unique_ptr<Impl> p_impl;

    // Helper methods
    std::string compute_sha256(const std::vector<uint8_t>& data);
    std::vector<uint8_t> read_file_bytes(const std::filesystem::path& file_path);
    std::string get_cache_key(const std::filesystem::path& file_path, size_t file_size,
                             const std::chrono::system_clock::time_point& last_modified);
    bool is_cache_valid(const std::string& cache_key) const;
    void update_cache(const std::string& cache_key, const std::string& digest);
    std::string get_cached_digest(const std::string& cache_key);
    std::filesystem::path get_manifest_path(const std::string& library_name) const;
    std::string serialize_manifest(const LibraryDigestManifest& manifest) const;
    LibraryDigestManifest deserialize_manifest(const std::string& serialized_data) const;
    bool compare_digests(const std::string& digest1, const std::string& digest2) const;
};

// Global verifier instance
DigestVerifier& get_digest_verifier();

// Utility functions
std::string calculate_sha256_from_file(const std::filesystem::path& file_path);
std::string calculate_sha256_from_string(const std::string& input);
bool verify_file_digest(const std::filesystem::path& file_path, const std::string& expected_digest);

} // namespace verification
} // namespace integration