/**
 * Library-Type-Specific Performance Baseline Measurements for Puzzle71Solver
 *
 * Establishes and tracks performance baselines for different types of integrated
 * libraries (crypto: 8min, utility: 3min, general: 5min) with automated measurement,
 * trend analysis, and performance regression detection for integration optimizations.
 *
 * @origin       https://github.com/Puzzle71Solver/Puzzle71Solver
 * @origin_path  src/integration/performance_baselines.h
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

/**
 * Library Type-Specific Performance Baseline System
 *
 * Tracks performance metrics for different library types with specific targets
 * and regression detection to ensure integration optimizations don't degrade performance.
 */
class PerformanceBaselines {
public:
    /**
     * Library types with specific performance targets
     */
    enum class LibraryType {
        CRYPTO,      // Target: 8 minutes
        UTILITY,     // Target: 3 minutes
        GENERAL,     // Target: 5 minutes
        UNKNOWN
    };

    /**
     * Performance metrics for a library type
     */
    struct PerformanceMetrics {
        LibraryType library_type;
        std::string library_name;
        std::chrono::milliseconds build_time;
        std::chrono::milliseconds integration_time;
        std::chrono::milliseconds test_time;
        std::size_t memory_usage_mb;
        std::size_t disk_usage_mb;
        double cpu_utilization_percent;
        std::chrono::system_clock::time_point measured_at;
        std::string build_configuration;
        std::map<std::string, double> custom_metrics;

        PerformanceMetrics() : library_type(LibraryType::UNKNOWN),
                              build_time(0), integration_time(0), test_time(0),
                              memory_usage_mb(0), disk_usage_mb(0),
                              cpu_utilization_percent(0.0) {}
    };

    /**
     * Performance baseline for a library type
     */
    struct PerformanceBaseline {
        LibraryType library_type;
        std::chrono::milliseconds target_build_time;
        std::chrono::milliseconds acceptable_variance;
        std::vector<PerformanceMetrics> historical_measurements;
        std::chrono::milliseconds baseline_build_time;
        std::chrono::system_clock::time_point baseline_established;
        std::size_t sample_count;
        double performance_trend;  // Positive = improvement (lower time)
        bool is_healthy;

        PerformanceBaseline() : library_type(LibraryType::UNKNOWN),
                               target_build_time(300000), // 5 minutes default
                               acceptable_variance(60000), // 1 minute variance
                               baseline_build_time(0),
                               sample_count(0),
                               performance_trend(0.0),
                               is_healthy(true) {}
    };

    /**
     * Performance regression alert
     */
    struct RegressionAlert {
        std::string alert_id;
        LibraryType library_type;
        std::string library_name;
        double regression_percentage;
        std::chrono::milliseconds current_time;
        std::chrono::milliseconds baseline_time;
        std::string severity;
        std::chrono::system_clock::time_point detected_at;
        std::string description;

        RegressionAlert() : library_type(LibraryType::UNKNOWN),
                           regression_percentage(0.0),
                           current_time(0), baseline_time(0),
                           severity("WARNING") {}
    };

    /**
     * Performance analysis results
     */
    struct PerformanceAnalysis {
        std::map<LibraryType, PerformanceBaseline> baselines;
        std::vector<RegressionAlert> regressions;
        std::map<std::string, double> performance_improvements;
        std::chrono::system_clock::time_point analysis_time;
        std::string summary;
        bool overall_healthy;

        PerformanceAnalysis() : overall_healthy(true) {}
    };

private:
    std::string storage_path_;
    std::map<LibraryType, PerformanceBaseline> baselines_;
    std::vector<PerformanceMetrics> recent_measurements_;
    std::string current_session_id_;
    mutable std::mutex baselines_mutex_;
    std::chrono::milliseconds regression_threshold_;

    /**
     * Convert library type to string
     */
    std::string library_type_to_string(LibraryType type) const;

    /**
     * Convert string to library type
     */
    LibraryType string_to_library_type(const std::string& type) const;

    /**
     * Determine library type from name and characteristics
     */
    LibraryType determine_library_type(const std::string& library_name,
                                       const std::vector<std::string>& source_files) const;

    /**
     * Calculate performance trend from historical data
     */
    double calculate_performance_trend(const std::vector<PerformanceMetrics>& measurements) const;

    /**
     * Detect performance regressions
     */
    std::vector<RegressionAlert> detect_regressions(const PerformanceMetrics& current) const;

    /**
     * Save baselines to storage
     */
    bool save_baselines() const;

    /**
     * Load baselines from storage
     */
    bool load_baselines();

    /**
     * Format duration for storage
     */
    std::string format_duration(std::chrono::milliseconds duration) const;

    /**
     * Parse duration from storage
     */
    std::chrono::milliseconds parse_duration(const std::string& duration_str) const;

    /**
     * Generate unique alert ID
     */
    std::string generate_alert_id() const;

public:
    /**
     * Constructor
     *
     * @param storage_path Directory for baseline storage
     * @param regression_threshold Percentage threshold for regression detection (default: 15%)
     */
    explicit PerformanceBaselines(
        const std::string& storage_path = "baselines/",
        double regression_threshold = 15.0
    );

    /**
     * Destructor
     */
    ~PerformanceBaselines();

    /**
     * Initialize default baselines for library types
     */
    void initialize_default_baselines();

