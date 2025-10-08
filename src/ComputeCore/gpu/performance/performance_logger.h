#pragma once

#include <chrono>
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <functional>
#include <mutex>
#include <nlohmann/json.hpp>

namespace puzzle71::gpu::performance {

using json = nlohmann::json;

enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARNING = 2,
    ERROR = 3,
    CRITICAL = 4
};

struct PerformanceLogEntry {
    std::chrono::high_resolution_clock::time_point timestamp;
    LogLevel level;
    std::string category;
    std::string message;
    json metadata;

    PerformanceLogEntry(LogLevel lvl, const std::string& cat, const std::string& msg, const json& meta = json{})
        : timestamp(std::chrono::high_resolution_clock::now()), level(lvl), category(cat), message(msg), metadata(meta) {}
};

class PerformanceLogger {
public:
    static PerformanceLogger& GetInstance();

    // Core logging methods
    void Log(LogLevel level, const std::string& category, const std::string& message, const json& metadata = json{});
    void Debug(const std::string& category, const std::string& message, const json& metadata = json{});
    void Info(const std::string& category, const std::string& message, const json& metadata = json{});
    void Warning(const std::string& category, const std::string& message, const json& metadata = json{});
    void Error(const std::string& category, const std::string& message, const json& metadata = json{});
    void Critical(const std::string& category, const std::string& message, const json& metadata = json{});

    // Performance-specific logging methods
    void LogKernelLaunch(const std::string& kernel_name, int grid_size, int block_size,
                        std::chrono::microseconds execution_time, const json& metadata = json{});
    void LogMemoryOperation(const std::string& operation, size_t size_bytes,
                           std::chrono::microseconds duration, const json& metadata = json{});
    void LogSynchronizationEvent(const std::string& sync_type, std::chrono::microseconds overhead,
                                const json& metadata = json{});
    void LogOptimizationResult(const std::string& optimization_name, const json& before_metrics,
                              const json& after_metrics, const json& metadata = json{});

    // Configuration and control
    void SetLogLevel(LogLevel level);
    void EnableFileLogging(const std::string& log_file_path);
    void DisableFileLogging();
    void EnableConsoleLogging(bool enabled);
    void SetMaxMemoryEntries(size_t max_entries);
    void Flush();
    void Clear();

    // Data access
    std::vector<PerformanceLogEntry> GetEntries(LogLevel min_level = LogLevel::DEBUG) const;
    std::vector<PerformanceLogEntry> GetEntriesByCategory(const std::string& category, LogLevel min_level = LogLevel::DEBUG) const;
    json GetEntriesAsJson(LogLevel min_level = LogLevel::DEBUG) const;

    // Performance analysis
    json GetPerformanceSummary(const std::string& category = "") const;
    std::map<std::string, double> GetAverageKernelTimes() const;
    std::map<std::string, size_t> GetMemoryOperationStats() const;

private:
    PerformanceLogger() = default;
    ~PerformanceLogger() = default;
    PerformanceLogger(const PerformanceLogger&) = delete;
    PerformanceLogger& operator=(const PerformanceLogger&) = delete;

    void WriteToFile(const PerformanceLogEntry& entry);
    void WriteToConsole(const PerformanceLogEntry& entry);
    std::string FormatEntry(const PerformanceLogEntry& entry) const;
    std::string LogLevelToString(LogLevel level) const;

    LogLevel current_level_ = LogLevel::INFO;
    bool console_logging_enabled_ = true;
    bool file_logging_enabled_ = false;
    std::string log_file_path_;
    size_t max_memory_entries_ = 10000;
    std::vector<PerformanceLogEntry> log_entries_;
    mutable std::mutex log_mutex_;
};

// Convenience macros for logging
#define PERF_LOG_DEBUG(category, message, ...) puzzle71::gpu::performance::PerformanceLogger::GetInstance().Debug(category, message, ##__VA_ARGS__)
#define PERF_LOG_INFO(category, message, ...) puzzle71::gpu::performance::PerformanceLogger::GetInstance().Info(category, message, ##__VA_ARGS__)
#define PERF_LOG_WARNING(category, message, ...) puzzle71::gpu::performance::PerformanceLogger::GetInstance().Warning(category, message, ##__VA_ARGS__)
#define PERF_LOG_ERROR(category, message, ...) puzzle71::gpu::performance::PerformanceLogger::GetInstance().Error(category, message, ##__VA_ARGS__)
#define PERF_LOG_CRITICAL(category, message, ...) puzzle71::gpu::performance::PerformanceLogger::GetInstance().Critical(category, message, ##__VA_ARGS__)

// RAII Performance logging helper
class ScopedPerformanceLogger {
public:
    ScopedPerformanceLogger(const std::string& category, const std::string& operation, const json& metadata = json{});
    ~ScopedPerformanceLogger();

private:
    std::string category_;
    std::string operation_;
    json metadata_;
    std::chrono::high_resolution_clock::time_point start_time_;
};

#define PERF_LOG_SCOPE(category, operation, ...) ScopedPerformanceLogger _scoped_logger(category, operation, ##__VA_ARGS__)

} // namespace puzzle71::gpu::performance