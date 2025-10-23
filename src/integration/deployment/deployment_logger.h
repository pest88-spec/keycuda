// T037: Add Configuration Decision Logging for Deployment Visibility
// Logs all deployment configuration decisions for transparency and debugging

#pragma once

#include <string>
#include <map>
#include <vector>
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace integration {
namespace deployment {

enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR
};

struct DeploymentSummary {
    std::string package_name;
    std::string version;
    std::string platform;
    size_t total_components;
    double total_size_mb;
    double build_time_seconds;
    bool validation_passed;
    bool attribution_compliant;
};

class DeploymentLogger {
public:
    DeploymentLogger();
    ~DeploymentLogger();

    // Main logging methods
    void log_configuration_decision(const std::string& decision,
                                   const std::map<std::string, std::string>& context,
                                   const std::string& reason = "");

    void log_dependency_resolution(const std::string& dependency,
                                   const std::string& resolution,
                                   const std::string& source);

    void log_package_component(const std::string& component,
                               const std::string& action,
                               const nlohmann::json& details);

    void log_validation_result(const std::string& validation_type,
                               bool passed,
                               const std::vector<std::string>& issues = {});

    void log_performance_metric(const std::string& metric_name,
                               double value,
                               const std::string& unit);

    void log_deployment_summary(const DeploymentSummary& summary);

    // Configuration
    void set_log_level(LogLevel level);
    void set_console_output(bool enabled);
    void set_log_file_path(const std::string& path);

    // Convenience methods for common scenarios
    void log_package_creation_start(const std::string& package_name,
                                    const std::string& version);

    void log_component_included(const std::string& component_type,
                                const std::string& component_name,
                                const std::string& source_path);

    void log_dependency_skipped(const std::string& dependency,
                                const std::string& reason);

    void log_compression_result(const std::string& algorithm,
                                size_t original_size,
                                size_t compressed_size,
                                double compression_ratio);

    void log_attribution_check(const std::string& library_name,
                                bool compliant,
                                const std::vector<std::string>& missing_items = {});

    void log_cross_platform_test(const std::string& platform,
                                 bool compatible,
                                 const std::vector<std::string>& issues = {});

private:
    void initialize_logger();
    void write_log_entry(const nlohmann::json& entry);
    void log_event(const std::string& event_type,
                   const std::map<std::string, std::string>& data);

    std::string get_default_log_path() const;
    std::string get_current_timestamp() const;
    std::string log_level_to_string(LogLevel level) const;

    // Member variables
    std::string log_file_path_;
    std::ofstream log_file_;
    LogLevel log_level_;
    bool console_output_;
};

// Global accessor
DeploymentLogger& get_deployment_logger();

// Convenience macros
#define LOG_DEPLOYMENT_CONFIG(decision, context, reason) \
    integration::deployment::get_deployment_logger().log_configuration_decision(decision, context, reason)

#define LOG_DEPLOYMENT_DEPENDENCY(dep, resolution, source) \
    integration::deployment::get_deployment_logger().log_dependency_resolution(dep, resolution, source)

#define LOG_DEPLOYMENT_COMPONENT(component, action, details) \
    integration::deployment::get_deployment_logger().log_package_component(component, action, details)

#define LOG_DEPLOYMENT_VALIDATION(type, passed, issues) \
    integration::deployment::get_deployment_logger().log_validation_result(type, passed, issues)

#define LOG_DEPLOYMENT_PERFORMANCE(metric, value, unit) \
    integration::deployment::get_deployment_logger().log_performance_metric(metric, value, unit)

} // namespace deployment
} // namespace integration