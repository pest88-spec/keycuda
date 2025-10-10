/**
 * Build Time Measurement Framework for Success Criteria Validation for Puzzle71Solver
 *
 * Provides comprehensive build time measurement and analysis for validating success
 * criteria including 5-minute fresh checkout build targets, setup complexity reduction,
 * and performance regression detection with automated benchmarking and reporting.
 *
 * @origin       https://github.com/Puzzle71Solver/Puzzle71Solver
 * @origin_path  src/integration/build_time_measurement.h
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
#include <functional>

/**
 * Build Time Measurement Framework
 *
 * Measures, tracks, and analyzes build times for validating success criteria
 * including 5-minute fresh checkout targets and setup complexity metrics.
 */
class BuildTimeMeasurement {
public:
    /**
     * Build phase types for measurement
     */
    enum class BuildPhase {
        DEPENDENCY_SETUP,
        CONFIGURATION,
        COMPILATION,
        LINKING,
        TESTING,
        PACKAGING,
        TOTAL_BUILD
    };

    /**
     * Build measurement result
     */
    struct BuildMeasurement {
        std::string measurement_id;
        std::chrono::system_clock::time_point start_time;
        std::chrono::system_clock::time_point end_time;
        std::map<BuildPhase, std::chrono::milliseconds> phase_times;
        std::chrono::milliseconds total_time;
        std::string build_configuration;
        std::map<std::string, std::string> build_parameters;
        std::string git_commit;
        std::string environment_info;
        std::vector<std::string> build_artifacts;
        bool build_successful;
        std::string error_message;
        std::map<std::string, double> custom_metrics;

        BuildMeasurement() : total_time(0), build_successful(false) {}
    };

    /**
     * Success criteria validation result
     */
    struct SuccessCriteriaValidation {
        bool meets_5_minute_target;
        std::chrono::milliseconds actual_build_time;
        std::chrono::milliseconds target_build_time;
        double performance_score;
        std::vector<std::string> passed_criteria;
        std::vector<std::string> failed_criteria;
        std::map<std::string, double> individual_scores;
        std::chrono::system_clock::time_point validated_at;
        std::string summary;

        SuccessCriteriaValidation() : meets_5_minute_target(false),
                                    actual_build_time(0),
                                    target_build_time(std::chrono::minutes(5)),
                                    performance_score(0.0) {}
    };

    /**
     * Build benchmark data
     */
    struct BuildBenchmark {
        std::string benchmark_id;
        std::chrono::system_clock::time_point timestamp;
        std::chrono::milliseconds build_time;
        std::string configuration;
        std::map<std::string, std::string> environment;
        std::map<BuildPhase, std::chrono::milliseconds> phase_breakdown;
        bool is_baseline;
        double baseline_improvement_percentage;

        BuildBenchmark() : build_time(0), is_baseline(false), baseline_improvement_percentage(0.0) {}
    };

    /**
     * Setup complexity metrics
     */
    struct SetupComplexityMetrics {
        size_t setup_steps_count;
        std::chrono::seconds setup_time;
        std::chrono::seconds time_to_first_build;
        size_t manual_interventions_required;
        size_t prerequisite_knowledge_items;
        double complexity_score;  // 0-100, lower is better
        std::map<std::string, bool> automation_features;
        std::chrono::system_clock::time_point measured_at;

        SetupComplexityMetrics() : setup_steps_count(0), setup_time(0),
                                  time_to_first_build(0), manual_interventions_required(0),
                                  prerequisite_knowledge_items(0), complexity_score(100.0) {}
    };

private:
    std::string storage_path_;
    std::vector<BuildMeasurement> measurements_;
    std::vector<BuildBenchmark> benchmarks_;
    std::map<std::string, SetupComplexityMetrics> setup_complexity_history_;
    std::chrono::milliseconds target_build_time_;
    std::chrono::milliseconds acceptable_variance_;
    std::string current_measurement_id_;
    mutable std::mutex measurement_mutex_;
    bool auto_save_enabled_;

    /**
     * Generate unique measurement ID
     */
    std::string generate_measurement_id() const;

    /**
     * Save measurements to storage
     */
    bool save_measurements() const;

    /**
     * Load measurements from storage
     */
    bool load_measurements();

