/**
 * Integration Evidence Collection and Verification System for Puzzle71Solver
 *
 * Provides comprehensive evidence collection for all integration operations including
 * source code inclusion, attribution compliance, build verification, and integrity
 * validation. Maintains auditable evidence trail for regulatory compliance.
 *
 * @origin       https://github.com/Puzzle71Solver/Puzzle71Solver
 * @origin_path  src/integration/evidence_collector.h
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

/**
 * Evidence Collection System
 *
 * Collects, stores, and verifies evidence for all integration operations to ensure
 * compliance with regulatory requirements and maintain audit trails.
 */
class EvidenceCollector {
public:
    /**
     * Types of evidence that can be collected
     */
    enum class EvidenceType {
        SOURCE_INCLUSION,
        ATTRIBUTION_COMPLIANCE,
        BUILD_VERIFICATION,
        INTEGRITY_VALIDATION,
        LICENSE_VERIFICATION,
        DEPENDENCY_EXTRACTION,
        CONFIGURATION_CHANGE,
        TEST_EXECUTION,
        DEPLOYMENT_VERIFICATION
    };

    /**
     * Evidence status
     */
    enum class EvidenceStatus {
        COLLECTED,
        VERIFIED,
        FAILED,
        PENDING,
        ARCHIVED
    };

    /**
     * Individual evidence item
     */
    struct EvidenceItem {
        std::string evidence_id;
        EvidenceType type;
        EvidenceStatus status;
        std::string description;
        std::chrono::system_clock::time_point collected_at;
        std::chrono::system_clock::time_point verified_at;
        std::string collected_by;
        std::string verified_by;
        std::map<std::string, std::string> metadata;
        std::vector<std::string> file_paths;
        std::string checksum;
        std::string notes;

        EvidenceItem() : type(EvidenceType::SOURCE_INCLUSION),
                        status(EvidenceStatus::PENDING),
                        collected_at(std::chrono::system_clock::now()),
                        verified_at(std::chrono::system_clock::time_point{}) {}
    };

    /**
     * Evidence collection session
     */
    struct CollectionSession {
        std::string session_id;
        std::string session_type;
        std::string description;
        std::chrono::system_clock::time_point started_at;
        std::chrono::system_clock::time_point completed_at;
        std::string initiated_by;
        std::vector<std::string> evidence_ids;
        std::map<std::string, std::string> session_metadata;

        CollectionSession() : started_at(std::chrono::system_clock::now()),
                            completed_at(std::chrono::system_clock::time_point{}) {}
    };

    /**
     * Verification result
     */
    struct VerificationResult {
        bool passed;
        std::string verification_type;
        std::vector<std::string> passed_checks;
        std::vector<std::string> failed_checks;
        std::vector<std::string> warnings;
        std::map<std::string, std::string> metrics;
        std::string summary;
        std::chrono::system_clock::time_point verified_at;

        VerificationResult() : passed(false),
                             verified_at(std::chrono::system_clock::now()) {}
    };

private:
    std::string storage_path_;
    std::map<std::string, EvidenceItem> evidence_items_;
    std::vector<CollectionSession> sessions_;
    std::string current_session_id_;
    mutable std::mutex evidence_mutex_;

    /**
     * Generate unique evidence ID
     */
    std::string generate_evidence_id() const;

    /**
     * Generate unique session ID
     */
    std::string generate_session_id() const;

    /**
     * Calculate file checksum
     */
    std::string calculate_file_checksum(const std::string& file_path) const;

    /**
     * Calculate directory checksum
     */
    std::string calculate_directory_checksum(const std::string& dir_path) const;

    /**
     * Save evidence to storage
     */
    bool save_evidence(const EvidenceItem& evidence) const;

    /**
     * Load evidence from storage
     */
    bool load_evidence();

    /**
     * Save session to storage
     */
    bool save_session(const CollectionSession& session) const;

    /**
     * Load sessions from storage
     */
    bool load_sessions();

    /**
     * Format timestamp for storage
     */
    std::string format_timestamp(std::chrono::system_clock::time_point tp) const;

    /**
     * Parse timestamp from storage
     */
    std::chrono::system_clock::time_point parse_timestamp(const std::string& ts) const;

    /**
     * Convert evidence type to string
     */
    std::string evidence_type_to_string(EvidenceType type) const;

    /**
     * Convert evidence status to string
     */
    std::string evidence_status_to_string(EvidenceStatus status) const;

    /**
     * Convert evidence to JSON
     */
    json evidence_to_json(const EvidenceItem& evidence) const;

    /**
     * Convert session to JSON
     */
    json session_to_json(const CollectionSession& session) const;

    /**
     * Convert stats to JSON
     */
    json stats_to_json(const EvidenceStats& stats) const;

    /**
     * Convert JSON to evidence
     */
    EvidenceItem json_to_evidence(const json& j) const;

    /**
     * Convert JSON to session
     */
    CollectionSession json_to_session(const json& j) const;

public:
    /**
     * Constructor
     *
     * @param storage_path Directory for evidence storage
     */
    explicit EvidenceCollector(const std::string& storage_path = "evidence/");

    /**
     * Destructor
     */
    ~EvidenceCollector();

    /**
     * Start new evidence collection session
     *
     * @param session_type Type of session
     * @param description Session description
     * @param initiated_by Who initiated the session
     * @return Session ID
     */
    std::string start_session(const std::string& session_type,
                             const std::string& description = "",
                             const std::string& initiated_by = "system");

    /**
     * End current collection session
     *
     * @param summary Session summary
     * @return True if session ended successfully
     */
    bool end_session(const std::string& summary = "");

