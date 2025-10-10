/**
 * @file           metrics.h
 * @brief          Integration metrics collection system for Puzzle71Solver
 * @author         Puzzle71Solver Team
 * @origin         https://github.com/Puzzle71Solver/Puzzle71Solver
 * @origin_path    src/integration/metrics.h
 * @origin_commit  <current_commit>
 * @origin_license MIT
 * @extracted_date   2025-10-10
 * @extracted_by     Puzzle71Solver Team
 * @modifications    Created for third-party dependency integration optimization
 * @spdx_license_identifier MIT
 *
 * Provides comprehensive metrics collection for integration operations including
 * performance tracking, success rates, resource usage, and quality metrics.
 */

#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <chrono>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace puzzle71 {
namespace integration {

// Forward declarations
class BuildTimeMeasurement;
class BaselineMetrics;
class EvidenceCollector;
class DigestVerifier;
class MetadataEnforcer;

/**
 * @brief Resource usage metrics
 */
struct ResourceUsage {
    double cpu_percent = 0.0;           ///< CPU usage percentage
    double memory_mb = 0.0;            ///< Memory usage in MB
    double disk_io_mb = 0.0;           ///< Disk I/O in MB
    double network_io_mb = 0.0;        ///< Network I/O in MB
    size_t thread_count = 0;           ///< Number of active threads
    size_t file_descriptor_count = 0;   ///< Number of open file descriptors

    ResourceUsage() = default;
};

/**
 * @brief Performance metric data point
 */
struct PerformanceMetric {
    std::string name;                                   ///< Metric name
    double value;                                       ///< Metric value
    std::string unit;                                   ///< Unit of measurement
    std::map<std::string, std::string> tags;          ///< Additional tags
    std::chrono::system_clock::time_point timestamp;    ///< When metric was recorded

    PerformanceMetric() = default;
    PerformanceMetric(const std::string& n, double v, const std::string& u)
        : name(n), value(v), unit(u), timestamp(std::chrono::system_clock::now()) {}
};

/**
 * @brief Metric statistics
 */
struct MetricStatistics {
    size_t count = 0;           ///< Number of data points
    double sum_value = 0.0;     ///< Sum of all values
    double avg_value = 0.0;     ///< Average value
    double min_value = 0.0;     ///< Minimum value
    double max_value = 0.0;     ///< Maximum value

    MetricStatistics() = default;
};

/**
 * @brief Integration session tracking
 */
struct IntegrationSession {
    enum class Status {
        ACTIVE,
        COMPLETED,
        FAILED,
        CANCELLED
    };

    std::string session_id;                                    ///< Unique session identifier
    std::string operation;                                     ///< Operation being performed
    std::chrono::steady_clock::time_point start_time;         ///< Session start time
    std::chrono::steady_clock::time_point end_time;           ///< Session end time
    std::map<std::string, std::string> parameters;           ///< Session parameters
    Status status = Status::ACTIVE;                           ///< Current status
    double duration_seconds = 0.0;                            ///< Session duration
    std::map<std::string, std::string> results;              ///< Session results
    ResourceUsage resource_usage;                             ///< Resource usage during session

    IntegrationSession() = default;
    IntegrationSession(const std::string& id, const std::string& op,
                      const std::chrono::steady_clock::time_point& start)
        : session_id(id), operation(op), start_time(start) {}
};

/**
 * @brief Operation-level metrics
 */
struct OperationMetrics {
    std::string operation_name;              ///< Name of the operation
    size_t total_attempts = 0;              ///< Total number of attempts
    size_t successful_operations = 0;       ///< Number of successful operations
    double total_duration_seconds = 0.0;    ///< Total duration across all attempts

    OperationMetrics() = default;
    explicit OperationMetrics(const std::string& name) : operation_name(name) {}

    /**
     * @brief Calculate average duration
     * @return Average duration in seconds
     */
    double avg_duration_seconds() const {
        return total_attempts > 0 ? total_duration_seconds / total_attempts : 0.0;
    }

    /**
     * @brief Calculate success rate
     * @return Success rate as percentage (0-100)
     */
    double success_rate() const {
        return total_attempts > 0 ? (static_cast<double>(successful_operations) / total_attempts) * 100.0 : 0.0;
    }
};

/**
 * @brief Integration metrics configuration
 */
struct MetricsConfig {
    std::chrono::seconds collection_interval_seconds{60};  ///< Collection interval
    size_t max_history_entries = 10000;                    ///< Maximum history entries to keep
    bool log_all_metrics = false;                          ///< Log all metrics or only significant ones
    bool enable_real_time_collection = true;              ///< Enable real-time metrics collection
    std::vector<std::string> critical_metrics = {          ///< Critical metrics to always log
        "build_time", "success_rate", "memory_usage", "cpu_usage"
    };

