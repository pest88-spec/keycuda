#pragma once

#include <memory>
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <fstream>
#include <sstream>
#include <mutex>
#include <nlohmann/json.hpp>
#include "adaptive_parallelism_scaling.h"

namespace keycuda {
namespace gpu {
namespace performance {

/**
 * @brief Configuration Decision Logging System
 *
 * Provides comprehensive logging and analysis of all configuration decisions
 * made by the adaptive parallelism scaling system. Features:
 * - Real-time decision logging with full context
 * - JSON and human-readable output formats
 * - Decision analysis and trend tracking
 * - Performance impact correlation
 * - Audit trail for compliance and debugging
 */

enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARNING = 2,
    ERROR = 3,
    CRITICAL = 4
};

enum class DecisionType {
    INITIAL_CONFIGURATION,
    MEMORY_CONSTRAINT_ADJUSTMENT,
    PERFORMANCE_BASED_SCALING,
    OCCUPANCY_OPTIMIZATION,
    DYNAMIC_RUNTIME_SCALING,
    FALLBACK_ACTIVATION,
    ARCHITECTURE_SPECIFIC_TWEAK,
    ERROR_RECOVERY,
    CONSTRAINT_VIOLATION,
    MANUAL_OVERRIDE
};

struct ConfigurationDecision {
    std::string decision_id;
    std::chrono::system_clock::time_point timestamp;
    DecisionType decision_type;
    LogLevel log_level;

    // Context information
    std::string gpu_name;
    int compute_capability;
    size_t total_memory_mb;
    size_t free_memory_mb;
    double memory_bandwidth_gb_per_sec;
    int sm_count;

    // Input configuration
    ParallelismConfiguration input_config;
    size_t workload_size;
    std::string operation_type;

    // Output configuration
    ParallelismConfiguration output_config;

    // Decision details
    std::string decision_logic;
    std::string rationale;
    double confidence_score;
    std::vector<std::string> constraints_applied;
    std::map<std::string, double> optimization_metrics;

    // Performance impact
    double estimated_throughput_mkeys_per_sec;
    double estimated_execution_time_ms;
    double memory_utilization_estimate;
    double expected_occupancy;

    // Additional metadata
    std::map<std::string, std::string> metadata;
    std::string error_message;  // Only for error decisions
};

struct DecisionAnalysis {
    std::chrono::system_clock::time_point analysis_timestamp;
    size_t total_decisions;
    size_t successful_configurations;
    size_t failed_configurations;
    double average_confidence_score;
    std::map<DecisionType, size_t> decision_type_counts;
    std::map<std::string, size_t> constraint_frequency;
    std::vector<std::string> most_common_rationales;
    double average_throughput_improvement;
    std::vector<std::string> performance_trends;
};

class ConfigurationLogger {
public:
    ConfigurationLogger();
    explicit ConfigurationLogger(const std::string& log_file_path);
    ~ConfigurationLogger();

    // Core logging methods
    void LogConfigurationDecision(const ConfigurationDecision& decision);
    void LogInitialConfiguration(const ParallelismConfiguration& config,
                                const GpuCapabilities& gpu_caps,
                                size_t workload_size,
                                double confidence_score,
                                const std::string& rationale);

    void LogMemoryConstraintAdjustment(const ParallelismConfiguration& input_config,
                                       const ParallelismConfiguration& output_config,
                                       const GpuCapabilities& gpu_caps,
                                       double memory_pressure,
                                       const std::string& constraint_type);

    void LogPerformanceBasedScaling(const ParallelismConfiguration& input_config,
                                    const ParallelismConfiguration& output_config,
                                    double current_throughput,
                                    double target_throughput,
                                    const std::string& scaling_reason);

    void LogDynamicRuntimeScaling(const ParallelismConfiguration& old_config,
                                  const ParallelismConfiguration& new_config,
                                  double performance_ratio,
                                  const std::string& trigger_event);

    void LogFallbackActivation(const std::string& reason,
                              const ParallelismConfiguration& safe_config,
                              const std::vector<std::string>& attempted_configs);

    void LogError(const std::string& error_message,
                  const ParallelismConfiguration& failed_config,
                  const std::string& error_type);

    // Analysis and reporting
    DecisionAnalysis GenerateAnalysis(std::chrono::hours time_window = std::chrono::hours(24)) const;
    std::vector<ConfigurationDecision> GetRecentDecisions(size_t count = 100) const;
    std::vector<ConfigurationDecision> GetDecisionsByType(DecisionType type,
                                                         std::chrono::hours time_window = std::chrono::hours(24)) const;

    // Export methods
    std::string ExportToJson(std::chrono::hours time_window = std::chrono::hours(24)) const;
    std::string ExportToCsv(std::chrono::hours time_window = std::chrono::hours(24)) const;
    std::string GenerateDecisionReport(std::chrono::hours time_window = std::chrono::hours(24)) const;

    // Configuration
    void SetLogLevel(LogLevel level);
    void EnableFileLogging(bool enabled);
    void SetLogFilePath(const std::string& file_path);
    void SetMaxLogEntries(size_t max_entries);
    void EnableRealTimeAnalysis(bool enabled);

    // Statistics
    size_t GetTotalDecisions() const;
    size_t GetDecisionsInLastHour() const;
    double GetAverageConfidenceScore() const;
    std::map<DecisionType, double> GetDecisionTypeDistribution() const;

    // Cleanup
    void ClearOldEntries(std::chrono::hours max_age = std::chrono::hours(168)); // 1 week default
    void ClearAllEntries();

private:
    std::vector<ConfigurationDecision> decisions_;
    mutable std::mutex decisions_mutex_;

    std::string log_file_path_;
    bool file_logging_enabled_;
    LogLevel current_log_level_;
    size_t max_log_entries_;
    bool real_time_analysis_enabled_;

    // Internal methods
    std::string GenerateDecisionId() const;
    std::string DecisionTypeToString(DecisionType type) const;
    std::string LogLevelToString(LogLevel level) const;
    std::string FormatDecisionForConsole(const ConfigurationDecision& decision) const;
    nlohmann::json DecisionToJson(const ConfigurationDecision& decision) const;

    void WriteToFile(const std::string& message);
    void UpdateStatistics(const ConfigurationDecision& decision);
    void PerformRealTimeAnalysis(const ConfigurationDecision& decision);

    // Analysis helpers
    std::vector<std::string> ExtractTopRationales(size_t count = 5) const;
    std::vector<std::string> IdentifyPerformanceTrends(std::chrono::hours time_window) const;
    double CalculateThroughputTrend(std::chrono::hours time_window) const;
    std::map<std::string, size_t> AnalyzeConstraintFrequency() const;

    // Statistics tracking
    mutable std::mutex stats_mutex_;
    size_t total_decisions_;
    std::chrono::system_clock::time_point last_analysis_;
    std::map<DecisionType, size_t> decision_type_counts_;
    std::map<std::string, size_t> rationale_counts_;
    double cumulative_confidence_;
    size_t confidence_samples_;
};

// Factory function
std::unique_ptr<ConfigurationLogger> CreateConfigurationLogger(const std::string& log_file_path = "");

// Utility functions
std::string FormatConfigurationForLog(const ParallelismConfiguration& config);
std::string FormatGpuCapabilitiesForLog(const GpuCapabilities& caps);
std::string FormatTimestamp(const std::chrono::system_clock::time_point& timestamp);
std::string CalculatePerformanceImpact(const ParallelismConfiguration& old_config,
                                      const ParallelismConfiguration& new_config);

} // namespace performance
} // namespace gpu
} // namespace keycuda