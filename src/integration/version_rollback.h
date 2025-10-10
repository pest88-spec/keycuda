// T050: Version rollback capability for compatibility issues
// Header file for comprehensive version rollback system

#ifndef INTEGRATION_VERSION_ROLLBACK_H
#define INTEGRATION_VERSION_ROLLBACK_H

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <chrono>
#include <memory>
#include <filesystem>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace integration {

// Structure for version backup information
struct VersionBackup {
    std::string backup_id;
    std::string dependency_name;
    std::string previous_version;
    std::string backup_path;
    std::string backup_timestamp;
    std::string backup_type;  // "full", "incremental", "config"
    std::map<std::string, std::string> metadata;
    std::vector<std::string> backed_up_files;
    std::vector<std::string> backed_up_configs;
    size_t backup_size_bytes = 0;
    std::string checksum;
    bool is_valid = true;
    bool is_compressed = false;
    std::string compression_algorithm;
};

// Structure for rollback planning
struct RollbackPlan {
    std::string rollback_id;
    std::string target_dependency;
    std::string from_version;
    std::string to_version;
    std::string rollback_type;  // "full", "partial", "config"
    std::vector<std::string> rollback_steps;
    std::vector<std::string> prerequisites;
    std::vector<std::string> post_rollback_actions;
    std::vector<std::string> dependencies_to_rollback;
    std::map<std::string, std::string> rollback_metadata;
    bool is_safe = true;
    std::string risk_level;  // "low", "medium", "high", "critical"
    std::vector<std::string> warnings;
    std::vector<std::string> potential_issues;
    double estimated_duration_seconds = 0.0;
    std::string backup_id_to_use;
    bool requires_downtime = false;
    int estimated_downtime_seconds = 0;
};

// Structure for rollback execution results
struct RollbackResult {
    std::string rollback_id;
    bool success = false;
    std::string actual_version;
    std::string rollback_timestamp;
    std::vector<std::string> performed_actions;
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
    std::map<std::string, std::string> result_metadata;
    double rollback_duration_seconds = 0.0;
    std::vector<std::string> rolled_back_files;
    std::vector<std::string> restored_configs;
    std::string backup_used;
    bool automatic_rollback = false;
    std::string trigger_reason;
    std::vector<std::string> validation_results;
};

// Structure for rollback risk assessment
struct RollbackRiskAssessment {
    std::string risk_level;  // "low", "medium", "high", "critical"
    double risk_score = 0.0;  // 0.0 to 10.0
    std::vector<std::string> risk_factors;
    std::vector<std::string> mitigation_strategies;
    std::vector<std::string> potential_breakages;
    std::vector<std::string> data_loss_risks;
    std::vector<std::string> performance_impacts;
    bool requires_testing = false;
    bool requires_staging = false;
    bool requires_backup = true;
    std::string recommended_approach;
};

// Structure for rollback statistics
struct RollbackStatistics {
    int total_rollbacks = 0;
    int successful_rollbacks = 0;
    int failed_rollbacks = 0;
    int emergency_rollbacks = 0;
    int automatic_rollbacks = 0;
    double average_rollback_duration = 0.0;
    std::map<std::string, int> rollback_by_dependency;
    std::map<std::string, int> rollback_by_risk_level;
    std::map<std::string, double> rollback_duration_by_type;
    std::vector<std::string> most_common_failure_reasons;
    std::string last_rollback_timestamp;
    std::string most_rolled_back_dependency;
    double rollback_success_rate = 0.0;
};

// Structure for safe restore points
struct SafeRestorePoint {
    std::string point_id;
    std::string label;
    std::string created_timestamp;
    std::string description;
    std::map<std::string, std::string> dependency_versions;
    std::map<std::string, std::string> system_state;
    std::vector<std::string> included_dependencies;
    std::vector<std::string> config_snapshots;
    size_t total_size_bytes = 0;
    bool is_valid = true;
    std::string checksum;
};

// Structure for rollback audit trail entry
struct RollbackAuditEntry {
    std::string entry_id;
    std::string timestamp;
    std::string event_type;  // "backup_created", "rollback_planned", "rollback_executed", "rollback_verified"
    std::string user_id;
    std::string dependency_name;
    std::string from_version;
    std::string to_version;
    std::string rollback_id;
    std::string backup_id;
    bool success = false;
    std::string reason;
    std::map<std::string, std::string> metadata;
    std::string ip_address;
    std::string user_agent;
};

class VersionRollbackManager {
public:
    explicit VersionRollbackManager(const std::string& backup_storage_path);
    ~VersionRollbackManager();

    // Initialization and configuration
    bool initialize();
    bool isInitialized() const { return initialized_; }
    void setBackupStoragePath(const std::string& path);
    void setMaxBackupsPerDependency(int max_backups);
    void setCompressionEnabled(bool enabled);
    void setAutomaticBackupCreation(bool enabled);
    void setRollbackTimeoutSeconds(int timeout_seconds);

