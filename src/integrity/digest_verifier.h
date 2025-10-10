/**
 * SHA-256 Digest Verification System for Puzzle71Solver
 *
 * Provides comprehensive SHA-256 digest verification for all integrated artifacts
 * including source files, build artifacts, and configuration files. Ensures integrity
 * and authenticity of all integrated components with automated verification.
 *
 * @origin       https://github.com/Puzzle71Solver/Puzzle71Solver
 * @origin_path  src/integrity/digest_verifier.h
 * @origin_commit <current_commit>
 * @origin_license MIT
 * @extracted_date   2025-10-10
 * @extracted_by     Puzzle71Solver Team
 * @modifications    Created for third-party dependency integration optimization
 * @spdx_license_identifier MIT
 */

#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <filesystem>
#include <fstream>
#include <openssl/sha.h>

/**
 * Digest Verification System
 *
 * Provides SHA-256 digest calculation, storage, and verification for all
 * integrated artifacts to ensure integrity and detect unauthorized modifications.
 */
class DigestVerifier {
public:
    /**
     * File digest information
     */
    struct FileDigest {
        std::string file_path;
        std::string sha256_digest;
        std::string algorithm;
        std::chrono::system_clock::time_point calculated_at;
        std::size_t file_size;
        std::string file_permissions;
        std::map<std::string, std::string> metadata;

        FileDigest() : algorithm("SHA-256"), calculated_at(std::chrono::system_clock::now()), file_size(0) {}
    };

    /**
     * Digest verification result
     */
    struct VerificationResult {
        bool is_valid;
        std::string file_path;
        std::string expected_digest;
        std::string actual_digest;
        std::string status_message;
        std::chrono::system_clock::time_point verified_at;

        VerificationResult() : is_valid(false), verified_at(std::chrono::system_clock::now()) {}
    };

    /**
     * Batch verification results
     */
    struct BatchVerificationResult {
        size_t total_files;
        size_t verified_files;
        size_t failed_files;
        size_t missing_files;
        std::vector<VerificationResult> individual_results;
        std::chrono::system_clock::time_point batch_completed_at;
        double verification_duration_ms;

        BatchVerificationResult() : total_files(0), verified_files(0), failed_files(0), missing_files(0),
                                  batch_completed_at(std::chrono::system_clock::now()), verification_duration_ms(0.0) {}
    };

    /**
     * Digest manifest for storing file digests
     */
    struct DigestManifest {
        std::string manifest_version;
        std::chrono::system_clock::time_point created_at;
        std::chrono::system_clock::time_point updated_at;
        std::string created_by;
        std::map<std::string, FileDigest> file_digests;
        std::string root_directory;
        std::vector<std::string> exclusion_patterns;
        std::map<std::string, std::string> manifest_metadata;

        DigestManifest() : manifest_version("1.0"), created_at(std::chrono::system_clock::now()),
                          updated_at(std::chrono::system_clock::now()), created_by("system") {}
    };

private:
    std::string storage_path_;
    std::string manifest_file_path_;
    DigestManifest manifest_;
    bool auto_save_enabled_;

    /**
     * Calculate SHA-256 digest for a file
     */
    std::string calculate_file_digest(const std::string& file_path) const;

    /**
     * Calculate SHA-256 digest for data
     */
    std::string calculate_data_digest(const std::vector<uint8_t>& data) const;

    /**
     * Save manifest to file
     */
    bool save_manifest() const;

    /**
     * Load manifest from file
     */
    bool load_manifest();

    /**
     * Check if file should be included based on exclusion patterns
     */
    bool should_include_file(const std::string& file_path) const;

    /**
     * Get relative path from root directory
     */
    std::string get_relative_path(const std::string& file_path) const;

    /**
     * Format timestamp for storage
     */
    std::string format_timestamp(std::chrono::system_clock::time_point tp) const;

    /**
     * Parse timestamp from storage
     */
    std::chrono::system_clock::time_point parse_timestamp(const std::string& ts) const;

    /**
     * Get file permissions as string
     */
    std::string get_file_permissions(const std::filesystem::path& path) const;

public:
    /**
     * Constructor
     *
     * @param storage_path Directory for digest storage
     * @param manifest_file_name Name of manifest file
     * @param auto_save Enable auto-save of manifest
     */
    explicit DigestVerifier(
        const std::string& storage_path = "integrity/",
        const std::string& manifest_file_name = "digest_manifest.json",
        bool auto_save = true
    );

    /**
     * Destructor
     */
    ~DigestVerifier();

    /**
     * Calculate and store digest for a single file
     *
     * @param file_path Path to file
     * @param update_existing Update existing digest if file exists
     * @return True if digest calculated and stored successfully
     */
    bool calculate_file_digest(const std::string& file_path, bool update_existing = true);

    /**
     * Calculate digests for all files in directory recursively
     *
     * @param directory_path Root directory to scan
     * @param exclusion_patterns File patterns to exclude
     * @return Number of files processed
     */
    size_t calculate_directory_digests(const std::string& directory_path,
                                      const std::vector<std::string>& exclusion_patterns = {});

