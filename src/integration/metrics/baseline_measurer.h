/**
 * Puzzle71Solver - Baseline Integration Measurement Framework
 *
 * Provides performance measurement and baseline establishment for integration
 * operations, tracking build times, resource usage, and integration efficiency.
 *
 * @author       Puzzle71Solver Team
 * @created      2025-10-09
 * @license      MIT
 */

#pragma once

#include <string>
#include <chrono>
#include <map>
#include <vector>
#include <memory>

namespace integration {
namespace metrics {

struct PerformanceMetrics {
    std::chrono::milliseconds duration{0};
    size_t memory_usage_mb = 0;
    size_t disk_usage_mb = 0;
    double cpu_usage_percent = 0.0;
    size_t files_processed = 0;
    size_t lines_of_code = 0;
    bool success = false;
    std::string error_message;
};

struct BaselineMetrics {
    std::string operation_name;
    PerformanceMetrics average_metrics;
    PerformanceMetrics best_case_metrics;
    PerformanceMetrics worst_case_metrics;
    std::vector<PerformanceMetrics> measurements;
    size_t sample_count = 0;
    std::chrono::system_clock::time_point last_updated;
    std::map<std::string, std::string> environment_info;
};

struct BuildMetrics {
    std::chrono::milliseconds total_build_time{0};
    std::chrono::milliseconds configuration_time{0};
    std::chrono::milliseconds compilation_time{0};
    std::chrono::milliseconds linking_time{0};
    size_t source_files_compiled = 0;
    size_t objects_linked = 0;
    std::map<std::string, std::chrono::milliseconds> target_build_times;
    bool incremental_build = false;
    std::string build_type;
    std::map<std::string, std::string> build_environment;
};

struct IntegrationMetrics {
    std::chrono::milliseconds total_integration_time{0};
    std::chrono::milliseconds extraction_time{0};
    std::chrono::milliseconds attribution_time{0};
    std::chrono::milliseconds build_system_update_time{0};
    std::chrono::milliseconds verification_time{0};
    size_t libraries_integrated = 0;
    size_t files_extracted = 0;
    size_t attribution_headers_added = 0;
    std::map<std::string, PerformanceMetrics> library_metrics;
};

class BaselineMeasurer {
public:
    BaselineMeasurer();
    explicit BaselineMeasurer(const std::string& baseline_file_path);
    ~BaselineMeasurer();

    // Configuration
    void set_baseline_storage_path(const std::string& file_path);
    void enable_auto_save(bool enabled);
    void set_sample_size(size_t sample_size);
    void load_baselines();
    bool save_baselines() const;

    // Build performance measurement
    std::string start_build_measurement(const std::string& build_type = "",
                                      const std::map<std::string, std::string>& environment = {});
    bool end_build_measurement(const std::string& measurement_id, BuildMetrics& metrics);
    BuildMetrics get_build_baseline(const std::string& build_type) const;

    // Integration performance measurement
    std::string start_integration_measurement(const std::string& library_name = "");
    bool end_integration_measurement(const std::string& measurement_id, IntegrationMetrics& metrics);
    IntegrationMetrics get_integration_baseline() const;

    // Generic performance measurement
    std::string start_measurement(const std::string& operation_name);
    bool end_measurement(const std::string& measurement_id, PerformanceMetrics& metrics);
    bool add_measurement_sample(const std::string& operation_name, const PerformanceMetrics& metrics);

    // Baseline management
    BaselineMetrics get_baseline(const std::string& operation_name) const;
    bool update_baseline(const std::string& operation_name, const PerformanceMetrics& metrics);
    bool remove_baseline(const std::string& operation_name);
    std::vector<std::string> get_available_baselines() const;

    // Performance analysis
    double compare_to_baseline(const std::string& operation_name, const PerformanceMetrics& metrics) const;
    bool is_performance_acceptable(const std::string& operation_name, const PerformanceMetrics& metrics,
                                  double tolerance_percentage = 10.0) const;
    std::vector<std::string> identify_performance_regressions(double threshold_percentage = 20.0) const;

    // Statistics and reporting
    std::map<std::string, BaselineMetrics> get_all_baselines() const;
    size_t get_baseline_count() const;
    std::string generate_performance_report() const;
    std::string export_baselines_json() const;

    // Baseline targets and validation
    void set_performance_target(const std::string& operation_name, std::chrono::milliseconds target_time);
    bool meets_performance_target(const std::string& operation_name, const PerformanceMetrics& metrics) const;
    std::map<std::string, std::chrono::milliseconds> get_performance_targets() const;

    // Budget monitoring
    bool exceeds_time_budget(const std::string& operation_name, const PerformanceMetrics& metrics,
                            std::chrono::milliseconds budget) const;
    std::map<std::string, std::chrono::milliseconds> get_default_time_budgets() const;

    // Integration-specific metrics
    void record_integration_step(const std::string& operation_id, const std::string& step_name,
                                std::chrono::milliseconds duration);
    std::map<std::string, std::chrono::milliseconds> get_integration_breakdown(const std::string& operation_id) const;

private:
    struct Impl;
    std::unique_ptr<Impl> p_impl;

    // Helper methods
    std::string generate_measurement_id() const;
    PerformanceMetrics measure_current_performance() const;
    void update_baseline_statistics(BaselineMetrics& baseline, const PerformanceMetrics& metrics);
    std::chrono::milliseconds get_average_duration(const BaselineMetrics& baseline) const;
};

// Global measurer instance
BaselineMeasurer& get_baseline_measurer();

} // namespace metrics
} // namespace integration