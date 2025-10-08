#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <chrono>
#include <json/json.h>
#include <fstream>
#include <mutex>

namespace puzzle71::gpu::performance {

/**
 * @brief Write-Once-Read-Many (WORM) audit logging system with integrity verification
 *
 * Provides tamper-evident audit logging for all GPU operations with:
 * - Immutable log entries with cryptographic integrity verification
 * - Append-only logging with hash chaining for tamper detection
 * - Comprehensive operation tracking with metadata preservation
 * - Performance-optimized logging with minimal overhead
 * - Configurable retention policies and log rotation
 * - Real-time integrity monitoring and alerting
 */

enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARNING = 2,
    ERROR = 3,
    CRITICAL = 4
};

enum class OperationType {
    KERNEL_LAUNCH,
    MEMORY_OPERATION,
    SYNCHRONIZATION,
    PERFORMANCE_OPTIMIZATION,
    ERROR_RECOVERY,
    CONFIGURATION_CHANGE,
    ACCURACY_VALIDATION,
    SYSTEM_EVENT
};

enum class IntegrityStatus {
    VALID,           // Log integrity verified
    TAMPERED,        // Tampering detected
    CORRUPTED,       // Log corruption detected
    INCOMPLETE,      // Missing log entries
    UNKNOWN          // Unable to determine status
};

struct AuditLogEntry {
    std::string entry_id;
    std::chrono::system_clock::time_point timestamp;
    LogLevel level;
    OperationType operation_type;
    std::string operation_description;
    std::string operator_id;
    std::string session_id;
    std::string gpu_device_id;

    // Operation-specific data
    json operation_parameters;
    json performance_metrics;
    json system_state;

    // Integrity verification data
    std::string entry_hash;
    std::string previous_entry_hash;
    std::string chain_hash;

    // Metadata
    std::map<std::string, std::string> metadata;
    std::chrono::microseconds execution_time;
    bool successful;
    std::string error_message;

    // Serialization
    std::string ToJsonString() const;
    bool FromJsonString(const std::string& json_string);
    std::string CalculateEntryHash() const;
};

struct IntegrityVerificationResult {
    bool chain_integrity_valid;
    size_t total_entries_verified;
    size_t valid_entries;
    size_t tampered_entries;
    size_t corrupted_entries;
    std::vector<std::string> tampered_entry_ids;
    std::vector<std::string> integrity_violations;
    std::chrono::system_clock::time_point verification_timestamp;
    double verification_score; // 0.0-1.0
};

struct AuditLogConfig {
    std::string log_file_path;
    LogLevel minimum_log_level = LogLevel::INFO;
    size_t max_log_file_size_mb = 100;
    size_t max_log_files = 10;
    bool enable_compression = false;
    bool enable_encryption = false;
    std::string encryption_key;
    bool enable_real_time_verification = true;
    std::chrono::seconds verification_interval{300}; // 5 minutes
    bool enable_performance_monitoring = true;
    double max_logging_overhead_percent = 1.0; // 1% max overhead
};

class WormAuditLogger {
public:
    explicit WormAuditLogger(const AuditLogConfig& config);
    ~WormAuditLogger();

    // Core logging operations
    std::string LogOperation(const AuditLogEntry& entry);
    std::string LogKernelLaunch(const std::string& kernel_name,
                              const json& launch_params,
                              const json& performance_data,
                              const std::string& operator_id = "");
    std::string LogMemoryOperation(const std::string& operation_type,
                                 size_t size_bytes,
                                 const std::string& device_id = "",
                                 const std::string& operator_id = "");
    std::string LogPerformanceOptimization(const std::string& optimization_name,
                                          const json& before_metrics,
                                          const json& after_metrics,
                                          const std::string& operator_id = "");
    std::string LogError(const std::string& error_type,
                        const std::string& error_message,
                        const json& context = {},
                        const std::string& operator_id = "");

    // Configuration management
    bool UpdateConfiguration(const AuditLogConfig& new_config);
    AuditLogConfig GetCurrentConfiguration() const;
    bool SetLogLevel(LogLevel level);
    void EnableRealTimeVerification(bool enabled);
    void SetPerformanceOverheadLimit(double max_overhead_percent);