    MetricsConfig() = default;
};

/**
 * @brief Current integration metrics snapshot
 */
struct IntegrationMetrics {
    std::chrono::system_clock::time_point timestamp;       ///< When metrics were captured
    size_t active_sessions;                                ///< Number of currently active sessions
    size_t total_sessions;                                 ///< Total sessions in history
    ResourceUsage current_resource_usage;                  ///< Current resource usage
    std::map<std::string, OperationMetrics> operation_metrics; ///< Per-operation metrics
    std::map<std::string, MetricStatistics> metric_statistics; ///< Metric statistics
    std::map<std::string, double> success_rates;           ///< Operation success rates

    IntegrationMetrics() : active_sessions(0), total_sessions(0) {}
};

/**
 * @brief Performance metrics collection result
 */
struct PerformanceMetrics {
    std::string metric_name;                               ///< Name of the metric
    std::vector<PerformanceMetric> metrics;                ///< Individual metric data points
    MetricStatistics statistics;                           ///< Computed statistics

    PerformanceMetrics() = default;
    explicit PerformanceMetrics(const std::string& name) : metric_name(name) {}
};

/**
 * @brief Integration metrics collector
 *
 * Provides comprehensive metrics collection for integration operations including
 * performance tracking, success rates, resource usage, and quality metrics.
 */
class IntegrationMetricsCollector {
public:
    /**
     * @brief Get singleton instance
     * @return Metrics collector instance
     */
    static IntegrationMetricsCollector& instance();

    /**
     * @brief Initialize metrics collector
     * @param storage_path Path for metrics storage
     * @param config Metrics collection configuration
     * @return True if initialization successful
     */
    bool initialize(const std::string& storage_path,
                   const MetricsConfig& config = MetricsConfig{});

    /**
     * @brief Record start of an integration operation
     * @param operation Operation name
     * @param parameters Operation parameters
     * @return Session ID for tracking
     */
    void record_integration_start(const std::string& operation,
                                 const std::map<std::string, std::string>& parameters = {});

    /**
     * @brief Record completion of an integration operation
     * @param session_id Session identifier
     * @param success Whether operation succeeded
     * @param results Operation results
     */
    void record_integration_complete(const std::string& session_id,
                                   bool success,
                                   const std::map<std::string, std::string>& results = {});

    /**
     * @brief Record resource usage for a session
     * @param session_id Session identifier
     * @param usage Resource usage metrics
     */
    void record_resource_usage(const std::string& session_id,
                               const ResourceUsage& usage);

    /**
     * @brief Record a performance metric
     * @param metric_name Name of the metric
     * @param value Metric value
     * @param unit Unit of measurement
     * @param tags Additional tags
     */
    void record_performance_metric(const std::string& metric_name,
                                  double value,
                                  const std::string& unit = "",
                                  const std::map<std::string, std::string>& tags = {});

    /**
     * @brief Get current metrics snapshot
     * @return Current integration metrics
     */
    IntegrationMetrics get_current_metrics() const;

    /**
     * @brief Get session history
     * @param limit Maximum number of sessions to return (0 = all)
     * @param operation_filter Filter by operation name (empty = all)
     * @return Session history
     */
    std::vector<IntegrationSession> get_session_history(
        size_t limit = 0,
        const std::string& operation_filter = "") const;

    /**
     * @brief Get performance metrics
     * @param metric_name Filter by metric name (empty = all)
     * @param start_time Start time filter
     * @param end_time End time filter
     * @return Performance metrics
     */
    PerformanceMetrics get_performance_metrics(
        const std::string& metric_name = "",
        const std::chrono::system_clock::time_point& start_time = std::chrono::system_clock::time_point{},
        const std::chrono::system_clock::time_point& end_time = std::chrono::system_clock::time_point{}) const;

    /**
     * @brief Export metrics to JSON
     * @param format Export format (json, text, html)
     * @return JSON representation of metrics
     */
    nlohmann::json export_metrics(const std::string& format = "json") const;

    /**
     * @brief Generate metrics report
     * @param output_path Output file path
     * @param report_type Report format (json, text, html)
     * @return True if report generated successfully
     */
    bool generate_report(const std::string& output_path,
                        const std::string& report_type = "json");

    /**
     * @brief Clean up old metrics
     * @param max_age Maximum age of metrics to keep
     */
    void cleanup_old_metrics(std::chrono::hours max_age = std::chrono::hours{24 * 7}); // 1 week default

