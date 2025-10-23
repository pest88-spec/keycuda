#pragma once

/**
 * @file metrics.h
 * @brief Integration metrics collection and logging system
 *
 * T025: Integration logging for setup complexity reduction
 *
 * This system provides comprehensive metrics collection and logging for the
 * third-party dependencies integration process, focusing on setup complexity
 * reduction measurement and optimization.
 *
 * @author T025 Implementation Team
 * @date 2025-10-22
 */

#include <chrono>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <fstream>
#include <mutex>
#include <functional>

namespace integration {
namespace metrics {

/**
 * @brief Metric value type for different measurement types
 */
enum class MetricType {
    COUNTER,      ///< Simple counter (e.g., number of steps)
    GAUGE,        ///< Current value (e.g., memory usage)
    TIMER,        ///< Duration measurement (e.g., build time)
    PERCENTAGE    ///< Percentage (e.g., complexity reduction)
};

/**
 * @brief Base metric value
 */
struct MetricValue {
    MetricType type;
    union {
        uint64_t counter_value;
        double gauge_value;
        std::chrono::milliseconds timer_value;
        double percentage_value;
    };
    std::string unit;

    MetricValue(MetricType t) : type(t) {}
};

/**
 * @brief Setup complexity metrics
 */
struct SetupComplexityMetrics {
    // Original setup metrics (baseline)
    std::chrono::milliseconds original_setup_time{0};
    size_t original_steps_count{0};
    size_t original_dependencies_count{0};
    bool original_required_internet{true};
    size_t original_manual_operations{0};

    // Optimized setup metrics (current)
    std::chrono::milliseconds optimized_setup_time{0};
    size_t optimized_steps_count{0};
    size_t optimized_dependencies_count{0};
    bool optimized_required_internet{false};
    size_t optimized_manual_operations{0};

    // Calculated improvements
    double time_reduction_percentage{0.0};
    double steps_reduction_percentage{0.0};
    double complexity_reduction_percentage{0.0};
    bool offline_build_enabled{false};

    // Quality metrics
    double setup_success_rate{100.0};
    double build_success_rate{100.0};
    size_t setup_attempts{0};
    size_t successful_setups{0};
};

/**
 * @brief Integration operation metrics
 */
struct IntegrationOperationMetrics {
    std::string operation_name;
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point end_time;
    std::chrono::milliseconds duration{0};
    bool success{false};
    std::string error_message;
    std::map<std::string, std::string> additional_data;
};

/**
 * @brief Library integration metrics
 */
struct LibraryIntegrationMetrics {
    std::string library_name;
    std::string library_version;
    std::chrono::milliseconds extraction_time{0};
    std::chrono::milliseconds attribution_time{0};
    std::chrono::milliseconds build_integration_time{0};
    size_t source_files_count{0};
    size_t attribution_files_count{0};
    double attribution_coverage_percentage{0.0};
    bool extraction_success{false};
    bool build_success{false};
};

/**
 * @brief Metrics collector interface
 */
class MetricsCollector {
public:
    virtual ~MetricsCollector() = default;

    /**
     * @brief Record a metric value
     * @param name Metric name
     * @param value Metric value
     * @param tags Optional tags for categorization
     */
    virtual void record_metric(const std::string& name,
                              const MetricValue& value,
                              const std::map<std::string, std::string>& tags = {}) = 0;

    /**
     * @brief Start a timer for an operation
     * @param name Timer name
     * @param tags Optional tags
     * @return Timer ID
     */
    virtual std::string start_timer(const std::string& name,
                                   const std::map<std::string, std::string>& tags = {}) = 0;

    /**
     * @brief End a timer and record the duration
     * @param timer_id Timer ID
     */
    virtual void end_timer(const std::string& timer_id) = 0;

    /**
     * @brief Record setup complexity metrics
     * @param metrics Setup complexity metrics
     */
    virtual void record_setup_metrics(const SetupComplexityMetrics& metrics) = 0;

    /**
     * @brief Record integration operation metrics
     * @param metrics Operation metrics
     */
    virtual void record_operation_metrics(const IntegrationOperationMetrics& metrics) = 0;

    /**
     * @brief Record library integration metrics
     * @param metrics Library metrics
     */
    virtual void record_library_metrics(const LibraryIntegrationMetrics& metrics) = 0;
};

/**
 * @brief Metrics logger for JSON output
 */
class MetricsLogger {
public:
    /**
     * @brief Initialize metrics logger
     * @param log_file Output log file path
     * @param enable_console Enable console output
     */
    virtual bool initialize(const std::string& log_file, bool enable_console = true) = 0;

