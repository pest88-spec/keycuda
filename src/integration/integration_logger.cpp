/**
 * Integration Logging System Implementation
 *
 * @origin       https://github.com/Puzzle71Solver/Puzzle71Solver
 * @origin_path  src/integration/integration_logger.cpp
 * @origin_commit <current_commit>
 * @origin_license MIT
 * @extracted_date   2025-10-10
 * @extracted_by     Puzzle71Solver Team
 * @modifications    Created for third-party dependency integration optimization
 * @spdx_license_identifier MIT
 */

#include "integration_logger.h"
#include <iostream>
#include <thread>

IntegrationLogger::IntegrationLogger(
    const std::string& log_file_path,
    LogLevel min_level,
    bool json_output,
    bool console_output
) : log_file_path_(log_file_path),
    min_level_(min_level),
    json_output_enabled_(json_output),
    console_output_enabled_(console_output) {

    // Open log file if path provided
    if (!log_file_path_.empty()) {
        log_file_.open(log_file_path_, std::ios::app);
        if (!log_file_.is_open()) {
            std::cerr << "Warning: Could not open log file: " << log_file_path_ << std::endl;
        }
    }
}

IntegrationLogger::~IntegrationLogger() {
    close();
}

std::string IntegrationLogger::level_to_string(LogLevel level) const {
    switch (level) {
        case LogLevel::DEBUG:    return "DEBUG";
        case LogLevel::INFO:     return "INFO";
        case LogLevel::WARNING:  return "WARNING";
        case LogLevel::ERROR:    return "ERROR";
        case LogLevel::CRITICAL: return "CRITICAL";
        default:                 return "UNKNOWN";
    }
}

std::string IntegrationLogger::operation_to_string(OperationType operation) const {
    switch (operation) {
        case OperationType::ATTRIBUTION_AUDIT:  return "ATTRIBUTION_AUDIT";
        case OperationType::LIBRARY_EXTRACTION:   return "LIBRARY_EXTRACTION";
        case OperationType::BUILD_INTEGRATION:    return "BUILD_INTEGRATION";
        case OperationType::VALIDATION:          return "VALIDATION";
        case OperationType::ERROR_HANDLING:      return "ERROR_HANDLING";
        case OperationType::COMPLIANCE_CHECK:     return "COMPLIANCE_CHECK";
        case OperationType::METRICS_COLLECTION:   return "METRICS_COLLECTION";
        default:                                   return "UNKNOWN_OPERATION";
    }
}

std::string IntegrationLogger::format_timestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);

    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

std::string IntegrationLogger::escape_json(const std::string& str) const {
    std::string escaped;
    for (char c : str) {
        switch (c) {
            case '"':  escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (c >= 0 && c < 32) {
                    escaped += "\\u" + std::to_string(c);
                } else {
                    escaped += c;
                }
                break;
        }
    }
    return escaped;
}

std::string IntegrationLogger::format_text_event(const IntegrationEvent& event) const {
    std::stringstream ss;
    ss << "[" << event.timestamp << "] "
       << "[" << level_to_string(event.level) << "] "
       << "[" << operation_to_string(event.operation) << "] ";

    if (!event.library.empty()) {
        ss << "[" << event.library << "] ";
    }

    if (!event.component.empty()) {
        ss << "[" << event.component << "] ";
    }

    ss << event.action;

    if (!event.status.empty()) {
        ss << " - " << event.status;
    }

    if (!event.details.empty()) {
        ss << " (" << event.details << ")";
    }

    if (event.duration > 0.0) {
        ss << " [" << std::fixed << std::setprecision(3) << event.duration << "s]";
    }

    return ss.str();
}

std::string IntegrationLogger::format_json_event(const IntegrationEvent& event) const {
    std::stringstream ss;
    ss << "{"
       << "\"timestamp\":\"" << event.timestamp << "\","
       << "\"level\":\"" << level_to_string(event.level) << "\","
       << "\"operation\":\"" << operation_to_string(event.operation) << "\"";

    if (!event.library.empty()) {
        ss << ",\"library\":\"" << escape_json(event.library) << "\"";
    }

    if (!event.component.empty()) {
        ss << ",\"component\":\"" << escape_json(event.component) << "\"";
    }

    ss << ",\"action\":\"" << escape_json(event.action) << "\"";

    if (!event.status.empty()) {
        ss << ",\"status\":\"" << escape_json(event.status) << "\"";
    }

    if (!event.details.empty()) {
        ss << ",\"details\":\"" << escape_json(event.details) << "\"";
    }

    if (event.duration > 0.0) {
        ss << ",\"duration\":" << std::fixed << std::setprecision(6) << event.duration;
    }

    ss << ",\"thread_id\":\"" << std::this_thread::get_id() << "\"}";
    ss << "}";

    return ss.str();
}