    /**
     * Collect source inclusion evidence
     *
     * @param library_name Name of the library
     * @param source_paths List of source file paths
     * @param integration_path Path where library was integrated
     * @param description Evidence description
     * @return Evidence ID if successful
     */
    std::string collect_source_inclusion(const std::string& library_name,
                                        const std::vector<std::string>& source_paths,
                                        const std::string& integration_path,
                                        const std::string& description = "");

    /**
     * Collect attribution compliance evidence
     *
     * @param library_name Name of the library
     * @param files_with_attribution Files that have proper attribution
     * @param files_missing_attribution Files missing attribution
     * @param coverage_percentage Attribution coverage percentage
     * @return Evidence ID if successful
     */
    std::string collect_attribution_compliance(const std::string& library_name,
                                              const std::vector<std::string>& files_with_attribution,
                                              const std::vector<std::string>& files_missing_attribution,
                                              double coverage_percentage);

    /**
     * Collect build verification evidence
     *
     * @param build_command Command that was executed
     * @param build_output Build output
     * @param build_success Whether build was successful
     * @param build_time Time taken to build
     * @param artifacts Produced artifacts
     * @return Evidence ID if successful
     */
    std::string collect_build_verification(const std::string& build_command,
                                          const std::string& build_output,
                                          bool build_success,
                                          double build_time,
                                          const std::vector<std::string>& artifacts);

    /**
     * Collect integrity validation evidence
     *
     * @param validation_type Type of validation performed
     * @param validated_items Items that were validated
     * @param validation_results Results of validation
     * @return Evidence ID if successful
     */
    std::string collect_integrity_validation(const std::string& validation_type,
                                            const std::vector<std::string>& validated_items,
                                            const std::map<std::string, bool>& validation_results);

    /**
     * Collect license verification evidence
     *
     * @param library_name Name of the library
     * @param detected_licenses Detected licenses
     * @param license_compliance Whether licenses are compliant
     * @param license_files License files found
     * @return Evidence ID if successful
     */
    std::string collect_license_verification(const std::string& library_name,
                                           const std::vector<std::string>& detected_licenses,
                                           bool license_compliance,
                                           const std::vector<std::string>& license_files);

    /**
     * Verify evidence item
     *
     * @param evidence_id Evidence ID to verify
     * @param verified_by Who verified the evidence
     * @param verification_notes Notes about verification
     * @return True if verification successful
     */
    bool verify_evidence(const std::string& evidence_id,
                        const std::string& verified_by = "system",
                        const std::string& verification_notes = "");

    /**
     * Get evidence item
     *
     * @param evidence_id Evidence ID
     * @return Evidence item (empty if not found)
     */
    EvidenceItem get_evidence(const std::string& evidence_id) const;

    /**
     * Get all evidence items
     *
     * @param type Filter by evidence type (optional)
     * @param status Filter by evidence status (optional)
     * @return Vector of evidence items
     */
    std::vector<EvidenceItem> get_all_evidence(EvidenceType type = EvidenceType::SOURCE_INCLUSION,
                                              EvidenceStatus status = EvidenceStatus::PENDING) const;

    /**
     * Get collection sessions
     *
     * @return Vector of collection sessions
     */
    std::vector<CollectionSession> get_sessions() const;

    /**
     * Generate evidence report
     *
     * @param session_id Session ID (empty for all sessions)
     * @param format Report format (json, text, csv)
     * @return Formatted report
     */
    std::string generate_report(const std::string& session_id = "",
                               const std::string& format = "json") const;

    /**
     * Export evidence
     *
     * @param evidence_ids Evidence IDs to export
     * @param export_path Export directory path
     * @param include_files Include actual files in export
     * @return True if export successful
     */
    bool export_evidence(const std::vector<std::string>& evidence_ids,
                        const std::string& export_path,
                        bool include_files = false);

    /**
     * Verify evidence integrity
     *
     * @param evidence_id Evidence ID to verify
     * @return Verification result
     */
    VerificationResult verify_evidence_integrity(const std::string& evidence_id) const;

    /**
     * Archive old evidence
     *
     * @param older_than Archive evidence older than this timestamp
     * @param archive_path Archive directory path
     * @return Number of items archived
     */
    size_t archive_evidence(std::chrono::system_clock::time_point older_than,
                           const std::string& archive_path = "evidence/archive/");

    /**
     * Get evidence statistics
     */
    struct EvidenceStats {
        size_t total_evidence_items;
        size_t total_sessions;
        std::map<EvidenceType, size_t> items_by_type;
        std::map<EvidenceStatus, size_t> items_by_status;
        std::chrono::system_clock::time_point last_collection;

        EvidenceStats() : total_evidence_items(0), total_sessions(0),
                         last_collection(std::chrono::system_clock::time_point{}) {}
    };

    EvidenceStats get_statistics() const;

    /**
     * Search evidence
     *
     * @param query Search query
     * @param search_metadata Search in metadata
     * @return Vector of matching evidence IDs
     */
    std::vector<std::string> search_evidence(const std::string& query,
                                            bool search_metadata = true) const;

    /**
     * Delete evidence
     *
     * @param evidence_id Evidence ID to delete
     * @param deleted_by Who deleted the evidence
     * @param reason Reason for deletion
     * @return True if deletion successful
     */
    bool delete_evidence(const std::string& evidence_id,
                        const std::string& deleted_by = "system",
                        const std::string& reason = "");
};

/**
 * RAII Evidence Session Manager
 */
class EvidenceSessionManager {
private:
    EvidenceCollector& collector_;
    std::string session_id_;
    bool committed_;

public:
    EvidenceSessionManager(EvidenceCollector& collector,
                          const std::string& session_type,
                          const std::string& description = "");
    ~EvidenceSessionManager();

    /**
     * Commit session with summary
     */
    void commit(const std::string& summary = "");

    /**
     * Get session ID
     */
    const std::string& get_session_id() const { return session_id_; }
};