    /**
     * @brief Log setup complexity metrics in JSON format
     * @param metrics Setup metrics
     */
    virtual void log_setup_metrics(const SetupComplexityMetrics& metrics) = 0;

    /**
     * @brief Log operation metrics in JSON format
     * @param metrics Operation metrics
     */
    virtual void log_operation_metrics(const IntegrationOperationMetrics& metrics) = 0;

    /**
     * @brief Generate comprehensive setup complexity report
     * @param metrics All metrics to include
     * @return JSON report string
     */
    virtual std::string generate_setup_report(const SetupComplexityMetrics& metrics) = 0;

    /**
     * @brief Log custom metric in JSON format
     * @param name Metric name
     * @param value Metric value
     * @param timestamp Timestamp
     * @param tags Optional tags
     */
    virtual void log_metric(const std::string& name,
                          const MetricValue& value,
                          std::chrono::system_clock::time_point timestamp,
                          const std::map<std::string, std::string>& tags = {}) = 0;
};

/**
 * @brief Setup complexity analyzer
 */
class SetupComplexityAnalyzer {
public:
    /**
     * @brief Calculate setup complexity reduction
     * @param original Original metrics
     * @param optimized Optimized metrics
     * @return Reduction percentage (0-100)
     */
    virtual double calculate_complexity_reduction(const SetupComplexityMetrics& original,
                                                  const SetupComplexityMetrics& optimized) = 0;

    /**
     * @brief Analyze setup steps and categorize complexity
     * @param steps List of setup steps
     * @return Complexity score
     */
    virtual double analyze_steps_complexity(const std::vector<std::string>& steps) = 0;

    /**
     * @brief Generate setup improvement recommendations
     * @param metrics Current metrics
     * @return List of recommendations
     */
    virtual std::vector<std::string> generate_improvements(const SetupComplexityMetrics& metrics) = 0;
};

/**
 * @brief Standard metrics collector implementation
 */
class StandardMetricsCollector : public MetricsCollector {
public:
    StandardMetricsCollector();
    ~StandardMetricsCollector() override;

    // MetricsCollector interface
    void record_metric(const std::string& name,
                      const MetricValue& value,
                      const std::map<std::string, std::string>& tags = {}) override;
    std::string start_timer(const std::string& name,
                           const std::map<std::string, std::string>& tags = {}) override;
    void end_timer(const std::string& timer_id) override;
    void record_setup_metrics(const SetupComplexityMetrics& metrics) override;
    void record_operation_metrics(const IntegrationOperationMetrics& metrics) override;
    void record_library_metrics(const LibraryIntegrationMetrics& metrics) override;

    /**
     * @brief Get all recorded metrics
     * @return Map of metrics
     */
    std::map<std::string, std::vector<MetricValue>> get_all_metrics() const;

    /**
     * @brief Clear all metrics
     */
    void clear_metrics();

    /**
     * @brief Set metrics logger
     * @param logger Logger instance
     */
    void set_logger(std::unique_ptr<MetricsLogger> logger);

private:
    struct TimerInfo {
        std::string name;
        std::chrono::system_clock::time_point start_time;
        std::map<std::string, std::string> tags;
    };

    mutable std::mutex mutex_;
    std::map<std::string, std::vector<MetricValue>> metrics_;
    std::map<std::string, TimerInfo> active_timers_;
    std::unique_ptr<MetricsLogger> logger_;

    std::string generate_timer_id();
    void log_metric_to_json(const std::string& name,
                           const MetricValue& value,
                           const std::map<std::string, std::string>& tags);
};

/**
 * @brief JSON metrics logger implementation
 */
class JsonMetricsLogger : public MetricsLogger {
public:
    JsonMetricsLogger();
    ~JsonMetricsLogger() override;

    // MetricsLogger interface
    bool initialize(const std::string& log_file, bool enable_console = true) override;
    void log_setup_metrics(const SetupComplexityMetrics& metrics) override;
    void log_operation_metrics(const IntegrationOperationMetrics& metrics) override;
    std::string generate_setup_report(const SetupComplexityMetrics& metrics) override;
    void log_metric(const std::string& name,
                   const MetricValue& value,
                   std::chrono::system_clock::time_point timestamp,
                   const std::map<std::string, std::string>& tags = {}) override;

private:
    std::ofstream log_file_;
    bool enable_console_;
    mutable std::mutex mutex_;

