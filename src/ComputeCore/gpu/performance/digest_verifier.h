#pragma once

#include <string>
#include <vector>
        <memory>
        <map>
        <chrono>
        <json/json.h>
        <openssl/sha.h>

namespace puzzle71::gpu::performance {

/**
 * @brief SHA-256 digest verification system for all project artifacts
 *
 * Provides comprehensive cryptographic verification with ≤250ms SLA:
 * - SHA-256 digest calculation and verification for all artifacts
 * - Real-time integrity monitoring with performance SLA compliance
 * - Cryptographic signature verification and chain of trust
 * - Batch verification for multiple artifacts
 * - Tamper detection and alerting
 * - Historical digest tracking and change management
 * - Cache optimization for repeated verifications
 */

enum class VerificationStatus {
    VERIFIED,           // Digest matches expected value
    FAILED,             // Digest does not match
    CORRUPTED,          // Artifact appears corrupted
    MISSING,            // Artifact or digest is missing
    EXPIRED,            // Verification has expired
    TIMEOUT,            // Verification exceeded SLA
    ERROR               // Verification encountered an error
};

enum class ArtifactType {
    SOURCE_CODE,        // .cpp, .cu, .h, .cuh files
    EXECUTABLE,         // Compiled binaries and libraries
    CONFIGURATION,      // Configuration files
    DATA,              // Data files and datasets
    DOCUMENTATION,     // Documentation and README files
    TEST,              // Test files and test data
    BUILD_ARTIFACT,    // Build system artifacts
    RUNTIME_OUTPUT     // Runtime generated files
};

struct DigestRecord {
    std::string artifact_path;
    std::string sha256_digest;
    ArtifactType artifact_type;
    std::chrono::system_clock::time_point verification_time;
    std::chrono::system_clock::time_point creation_time;
    size_t file_size_bytes;
    VerificationStatus status;
    std::chrono::microseconds verification_duration;
    std::string verification_error;
    json metadata;
};

struct VerificationReport {
    std::string report_timestamp;
    std::chrono::microseconds total_verification_time;
    size_t total_artifacts_verified;
    size_t verified_artifacts;
    size_t failed_verifications;
    size_t corrupted_artifacts;
    size_t missing_artifacts;
    size_t timeout_artifacts;

    std::vector<DigestRecord> verification_records;
    std::vector<std::string> critical_issues;
    std::vector<std::string> tampered_files;
    std::vector<std::string> recommendations;

    double integrity_score; // 0-100%
    bool meets_sla_requirements; // All verifications ≤250ms
    bool overall_integrity_valid;
    std::chrono::microseconds max_verification_time;
    std::chrono::microseconds avg_verification_time;
};

struct VerificationConfig {
    std::chrono::microseconds max_verification_time; // Default: 250ms
    bool enable_parallel_verification;
    size_t thread_pool_size;
    bool enable_caching;
    bool enable_continuous_monitoring;
    std::chrono::minutes cache_expiry_time;
    std::vector<std::string> trusted_directories;
    std::vector<std::string> exclude_patterns;
    bool verify_signatures;
    bool alert_on_tampering;
};

class DigestVerifier {
public:
    explicit DigestVerifier(const VerificationConfig& config = VerificationConfig{});
    ~DigestVerifier();

    // Single artifact verification
    DigestRecord VerifyArtifact(const std::string& artifact_path, const std::string& expected_digest = "");
    VerificationStatus VerifyDigestMatch(const std::string& artifact_path, const std::string& expected_digest);
    std::string CalculateSHA256Digest(const std::string& artifact_path);

    // Batch verification
    std::vector<DigestRecord> VerifyArtifacts(const std::vector<std::string>& artifact_paths);
    VerificationReport VerifyDirectory(const std::string& directory_path, bool recursive = true);
    VerificationReport VerifyProjectArtifacts(const std::string& project_root);

    // Digest management
    bool StoreDigestRecord(const DigestRecord& record);
    bool LoadDigestDatabase(const std::string& database_file);
    bool SaveDigestDatabase(const std::string& database_file) const;
    bool UpdateDigestForArtifact(const std::string& artifact_path);

    // Real-time monitoring
    void EnableContinuousMonitoring(bool enabled);
    void StartMonitoring();
    void StopMonitoring();
    bool IsMonitoringActive() const;
    void AddWatchPath(const std::string& path);

    // Tamper detection
    bool DetectTampering(const DigestRecord& current_record, const DigestRecord& stored_record);
    std::vector<std::string> GetTamperedFiles() const;
    void ReportTampering(const std::string& artifact_path, const std::string& details);

    // Performance monitoring
    bool VerifyWithinSLA(const std::string& artifact_path) const;
    std::chrono::microseconds GetVerificationTime(const std::string& artifact_path) const;
    double GetSLAComplianceRate() const;
    VerificationReport GeneratePerformanceReport() const;

    // Integrity analysis
    bool VerifyIntegrityChain(const std::string& artifact_path);
    std::vector<std::string> GetIntegrityViolations() const;
    double CalculateIntegrityScore() const;

    // Configuration
    void SetVerificationConfig(const VerificationConfig& config);
    void SetMaxVerificationTime(std::chrono::microseconds max_time);
    void AddTrustedDirectory(const std::string& directory);
    void AddExcludePattern(const std::string& pattern);

