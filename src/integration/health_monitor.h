#pragma once

/**
 * @file health_monitor.h
 * @brief Health monitoring system for integration framework
 *
 * This file defines the health monitoring system that provides real-time
 * health status monitoring for the third-party dependencies integration
 * framework. It includes dependency conflict detection, system health
 * assessment, and automated health reporting capabilities.
 *
 * Created: 2025-10-22
 * Feature: Third-Party Dependencies Integration Optimization
 */

#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <memory>
#include <functional>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>

namespace integration {
namespace health {

/**
 * @brief Health status enumeration
 */
enum class HealthStatus {
    HEALTHY,        ///< System is operating normally
    WARNING,        ///< System has issues but can continue operating
    DEGRADED,       ///< System performance is impacted
    CRITICAL,       ///< System has serious issues
    UNKNOWN         ///< Health status cannot be determined
};

/**
 * @brief Health check category
 */
enum class HealthCategory {
    DEPENDENCIES,   ///< Third-party library dependencies
    BUILD_SYSTEM,   ///< Build system configuration
    INTEGRITY,      ///< Integration integrity
    PERFORMANCE,    ///< System performance
    RESOURCES,      ///< System resources
    CONFIGURATION   ///< Configuration consistency
};

/**
 * @brief Health check result
 */
struct HealthCheckResult {
    std::string check_name;
    HealthCategory category;
    HealthStatus status;
    std::string message;
    std::chrono::system_clock::time_point timestamp;
    std::map<std::string, std::string> details;
    double metric_value;           ///< Numeric metric (e.g., percentage, count)
    std::string metric_unit;       ///< Unit for the metric (e.g., "%", "ms", "files")
};

/**
 * @brief Dependency conflict information
 */
struct DependencyConflict {
    std::string library_name;
    std::string conflict_type;      ///< "version", "symbol", "path", "license"
    std::string conflict_description;
    std::vector<std::string> conflicting_libraries;
    std::string severity;           ///< "low", "medium", "high", "critical"
    std::string resolution_hint;
};

/**
 * @brief System health summary
 */
struct SystemHealthSummary {
    HealthStatus overall_status;
    std::chrono::system_clock::time_point timestamp;
    std::vector<HealthCheckResult> check_results;
    std::vector<DependencyConflict> conflicts;
    int total_checks;
    int passed_checks;
    int warning_checks;
    int failed_checks;
    double health_score;            ///< 0.0 to 100.0
};

/**
 * @brief Health check function type
 */
using HealthCheckFunction = std::function<HealthCheckResult()>;

/**
 * @brief Health monitor configuration
 */
struct HealthMonitorConfig {
    std::chrono::milliseconds check_interval{30000};  ///< Default 30 seconds
    std::chrono::milliseconds check_timeout{5000};    ///< Default 5 seconds
    bool enable_continuous_monitoring{true};
    bool enable_dependency_conflict_detection{true};
    bool enable_performance_monitoring{true};
    bool enable_resource_monitoring{true};
    int max_history_size{1000};
    std::vector<HealthCategory> enabled_categories{
        HealthCategory::DEPENDENCIES,
        HealthCategory::BUILD_SYSTEM,
        HealthCategory::INTEGRITY,
        HealthCategory::PERFORMANCE,
        HealthCategory::RESOURCES,
        HealthCategory::CONFIGURATION
    };
};

/**
 * @brief Health monitoring interface
 */
class HealthMonitor {
public:
    virtual ~HealthMonitor() = default;

    /**
     * @brief Initialize the health monitor
     * @param config Configuration for the health monitor
     * @return True if initialization successful
     */
    virtual bool initialize(const HealthMonitorConfig& config = {}) = 0;

    /**
     * @brief Start continuous health monitoring
     * @return True if monitoring started successfully
     */
    virtual bool start_monitoring() = 0;

    /**
     * @brief Stop continuous health monitoring
     */
    virtual void stop_monitoring() = 0;

    /**
     * @brief Perform a single health check
     * @return Health check result
     */
    virtual HealthCheckResult perform_health_check() = 0;

