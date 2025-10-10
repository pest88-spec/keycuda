/**
 * Baseline Integration Measurement Framework for Puzzle71Solver
 *
 * Provides comprehensive metrics collection and baseline establishment for
 * build performance, integration operations, and system resource usage.
 * Tracks timing, resource consumption, and success rates to measure
 * optimization effectiveness.
 *
 * @origin       https://github.com/Puzzle71Solver/Puzzle71Solver
 * @origin_path  src/integration/baseline_metrics.h
 * @origin_commit <current_commit>
 * @origin_license MIT
 * @extracted_date   2025-10-10
 * @extracted_by     Puzzle71Solver Team
 * @modifications    Created for third-party dependency integration optimization
 * @spdx_license_identifier MIT
 */

#pragma once

#include <chrono>
#include <string>
#include <map>
#include <vector>
#include <memory>
#include <fstream>

/**
 * Baseline Metrics Framework
 *
 * Collects, stores, and analyzes metrics for integration operations
 * including build times, resource usage, and success rates. Provides
 * baseline establishment and trend analysis capabilities.
 */
class BaselineMetrics {
public:
    /**
     * Metric categories for different types of measurements
     */
    enum class MetricCategory {
        BUILD_PERFORMANCE,
        INTEGRATION_OPERATIONS,
        RESOURCE_USAGE,
        SUCCESS_RATES,
        COMPLIANCE_METRICS
    };

    /**
     * Individual metric data point
     */
    struct MetricData {
        std::string timestamp;
        std::string category;
        std::string metric_name;
        double value;
        std::string unit;
        std::map<std::string, std::string> tags;

        MetricData() : value(0.0) {}
    };

    /**
     * Baseline reference point for comparison
     */
    struct Baseline {
        std::string metric_name;
        double baseline_value;
        std::string unit;
        std::string established_timestamp;
        std::string description;
        size_t sample_count;

        Baseline() : baseline_value(0.0), sample_count(0) {}
    };

    /**
     * Metric collection session for grouping related measurements
     */
    struct CollectionSession {
        std::string session_id;
        std::string session_type;
        std::chrono::system_clock::time_point start_time;
        std::chrono::system_clock::time_point end_time;
        std::vector<MetricData> metrics;
        std::map<std::string, std::string> session_tags;

        CollectionSession() : start_time(std::chrono::system_clock::now()) {}
    };

private:
    std::string storage_path_;
    std::vector<MetricData> metrics_history_;
    std::map<std::string, Baseline> baselines_;
    std::map<std::string, CollectionSession> active_sessions_;
    std::string current_session_id_;

    /**
     * Generate unique session ID
     */
    std::string generate_session_id() const;

    /**
     * Convert metric category to string
     */
    std::string category_to_string(MetricCategory category) const;

    /**
     * Format timestamp in ISO 8601 format
     */
    std::string format_timestamp() const;

    /**
     * Calculate statistical measures
     */
    struct Statistics {
        double mean;
        double median;
        double min_value;
        double max_value;
        double std_deviation;
        size_t count;

        Statistics() : mean(0.0), median(0.0), min_value(0.0), max_value(0.0), std_deviation(0.0), count(0) {}
    };

    /**
     * Calculate statistics for metric values
     */
    Statistics calculate_statistics(const std::vector<double>& values) const;

    /**
     * Load metrics from storage
     */
    void load_metrics();

    /**
     * Save metrics to storage
     */
    void save_metrics() const;

    /**
     * Load baselines from storage
     */
    void load_baselines();

    /**
     * Save baselines to storage
     */
    void save_baselines() const;

public:
    /**
     * Constructor
     *
     * @param storage_path Directory path for metric storage
     */
    explicit BaselineMetrics(const std::string& storage_path = "");

    /**
     * Destructor
     */
    ~BaselineMetrics();

    /**
     * Start a new metric collection session
     *
     * @param session_type Type of session (e.g., "build", "integration_test")
     * @param tags Additional session tags
     * @return Session ID for tracking
     */
    std::string start_session(
        const std::string& session_type,
        const std::map<std::string, std::string>& tags = {}
    );

    /**
     * End a collection session
     *
     * @param session_id Session ID to end
     */
    void end_session(const std::string& session_id);

    /**
     * Record a metric measurement
     *
     * @param category Metric category
     * @param metric_name Name of the metric
     * @param value Measured value
     * @param unit Unit of measurement
     * @param tags Additional tags
     * @param session_id Session ID (optional, uses current if not provided)
     */
    void record_metric(
        MetricCategory category,
        const std::string& metric_name,
        double value,
        const std::string& unit,
        const std::map<std::string, std::string>& tags = {},
        const std::string& session_id = ""
    );

