// T037: Add Configuration Decision Logging for Deployment Visibility
// Logs all deployment configuration decisions for transparency and debugging

#include "deployment_logger.h"
#include <fstream>
#include <sstream>
#include <iomanip>

namespace integration {
namespace deployment {

DeploymentLogger::DeploymentLogger() {
    log_file_path_ = get_default_log_path();
    log_level_ = LogLevel::INFO;
    console_output_ = true;

    initialize_logger();
}

DeploymentLogger::~DeploymentLogger() {
    if (log_file_.is_open()) {
        log_file_.close();
    }
}

void DeploymentLogger::initialize_logger() {
    // Create logs directory if it doesn't exist
    std::filesystem::path log_dir = std::filesystem::path(log_file_path_).parent_path();
    if (!std::filesystem::exists(log_dir)) {
        std::filesystem::create_directories(log_dir);
    }

    // Open log file
    log_file_.open(log_file_path_, std::ios::app);
    if (!log_file_.is_open()) {
        std::cerr << "Warning: Could not open deployment log file: " << log_file_path_ << std::endl;
    }

    // Log initialization
    log_event("logger_initialized", {
        {"log_file", log_file_path_},
        {"log_level", log_level_to_string(log_level_)},
        {"console_output", console_output_ ? "true" : "false"}
    });
}

void DeploymentLogger::log_configuration_decision(const std::string& decision,
                                                   const std::map<std::string, std::string>& context,
                                                   const std::string& reason) {
    nlohmann::json log_entry = {
        {"timestamp", get_current_timestamp()},
        {"type", "configuration_decision"},
        {"decision", decision},
        {"reason", reason},
        {"context", context}
    };

    write_log_entry(log_entry);

    if (console_output_) {
        std::cout << "[DEPLOYMENT-CONFIG] " << decision;
        if (!reason.empty()) {
            std::cout << " (" << reason << ")";
        }
        std::cout << std::endl;
    }
}

void DeploymentLogger::log_dependency_resolution(const std::string& dependency,
                                                  const std::string& resolution,
                                                  const std::string& source) {
    log_event("dependency_resolution", {
        {"dependency", dependency},
        {"resolution", resolution},
        {"source", source}
    });
}

void DeploymentLogger::log_package_component(const std::string& component,
                                             const std::string& action,
                                             const nlohmann::json& details) {
    nlohmann::json log_entry = {
        {"timestamp", get_current_timestamp()},
        {"type", "package_component"},
        {"component", component},
        {"action", action},
        {"details", details}
    };

    write_log_entry(log_entry);
}

void DeploymentLogger::log_validation_result(const std::string& validation_type,
                                               bool passed,
                                               const std::vector<std::string>& issues) {
    log_event("validation_result", {
        {"validation_type", validation_type},
        {"passed", passed},
        {"issues_count", issues.size()},
        {"issues", issues}
    });
}

void DeploymentLogger::log_performance_metric(const std::string& metric_name,
                                               double value,
                                               const std::string& unit) {
    log_event("performance_metric", {
        {"metric_name", metric_name},
        {"value", value},
        {"unit", unit}
    });
}

void DeploymentLogger::log_deployment_summary(const DeploymentSummary& summary) {
    nlohmann::json log_entry = {
        {"timestamp", get_current_timestamp()},
        {"type", "deployment_summary"},
        {"summary", {
            {"package_name", summary.package_name},
            {"version", summary.version},
            {"platform", summary.platform},
            {"total_components", summary.total_components},
            {"total_size_mb", summary.total_size_mb},
            {"build_time_seconds", summary.build_time_seconds},
            {"validation_passed", summary.validation_passed},
            {"attribution_compliant", summary.attribution_compliant}
        }}
    };

    write_log_entry(log_entry);

    // Also write a separate summary file
    std::string summary_file = std::filesystem::path(log_file_path_).parent_path() / "deployment_summary.json";
    std::ofstream summary_out(summary_file);
    if (summary_out.is_open()) {
        summary_out << log_entry.dump(2) << std::endl;
        summary_out.close();
    }
}

void DeploymentLogger::set_log_level(LogLevel level) {
    log_level_ = level;
}

void DeploymentLogger::set_console_output(bool enabled) {
    console_output_ = enabled;
}

void DeploymentLogger::set_log_file_path(const std::string& path) {
    if (log_file_.is_open()) {
        log_file_.close();
    }
    log_file_path_ = path;
    initialize_logger();
}

std::string DeploymentLogger::get_default_log_path() {
    // Try environment variable first
    const char* env_path = std::getenv("DEPLOYMENT_LOG_PATH");
    if (env_path && strlen(env_path) > 0) {
        return std::string(env_path) + "/deployment.log";
    }

    // Default to project build directory
    return "build/logs/deployment.log";
}

std::string DeploymentLogger::get_current_timestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
    return ss.str();
}

std::string DeploymentLogger::log_level_to_string(LogLevel level) const {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO: return "INFO";
        case LogLevel::WARNING: return "WARNING";
        case LogLevel::ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

void DeploymentLogger::write_log_entry(const nlohmann::json& entry) {
    // Write to file if open
    if (log_file_.is_open()) {
        log_file_ << entry.dump() << std::endl;
        log_file_.flush();
    }

    // Also write to structured log file for easier parsing
    std::string structured_log_path = std::filesystem::path(log_file_path_).parent_path() / "deployment_structured.log";
    std::ofstream structured_log(structured_log_path, std::ios::app);
    if (structured_log.is_open()) {
        structured_log << entry.dump() << std::endl;
        structured_log.close();
    }
}

void DeploymentLogger::log_event(const std::string& event_type,
                                  const std::map<std::string, std::string>& data) {
    nlohmann::json log_entry = {
        {"timestamp", get_current_timestamp()},
        {"type", event_type},
        {"data", data}
    };

    write_log_entry(log_entry);
}

// Convenience functions for common logging scenarios
void DeploymentLogger::log_package_creation_start(const std::string& package_name,
                                                   const std::string& version) {
    log_configuration_decision("package_creation_start", {
        {"package_name", package_name},
        {"version", version}
    }, "Starting deployment package creation");
}

void DeploymentLogger::log_component_included(const std::string& component_type,
                                               const std::string& component_name,
                                               const std::string& source_path) {
    log_package_component(component_name, "included", {
        {"type", component_type},
        {"source_path", source_path},
        {"size_bytes", std::filesystem::file_size(source_path)}
    });
}

void DeploymentLogger::log_dependency_skipped(const std::string& dependency,
                                               const std::string& reason) {
    log_dependency_resolution(dependency, "skipped", reason);
}

void DeploymentLogger::log_compression_result(const std::string& algorithm,
                                             size_t original_size,
                                             size_t compressed_size,
                                             double compression_ratio) {
    log_configuration_decision("compression_completed", {
        {"algorithm", algorithm},
        {"original_size_bytes", std::to_string(original_size)},
        {"compressed_size_bytes", std::to_string(compressed_size)},
        {"compression_ratio", std::to_string(compression_ratio)}
    }, "Package compression completed successfully");
}

void DeploymentLogger::log_attribution_check(const std::string& library_name,
                                             bool compliant,
                                             const std::vector<std::string>& missing_items) {
    log_validation_result("attribution_compliance", compliant, missing_items);

    if (!compliant) {
        log_configuration_decision("attribution_issue_detected", {
            {"library", library_name},
            {"missing_items_count", std::to_string(missing_items.size())}
        }, "Library missing required attribution elements");
    }
}

void DeploymentLogger::log_cross_platform_test(const std::string& platform,
                                                bool compatible,
                                                const std::vector<std::string>& issues) {
    log_validation_result("platform_compatibility", compatible, issues);

    log_configuration_decision("platform_test_result", {
        {"platform", platform},
        {"compatible", compatible ? "true" : "false"},
        {"issues_count", std::to_string(issues.size())}
    }, compatible ? "Platform compatibility verified" : "Platform compatibility issues found");
}

// Global instance
static std::unique_ptr<DeploymentLogger> g_deployment_logger;

DeploymentLogger& get_deployment_logger() {
    if (!g_deployment_logger) {
        g_deployment_logger = std::make_unique<DeploymentLogger>();
    }
    return *g_deployment_logger;
}

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