/**
 * Puzzle71Solver - Deployment Configuration Logger
 *
 * Provides comprehensive logging of deployment configuration decisions,
 * build choices, and deployment parameters for visibility and debugging.
 *
 * @author       Puzzle71Solver Team
 * @created      2025-10-10
 * @license      MIT
 */

#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <chrono>
#include <filesystem>

namespace integration {
namespace deployment {

/**
 * Log levels for configuration logging
 */
enum class LogLevel {
    DEBUG,
    INFO,
    WARN,
    ERROR
};

/**
 * Structure to hold configuration decision information
 */
struct ConfigDecision {
    std::string decision_type;
    std::string description;
    std::map<std::string, std::string> context;
    int64_t timestamp;
    std::string iso_timestamp;
};

/**
 * Deployment Configuration Logger
 *
 * Provides comprehensive logging of deployment-related configuration decisions,
 * build choices, and deployment parameters for visibility, debugging, and auditing.
 */
class DeploymentConfigLogger {
public:
    // Get singleton instance
    static DeploymentConfigLogger& get_instance();

    // Delete copy constructor and assignment operator
    DeploymentConfigLogger(const DeploymentConfigLogger&) = delete;
    DeploymentConfigLogger& operator=(const DeploymentConfigLogger&) = delete;

    ~DeploymentConfigLogger();

    /**
     * Initialize the configuration logger
     * @param log_file_path Path to log file (optional)
     * @param level Minimum log level
     */
    void initialize(const std::string& log_file_path = "", LogLevel level = LogLevel::INFO);

    /**
     * Enable or disable configuration logging
     * @param enabled Whether logging should be enabled
     */
    void set_enabled(bool enabled);

    /**
     * Log a configuration decision
     * @param decision_type Type of decision
     * @param description Description of the decision
     * @param context Additional context information
     */
    void log_decision(const std::string& decision_type,
                      const std::string& description,
                      const std::map<std::string, std::string>& context = {});

    /**
     * Log a CMake option setting
     * @param option_name Name of the CMake option
     * @param option_value Value of the option
     * @param source Source of the option (default, cache, command line, etc.)
     */
    void log_cmake_option(const std::string& option_name,
                         const std::string& option_value,
                         const std::string& source = "unknown");

    /**
     * Log a feature flag configuration
     * @param feature_name Name of the feature
     * @param enabled Whether the feature is enabled
     * @param reason Reason for the configuration
     */
    void log_feature_flag(const std::string& feature_name,
                         bool enabled,
                         const std::string& reason = "unknown");

    /**
     * Log dependency resolution information
     * @param dependency_name Name of the dependency
     * @param resolution_type How the dependency was resolved (extracted, system, external)
     * @param version Version of the dependency
     * @param source_path Path to the dependency source
     */
    void log_dependency_resolution(const std::string& dependency_name,
                                 const std::string& resolution_type,
                                 const std::string& version,
                                 const std::string& source_path);

    /**
     * Log build configuration information
     * @param config_type Type of build configuration
     * @param config Configuration parameters
     */
    void log_build_configuration(const std::string& config_type,
                                const std::map<std::string, std::string>& config);

    /**
     * Log deployment target configuration
     * @param target_name Name of the deployment target
     * @param dependencies List of target dependencies
     * @param properties Target properties
     */
    void log_deployment_target(const std::string& target_name,
                              const std::vector<std::string>& dependencies,
                              const std::map<std::string, std::string>& properties);

    /**
     * Log package configuration information
     * @param package_format Format of the package (tar.gz, zip, etc.)
     * @param package_config Package configuration parameters
     */
    void log_package_configuration(const std::string& package_format,
                                const std::map<std::string, std::string>& package_config);

    /**
     * Log environment detection information
     * @param env_info Environment information map
     */
    void log_environment_detection(const std::map<std::string, std::string>& env_info);

    /**
     * Get complete decision history
     * @return Vector of all configuration decisions
     */
    std::vector<ConfigDecision> get_decision_history() const;

    /**
     * Get decisions filtered by type
     * @param decision_type Type of decisions to retrieve
     * @return Vector of matching decisions
     */
    std::vector<ConfigDecision> get_decisions_by_type(const std::string& decision_type) const;

    /**
     * Generate a comprehensive configuration report
     * @return Markdown formatted report string
     */
    std::string generate_configuration_report() const;

    /**
     * Save configuration report to file
     * @param output_path Path to save the report
     * @return True if successful, false otherwise
     */
    bool save_configuration_report(const std::string& output_path) const;

    /**
     * Clear decision history
     */
    void clear_history();

    /**
     * Get total number of decisions logged
     * @return Decision count
     */
    size_t get_decision_count() const;

private:
    // Private constructor for singleton pattern
    DeploymentConfigLogger();

    // Instance management
    static std::unique_ptr<DeploymentConfigLogger> s_instance;
    static std::mutex s_instance_mutex;

    // Configuration state
    mutable std::mutex m_mutex;
    bool m_enabled;
    LogLevel m_log_level;
    std::string m_log_file_path;
    std::vector<ConfigDecision> m_decision_history;
    std::chrono::system_clock::time_point m_start_time;

    // Helper methods
    std::string get_current_timestamp() const;
    std::string format_iso_timestamp(const std::chrono::system_clock::time_point& time_point) const;
    std::string log_level_to_string(LogLevel level) const;
    std::string join_strings(const std::vector<std::string>& strings, const std::string& delimiter) const;
    void write_decision_to_file(const ConfigDecision& decision);
};

// Utility functions for easy access
inline DeploymentConfigLogger& get_deployment_config_logger() {
    return DeploymentConfigLogger::get_instance();
}

/**
 * Macro for easy configuration decision logging
 */
#define LOG_CONFIG_DECISION(type, description, ...) \
    integration::deployment::get_deployment_config_logger().log_decision(type, description, __VA_ARGS__)

#define LOG_CMAKE_OPTION(option, value, source) \
    integration::deployment::get_deployment_config_logger().log_cmake_option(option, value, source)

#define LOG_FEATURE_FLAG(feature, enabled, reason) \
    integration::deployment::get_deployment_config_logger().log_feature_flag(feature, enabled, reason)

#define LOG_DEPENDENCY_RESOLUTION(dep, type, version, path) \
    integration::deployment::get_deployment_config_logger().log_dependency_resolution(dep, type, version, path)

} // namespace deployment
} // namespace integration