    /**
     * Establish baseline for a metric
     *
     * @param metric_name Name of the metric
     * @param description Description of what the metric measures
     * @param sample_count Number of samples required for stable baseline
     * @param tags Additional tags for filtering
     * @return True if baseline was established successfully
     */
    bool establish_baseline(
        const std::string& metric_name,
        const std::string& description,
        size_t sample_count = 10,
        const std::map<std::string, std::string>& tags = {}
    );

    /**
     * Get baseline for a metric
     *
     * @param metric_name Name of the metric
     * @return Baseline information (empty if not established)
     */
    Baseline get_baseline(const std::string& metric_name) const;

    /**
     * Compare metric against baseline
     *
     * @param metric_name Name of the metric
     * @param current_value Current metric value
     * @return Comparison result (positive if above baseline, negative if below)
     */
    double compare_to_baseline(const std::string& metric_name, double current_value) const;

    /**
     * Get metric statistics
     *
     * @param metric_name Name of the metric
     * @param tags Filter tags (optional)
     * @return Statistical measures
     */
    Statistics get_statistics(
        const std::string& metric_name,
        const std::map<std::string, std::string>& tags = {}
    ) const;

    /**
     * Get metrics for a time period
     *
     * @param start_time Start timestamp (optional)
     * @param end_time End timestamp (optional)
     * @param tags Filter tags (optional)
     * @return Vector of matching metrics
     */
    std::vector<MetricData> get_metrics(
        const std::chrono::system_clock::time_point* start_time = nullptr,
        const std::chrono::system_clock::time_point* end_time = nullptr,
        const std::map<std::string, std::string>& tags = {}
    ) const;

    /**
     * Get all established baselines
     *
     * @return Map of metric names to baseline data
     */
    std::map<std::string, Baseline> get_all_baselines() const;

    /**
     * Generate performance report
     *
     * @param start_time Start time for report period
     * @param end_time End time for report period
     * @param format Output format (json, csv, text)
     * @return Formatted report string
     */
    std::string generate_report(
        const std::chrono::system_clock::time_point& start_time,
        const std::chrono::system_clock::time_point& end_time,
        const std::string& format = "json"
    ) const;

    /**
     * Export metrics to CSV file
     *
     * @param file_path Output file path
     * @param start_time Start time for export
     * @param end_time End time for export
     * @return True if export successful
     */
    bool export_to_csv(
        const std::string& file_path,
        const std::chrono::system_clock::time_point& start_time,
        const std::chrono::system_clock::time_point& end_time
    ) const;

    /**
     * Clear all metrics and baselines
     */
    void clear_all();

    /**
     * Clear metrics older than specified time
     *
     * @param older_than Remove metrics older than this timestamp
     */
    void clear_metrics_older_than(const std::chrono::system_clock::time_point& older_than);

    /**
     * Get active sessions
     *
     * @return Map of session IDs to session data
     */
    std::map<std::string, CollectionSession> get_active_sessions() const;

    /**
     * Get performance trend for a metric
     *
     * @param metric_name Name of the metric
     * @param period_days Number of days to analyze
     * @return Trend analysis result (positive = improving, negative = declining)
     */
    double get_performance_trend(const std::string& metric_name, size_t period_days = 7) const;
};

/**
 * RAII Timer for automatic metric recording
 */
class MetricTimer {
private:
    BaselineMetrics& metrics_;
    BaselineMetrics::MetricCategory category_;
    std::string metric_name_;
    std::string unit_;
    std::map<std::string, std::string> tags_;
    std::string session_id_;
    std::chrono::steady_clock::time_point start_time_;
    bool completed_;

public:
    MetricTimer(
        BaselineMetrics& metrics,
        BaselineMetrics::MetricCategory category,
        const std::string& metric_name,
        const std::string& unit = "seconds",
        const std::map<std::string, std::string>& tags = {},
        const std::string& session_id = ""
    );

    ~MetricTimer();

    /**
     * Complete the timer and record the metric
     *
     * @param additional_tags Additional tags to add
     */
    void complete(const std::map<std::string, std::string>& additional_tags = {});
};

// Convenience macros for common metric recording patterns
#define METRICS_TIMER(metrics, category, name, unit, tags) \
    MetricTimer timer(metrics, BaselineMetrics::category, name, unit, tags)

#define RECORD_METRIC(metrics, category, name, value, unit, tags) \
    (metrics).record_metric(BaselineMetrics::category, name, value, unit, tags)