    /**
     * Calculate digests for specific files
     *
     * @param file_paths List of file paths
     * @return Number of files processed successfully
     */
    size_t calculate_file_digests(const std::vector<std::string>& file_paths);

    /**
     * Verify digest for a single file
     *
     * @param file_path Path to file
     * @return Verification result
     */
    VerificationResult verify_file_digest(const std::string& file_path) const;

    /**
     * Verify digests for all files in manifest
     *
     * @return Batch verification results
     */
    BatchVerificationResult verify_all_digests() const;

    /**
     * Verify digests for specific files
     *
     * @param file_paths List of file paths to verify
     * @return Batch verification results
     */
    BatchVerificationResult verify_file_digests(const std::vector<std::string>& file_paths) const;

    /**
     * Verify digest for data buffer
     *
     * @param data Data to verify
     * @param expected_digest Expected SHA-256 digest
     * @return True if digest matches
     */
    bool verify_data_digest(const std::vector<uint8_t>& data, const std::string& expected_digest) const;

    /**
     * Get stored digest for file
     *
     * @param file_path Path to file
     * @return File digest information (empty if not found)
     */
    FileDigest get_file_digest(const std::string& file_path) const;

    /**
     * Remove digest entry for file
     *
     * @param file_path Path to file
     * @return True if entry removed
     */
    bool remove_file_digest(const std::string& file_path);

    /**
     * Update manifest metadata
     *
     * @param key Metadata key
     * @param value Metadata value
     */
    void update_manifest_metadata(const std::string& key, const std::string& value);

    /**
     * Add exclusion pattern
     *
     * @param pattern File pattern to exclude
     */
    void add_exclusion_pattern(const std::string& pattern);

    /**
     * Remove exclusion pattern
     *
     * @param pattern File pattern to remove
     */
    void remove_exclusion_pattern(const std::string& pattern);

    /**
     * Export manifest to file
     *
     * @param export_path Export file path
     * @param format Export format (json, csv)
     * @return True if export successful
     */
    bool export_manifest(const std::string& export_path, const std::string& format = "json") const;

    /**
     * Import manifest from file
     *
     * @param import_path Import file path
     * @param merge_mode true=merge with existing, false=replace all
     * @return True if import successful
     */
    bool import_manifest(const std::string& import_path, bool merge_mode = true);

    /**
     * Generate digest report
     *
     * @param format Report format (json, text, csv)
     * @return Formatted report
     */
    std::string generate_report(const std::string& format = "json") const;

    /**
     * Search for files by digest
     *
     * @param sha256_digest SHA-256 digest to search for
     * @return List of files with matching digest
     */
    std::vector<std::string> find_files_by_digest(const std::string& sha256_digest) const;

    /**
     * Get digest statistics
     */
    struct DigestStats {
        size_t total_files;
        size_t total_size_bytes;
        std::map<std::string, size_t> file_extension_distribution;
        std::chrono::system_clock::time_point oldest_digest;
        std::chrono::system_clock::time_point newest_digest;
        size_t duplicate_files_count;

        DigestStats() : total_files(0), total_size_bytes(0), duplicate_files_count(0) {}
    };

    DigestStats get_statistics() const;

    /**
     * Find duplicate files by digest
     *
     * @return Map of digest to list of file paths
     */
    std::map<std::string, std::vector<std::string>> find_duplicate_files() const;

    /**
     * Validate manifest integrity
     *
     * @return True if manifest is internally consistent
     */
    bool validate_manifest_integrity() const;

    /**
     * Clean up orphaned digest entries (files that no longer exist)
     *
     * @return Number of entries removed
     */
    size_t cleanup_orphaned_entries();

    /**
     * Enable/disable auto-save
     *
     * @param enabled Enable auto-save
     */
    void set_auto_save(bool enabled);

    /**
     * Force save manifest
     *
     * @return True if save successful
     */
    bool force_save() const;

    /**
     * Check if manifest has unsaved changes
     *
     * @return True if there are unsaved changes
     */
    bool has_unsaved_changes() const;

    /**
     * Clear all digest entries
     */
    void clear_all_digests();

    /**
     * Create backup of current manifest
     *
     * @param backup_path Backup file path
     * @return True if backup successful
     */
    bool create_backup(const std::string& backup_path) const;

    /**
     * Restore manifest from backup
     *
     * @param backup_path Backup file path
     * @return True if restore successful
     */
    bool restore_from_backup(const std::string& backup_path);
};

/**
 * RAII Digest Verification Session
 */
class DigestVerificationSession {
private:
    const DigestVerifier& verifier_;
    std::vector<std::string> verified_files_;
    std::vector<std::string> failed_files_;
    bool session_active_;

public:
    explicit DigestVerificationSession(const DigestVerifier& verifier);
    ~DigestVerificationSession();

    /**
     * Verify file and add to session results
     */
    bool verify_file(const std::string& file_path);

    /**
     * Get verification results
     */
    const std::vector<std::string>& get_verified_files() const { return verified_files_; }
    const std::vector<std::string>& get_failed_files() const { return failed_files_; }

    /**
     * Get session summary
     */
    std::string get_session_summary() const;

    /**
     * End session
     */
    void end_session();
};