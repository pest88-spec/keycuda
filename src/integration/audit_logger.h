/**
 * Audit Logging System with Integrity Verification for Puzzle71Solver
 *
 * Provides comprehensive audit logging for all integration operations with
 * cryptographic integrity verification, tamper detection, and secure storage.
 * Ensures complete audit trail for regulatory compliance and security auditing.
 *
 * @origin       https://github.com/Puzzle71Solver/Puzzle71Solver
 * @origin_path  src/integration/audit_logger.h
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
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>      // P1-006: For flush thread
#include <atomic>      // P1-006: For atomic flags
#include <openssl/sha.h>

/**
 * Audit Logging System with Integrity Verification
 *
 * Provides tamper-evident audit logging for all integration operations
 * with cryptographic integrity checks and secure storage mechanisms.
 */
class AuditLogger {
public:
    /**
     * Audit log entry types
     */
    enum class LogEntryType {
        INTEGRATION_START,
        INTEGRATION_COMPLETE,
        INTEGRATION_ERROR,
        CONFIGURATION_CHANGE,
        METADATA_UPDATE,
        INTEGRITY_CHECK,
        BUILD_OPERATION,
        DEPLOYMENT_OPERATION,
        VERSION_UPDATE,
        ACCESS_CONTROL,
        SECURITY_EVENT,
        SYSTEM_EVENT
    };

    /**
     * Audit log severity levels
     */
    enum class Severity {
        DEBUG,
        INFO,
        WARNING,
        ERROR,
        CRITICAL
    };

    /**
     * Audit log entry with integrity verification
     */
    struct AuditEntry {
        std::string entry_id;
        LogEntryType entry_type;
        Severity severity;
        std::chrono::system_clock::time_point timestamp;
        std::string user_id;
        std::string session_id;
        std::string operation;
        std::string resource;
        std::map<std::string, std::string> details;
        std::string previous_hash;  // For blockchain-style integrity
        std::string entry_hash;     // SHA-256 of this entry
        std::string signature;      // Digital signature (optional)

        AuditEntry() : entry_type(LogEntryType::SYSTEM_EVENT),
                      severity(Severity::INFO),
                      timestamp(std::chrono::system_clock::now()) {}
    };

    /**
     * Integrity verification result
     */
    struct IntegrityResult {
        bool is_valid;
        std::vector<size_t> tampered_entries;
        std::string last_valid_hash;
        std::vector<std::string> integrity_violations;
        size_t total_entries_checked;
        std::chrono::system_clock::time_point verification_time;

        IntegrityResult() : is_valid(true), total_entries_checked(0),
                          verification_time(std::chrono::system_clock::now()) {}
    };

    /**
     * Audit log statistics
     */
    struct AuditStats {
        size_t total_entries;
        size_t entries_by_type[12];  // Corresponds to LogEntryType enum
        size_t entries_by_severity[5]; // Corresponds to Severity enum
        std::chrono::system_clock::time_point oldest_entry;
        std::chrono::system_clock::time_point newest_entry;
        size_t total_log_size_bytes;
        std::map<std::string, size_t> operations_by_count;

        AuditStats() : total_entries(0), total_log_size_bytes(0) {
            std::fill(std::begin(entries_by_type), std::end(entries_by_type), 0);
            std::fill(std::begin(entries_by_severity), std::end(entries_by_severity), 0);
        }
    };

private:
    std::string log_directory_;
    std::string current_log_file_;
    std::vector<AuditEntry> entries_;
    std::string last_entry_hash_;
    mutable std::mutex log_mutex_;
    bool integrity_enabled_;
    size_t max_entries_per_file_;
    size_t max_log_files_;
    bool auto_rotate_enabled_;

    // P1-006: WORM (Write-Once-Read-Many) storage members
    std::ofstream worm_file_;                                    // WORM file stream (append-only)
    std::chrono::steady_clock::time_point last_flush_time_;      // Last flush timestamp
    std::mutex flush_mutex_;                                     // Flush operation mutex
    std::thread flush_thread_;                                   // Background flush thread
    std::atomic<bool> flush_thread_running_;                     // Flush thread running flag

    /**
     * Generate unique entry ID
     */
    std::string generate_entry_id() const;

    /**
     * Calculate entry hash with previous hash
     */
    std::string calculate_entry_hash(const AuditEntry& entry) const;

    /**
     * Serialize entry to JSON
     */
    std::string serialize_entry(const AuditEntry& entry) const;