    std::string metric_value_to_json(const MetricValue& value) const;
    std::string tags_to_json(const std::map<std::string, std::string>& tags) const;
    std::string time_to_string(std::chrono::system_clock::time_point tp) const;
    void write_json_log(const std::string& json_line);
};

/**
 * @brief Standard setup complexity analyzer implementation
 */
class StandardSetupComplexityAnalyzer : public SetupComplexityAnalyzer {
public:
    double calculate_complexity_reduction(const SetupComplexityMetrics& original,
                                         const SetupComplexityMetrics& optimized) override;
    double analyze_steps_complexity(const std::vector<std::string>& steps) override;
    std::vector<std::string> generate_improvements(const SetupComplexityMetrics& metrics) override;

private:
    double calculate_weighted_complexity(const SetupComplexityMetrics& metrics);
};

/**
 * @brief Integration metrics manager
 */
class IntegrationMetricsManager {
public:
    /**
     * @brief Initialize metrics manager
     * @param log_file Output log file
     * @param enable_console Enable console logging
     * @return True if successful
     */
    bool initialize(const std::string& log_file = "build/integration-metrics.jsonl",
                   bool enable_console = true);

    /**
     * @brief Get metrics collector
     * @return Metrics collector
     */
    MetricsCollector& collector();

    /**
     * @brief Get setup complexity analyzer
     * @return Analyzer
     */
    SetupComplexityAnalyzer& analyzer();

    /**
     * @brief Setup automatic setup complexity tracking
     * @param collector Metrics collector to use
     * @return Tracker ID
     */
    std::string setup_auto_tracking(MetricsCollector& collector);

    /**
     * @brief Generate and log setup complexity reduction report
     * @param baseline Baseline metrics
     * @param current Current metrics
     */
    void log_complexity_reduction_report(const SetupComplexityMetrics& baseline,
                                         const SetupComplexityMetrics& current);

    /**
     * @brief Create metrics collector
     * @return Unique pointer to collector
     */
    static std::unique_ptr<MetricsCollector> create_collector();

    /**
     * @brief Create metrics logger
     * @return Unique pointer to logger
     */
    static std::unique_ptr<MetricsLogger> create_logger();

    /**
     * @brief Create setup complexity analyzer
     * @return Unique pointer to analyzer
     */
    static std::unique_ptr<SetupComplexityAnalyzer> create_analyzer();

private:
    std::unique_ptr<StandardMetricsCollector> collector_;
    std::unique_ptr<JsonMetricsLogger> logger_;
    std::unique_ptr<StandardSetupComplexityAnalyzer> analyzer_;
    bool initialized_{false};
};

/**
 * @brief RAII timer helper for automatic metric collection
 */
class ScopedTimer {
public:
    ScopedTimer(MetricsCollector& collector,
                const std::string& name,
                const std::map<std::string, std::string>& tags = {});
    ~ScopedTimer();

    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
    MetricsCollector& collector_;
    std::string timer_id_;
};

/**
 * @brief Setup metrics builder helper
 */
class SetupMetricsBuilder {
public:
    SetupMetricsBuilder& set_original_setup_time(std::chrono::milliseconds time);
    SetupMetricsBuilder& set_original_steps(size_t count);
    SetupMetricsBuilder& set_original_dependencies(size_t count);
    SetupMetricsBuilder& set_original_requires_internet(bool requires);
    SetupMetricsBuilder& set_original_manual_operations(size_t count);

    SetupMetricsBuilder& set_optimized_setup_time(std::chrono::milliseconds time);
    SetupMetricsBuilder& set_optimized_steps(size_t count);
    SetupMetricsBuilder& set_optimized_dependencies(size_t count);
    SetupMetricsBuilder& set_optimized_requires_internet(bool requires);
    SetupMetricsBuilder& set_optimized_manual_operations(size_t count);

    SetupMetricsBuilder& set_success_rates(double setup_rate, double build_rate);
    SetupMetricsBuilder& set_attempts(size_t setup_attempts, size_t successful_setups);

    SetupComplexityMetrics build() const;

private:
    SetupComplexityMetrics metrics_;
};

// Utility functions
std::string format_time_duration(std::chrono::milliseconds duration);
std::string format_percentage(double percentage);
std::string create_setup_metric_name(const std::string& operation);
std::map<std::string, std::string> create_default_tags(const std::string& component);

} // namespace metrics
} // namespace integration