void IntegrationLogger::write_log_entry(const std::string& entry) {
    // Write to console if enabled
    if (console_output_enabled_) {
        std::cout << entry << std::endl;
    }

    // Write to file if available
    if (log_file_.is_open()) {
        log_file_ << entry << std::endl;
        log_file_.flush();
    }
}

void IntegrationLogger::log(
    LogLevel level,
    OperationType operation,
    const std::string& library,
    const std::string& component,
    const std::string& action,
    const std::string& status,
    const std::string& details,
    double duration
) {
    // Check minimum level
    if (level < min_level_) {
        return;
    }

    // Create event
    IntegrationEvent event;
    event.timestamp = format_timestamp();
    event.level = level;
    event.operation = operation;
    event.library = library;
    event.component = component;
    event.action = action;
    event.status = status;
    event.details = details;
    event.duration = duration;

    // Format and write entry
    std::string entry;
    if (json_output_enabled_) {
        entry = format_json_event(event);
    } else {
        entry = format_text_event(event);
    }

    write_log_entry(entry);
}

void IntegrationLogger::info(
    OperationType operation,
    const std::string& library,
    const std::string& component,
    const std::string& action,
    const std::string& status,
    const std::string& details
) {
    log(LogLevel::INFO, operation, library, component, action, status, details);
}

void IntegrationLogger::warning(
    OperationType operation,
    const std::string& library,
    const std::string& component,
    const std::string& action,
    const std::string& status,
    const std::string& details
) {
    log(LogLevel::WARNING, operation, library, component, action, status, details);
}

void IntegrationLogger::error(
    OperationType operation,
    const std::string& library,
    const std::string& component,
    const std::string& action,
    const std::string& status,
    const std::string& details
) {
    log(LogLevel::ERROR, operation, library, component, action, status, details);
}

void IntegrationLogger::critical(
    OperationType operation,
    const std::string& library,
    const std::string& component,
    const::string& action,
    const std::string& status,
    const std::string& details
) {
    log(LogLevel::CRITICAL, operation, library, component, action, status, details);
}

void IntegrationLogger::set_min_level(LogLevel level) {
    min_level_ = level;
}

void IntegrationLogger::set_json_output(bool enabled) {
    json_output_enabled_ = enabled;
}

void IntegrationLogger::set_console_output(bool enabled) {
    console_output_enabled_ = enabled;
}

IntegrationLogger::LogStats IntegrationLogger::get_statistics() const {
    // This would require maintaining counters during logging
    // For now, return empty stats
    return {0, 0, 0, 0, 0};
}

void IntegrationLogger::flush() {
    if (log_file_.is_open()) {
        log_file_.flush();
    }
}

void IntegrationLogger::close() {
    if (log_file_.is_open()) {
        log_file_.close();
    }
}

// IntegrationTimer implementation
IntegrationTimer::IntegrationTimer(
    IntegrationLogger& logger,
    IntegrationLogger::LogLevel level,
    IntegrationLogger::OperationType operation,
    const std::string& library,
    const std::string& component,
    const std::string& action
) : logger_(logger),
    level_(level),
    operation_(operation),
    library_(library),
    component_(component),
    action_(action),
    start_time_(std::chrono::steady_clock::now()),
    completed_(false) {
}

IntegrationTimer::~IntegrationTimer() {
    if (!completed_) {
        // Auto-complete with unknown status
        complete("UNKNOWN", "Timer auto-completed");
    }
}

void IntegrationTimer::complete(const std::string& status, const std::string& details) {
    if (completed_) {
        return;
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::duration<double>>(end_time - start_time_);

    logger_.log(level_, operation_, library_, component_, action_, status, details, duration.count());
    completed_ = true;
}

void IntegrationTimer::fail(const std::string& error) {
    complete("FAILED", error);
}