    /**
     * Calculate performance score for build time
     */
    double calculate_performance_score(std::chrono::milliseconds build_time) const;

    /**
     * Validate specific success criteria
     */
    std::pair<bool, std::string> validate_criterion(const std::string& criterion,
                                                    const BuildMeasurement& measurement) const;

    /**
     * Format timestamp for storage
     */
    std::string format_timestamp(std::chrono::system_clock::time_point tp) const;

    /**
     * Parse timestamp from storage
     */
    std::chrono::system_clock::time_point parse_timestamp(const std::string& ts) const;

    /**
     * Convert build phase to string
     */
    std::string build_phase_to_string(BuildPhase phase) const;

    /**
     * Convert string to build phase
     */
    BuildPhase string_to_build_phase(const std::string& phase) const;

    /**
     * Get current git commit
     */
    std::string get_git_commit() const;

    /**
     * Get environment information
     */
    std::string get_environment_info() const;

    /**
     * Execute command and capture output
     */
    std::pair<int, std::string> execute_command(const std::string& command) const;

public:
    /**
     * Constructor
     *
     * @param storage_path Directory for measurement storage
     * @param target_build_time Target build time (default: 5 minutes)
     * @param acceptable_variance Acceptable variance from target (default: 30 seconds)
     * @param auto_save Enable auto-save of measurements
     */
    explicit BuildTimeMeasurement(
        const std::string& storage_path = "build_measurements/",
        std::chrono::milliseconds target_build_time = std::chrono::minutes(5),
        std::chrono::milliseconds acceptable_variance = std::chrono::seconds(30),
        bool auto_save = true
    );

    /**
     * Destructor
     */
    ~BuildTimeMeasurement();

    /**
     * Start build measurement
     *
     * @param configuration Build configuration
     * @param build_parameters Build parameters
     * @return Measurement ID
     */
    std::string start_measurement(const std::string& configuration = "Release",
                                 const std::map<std::string, std::string>& build_parameters = {});

    /**
     * Record build phase completion
     *
     * @param measurement_id Measurement ID
     * @param phase Build phase
     * @param phase_duration Phase duration
     * @return True if phase recorded successfully
     */
    bool record_phase(const std::string& measurement_id,
                     BuildPhase phase,
                     std::chrono::milliseconds phase_duration);

    /**
     * Complete build measurement
     *
     * @param measurement_id Measurement ID
     * @param build_successful Whether build was successful
     * @param error_message Error message if build failed
     * @param artifacts List of build artifacts
     * @return True if measurement completed successfully
     */
    bool complete_measurement(const std::string& measurement_id,
                             bool build_successful,
                             const std::string& error_message = "",
                             const std::vector<std::string>& artifacts = {});

    /**
     * Perform automated build measurement
     *
     * @param build_command Build command to execute
     * @param configuration Build configuration
     * @return Build measurement result
     */
    BuildMeasurement perform_automated_measurement(const std::string& build_command,
                                                   const std::string& configuration = "Release");

    /**
     * Validate success criteria against measurement
     *
     * @param measurement_id Measurement ID to validate
     * @return Success criteria validation result
     */
    SuccessCriteriaValidation validate_success_criteria(const std::string& measurement_id) const;

    /**
     * Get measurement by ID
     *
     * @param measurement_id Measurement ID
     * @return Build measurement (empty if not found)
     */
    BuildMeasurement get_measurement(const std::string& measurement_id) const;

    /**
     * Get recent measurements
     *
     * @param limit Maximum number of measurements to return
     * @param successful_only Return only successful builds
     * @return Vector of recent measurements
     */
    std::vector<BuildMeasurement> get_recent_measurements(size_t limit = 10,
                                                          bool successful_only = false) const;

    /**
     * Establish build baseline
     *
     * @param measurements Measurements to use for baseline
     * @return True if baseline established successfully
     */
    bool establish_baseline(const std::vector<BuildMeasurement>& measurements);

    /**
     * Compare measurement against baseline
     *
     * @param measurement_id Measurement ID to compare
     * @return Performance improvement percentage (positive = improvement)
     */
    double compare_to_baseline(const std::string& measurement_id) const;

