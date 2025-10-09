/**
 * Puzzle71Solver - Integration Metrics Collection System
 *
 * Provides comprehensive metrics collection and analysis for third-party library
 * integration operations, including performance tracking, quality metrics,
 * and trend analysis.
 *
 * @author       Puzzle71Solver Team
 * @created      2025-10-09
 * @license      MIT
 */

#pragma once

#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <memory>
#include <functional>
#include <filesystem>

namespace integration::metrics {

// Basic metrics types
using MetricValue = double;
using MetricTimestamp = std::chrono::system_clock::time_point;

// Metric categories
enum class MetricCategory {
    PERFORMANCE,     // Timing, resource usage, efficiency
    QUALITY,        // Completeness, correctness, coverage
    RELIABILITY,    // Success rates, failure analysis
    OPERATIONAL,    // Usage statistics, health status
    COMPLIANCE      // Attribution, license, standards compliance
};

// Metric types
enum class MetricType {
    COUNTER,        // Cumulative count (e.g., total integrations)
    GAUGE,          // Current value (e.g., memory usage)
    TIMER,          // Duration measurements
    RATIO,          // Percentage or fraction
    HISTOGRAM,      // Distribution of values
    BOOLEAN         // Yes/no, success/failure
};

// Individual metric data point
struct MetricDataPoint {
    MetricTimestamp timestamp;
    MetricValue value;
    std::map<std::string, std::string> tags;
    std::string description;
};

// Metric definition
struct MetricDefinition {
    std::string name;
    std::string description;
    MetricCategory category;
    MetricType type;
    std::string unit;
    std::map<std::string, std::string> default_tags;
};

// Performance metrics
struct PerformanceMetrics {
    // Timing metrics
    std::chrono::milliseconds total_integration_time{0};
    std::chrono::milliseconds extraction_time{0};
    std::chrono::milliseconds attribution_time{0};
    std::chrono::milliseconds build_time{0};
    std::chrono::milliseconds verification_time{0};

    // Resource metrics
    size_t peak_memory_usage_bytes{0};
    size_t disk_space_used_bytes{0};
    size_t network_bytes_transferred{0};

    // Efficiency metrics
    double files_processed_per_second{0.0};
    double megabytes_per_second{0.0};
    double cpu_utilization_percent{0.0};

    // Quality metrics
    size_t total_files_processed{0};
    size_t successful_operations{0};
    size_t failed_operations{0};
    double success_rate{0.0};
};

// Quality metrics
struct QualityMetrics {
    // Completeness metrics
    double attribution_coverage_percent{0.0};
    double source_integrity_score{0.0};
    double documentation_coverage_percent{0.0};

    // Correctness metrics
    double build_success_rate{0.0};
    double test_pass_rate{0.0};
    double verification_success_rate{0.0};

    // Standards compliance
    double license_compliance_score{0.0};
    double coding_standards_score{0.0};
    double attribution_standards_score{0.0};
};

// Reliability metrics
struct ReliabilityMetrics {
    // Success/failure rates
    double overall_success_rate{0.0};
    double extraction_success_rate{0.0};
    double build_success_rate{0.0};
    double attribution_success_rate{0.0};

    // Failure analysis
    std::map<std::string, size_t> failure_types;
    std::map<std::string, std::chrono::milliseconds> recovery_times;
    double mean_recovery_time_seconds{0.0};

    // System health
    double system_availability_percent{0.0};
    double mean_time_between_failures_hours{0.0};
};

// Operational metrics
struct OperationalMetrics {
    // Usage statistics
    size_t total_integrations{0};
    size_t active_libraries{0};
    size_t concurrent_operations{0};

    // Resource utilization
    double disk_space_utilization_percent{0.0};
    double memory_utilization_percent{0.0};
    double cpu_utilization_percent{0.0};

    // Health status
    size_t warning_count{0};
    size_t error_count{0};
    size_t critical_issues_count{0};
};

// Compliance metrics
struct ComplianceMetrics {
    // Attribution compliance
    double attribution_header_coverage{0.0};
    double license_file_coverage{0.0};
    double source_origin_tracking{0.0};

    // License compliance
    double license_compatibility_score{0.0};
    double license_documentation_score{0.0};

    // Standards compliance
    double coding_standards_adherence{0.0};
    double documentation_completeness{0.0};
    double integration_process_compliance{0.0};
};

// Comprehensive metrics report
struct IntegrationMetricsReport {
    std::string library_name;
    MetricTimestamp report_timestamp;
    std::chrono::seconds reporting_period{0};

    PerformanceMetrics performance;
    QualityMetrics quality;
    ReliabilityMetrics reliability;
    OperationalMetrics operational;
    ComplianceMetrics compliance;

    // Trend data
    std::map<std::string, std::vector<MetricDataPoint>> historical_data;

    // Summary scores
    double overall_score{0.0};
    std::map<std::string, double> category_scores;
    std::vector<std::string> recommendations;
};

// Metrics collector interface
class MetricsCollector {
public:
    virtual ~MetricsCollector() = default;

    // Core metrics collection
    virtual void record_metric(const std::string& name, MetricValue value,
                              const std::map<std::string, std::string>& tags = {}) = 0;
    virtual void start_timer(const std::string& name,
                           const std::map<std::string, std::string>& tags = {}) = 0;
    virtual void end_timer(const std::string& name) = 0;
    virtual void increment_counter(const std::string& name, MetricValue increment = 1.0,
                                 const std::map<std::string, std::string>& tags = {}) = 0;
    virtual void set_gauge(const std::string& name, MetricValue value,
                         const std::map<std::string, std::string>& tags = {}) = 0;

