/**
 * Puzzle71Solver - Integration Logging Infrastructure
 *
 * Provides comprehensive logging infrastructure for third-party library integration
 * operations with JSON output capability and structured logging.
 *
 * @author       Puzzle71Solver Team
 * @created      2025-10-09
 * @license      MIT
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include <chrono>
#include <map>
#include <sstream>
#include <iomanip>

namespace integration {
namespace logging {

enum class LogLevel {
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARNING = 3,
    ERROR = 4,
    FATAL = 5
};

enum class LogCategory {
    GENERAL,
    INTEGRATION,
    ATTRIBUTION,
    VERIFICATION,
    BUILD,
    CONFIGURATION,
    PERFORMANCE,
    AUDIT
};

struct LogEntry {
    std::chrono::system_clock::time_point timestamp;
    LogLevel level;
    LogCategory category;
    std::string message;
    std::string component;
    std::map<std::string, std::string> metadata;
    std::string operation_id;
    std::string library_name;

    LogEntry() : timestamp(std::chrono::system_clock::now()) {}
};

class JsonLogger {
public:
    JsonLogger();
    explicit JsonLogger(const std::string& log_file_path);
    ~JsonLogger();

    // Configuration
    void set_log_file(const std::string& file_path);
    void set_log_level(LogLevel level);
    void enable_console_output(bool enabled);
    void enable_file_output(bool enabled);
    void enable_json_output(bool enabled);

    // Core logging methods
    void log(LogLevel level, LogCategory category, const std::string& message,
            const std::string& component = "",
            const std::map<std::string, std::string>& metadata = {},
            const std::string& operation_id = "",
            const std::string& library_name = "");

    // Convenience methods by log level
    void trace(const std::string& message, LogCategory category = LogCategory::GENERAL,
               const std::map<std::string, std::string>& metadata = {},
               const std::string& operation_id = "", const std::string& library_name = "");

    void debug(const std::string& message, LogCategory category = LogCategory::GENERAL,
               const std::map<std::string, std::string>& metadata = {},
               const std::string& operation_id = "", const std::string& library_name = "");

    void info(const std::string& message, LogCategory category = LogCategory::GENERAL,
              const std::map<std::string, std::string>& metadata = {},
              const std::string& operation_id = "", const std::string& library_name = "");

    void warning(const std::string& message, LogCategory category = LogCategory::GENERAL,
                 const std::map<std::string, std::string>& metadata = {},
                 const std::string& operation_id = "", const std::string& library_name = "");

    void error(const std::string& message, LogCategory category = LogCategory::GENERAL,
               const std::map<std::string, std::string>& metadata = {},
               const std::string& operation_id = "", const std::string& library_name = "");

    void fatal(const std::string& message, LogCategory category = LogCategory::GENERAL,
               const std::map<std::string, std::string>& metadata = {},
               const std::string& operation_id = "", const std::string& library_name = "");

    // Integration-specific logging methods
    void log_integration_start(const std::string& library_name, const std::string& operation_id,
                              const std::map<std::string, std::string>& metadata = {});

    void log_integration_complete(const std::string& library_name, const std::string& operation_id,
                                 bool success, const std::map<std::string, std::string>& metrics = {});

    void log_attribution_added(const std::string& file_path, const std::string& library_name,
                              const std::string& operation_id);

    void log_verification_result(const std::string& library_name, const std::string& verification_type,
                                 bool passed, const std::map<std::string, std::string>& details = {});

    void log_performance_metric(const std::string& operation_name, double duration_ms,
                               const std::map<std::string, std::string>& additional_metrics = {});

    // Query and analysis methods
    std::vector<LogEntry> get_logs(LogLevel min_level = LogLevel::TRACE,
                                 LogCategory category = LogCategory::GENERAL) const;

    std::vector<LogEntry> get_logs_for_operation(const std::string& operation_id) const;

    std::vector<LogEntry> get_logs_for_library(const std::string& library_name) const;

    // Export methods
    bool export_logs_json(const std::string& output_path) const;
    bool export_logs_csv(const std::string& output_path) const;
    std::string get_logs_json_string(LogLevel min_level = LogLevel::TRACE) const;

    // Utility methods
    void flush();
    void clear();
    size_t get_log_count() const;
    std::string generate_operation_id() const;

    // Statistics
    std::map<LogLevel, size_t> get_log_counts_by_level() const;
    std::map<LogCategory, size_t> get_log_counts_by_category() const;

private:
    struct Impl;
    std::unique_ptr<Impl> p_impl;

    // Helper methods
    std::string log_level_to_string(LogLevel level) const;
    std::string log_category_to_string(LogCategory category) const;
    std::string timestamp_to_string(const std::chrono::system_clock::time_point& timestamp) const;
    std::string entry_to_json(const LogEntry& entry) const;
    std::string map_to_json(const std::map<std::string, std::string>& metadata) const;
    void write_entry(const LogEntry& entry);
};

// Global logger instance
JsonLogger& get_integration_logger();

// Logging macros for convenience
#define LOG_TRACE(message, ...) \
    integration::logging::get_integration_logger().trace(message, __VA_ARGS__)

#define LOG_DEBUG(message, ...) \
    integration::logging::get_integration_logger().debug(message, __VA_ARGS__)

#define LOG_INFO(message, ...) \
    integration::logging::get_integration_logger().info(message, __VA_ARGS__)

#define LOG_WARNING(message, ...) \
    integration::logging::get_integration_logger().warning(message, __VA_ARGS__)

#define LOG_ERROR(message, ...) \
    integration::logging::get_integration_logger().error(message, __VA_ARGS__)

#define LOG_FATAL(message, ...) \
    integration::logging::get_integration_logger().fatal(message, __VA_ARGS__)

// Category-specific macros
#define LOG_INTEGRATION_INFO(message, ...) \
    integration::logging::get_integration_logger().info(message, integration::logging::LogCategory::INTEGRATION, __VA_ARGS__)

#define LOG_ATTRIBUTION_INFO(message, ...) \
    integration::logging::get_integration_logger().info(message, integration::logging::LogCategory::ATTRIBUTION, __VA_ARGS__)

#define LOG_VERIFICATION_INFO(message, ...) \
    integration::logging::get_integration_logger().info(message, integration::logging::LogCategory::VERIFICATION, __VA_ARGS__)

#define LOG_PERFORMANCE_INFO(message, ...) \
    integration::logging::get_integration_logger().info(message, integration::logging::LogCategory::PERFORMANCE, __VA_ARGS__)

#define LOG_AUDIT_INFO(message, ...) \
    integration::logging::get_integration_logger().info(message, integration::logging::LogCategory::AUDIT, __VA_ARGS__)

} // namespace logging
} // namespace integration