    // Utilities
    std::string GenerateDigestReport(const std::string& format = "json") const;
    bool ExportVerificationReport(const std::string& file_path, const std::string& format = "json") const;
    void ClearCache();
    std::vector<std::string> GetVerifiedArtifacts() const;

private:
    VerificationConfig config_;
    mutable std::mutex verifier_mutex_;

    // Digest database
    std::map<std::string, DigestRecord> digest_database_;
    std::string database_file_path_;

    // Performance tracking
    std::map<std::string, std::chrono::microseconds> verification_times_;
    std::chrono::microseconds total_verification_time_;
    size_t successful_verifications_;
    size_t failed_verifications_;

    // Cache
    std::map<std::string, std::pair<std::string, std::chrono::system_clock::time_point>> digest_cache_;
    std::map<std::string, std::chrono::system_clock::time_point> cache_timestamps_;

    // Monitoring
    bool monitoring_active_;
    std::thread monitoring_thread_;
    std::atomic<bool> stop_monitoring_;
    std::vector<std::string> watch_paths_;

    // Tamper detection
    std::vector<std::string> tampered_files_;
    std::vector<std::string> integrity_violations_;

    // Internal methods
    std::string ComputeSHA256Hash(const std::string& file_path) const;
    std::string ComputeSHA256Hash(const std::vector<uint8_t>& data) const;
    bool ReadFileContents(const std::string& file_path, std::vector<uint8_t>& contents) const;
    ArtifactType DetermineArtifactType(const std::string& file_path) const;
    size_t GetFileSize(const std::string& file_path) const;

    // Cache management
    bool IsCached(const std::string& file_path) const;
    std::string GetCachedDigest(const std::string& file_path) const;
    void CacheDigest(const std::string& file_path, const std::string& digest);
    bool IsCacheExpired(const std::string& file_path) const;

    // File discovery
    std::vector<std::string> DiscoverArtifacts(const std::string& directory_path, bool recursive) const;
    bool ShouldExcludeFile(const std::string& file_path) const;

    // Monitoring loop
    void MonitoringLoop();
    void ProcessWatchPath(const std::string& path);

    // Performance analysis
    bool ExceedsSLA(std::chrono::microseconds verification_time) const;
    void UpdatePerformanceMetrics(std::chrono::microseconds verification_time, bool successful);
    std::chrono::microseconds GetAverageVerificationTime() const;

    // Serialization
    json DigestRecordToJson(const DigestRecord& record) const;
    json VerificationReportToJson(const VerificationReport& report) const;
    DigestRecord JsonToDigestRecord(const json& j) const;
    VerificationReport JsonToVerificationReport(const json& j) const;

    // Database management
    bool LoadDatabaseFromJson(const std::string& json_content);
    std::string SerializeDatabaseToJson() const;
};

/**
 * @brief Scoped digest verifier for automatic verification during operations
 */
class ScopedDigestVerifier {
public:
    ScopedDigestVerifier(DigestVerifier& verifier, const std::string& artifact_path);
    ~ScopedDigestVerifier();

    bool IsVerified() const;
    VerificationStatus GetStatus() const;
    std::chrono::microseconds GetVerificationTime() const;

private:
    DigestVerifier& verifier_;
    std::string artifact_path_;
    DigestRecord verification_record_;
    bool verification_completed_;
};

/**
 * @brief High-performance batch digest calculator for multiple files
 */
class BatchDigestCalculator {
public:
    explicit BatchDigestCalculator(size_t thread_count = 4);
    ~BatchDigestCalculator();

    std::vector<std::pair<std::string, std::string>> CalculateDigests(const std::vector<std::string>& file_paths);
    std::vector<DigestRecord> CalculateDigestRecords(const std::vector<std::string>& file_paths);
    void SetMaxBatchTime(std::chrono::milliseconds max_time);

private:
    size_t thread_count_;
    std::chrono::milliseconds max_batch_time_;
    std::vector<std::thread> worker_threads_;
    std::queue<std::string> file_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::atomic<bool> stop_processing_;
    std::vector<std::pair<std::string, std::string>> results_;
    std::mutex results_mutex_;

    void WorkerThread();
    std::string CalculateFileDigest(const std::string& file_path);
};

/**
 * @brief Utility class for cryptographic operations and security
 */
class CryptoUtils {
public:
    static std::string BytesToHex(const std::vector<uint8_t>& bytes);
    static std::vector<uint8_t> HexToBytes(const std::string& hex);
    static std::string GenerateSecureHash(const std::string& input);
    static bool VerifyFileSignature(const std::string& file_path, const std::string& signature, const std::string& public_key);
    static std::string GenerateHMAC(const std::string& data, const std::string& secret_key);
    static bool VerifyHMAC(const std::string& data, const std::string& hmac, const std::string& secret_key);

private:
    static const size_t HASH_SIZE = 32; // SHA-256 produces 32-byte hash
};

/**
 * @brief Factory function to create and configure digest verifier
 */
std::unique_ptr<DigestVerifier> CreateDigestVerifier(
    const VerificationConfig& config = VerificationConfig{});

} // namespace puzzle71::gpu::performance