    /**
     * Deserialize entry from JSON
     */
    AuditEntry deserialize_entry(const std::string& json_str) const;

    /**
     * Write entry to log file
     */
    bool write_entry_to_file(const AuditEntry& entry);

    /**
     * Load entries from log file
     */
    bool load_entries_from_file(const std::string& filename);

    /**
     * Rotate log file if needed
     */
    bool rotate_log_file_if_needed();

    /**
     * Clean up old log files
     */
    void cleanup_old_log_files();

    /**
     * Get current log file path
     */
    std::string get_current_log_file_path() const;

    /**
     * Format timestamp for storage
     */
    std::string format_timestamp(std::chrono::system_clock::time_point tp) const;

    /**
     * Parse timestamp from storage
     */
    std::chrono::system_clock::time_point parse_timestamp(const std::string& ts) const;

    /**
     * Convert entry type to string
     */
    std::string entry_type_to_string(LogEntryType type) const;

    /**
     * Convert severity to string
     */
    std::string severity_to_string(Severity level) const;

    /**
     * Convert string to entry type
     */
    LogEntryType string_to_entry_type(const std::string& str) const;

    /**
     * Convert string to severity
     */
    Severity string_to_severity(const std::string& str) const;

    // P1-006: WORM storage private methods

    /**
     * Start background flush thread
     */
    void start_flush_thread();

    /**
     * Stop background flush thread
     */
    void stop_flush_thread();

    /**
     * Flush thread worker function
     */
    void flush_thread_worker();

    /**
     * Force flush to disk (fsync)
     */
    void force_flush();

    /**
     * Set file as immutable (read-only)
     *
     * @param path File path
     * @return True if successful
     */
    bool set_file_immutable(const std::string& path);

    /**
     * Remove immutable attribute from file
     *
     * @param path File path
     * @return True if successful
     */
    bool remove_file_immutable(const std::string& path);

public:
    /**
     * Constructor
     *
     * @param log_directory Directory for audit logs
     * @param integrity_enabled Enable integrity verification
     * @param max_entries_per_file Maximum entries per log file
     * @param max_log_files Maximum number of log files to keep
     * @param auto_rotate Enable automatic log rotation
     */
    explicit AuditLogger(
        const std::string& log_directory = "audit/",
        bool integrity_enabled = true,
        size_t max_entries_per_file = 10000,
        size_t max_log_files = 100,
        bool auto_rotate = true
    );

    /**
     * Destructor
     */
    ~AuditLogger();

    /**
     * Log audit entry
     *
     * @param entry_type Type of audit entry
     * @param severity Severity level
     * @param user_id User performing the operation
     * @param session_id Session identifier
     * @param operation Operation being performed
     * @param resource Resource being operated on
     * @param details Additional details
     * @return True if log entry created successfully
     */
    bool log_entry(LogEntryType entry_type,
                   Severity severity,
                   const std::string& user_id,
                   const std::string& session_id,
                   const std::string& operation,
                   const std::string& resource,
                   const std::map<std::string, std::string>& details = {});

    /**
     * Log integration operation
     */
    bool log_integration_operation(const std::string& user_id,
                                   const std::string& session_id,
                                   const std::string& operation,
                                   const std::string& library_name,
                                   const std::map<std::string, std::string>& details = {});

    /**
     * Log configuration change
     */
    bool log_configuration_change(const std::string& user_id,
                                  const std::string& session_id,
                                  const std::string& config_key,
                                  const std::string& old_value,
                                  const std::string& new_value);

    /**
     * Log integrity check
     */
    bool log_integrity_check(const std::string& user_id,
                             const std::string& resource,
                             bool check_passed,
                             const std::string& check_details);

    /**
     * Log security event
     */
    bool log_security_event(const std::string& user_id,
                            const std::string& event_type,
                            const std::string& description,
                            Severity severity = Severity::WARNING);

    /**
     * Verify audit log integrity
     *
     * @return Integrity verification result
     */
    IntegrityResult verify_log_integrity() const;

    /**
     * Get audit entries
     *
     * @param entry_type Filter by entry type (optional)
     * @param severity Filter by severity level (optional)
     * @param start_time Filter by start time (optional)
     * @param end_time Filter by end time (optional)
     * @param limit Maximum number of entries to return (0 = all)
     * @return Vector of audit entries
     */
    std::vector<AuditEntry> get_entries(LogEntryType entry_type = LogEntryType::SYSTEM_EVENT,
                                        Severity severity = Severity::DEBUG,
                                        std::chrono::system_clock::time_point* start_time = nullptr,
                                        std::chrono::system_clock::time_point* end_time = nullptr,
                                        size_t limit = 0) const;