    /**
     * @brief Get current system health summary
     * @return System health summary
     */
    virtual SystemHealthSummary get_health_summary() = 0;

    /**
     * @brief Register a custom health check
     * @param name Name of the health check
     * @param category Health check category
     * @param check_function Function to perform the health check
     */
    virtual void register_health_check(
        const std::string& name,
        HealthCategory category,
        HealthCheckFunction check_function) = 0;

    /**
     * @brief Get dependency conflicts
     * @return List of dependency conflicts
     */
    virtual std::vector<DependencyConflict> get_dependency_conflicts() = 0;

    /**
     * @brief Check for dependency conflicts
     * @return List of new or updated conflicts
     */
    virtual std::vector<DependencyConflict> check_dependency_conflicts() = 0;

    /**
     * @brief Get health check history
     * @param category Optional category filter
     * @param limit Maximum number of results to return
     * @return Health check history
     */
    virtual std::vector<HealthCheckResult> get_health_history(
        std::optional<HealthCategory> category = std::nullopt,
        size_t limit = 100) = 0;

    /**
     * @brief Set health status change callback
     * @param callback Function to call when health status changes
     */
    virtual void set_status_change_callback(
        std::function<void(const SystemHealthSummary&)> callback) = 0;

    /**
     * @brief Get monitor statistics
     * @return Monitor statistics
     */
    virtual std::map<std::string, double> get_statistics() = 0;
};

/**
 * @brief Standard health monitor implementation
 */
class StandardHealthMonitor : public HealthMonitor {
public:
    StandardHealthMonitor();
    ~StandardHealthMonitor() override;

    // HealthMonitor interface
    bool initialize(const HealthMonitorConfig& config = {}) override;
    bool start_monitoring() override;
    void stop_monitoring() override;
    HealthCheckResult perform_health_check() override;
    SystemHealthSummary get_health_summary() override;
    void register_health_check(
        const std::string& name,
        HealthCategory category,
        HealthCheckFunction check_function) override;
    std::vector<DependencyConflict> get_dependency_conflicts() override;
    std::vector<DependencyConflict> check_dependency_conflicts() override;
    std::vector<HealthCheckResult> get_health_history(
        std::optional<HealthCategory> category = std::nullopt,
        size_t limit = 100) override;
    void set_status_change_callback(
        std::function<void(const SystemHealthSummary&)> callback) override;
    std::map<std::string, double> get_statistics() override;

private:
    struct Impl;
    std::unique_ptr<Impl> p_impl;
};

/**
 * @brief Create standard health monitor instance
 * @return Unique pointer to health monitor
 */
std::unique_ptr<HealthMonitor> create_health_monitor();

/**
 * @brief Convert health status to string
 * @param status Health status
 * @return String representation
 */
std::string health_status_to_string(HealthStatus status);

/**
 * @brief Convert health category to string
 * @param category Health category
 * @return String representation
 */
std::string health_category_to_string(HealthCategory category);

/**
 * @brief Convert string to health status
 * @param status_str String representation
 * @return Health status
 */
HealthStatus string_to_health_status(const std::string& status_str);

/**
 * @brief Convert string to health category
 * @param category_str String representation
 * @return Health category
 */
HealthCategory string_to_health_category(const std::string& category_str);

/**
 * @brief Calculate health score from check results
 * @param results Health check results
 * @return Health score (0.0 to 100.0)
 */
double calculate_health_score(const std::vector<HealthCheckResult>& results);

/**
 * @brief Determine overall health status
 * @param results Health check results
 * @return Overall health status
 */
HealthStatus determine_overall_status(const std::vector<HealthCheckResult>& results);

/**
 * @brief Format health check result for logging
 * @param result Health check result
 * @return Formatted string
 */
std::string format_health_result(const HealthCheckResult& result);

/**
 * @brief Format dependency conflict for logging
 * @param conflict Dependency conflict
 * @return Formatted string
 */
std::string format_dependency_conflict(const DependencyConflict& conflict);

} // namespace health
} // namespace integration