    /**
     * Record performance measurement for a library
     *
     * @param library_name Library name
     * @param source_files Library source files
     * @param metrics Performance metrics
     * @return True if measurement recorded successfully
     */
    bool record_measurement(const std::string& library_name,
                           const std::vector<std::string>& source_files,
                           const PerformanceMetrics& metrics);

    /**
     * Measure library performance automatically
     *
     * @param library_name Library name
     * @param library_path Library path
     * @param build_command Build command to execute
     * @return Measured performance metrics
     */
    PerformanceMetrics measure_library_performance(const std::string& library_name,
                                                  const std::string& library_path,
                                                  const std::string& build_command);

    /**
     * Establish baseline for library type
     *
     * @param library_type Library type
     * @param measurements Measurements to use for baseline
     * @return True if baseline established successfully
     */
    bool establish_baseline(LibraryType library_type,
                           const std::vector<PerformanceMetrics>& measurements);

    /**
     * Get baseline for library type
     *
     * @param library_type Library type
     * @return Performance baseline (empty if not established)
     */
    PerformanceBaseline get_baseline(LibraryType library_type) const;

    /**
     * Update baseline with new measurement
     *
     * @param library_type Library type
     * @param measurement New measurement
     * @return True if baseline updated successfully
     */
    bool update_baseline(LibraryType library_type, const PerformanceMetrics& measurement);

    /**
     * Compare measurement against baseline
     *
     * @param measurement Performance measurement
     * @return Performance difference in percentage
     */
    double compare_to_baseline(const PerformanceMetrics& measurement) const;

    /**
     * Perform comprehensive performance analysis
     *
     * @return Performance analysis results
     */
    PerformanceAnalysis analyze_performance() const;

    /**
     * Check for performance regressions
     *
     * @param library_type Library type to check (optional)
     * @return Vector of regression alerts
     */
    std::vector<RegressionAlert> check_regressions(LibraryType library_type = LibraryType::UNKNOWN) const;

    /**
     * Get performance statistics for library type
     */
    struct LibraryStats {
        LibraryType library_type;
        std::chrono::milliseconds average_build_time;
        std::chrono::milliseconds min_build_time;
        std::chrono::milliseconds max_build_time;
        std::chrono::milliseconds target_build_time;
        double performance_score;  // 0-100 score
        size_t sample_count;
        bool meets_target;

        LibraryStats() : library_type(LibraryType::UNKNOWN),
                        average_build_time(0), min_build_time(0), max_build_time(0),
                        target_build_time(0), performance_score(0.0),
                        sample_count(0), meets_target(false) {}
    };

    /**
     * Get performance statistics for all library types
     *
     * @return Map of library types to statistics
     */
    std::map<LibraryType, LibraryStats> get_statistics() const;

    /**
     * Generate performance report
     *
     * @param format Report format (json, html, text)
     * @param include_trends Include trend analysis
     * @return Formatted performance report
     */
    std::string generate_report(const std::string& format = "json",
                                bool include_trends = true) const;

    /**
     * Export performance data
     *
     * @param export_path Export file path
     * @param library_types Library types to export (empty for all)
     * @param format Export format (json, csv)
     * @return True if export successful
     */
    bool export_performance_data(const std::string& export_path,
                                 const std::vector<LibraryType>& library_types = {},
                                 const std::string& format = "json") const;

    /**
     * Import performance data
     *
     * @param import_path Import file path
     * @param merge_mode true=merge with existing, false=replace all
     * @return True if import successful
     */
    bool import_performance_data(const std::string& import_path, bool merge_mode = true);

    /**
     * Clear old performance measurements
     *
     * @param older_than Remove measurements older than this time
     * @return Number of measurements cleared
     */
    size_t clear_old_measurements(std::chrono::system_clock::time_point older_than);

    /**
     * Set regression threshold
     *
     * @param threshold Percentage threshold for regression detection
     */
    void set_regression_threshold(double threshold);

    /**
     * Get regression threshold
     *
     * @return Current regression threshold
     */
    double get_regression_threshold() const;

    /**
     * Force baseline recalculation
     *
     * @param library_type Library type to recalculate (optional)
     * @return True if recalculation successful
     */
    bool force_recalculate_baselines(LibraryType library_type = LibraryType::UNKNOWN);

    /**
     * Get performance improvement percentage
     *
     * @param library_type Library type
     * @param period_days Number of days to analyze
     * @return Performance improvement percentage
     */
    double get_performance_improvement(LibraryType library_type, size_t period_days = 30) const;

    /**
     * Validate library type targets
     *
     * @return Map of library types to target validation status
     */
    std::map<LibraryType, bool> validate_targets() const;
};

/**
 * RAII Performance Measurement Session
 */
class PerformanceMeasurementSession {
private:
    PerformanceBaselines& baselines_;
    std::string library_name_;
    std::vector<std::string> source_files_;
    std::chrono::steady_clock::time_point start_time_;
    bool session_active_;

public:
    PerformanceMeasurementSession(PerformanceBaselines& baselines,
                                 const std::string& library_name,
                                 const std::vector<std::string>& source_files);
    ~PerformanceMeasurementSession();

    /**
     * End measurement session and record results
     *
     * @param build_time Build time in milliseconds
     * @param integration_time Integration time in milliseconds
     * @param additional_metrics Additional performance metrics
     * @return True if measurement recorded successfully
     */
    bool complete(std::chrono::milliseconds build_time,
                  std::chrono::milliseconds integration_time,
                  const std::map<std::string, double>& additional_metrics = {});

    /**
     * Check if session is active
     */
    bool is_active() const { return session_active_; }
};