    /**
     * @brief Get metrics configuration
     * @return Current configuration
     */
    const MetricsConfig& get_config() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return config_;
    }

    /**
     * @brief Update metrics configuration
     * @param config New configuration
     */
    void set_config(const MetricsConfig& config) {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = config;
        setup_scheduled_collection();
    }

private:
    IntegrationMetricsCollector() = default;
    ~IntegrationMetricsCollector() = default;
    IntegrationMetricsCollector(const IntegrationMetricsCollector&) = delete;
    IntegrationMetricsCollector& operator=(const IntegrationMetricsCollector&) = delete;

    mutable std::mutex mutex_;
    std::string storage_path_;
    MetricsConfig config_;

    // Session tracking
    std::map<std::string, IntegrationSession> active_sessions_;
    std::vector<IntegrationSession> session_history_;
    mutable std::atomic<uint64_t> session_counter_{0};

    // Metrics storage
    std::vector<PerformanceMetric> performance_metrics_;
    std::map<std::string, MetricStatistics> metric_statistics_;
    std::map<std::string, OperationMetrics> operation_metrics_;

    // Resource tracking
    ResourceUsage current_resource_usage_;
    std::vector<std::pair<std::chrono::system_clock::time_point, ResourceUsage>> resource_history_;

    // Integration with other components
    std::unique_ptr<BuildTimeMeasurement> build_metrics_;
    std::unique_ptr<BaselineMetrics> baseline_metrics_;

    // Private helper methods
    std::string generate_session_id() const;
    void update_operation_metrics(const std::string& operation, bool success, double duration);
    void update_metric_statistics(const std::string& metric_name, double value);
    bool is_significant_metric(const std::string& metric_name, double value) const;
    void setup_scheduled_collection();
    void load_historical_metrics();
    void update_historical_metrics();
    std::string format_timestamp(std::chrono::system_clock::time_point tp) const;

    // Report generation methods
    void generate_text_report(std::ofstream& file, const IntegrationMetrics& metrics) const;
    void generate_html_report(std::ofstream& file, const IntegrationMetrics& metrics) const;
};

/**
 * @brief RAII Metrics collection scope
 */
class MetricsCollectionScope {
private:
    IntegrationMetricsCollector& collector_;
    std::string session_id_;
    std::chrono::steady_clock::time_point start_time_;
    bool completed_;

public:
    explicit MetricsCollectionScope(IntegrationMetricsCollector& collector,
                                  const std::string& operation,
                                  const std::map<std::string, std::string>& parameters = {})
        : collector_(collector), completed_(false) {
        collector_.record_integration_start(operation, parameters);
        start_time_ = std::chrono::steady_clock::now();
    }

    ~MetricsCollectionScope() {
        if (!completed_) {
            complete(false);
        }
    }

    /**
     * @brief Complete the metrics collection
     * @param success Whether operation succeeded
     * @param results Operation results
     */
    void complete(bool success = true,
                 const std::map<std::string, std::string>& results = {}) {
        if (!completed_) {
            collector_.record_integration_complete(session_id_, success, results);
            completed_ = true;
        }
    }

    /**
     * @brief Record resource usage
     * @param usage Resource usage metrics
     */
    void record_resource_usage(const ResourceUsage& usage) {
        collector_.record_resource_usage(session_id_, usage);
    }

    /**
     * @brief Record a performance metric
     * @param name Metric name
     * @param value Metric value
     * @param unit Unit of measurement
     * @param tags Additional tags
     */
    void record_metric(const std::string& name, double value,
                      const std::string& unit = "",
                      const std::map<std::string, std::string>& tags = {}) {
        collector_.record_performance_metric(name, value, unit, tags);
    }

    /**
     * @brief Get session ID
     * @return Session identifier
     */
    const std::string& get_session_id() const { return session_id_; }

    /**
     * @brief Get elapsed time
     * @return Elapsed time in seconds
     */
    double get_elapsed_seconds() const {
        auto elapsed = std::chrono::steady_clock::now() - start_time_;
        return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() / 1000.0;
    }
};

/**
 * @brief Convenience macros for metrics collection
 */
#define METRICS_SCOPE(collector, operation, ...) \
    MetricsCollectionScope _metrics_scope(collector, operation, ##__VA_ARGS__)

#define METRICS_RECORD(metric_name, value, unit, ...) \
    collector.record_performance_metric(metric_name, value, unit, ##__VA_ARGS__)

#define METRICS_RESOURCE_USAGE(usage) \
    collector.record_resource_usage(_metrics_scope.get_session_id(), usage)

} // namespace integration
} // namespace puzzle71