    // Batch operations
    virtual void record_batch(const std::vector<std::pair<std::string, MetricValue>>& metrics,
                             const std::map<std::string, std::string>& tags = {}) = 0;

    // Query operations
    virtual std::vector<MetricDataPoint> get_metric_history(const std::string& name,
                                                          const MetricTimestamp& since,
                                                          const MetricTimestamp& until = {}) const = 0;
    virtual MetricValue get_latest_value(const std::string& name) const = 0;
    virtual std::map<std::string, MetricValue> get_all_latest_values() const = 0;

    // Configuration
    virtual void set_metric_definition(const MetricDefinition& definition) = 0;
    virtual void enable_metric(const std::string& name, bool enabled = true) = 0;
    virtual void set_retention_period(const std::string& name, std::chrono::seconds period) = 0;
};

// Metrics analyzer
class MetricsAnalyzer {
public:
    virtual ~MetricsAnalyzer() = default;

    // Analysis functions
    virtual IntegrationMetricsReport generate_report(const std::string& library_name,
                                                     const MetricTimestamp& since,
                                                     const MetricTimestamp& until = {}) const = 0;

    virtual std::vector<std::string> detect_anomalies(const std::string& library_name,
                                                       const MetricTimestamp& since) const = 0;

    virtual std::map<std::string, double> calculate_trends(const std::string& library_name,
                                                          const MetricTimestamp& since) const = 0;

    // Predictive analysis
    virtual std::map<std::string, double> predict_performance(const std::string& library_name) const = 0;
    virtual std::vector<std::string> generate_recommendations(const std::string& library_name) const = 0;
};

// Metrics exporter
class MetricsExporter {
public:
    virtual ~MetricsExporter() = default;

    // Export functions
    virtual bool export_to_json(const std::string& filename,
                               const MetricTimestamp& since,
                               const MetricTimestamp& until = {}) const = 0;
    virtual bool export_to_csv(const std::string& filename,
                              const std::string& metric_name,
                              const MetricTimestamp& since,
                              const MetricTimestamp& until = {}) const = 0;
    virtual bool export_to_prometheus_format(const std::string& filename) const = 0;

    // Real-time streaming
    virtual void start_realtime_export(const std::string& endpoint,
                                     std::chrono::seconds interval) = 0;
    virtual void stop_realtime_export() = 0;
};

// Main metrics manager
class MetricsManager {
public:
    MetricsManager();
    ~MetricsManager();

    // Initialization
    bool initialize(const std::filesystem::path& data_dir);
    bool shutdown();

    // Core functionality
    MetricsCollector& collector();
    MetricsAnalyzer& analyzer();
    std::unique_ptr<MetricsExporter> create_exporter(const std::string& type);

    // Convenience methods for common operations
    void record_integration_start(const std::string& library_name);
    void record_integration_complete(const std::string& library_name,
                                   const PerformanceMetrics& metrics);
    void record_integration_failure(const std::string& library_name,
                                   const std::string& error_type);
    void record_quality_metrics(const std::string& library_name,
                               const QualityMetrics& metrics);

    // Reporting
    IntegrationMetricsReport generate_comprehensive_report(const std::string& library_name,
                                                         const MetricTimestamp& since,
                                                         const MetricTimestamp& until = {}) const;

    // Configuration
    void set_default_tags(const std::map<std::string, std::string>& tags);
    void enable_auto_export(bool enabled, std::chrono::seconds interval);
    void set_retention_policy(std::chrono::seconds default_retention);

private:
    struct Impl;
    std::unique_ptr<Impl> p_impl;
};

// Global metrics manager instance
MetricsManager& get_metrics_manager();

// Convenience macros for metrics collection
#define INTEGRATION_METRICS_START(library_name) \
    integration::metrics::get_metrics_manager().record_integration_start(library_name)

#define INTEGRATION_METRICS_COMPLETE(library_name, perf_metrics) \
    integration::metrics::get_metrics_manager().record_integration_complete(library_name, perf_metrics)

#define INTEGRATION_METRICS_FAILURE(library_name, error_type) \
    integration::metrics::get_metrics_manager().record_integration_failure(library_name, error_type)

#define INTEGRATION_TIMER_SCOPE(name) \
    integration::metrics::TimerScope timer_scope(name)

// RAII timer helper
class TimerScope {
public:
    TimerScope(const std::string& name,
              const std::map<std::string, std::string>& tags = {});
    ~TimerScope();

private:
    std::string timer_name_;
    std::chrono::high_resolution_clock::time_point start_time_;
};

// Metric definitions
namespace metrics {
    // Performance metrics
    extern const MetricDefinition INTEGRATION_TOTAL_TIME;
    extern const MetricDefinition EXTRACTION_TIME;
    extern const MetricDefinition ATTRIBUTION_TIME;
    extern const MetricDefinition BUILD_TIME;
    extern const MetricDefinition VERIFICATION_TIME;

    // Quality metrics
    extern const MetricDefinition ATTRIBUTION_COVERAGE;
    extern const MetricDefinition SOURCE_INTEGRITY_SCORE;
    extern const MetricDefinition BUILD_SUCCESS_RATE;

    // Reliability metrics
    extern const MetricDefinition OVERALL_SUCCESS_RATE;
    extern const MetricDefinition FAILURE_COUNT;
    extern const MetricDefinition RECOVERY_TIME;

    // Operational metrics
    extern const MetricDefinition DISK_USAGE;
    extern const MetricDefinition MEMORY_USAGE;
    extern const MetricDefinition CPU_UTILIZATION;

    // Compliance metrics
    extern const MetricDefinition LICENSE_COMPLIANCE;
    extern const MetricDefinition ATTRIBUTION_COMPLIANCE;
    extern const MetricDefinition STANDARDS_COMPLIANCE;
}

} // namespace integration::metrics