    // Backup creation and management
    VersionBackup createVersionBackup(const std::string& dependency_name,
                                     const std::string& current_version,
                                     const std::string& backup_type = "full");
    bool deleteBackup(const std::string& backup_id);
    std::vector<VersionBackup> listAvailableBackups(const std::string& dependency_name = "");
    std::vector<VersionBackup> listBackupsByType(const std::string& backup_type);
    VersionBackup getBackupInfo(const std::string& backup_id);
    bool validateBackupIntegrity(const std::string& backup_id);
    bool repairBackup(const std::string& backup_id);
    std::vector<std::string> getBackupStorageUsage();

    // Rollback planning and analysis
    RollbackPlan createRollbackPlan(const std::string& dependency_name,
                                   const std::string& target_version,
                                   const std::string& backup_id = "");
    bool validateRollbackPlan(const RollbackPlan& plan);
    RollbackRiskAssessment assessRollbackRisk(const RollbackPlan& plan);
    std::vector<std::string> analyzeRollbackImpact(const RollbackPlan& plan);
    bool checkRollbackPrerequisites(const RollbackPlan& plan);
    std::vector<std::string> getRollbackPrerequisites(const RollbackPlan& plan);
    bool simulateRollback(const RollbackPlan& plan);

    // Rollback execution
    RollbackResult executeRollback(const RollbackPlan& plan);
    bool rollbackToVersion(const std::string& dependency_name, const std::string& target_version);
    bool rollbackWithBackup(const std::string& backup_id);
    bool rollbackDependencyChain(const std::vector<std::string>& dependencies,
                                 const std::map<std::string, std::string>& target_versions);
    std::vector<std::string> previewRollbackActions(const RollbackPlan& plan);
    bool cancelRollback(const std::string& rollback_id);

    // Rollback verification and validation
    bool verifyRollbackSuccess(const RollbackResult& result);
    std::vector<std::string> validateRolledBackDependencies();
    bool testRolledBackConfiguration();
    std::vector<std::string> checkPostRollbackCompatibility();
    bool validateRolledBackFiles(const RollbackResult& result);
    bool runPostRollbackTests(const std::string& dependency_name);

    // Rollback history and audit
    std::vector<RollbackResult> getRollbackHistory(const std::string& dependency_name = "",
                                                  int limit = 100);
    bool recordRollbackEvent(const RollbackResult& result);
    bool recordAuditEntry(const RollbackAuditEntry& entry);
    RollbackStatistics getRollbackStatistics();
    std::vector<RollbackAuditEntry> getRollbackAuditTrail(const std::string& dependency_name = "");
    std::vector<RollbackAuditEntry> getAuditTrailByTimeRange(const std::string& start_time,
                                                            const std::string& end_time);
    bool exportAuditTrail(const std::string& file_path, const std::string& format = "json");

    // Emergency rollback capabilities
    RollbackResult emergencyRollback(const std::string& dependency_name);
    bool createSafeRestorePoint(const std::string& label = "",
                               const std::string& description = "");
    bool restoreFromSafePoint(const std::string& label);
    std::vector<SafeRestorePoint> getAvailableSafeRestorePoints();
    bool deleteSafeRestorePoint(const std::string& label);
    bool validateSafeRestorePoint(const std::string& label);
    bool createAutomaticSafePoints(bool enabled);

    // Scheduled rollback support
    bool scheduleRollback(const RollbackPlan& plan, const std::string& scheduled_time);
    bool cancelScheduledRollback(const std::string& rollback_id);
    std::vector<RollbackPlan> getScheduledRollbacks();
    bool executeScheduledRollbacks();

    // Rollback cleanup and maintenance
    bool cleanupOldBackups(int max_backups_to_keep = 10);
    bool cleanupFailedRollbacks();
    std::vector<std::string> getOrphanedBackups();
    bool optimizeBackupStorage();
    bool compactBackups(const std::string& dependency_name);
    bool migrateBackupStorage(const std::string& new_path);

    // Configuration and settings management
    json getConfiguration() const;
    bool setConfiguration(const json& config);
    bool loadConfigurationFromFile(const std::string& config_file);
    bool saveConfigurationToFile(const std::string& config_file) const;
    void resetToDefaults();

    // Monitoring and health checks
    bool isHealthy() const;
    std::vector<std::string> getHealthCheckResults();
    std::map<std::string, std::string> getSystemStatus();
    bool testBackupRestoreCapability();
    double getBackupCompressionRatio() const;
    std::vector<std::string> getPerformanceMetrics();

    // Integration with other systems
    bool integrateWithDependencyManager();
    bool integrateWithBuildSystem();
    bool integrateWithDeploymentSystem();
    std::vector<std::string> getIntegrationStatus();
    bool syncWithExternalSources();

