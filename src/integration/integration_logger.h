/**
 * Integration Logging System for Puzzle71Solver
 *
 * Provides structured logging with JSON output capability for third-party
 * library integration operations, including attribution, extraction, and
 * build system integration activities.
 *
 * @origin       https://github.com/Puzzle71Solver/Puzzle71Solver
 * @origin_path  src/integration/integration_logger.h
 * @origin_commit <current_commit>
 * @origin_license MIT
 * @extracted_date   2025-10-10
 * @extracted_by     Puzzle71Solver Team
 * @modifications    Created for third-party dependency integration optimization
 * @spdx_license_identifier MIT
 */

#pragma once

#include <string>
#include <fstream>
#include <memory>
#include <chrono>
#include <iomanip>
#include <sstream>

/**
 * Integration Logger Class
 *
 * Provides structured logging for all integration operations with support for
 * both human-readable and JSON output formats. Logs include timestamps,
 * operation types, status, and detailed metadata.
 */
class IntegrationLogger {
public:
    /**
     * Log levels for integration operations
     */
    enum class LogLevel {
        DEBUG = 0,
        INFO = 1,
        WARNING = 2,
        ERROR = 3,
        CRITICAL = 4
    };

    /**
     * Operation types for integration logging
     */
    enum class OperationType {
        ATTRIBUTION_AUDIT,
        LIBRARY_EXTRACTION,
        BUILD_INTEGRATION,
        VALIDATION,
        ERROR_HANDLING,
        COMPLIANCE_CHECK,
        METRICS_COLLECTION
    };

    /**
     * Integration event structure
     */
    struct IntegrationEvent {
        std::string timestamp;
        LogLevel level;
        OperationType operation;
        std::string library;
        std::string component;
        std::string action;
        std::string status;
        std::string details;
        std::chrono::steady_clock::duration<double> duration;

        IntegrationEvent() : duration(0.0) {}
    };

private:
    std::string log_file_path_;
    std::ofstream log_file_;
    LogLevel min_level_;
    bool json_output_enabled_;
    bool console_output_enabled_;

    /**
     * Convert log level to string
     */
    std::string level_to_string(LogLevel level) const;

    /**
     * Convert operation type to string
     */
    std::string operation_to_string(OperationType operation) const;

    /**
     * Format timestamp in ISO 8601 format
     */
    std::string format_timestamp() const;

    /**
     * Format event as human-readable text
     */
    std::string format_text_event(const IntegrationEvent& event) const;

    /**
     * Format event as JSON
     */
    std::string format_json_event(const IntegrationEvent& event) const;

    /**
     * Escape JSON string
     */
    std::string escape_json(const std::string& str) const;

    /**
     * Write log entry to all configured outputs
     */
    void write_log_entry(const std::string& entry);

public:
    /**
     * Constructor
     *
     * @param log_file_path Path to log file (optional)
     * @param min_level Minimum log level to record
     * @param json_output Enable JSON formatted output
     * @param console_output Enable console output
     */
    explicit IntegrationLogger(
        const std::string& log_file_path = "",
        LogLevel min_level = LogLevel::INFO,
        bool json_output = true,
        bool console_output = true
    );

    /**
     * Destructor - closes log file
     */
    ~IntegrationLogger();

    /**
     * Log an integration event
     *
     * @param level Log level
     * @param operation Type of operation
     * @param library Library name (optional)
     * @param component Component name (optional)
     * @param action Action description
     * @param status Status of the action
     * @param details Additional details (optional)
     * @param duration Operation duration in seconds (optional)
     */
    void log(
        LogLevel level,
        OperationType operation,
        const std::string& library = "",
        const std::string& component = "",
        const std::string& action = "",
        const std::string& status = "",
        const std::string& details = "",
        double duration = 0.0
    );

    /**
     * Convenience method for logging info events
     */
    void info(
        OperationType operation,
        const std::string& library = "",
        const std::string& component = "",
        const std::string& action = "",
        const std::string& status = "",
        const std::string& details = ""
    );

    /**
     * Convenience method for logging warning events
     */
    void warning(
        OperationType operation,
        const std::string& library = "",
        const std::string& component = "",
        const std::string& action = "",
        const std::string& status = "",
        const std::string& details = ""
    );

    /**
     * Convenience method for logging error events
     */
    void error(
        OperationType operation,
        const std::string& library = "",
        const std::string& component = "",
        const std::string& action = "",
        const std::string& status = "",
        const std::string& details = ""
    );

    /**
     * Convenience method for logging critical events
     */
    void critical(
        OperationType operation,
        const std::string& library = "",
        const std::string& component = "",
        const std::string& action = "",
        const std::string& status = "",
        const std::string& details = ""
    );

    /**
     * Set minimum log level
     */
    void set_min_level(LogLevel level);

    /**
     * Enable/disable JSON output
     */
    void set_json_output(bool enabled);

    /**
     * Enable/disable console output
     */
    void set_console_output(bool enabled);

    /**
     * Get current log statistics
     */
    struct LogStats {
        size_t total_events;
        size_t info_events;
        size_t warning_events;
        size_t error_events;
        size_t critical_events;
    };

    LogStats get_statistics() const;

    /**
     * Flush log buffer
     */
    void flush();

    /**
     * Close log file
     */
    void close();
};

/**
 * RAII Timer for measuring operation duration
 */
class IntegrationTimer {
private:
    IntegrationLogger& logger_;
    IntegrationLogger::LogLevel level_;
    IntegrationLogger::OperationType operation_;
    std::string library_;
    std::string component_;
    std::string action_;
    std::chrono::steady_clock::time_point start_time_;
    bool completed_;

public:
    IntegrationTimer(
        IntegrationLogger& logger,
        IntegrationLogger::LogLevel level,
        IntegrationLogger::OperationType operation,
        const std::string& library = "",
        const std::string& component = "",
        const std::string& action = ""
    );

    ~IntegrationTimer();

    /**
     * Complete the timer and log the result
     *
     * @param status Status of the operation
     * @param details Additional details
     */
    void complete(const std::string& status, const std::string& details = "");

    /**
     * Mark operation as failed
     *
     * @param error Error message
     */
    void fail(const std::string& error);
};

// Convenience macros for common logging patterns
#define LOG_INTEGRATION_INFO(logger, op, lib, comp, action, status, details) \
    (logger).info(IntegrationLogger::OperationType::op, lib, comp, action, status, details)

#define LOG_INTEGRATION_WARNING(logger, op, lib, comp, action, status, details) \
    (logger).warning(IntegrationLogger::OperationType::op, lib, comp, action, status, details)

#define LOG_INTEGRATION_ERROR(logger, op, lib, comp, action, status, details) \
    (logger).error(IntegrationLogger::OperationType::op, lib, comp, action, status, details)

#define INTEGRATION_TIMER(logger, level, op, lib, comp, action) \
    IntegrationTimer timer(logger, level, IntegrationLogger::OperationType::op, lib, comp, action)