    /**
     * Get build time statistics
     */
    struct BuildTimeStats {
        std::chrono::milliseconds average_build_time;
        std::chrono::milliseconds min_build_time;
        std::chrono::milliseconds max_build_time;
        std::chrono::milliseconds median_build_time;
        size_t total_measurements;
        size_t successful_builds;
        double success_rate;
        std::chrono::milliseconds target_build_time;
        double meets_target_percentage;
        std::map<BuildPhase, std::chrono::milliseconds> average_phase_times;

        BuildTimeStats() : average_build_time(0), min_build_time(0), max_build_time(0),
                          median_build_time(0), total_measurements(0), successful_builds(0),
                          success_rate(0.0), target_build_time(0), meets_target_percentage(0.0) {}
    };

    /**
     * Get build time statistics
     *
     * @param period_days Number of days to analyze (0 = all time)
     * @return Build time statistics
     */
    BuildTimeStats get_statistics(size_t period_days = 0) const;

    /**
     * Measure setup complexity
     *
     * @param setup_procedure List of setup steps
     * @param automation_features Available automation features
     * @return Setup complexity metrics
     */
    SetupComplexityMetrics measure_setup_complexity(
        const std::vector<std::string>& setup_procedure,
        const std::map<std::string, bool>& automation_features = {});

    /**
     * Calculate setup complexity reduction
     *
     * @param baseline_setup Baseline setup complexity
     * @param current_setup Current setup complexity
     * @return Reduction percentage
     */
    double calculate_complexity_reduction(const SetupComplexityMetrics& baseline_setup,
                                         const SetupComplexityMetrics& current_setup) const;

    /**
     * Generate build performance report
     *
     * @param format Report format (json, html, text)
     * @param include_benchmarks Include benchmark comparisons
     * @return Formatted report
     */
    std::string generate_performance_report(const std::string& format = "json",
                                            bool include_benchmarks = true) const;

    /**
     * Export build measurements
     *
     * @param export_path Export file path
     * @param measurement_ids Specific measurements to export (empty = all)
     * @param format Export format (json, csv)
     * @return True if export successful
     */
    bool export_measurements(const std::string& export_path,
                             const std::vector<std::string>& measurement_ids = {},
                             const std::string& format = "json") const;

    /**
     * Import build measurements
     *
     * @param import_path Import file path
     * @param merge_mode true=merge with existing, false=replace all
     * @return True if import successful
     */
    bool import_measurements(const std::string& import_path, bool merge_mode = true);

    /**
     * Detect build performance regressions
     *
     * @param threshold_percentage Regression threshold percentage
     * @return Vector of measurement IDs with regressions
     */
    std::vector<std::string> detect_regressions(double threshold_percentage = 15.0) const;

    /**
     * Clear old measurements
     *
     * @param older_than Remove measurements older than this time
     * @return Number of measurements cleared
     */
    size_t clear_old_measurements(std::chrono::system_clock::time_point older_than);

    /**
     * Set target build time
     *
     * @param target_time New target build time
     */
    void set_target_build_time(std::chrono::milliseconds target_time);

    /**
     * Get target build time
     *
     * @return Current target build time
     */
    std::chrono::milliseconds get_target_build_time() const;

    /**
     * Force save measurements
     *
     * @return True if save successful
     */
    bool force_save() const;

    /**
     * Enable/disable auto-save
     *
     * @param enabled Enable auto-save
     */
    void set_auto_save(bool enabled);
};

/**
 * RAII Build Measurement Session
 */
class BuildMeasurementSession {
private:
    BuildTimeMeasurement& measurement_;
    std::string measurement_id_;
    std::chrono::steady_clock::time_point session_start_;
    bool session_active_;

public:
    BuildMeasurementSession(BuildTimeMeasurement& measurement,
                           const std::string& configuration = "Release",
                           const std::map<std::string, std::string>& build_parameters = {});
    ~BuildMeasurementSession();

    /**
     * Record build phase
     */
    void record_phase(BuildTimeMeasurement::BuildPhase phase,
                     std::chrono::milliseconds duration);

    /**
     * Complete measurement
     */
    void complete(bool build_successful = true,
                  const std::string& error_message = "",
                  const std::vector<std::string>& artifacts = {});

    /**
     * Get measurement ID
     */
    const std::string& get_measurement_id() const { return measurement_id_; }

    /**
     * Check if session is active
     */
    bool is_active() const { return session_active_; }
};