/**
 * Puzzle71Solver - Integration Logging Infrastructure Implementation
 *
 * Provides comprehensive logging infrastructure for third-party library integration
 * operations with JSON output capability and structured logging.
 *
 * @author       Puzzle71Solver Team
 * @created      2025-10-09
 * @license      MIT
 */

#include "integration_logger.h"
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <random>
#include <iomanip>

namespace integration {
namespace logging {

struct JsonLogger::Impl {
    std::vector<LogEntry> log_entries;
    std::ofstream log_file;
    std::string log_file_path;
    LogLevel min_level = LogLevel::INFO;
    bool console_output_enabled = true;
    bool file_output_enabled = false;
    bool json_output_enabled = true;
    mutable std::mutex log_mutex;

    std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<uint64_t> dist;

    void write_to_file(const std::string& content) {
        if (file_output_enabled && log_file.is_open()) {
            log_file << content << std::endl;
            log_file.flush();
        }
    }

    void write_to_console(const std::string& content) {
        if (console_output_enabled) {
            std::cout << content << std::endl;
        }
    }
};

JsonLogger::JsonLogger() : p_impl(std::make_unique<Impl>()) {}

JsonLogger::JsonLogger(const std::string& log_file_path) : JsonLogger() {
    set_log_file(log_file_path);
}

JsonLogger::~JsonLogger() {
    if (p_impl->log_file.is_open()) {
        p_impl->log_file.close();
    }
}

void JsonLogger::set_log_file(const std::string& file_path) {
    std::lock_guard<std::mutex> lock(p_impl->log_mutex);

    if (p_impl->log_file.is_open()) {
        p_impl->log_file.close();
    }

    p_impl->log_file_path = file_path;

    if (!file_path.empty()) {
        p_impl->log_file.open(file_path, std::ios::app);
        if (p_impl->log_file.is_open()) {
            p_impl->file_output_enabled = true;
            // Write opening marker for new logging session
            p_impl->log_file << "\n// Logging session started at "
                            << timestamp_to_string(std::chrono::system_clock::now())
                            << "\n";
        } else {
            std::cerr << "Warning: Failed to open log file: " << file_path << std::endl;
            p_impl->file_output_enabled = false;
        }
    } else {
        p_impl->file_output_enabled = false;
    }
}

void JsonLogger::set_log_level(LogLevel level) {
    p_impl->min_level = level;
}

void JsonLogger::enable_console_output(bool enabled) {
    p_impl->console_output_enabled = enabled;
}

void JsonLogger::enable_file_output(bool enabled) {
    p_impl->file_output_enabled = enabled;
}

void JsonLogger::enable_json_output(bool enabled) {
    p_impl->json_output_enabled = enabled;
}

void JsonLogger::log(LogLevel level, LogCategory category, const std::string& message,
                    const std::string& component,
                    const std::map<std::string, std::string>& metadata,
                    const std::string& operation_id,
                    const std::string& library_name) {
    if (level < p_impl->min_level) {
        return;
    }

    LogEntry entry;
    entry.timestamp = std::chrono::system_clock::now();
    entry.level = level;
    entry.category = category;
    entry.message = message;
    entry.component = component;
    entry.metadata = metadata;
    entry.operation_id = operation_id;
    entry.library_name = library_name;

    write_entry(entry);
}

void JsonLogger::trace(const std::string& message, LogCategory category,
                      const std::map<std::string, std::string>& metadata,
                      const std::string& operation_id, const std::string& library_name) {
    log(LogLevel::TRACE, category, message, "integration_logger", metadata, operation_id, library_name);
}

void JsonLogger::debug(const std::string& message, LogCategory category,
                       const std::map<std::string, std::string>& metadata,
                       const std::string& operation_id, const std::string& library_name) {
    log(LogLevel::DEBUG, category, message, "integration_logger", metadata, operation_id, library_name);
}

void JsonLogger::info(const std::string& message, LogCategory category,
                      const std::map<std::string, std::string>& metadata,
                      const std::string& operation_id, const std::string& library_name) {
    log(LogLevel::INFO, category, message, "integration_logger", metadata, operation_id, library_name);
}

void JsonLogger::warning(const std::string& message, LogCategory category,
                         const std::map<std::string, std::string>& metadata,
                         const std::string& operation_id, const std::string& library_name) {
    log(LogLevel::WARNING, category, message, "integration_logger", metadata, operation_id, library_name);
}

void JsonLogger::error(const std::string& message, LogCategory category,
                       const std::map<std::string, std::string>& metadata,
                       const std::string& operation_id, const std::string& library_name) {
    log(LogLevel::ERROR, category, message, "integration_logger", metadata, operation_id, library_name);
}

void JsonLogger::fatal(const std::string& message, LogCategory category,
                       const std::map<std::string, std::string>& metadata,
                       const std::string& operation_id, const std::string& library_name) {
    log(LogLevel::FATAL, category, message, "integration_logger", metadata, operation_id, library_name);
}

void JsonLogger::log_integration_start(const std::string& library_name, const std::string& operation_id,
                                      const std::map<std::string, std::string>& metadata) {
    auto extended_metadata = metadata;
    extended_metadata["event_type"] = "integration_start";
    extended_metadata["library_name"] = library_name;

    info("Starting integration of library: " + library_name, LogCategory::INTEGRATION,
         extended_metadata, operation_id, library_name);
}

void JsonLogger::log_integration_complete(const std::string& library_name, const std::string& operation_id,
                                         bool success, const std::map<std::string, std::string>& metrics) {
    auto extended_metadata = metrics;
    extended_metadata["event_type"] = "integration_complete";
    extended_metadata["library_name"] = library_name;
    extended_metadata["success"] = success ? "true" : "false";

    if (success) {
        info("Successfully completed integration of library: " + library_name, LogCategory::INTEGRATION,
             extended_metadata, operation_id, library_name);
    } else {
        error("Failed to complete integration of library: " + library_name, LogCategory::INTEGRATION,
              extended_metadata, operation_id, library_name);
    }
}

void JsonLogger::log_attribution_added(const std::string& file_path, const std::string& library_name,
                                      const std::string& operation_id) {
    std::map<std::string, std::string> metadata;
    metadata["event_type"] = "attribution_added";
    metadata["file_path"] = file_path;
    metadata["library_name"] = library_name;

    info("Added attribution header to: " + file_path, LogCategory::ATTRIBUTION,
         metadata, operation_id, library_name);
}

void JsonLogger::log_verification_result(const std::string& library_name, const std::string& verification_type,
                                        bool passed, const std::map<std::string, std::string>& details) {
    auto extended_metadata = details;
    extended_metadata["event_type"] = "verification_result";
    extended_metadata["library_name"] = library_name;
    extended_metadata["verification_type"] = verification_type;
    extended_metadata["passed"] = passed ? "true" : "false";

    if (passed) {
        info("Verification passed: " + verification_type + " for " + library_name, LogCategory::VERIFICATION,
             extended_metadata, "", library_name);
    } else {
        warning("Verification failed: " + verification_type + " for " + library_name, LogCategory::VERIFICATION,
                extended_metadata, "", library_name);
    }
}

void JsonLogger::log_performance_metric(const std::string& operation_name, double duration_ms,
                                       const std::map<std::string, std::string>& additional_metrics) {
    auto metadata = additional_metrics;
    metadata["event_type"] = "performance_metric";
    metadata["operation_name"] = operation_name;
    metadata["duration_ms"] = std::to_string(duration_ms);

    info("Performance metric: " + operation_name + " took " + std::to_string(duration_ms) + "ms",
         LogCategory::PERFORMANCE, metadata);
}

std::vector<LogEntry> JsonLogger::get_logs(LogLevel min_level, LogCategory category) const {
    std::lock_guard<std::mutex> lock(p_impl->log_mutex);

    std::vector<LogEntry> filtered_logs;
    for (const auto& entry : p_impl->log_entries) {
        if (entry.level >= min_level && entry.category == category) {
            filtered_logs.push_back(entry);
        }
    }

    return filtered_logs;
}

std::vector<LogEntry> JsonLogger::get_logs_for_operation(const std::string& operation_id) const {
    std::lock_guard<std::mutex> lock(p_impl->log_mutex);

    std::vector<LogEntry> operation_logs;
    for (const auto& entry : p_impl->log_entries) {
        if (entry.operation_id == operation_id) {
            operation_logs.push_back(entry);
        }
    }

    return operation_logs;
}

std::vector<LogEntry> JsonLogger::get_logs_for_library(const std::string& library_name) const {
    std::lock_guard<std::mutex> lock(p_impl->log_mutex);

    std::vector<LogEntry> library_logs;
    for (const auto& entry : p_impl->log_entries) {
        if (entry.library_name == library_name) {
            library_logs.push_back(entry);
        }
    }

    return library_logs;
}

bool JsonLogger::export_logs_json(const std::string& output_path) const {
    std::ofstream output_file(output_path);
    if (!output_file.is_open()) {
        return false;
    }

    output_file << "[\n";

    bool first = true;
    for (const auto& entry : p_impl->log_entries) {
        if (!first) {
            output_file << ",\n";
        }
        output_file << entry_to_json(entry);
        first = false;
    }

    output_file << "\n]\n";
    output_file.close();
    return true;
}

bool JsonLogger::export_logs_csv(const std::string& output_path) const {
    std::ofstream output_file(output_path);
    if (!output_file.is_open()) {
        return false;
    }

    // CSV header
    output_file << "timestamp,level,category,message,component,operation_id,library_name,metadata\n";

    for (const auto& entry : p_impl->log_entries) {
        output_file << timestamp_to_string(entry.timestamp) << ","
                   << log_level_to_string(entry.level) << ","
                   << log_category_to_string(entry.category) << ","
                   << "\"" << entry.message << "\","
                   << entry.component << ","
                   << entry.operation_id << ","
                   << entry.library_name << ","
                   << "\"" << map_to_json(entry.metadata) << "\"\n";
    }

    output_file.close();
    return true;
}

std::string JsonLogger::get_logs_json_string(LogLevel min_level) const {
    std::ostringstream oss;
    oss << "[\n";

    bool first = true;
    for (const auto& entry : p_impl->log_entries) {
        if (entry.level < min_level) continue;

        if (!first) {
            oss << ",\n";
        }
        oss << entry_to_json(entry);
        first = false;
    }

    oss << "\n]";
    return oss.str();
}

void JsonLogger::flush() {
    std::lock_guard<std::mutex> lock(p_impl->log_mutex);
    if (p_impl->log_file.is_open()) {
        p_impl->log_file.flush();
    }
}

void JsonLogger::clear() {
    std::lock_guard<std::mutex> lock(p_impl->log_mutex);
    p_impl->log_entries.clear();
}

size_t JsonLogger::get_log_count() const {
    std::lock_guard<std::mutex> lock(p_impl->log_mutex);
    return p_impl->log_entries.size();
}

std::string JsonLogger::generate_operation_id() const {
    std::ostringstream oss;
    oss << std::hex << p_impl->dist(p_impl->rng);
    return oss.str();
}

std::map<LogLevel, size_t> JsonLogger::get_log_counts_by_level() const {
    std::lock_guard<std::mutex> lock(p_impl->log_mutex);

    std::map<LogLevel, size_t> counts;
    for (const auto& entry : p_impl->log_entries) {
        counts[entry.level]++;
    }

    return counts;
}

std::map<LogCategory, size_t> JsonLogger::get_log_counts_by_category() const {
    std::lock_guard<std::mutex> lock(p_impl->log_mutex);

    std::map<LogCategory, size_t> counts;
    for (const auto& entry : p_impl->log_entries) {
        counts[entry.category]++;
    }

    return counts;
}

std::string JsonLogger::log_level_to_string(LogLevel level) const {
    switch (level) {
        case LogLevel::TRACE: return "TRACE";
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO: return "INFO";
        case LogLevel::WARNING: return "WARNING";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::FATAL: return "FATAL";
        default: return "UNKNOWN";
    }
}

std::string JsonLogger::log_category_to_string(LogCategory category) const {
    switch (category) {
        case LogCategory::GENERAL: return "GENERAL";
        case LogCategory::INTEGRATION: return "INTEGRATION";
        case LogCategory::ATTRIBUTION: return "ATTRIBUTION";
        case LogCategory::VERIFICATION: return "VERIFICATION";
        case LogCategory::BUILD: return "BUILD";
        case LogCategory::CONFIGURATION: return "CONFIGURATION";
        case LogCategory::PERFORMANCE: return "PERFORMANCE";
        case LogCategory::AUDIT: return "AUDIT";
        default: return "UNKNOWN";
    }
}

std::string JsonLogger::timestamp_to_string(const std::chrono::system_clock::time_point& timestamp) const {
    auto time_t = std::chrono::system_clock::to_time_t(timestamp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        timestamp.time_since_epoch()) % 1000;

    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
    return oss.str();
}

std::string JsonLogger::map_to_json(const std::map<std::string, std::string>& metadata) const {
    std::ostringstream oss;
    oss << "{";

    bool first = true;
    for (const auto& pair : metadata) {
        if (!first) {
            oss << ",";
        }
        oss << "\"" << pair.first << "\":\"" << pair.second << "\"";
        first = false;
    }

    oss << "}";
    return oss.str();
}

std::string JsonLogger::entry_to_json(const LogEntry& entry) const {
    std::ostringstream oss;
    oss << "{"
        << "\"timestamp\":\"" << timestamp_to_string(entry.timestamp) << "\","
        << "\"level\":\"" << log_level_to_string(entry.level) << "\","
        << "\"category\":\"" << log_category_to_string(entry.category) << "\","
        << "\"message\":\"" << entry.message << "\","
        << "\"component\":\"" << entry.component << "\","
        << "\"operation_id\":\"" << entry.operation_id << "\","
        << "\"library_name\":\"" << entry.library_name << "\","
        << "\"metadata\":" << map_to_json(entry.metadata)
        << "}";
    return oss.str();
}

void JsonLogger::write_entry(const LogEntry& entry) {
    std::lock_guard<std::mutex> lock(p_impl->log_mutex);

    p_impl->log_entries.push_back(entry);

    std::string output;
    if (p_impl->json_output_enabled) {
        output = entry_to_json(entry);
    } else {
        // Simple text output
        output = "[" + timestamp_to_string(entry.timestamp) + "] " +
                "[" + log_level_to_string(entry.level) + "] " +
                "[" + log_category_to_string(entry.category) + "] " +
                entry.message;
    }

    p_impl->write_to_console(output);
    p_impl->write_to_file(output);
}

// Global logger instance
JsonLogger& get_integration_logger() {
    static JsonLogger instance;
    return instance;
}

} // namespace logging
} // namespace integration