    /**
     * Search audit entries
     *
     * @param query Search query
     * @param search_details Search in details
     * @param case_sensitive Case sensitive search
     * @return Vector of matching entries
     */
    std::vector<AuditEntry> search_entries(const std::string& query,
                                           bool search_details = true,
                                           bool case_sensitive = false) const;

    /**
     * Get audit statistics
     *
     * @return Audit statistics
     */
    AuditStats get_statistics() const;

    /**
     * Export audit log
     *
     * @param export_path Export file path
     * @param start_time Start time for export range
     * @param end_time End time for export range
     * @param include_integrity_info Include hash information
     * @param format Export format (json, csv, txt)
     * @return True if export successful
     */
    bool export_log(const std::string& export_path,
                    std::chrono::system_clock::time_point* start_time = nullptr,
                    std::chrono::system_clock::time_point* end_time = nullptr,
                    bool include_integrity_info = true,
                    const std::string& format = "json") const;

    /**
     * Import audit log
     *
     * @param import_path Import file path
     * @param merge_mode true=merge with existing, false=replace all
     * @param verify_after_import Verify integrity after import
     * @return True if import successful
     */
    bool import_log(const std::string& import_path,
                    bool merge_mode = true,
                    bool verify_after_import = true);

    /**
     * Generate audit report
     *
     * @param report_type Type of report (summary, security, operations, integrity)
     * @param start_time Report start time
     * @param end_time Report end time
     * @param format Report format (json, html, text)
     * @return Formatted report
     */
    std::string generate_report(const std::string& report_type = "summary",
                                std::chrono::system_clock::time_point* start_time = nullptr,
                                std::chrono::system_clock::time_point* end_time = nullptr,
                                const std::string& format = "json") const;

    /**
     * Clear audit log
     *
     * @param older_than Clear entries older than this time
     * @param preserve_integrity Preserve integrity chain
     * @return Number of entries cleared
     */
    size_t clear_log(std::chrono::system_clock::time_point older_than,
                     bool preserve_integrity = true);

    /**
     * Compact audit log
     *
     * @param keep_summary Keep summary entries only
     * @return True if compaction successful
     */
    bool compact_log(bool keep_summary = true);

    /**
     * Backup audit log
     *
     * @param backup_path Backup directory path
     * @param compress Compress backup files
     * @return True if backup successful
     */
    bool backup_log(const std::string& backup_path, bool compress = true);

    /**
     * Restore audit log from backup
     *
     * @param backup_path Backup directory path
     * @param verify_after_restore Verify integrity after restore
     * @return True if restore successful
     */
    bool restore_from_backup(const std::string& backup_path,
                             bool verify_after_restore = true);

    /**
     * Enable/disable integrity verification
     *
     * @param enabled Enable integrity verification
     */
    void set_integrity_enabled(bool enabled);

    /**
     * Check if integrity verification is enabled
     *
     * @return True if integrity verification is enabled
     */
    bool is_integrity_enabled() const;

    /**
     * Force integrity verification
     *
     * @return Integrity verification result
     */
    IntegrityResult force_integrity_verification() const;

    /**
     * Get last valid hash
     *
     * @return Last valid hash in the chain
     */
    std::string get_last_valid_hash() const;

    /**
     * Validate log chain continuity
     *
     * @return True if log chain is continuous
     */
    bool validate_chain_continuity() const;
};

/**
 * RAII Audit Session for automatic session logging
 */
class AuditSession {
private:
    AuditLogger& logger_;
    std::string session_id_;
    std::string user_id_;
    bool session_active_;
    std::chrono::system_clock::time_point start_time_;

public:
    AuditSession(AuditLogger& logger,
                 const std::string& user_id,
                 const std::string& session_type = "");
    ~AuditSession();

    /**
     * Log operation within session
     */
    bool log_operation(const std::string& operation,
                       const std::string& resource,
                       const std::map<std::string, std::string>& details = {});

    /**
     * End session manually
     */
    void end_session();

    /**
     * Get session ID
     */
    const std::string& get_session_id() const { return session_id_; }
};