    // Integrity verification
    IntegrityVerificationResult VerifyLogIntegrity() const;
    bool VerifyEntryIntegrity(const std::string& entry_id) const;
    std::vector<std::string> DetectTampering() const;
    bool RestoreFromBackup() const;

    // Log management
    bool RotateLogFile();
    bool CompactLogs();
    std::vector<AuditLogEntry> GetEntriesByTimeRange(
        std::chrono::system_clock::time_point start,
        std::chrono::system_clock::time_point end) const;
    std::vector<AuditLogEntry> GetEntriesByOperator(const std::string& operator_id) const;
    std::vector<AuditLogEntry> GetEntriesByOperationType(OperationType type) const;

    // Performance monitoring
    double GetAverageLoggingOverhead() const;
    std::chrono::microseconds GetMaxLoggingLatency() const;
    size_t GetTotalLogEntries() const;
    size_t GetCurrentLogFileSize() const;

    // Search and analysis
    std::vector<AuditLogEntry> SearchLogs(const std::string& query) const;
    std::map<std::string, size_t> GetOperationStatistics() const;
    std::map<LogLevel, size_t> GetLogLevelDistribution() const;
    std::vector<std::string> GetCriticalErrors() const;

    // Export and backup
    bool ExportLogs(const std::string& export_path, const std::string& format = "json") const;
    bool CreateBackup() const;
    bool RestoreFromBackup(const std::string& backup_path);

private:
    AuditLogConfig config_;
    mutable std::mutex log_mutex_;

    // Log file management
    std::ofstream log_file_;
    std::string current_log_file_path_;
    size_t current_file_size_;

    // Integrity tracking
    std::string last_entry_hash_;
    std::map<std::string, AuditLogEntry> entry_cache_;
    std::vector<std::string> entry_chain_;

    // Performance monitoring
    std::chrono::steady_clock::time_point start_time_;
    std::vector<std::chrono::microseconds> logging_latencies_;
    double total_logging_overhead_;

    // Background verification
    std::thread verification_thread_;
    std::atomic<bool> stop_verification_;
    IntegrityStatus last_integrity_status_;

    // Internal methods
    bool InitializeLogFile();
    void WriteEntryToFile(const AuditLogEntry& entry);
    std::string GenerateEntryId() const;
    std::string CalculateChainHash(const std::string& current_entry_hash) const;
    bool ValidateEntryFormat(const AuditLogEntry& entry) const;

    // Performance optimization
    void UpdatePerformanceMetrics(std::chrono::microseconds logging_time);
    bool IsWithinPerformanceBudget() const;

    // Integrity verification
    void VerificationLoop();
    bool VerifyEntryChain() const;
    bool VerifySingleEntry(const AuditLogEntry& entry) const;

    // Log rotation and management
    bool ShouldRotateLogFile() const;
    bool PerformLogRotation();
    std::string GenerateLogFileName(size_t sequence_number) const;

    // File operations
    bool WriteToFile(const std::string& content);
    std::string ReadFromFile(const std::string& file_path) const;
    bool BackupCurrentFile() const;

    // Serialization helpers
    std::string SerializeEntry(const AuditLogEntry& entry) const;
    AuditLogEntry DeserializeEntry(const std::string& serialized_entry) const;
    json EntryToJson(const AuditLogEntry& entry) const;
    AuditLogEntry JsonToEntry(const json& j) const;
};

/**
 * @brief Scoped audit logger for automatic operation logging
 */
class ScopedAuditLogger {
public:
    ScopedAuditLogger(WormAuditLogger& logger,
                     OperationType operation_type,
                     const std::string& operation_description,
                     const std::string& operator_id = "");

    ~ScopedAuditLogger();

    void SetSuccess(bool successful);
    void SetError(const std::string& error_message);
    void AddMetadata(const std::string& key, const std::string& value);
    void AddPerformanceMetric(const std::string& metric, double value);

private:
    WormAuditLogger& logger_;
    AuditLogEntry entry_;
    std::chrono::high_resolution_clock::time_point start_time_;
    bool completed_;
};

/**
 * @brief Factory function to create and configure WORM audit logger
 */
std::unique_ptr<WormAuditLogger> CreateWormAuditLogger(
    const std::string& log_file_path = "audit.log",
    LogLevel minimum_level = LogLevel::INFO,
    bool enable_real_time_verification = true);

} // namespace puzzle71::gpu::performance