    // Advanced features
    bool createDifferentialBackup(const std::string& dependency_name,
                                 const std::string& base_backup_id);
    bool performIncrementalRollback(const std::string& dependency_name,
                                   const std::string& target_version);
    bool rollbackWithValidation(const RollbackPlan& plan,
                               const std::vector<std::string>& validation_tests);
    std::vector<std::string> generateRollbackReport(const RollbackResult& result);

private:
    std::string backup_storage_path_;
    bool initialized_ = false;
    int max_backups_per_dependency_ = 10;
    bool compression_enabled_ = true;
    bool automatic_backup_creation_ = true;
    int rollback_timeout_seconds_ = 300;  // 5 minutes

    // Internal storage
    std::map<std::string, VersionBackup> backups_;
    std::map<std::string, RollbackPlan> rollback_plans_;
    std::vector<RollbackResult> rollback_history_;
    std::vector<RollbackAuditEntry> audit_trail_;
    std::map<std::string, SafeRestorePoint> safe_restore_points_;
    std::map<std::string, RollbackPlan> scheduled_rollbacks_;

    // Internal helper functions
    bool ensureBackupDirectoryExists(const std::string& path);
    std::string generateBackupId() const;
    std::string generateRollbackId() const;
    std::string getCurrentTimestamp() const;
    std::string calculateChecksum(const std::string& file_path) const;
    std::string compressBackup(const std::string& source_path, const std::string& target_path) const;
    bool decompressBackup(const std::string& compressed_path, const std::string& target_path) const;

    // Backup creation helpers
    bool createFullBackup(const std::string& dependency_name,
                         const std::string& current_version,
                         VersionBackup& backup);
    bool createIncrementalBackup(const std::string& dependency_name,
                                const std::string& current_version,
                                const std::string& base_backup_id,
                                VersionBackup& backup);
    bool createConfigBackup(const std::string& dependency_name,
                           const std::string& current_version,
                           VersionBackup& backup);
    std::vector<std::string> getDependencyFiles(const std::string& dependency_name) const;
    std::vector<std::string> getDependencyConfigs(const std::string& dependency_name) const;

    // Rollback execution helpers
    bool executeRollbackStep(const std::string& step, RollbackResult& result);
    bool restoreFilesFromBackup(const VersionBackup& backup, RollbackResult& result);
    bool restoreConfigsFromBackup(const VersionBackup& backup, RollbackResult& result);
    bool updateDependencyVersion(const std::string& dependency_name,
                                const std::string& new_version);
    bool validateRollbackResult(const RollbackPlan& plan, RollbackResult& result);

    // Risk assessment helpers
    double calculateRiskScore(const RollbackPlan& plan) const;
    std::vector<std::string> identifyRiskFactors(const RollbackPlan& plan) const;
    std::vector<std::string> generateMitigationStrategies(const RollbackPlan& plan) const;
    std::string determineRiskLevel(double risk_score) const;
    bool analyzeDependencyImpact(const std::string& dependency_name,
                                const std::string& from_version,
                                const std::string& to_version);

    // File system helpers
    bool copyFile(const std::string& source, const std::string& destination);
    bool copyDirectory(const std::string& source, const std::string& destination);
    std::vector<std::string> findFiles(const std::string& directory,
                                      const std::string& pattern = "*") const;
    size_t calculateDirectorySize(const std::string& directory) const;
    bool createDirectory(const std::string& path) const;
    bool removeDirectory(const std::string& path) const;

    // Serialization helpers
    json backupToJSON(const VersionBackup& backup) const;
    VersionBackup jsonToBackup(const json& json_backup) const;
    json rollbackPlanToJSON(const RollbackPlan& plan) const;
    RollbackPlan jsonToRollbackPlan(const json& json_plan) const;
    json rollbackResultToJSON(const RollbackResult& result) const;
    RollbackResult jsonToRollbackResult(const json& json_result) const;
    json riskAssessmentToJSON(const RollbackRiskAssessment& assessment) const;
    RollbackRiskAssessment jsonToRiskAssessment(const json& json_assessment) const;

    // Validation helpers
    bool validateDependencyName(const std::string& name) const;
    bool validateVersionString(const std::string& version) const;
    bool validateBackupPath(const std::string& path) const;
    bool validateRollbackId(const std::string& rollback_id) const;

    // Logging and monitoring helpers
    void logEvent(const std::string& event_type, const std::string& message) const;
    void logError(const std::string& error_type, const std::string& message) const;
    void logWarning(const std::string& warning_type, const std::string& message) const;
    void updateMetrics(const std::string& metric_name, double value) const;

    // Performance optimization helpers
    std::vector<VersionBackup> cacheBackups(const std::string& dependency_name) const;
    bool isCacheValid(const std::string& cache_key) const;
    void invalidateCache(const std::string& cache_key) const;
    void performPeriodicMaintenance();
};

} // namespace integration

#endif // INTEGRATION_VERSION_ROLLBACK_H