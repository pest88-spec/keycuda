#include "ComputeCore/gpu/performance/performance_logger.h"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <mutex>
#include <numeric>

namespace puzzle71::gpu::performance {

PerformanceLogger& PerformanceLogger::GetInstance() {
    static PerformanceLogger instance;
    return instance;
}

void PerformanceLogger::Log(LogLevel level, const std::string& category, const std::string& message, const json& metadata) {
    if (level < current_level_) {
        return;
    }

    std::lock_guard<std::mutex> lock(log_mutex_);
    PerformanceLogEntry entry(level, category, message, metadata);

    // Add to memory buffer
    log_entries_.push_back(entry);

    // Trim if exceeds max entries
    if (log_entries_.size() > max_memory_entries_) {
        log_entries_.erase(log_entries_.begin(), log_entries_.begin() + (log_entries_.size() - max_memory_entries_));
    }

    // Output based on configuration
    if (console_logging_enabled_) {
        WriteToConsole(entry);
    }

    if (file_logging_enabled_) {
        WriteToFile(entry);
    }
}

void PerformanceLogger::Debug(const std::string& category, const std::string& message, const json& metadata) {
    Log(LogLevel::DEBUG, category, message, metadata);
}

void PerformanceLogger::Info(const std::string& category, const std::string& message, const json& metadata) {
    Log(LogLevel::INFO, category, message, metadata);
}

void PerformanceLogger::Warning(const std::string& category, const std::string& message, const json& metadata) {
    Log(LogLevel::WARNING, category, message, metadata);
}

void PerformanceLogger::Error(const std::string& category, const std::string& message, const json& metadata) {
    Log(LogLevel::ERROR, category, message, metadata);
}

void PerformanceLogger::Critical(const std::string& category, const std::string& message, const json& metadata) {
    Log(LogLevel::CRITICAL, category, message, metadata);
}

void PerformanceLogger::LogKernelLaunch(const std::string& kernel_name, int grid_size, int block_size,
                                       std::chrono::microseconds execution_time, const json& metadata) {
    json kernel_metadata = metadata;
    kernel_metadata["kernel_name"] = kernel_name;
    kernel_metadata["grid_size"] = grid_size;
    kernel_metadata["block_size"] = block_size;
    kernel_metadata["execution_time_us"] = execution_time.count();
    kernel_metadata["throughput_ops_per_sec"] = static_cast<double>(grid_size * block_size) / (execution_time.count() / 1000000.0);

    Log(LogLevel::INFO, "kernel_launch",
        std::string("Kernel '") + kernel_name + "' executed", kernel_metadata);
}

void PerformanceLogger::LogMemoryOperation(const std::string& operation, size_t size_bytes,
                                          std::chrono::microseconds duration, const json& metadata) {
    json mem_metadata = metadata;
    mem_metadata["operation"] = operation;
    mem_metadata["size_bytes"] = size_bytes;
    mem_metadata["duration_us"] = duration.count();

    if (duration.count() > 0) {
        mem_metadata["bandwidth_mb_per_sec"] = (size_bytes / (1024.0 * 1024.0)) / (duration.count() / 1000000.0);
    }

    Log(LogLevel::INFO, "memory_operation",
        std::string("Memory operation '") + operation + "' completed", mem_metadata);
}

void PerformanceLogger::LogSynchronizationEvent(const std::string& sync_type, std::chrono::microseconds overhead,
                                               const json& metadata) {
    json sync_metadata = metadata;
    sync_metadata["sync_type"] = sync_type;
    sync_metadata["overhead_us"] = overhead.count();

    Log(LogLevel::INFO, "synchronization",
        std::string("Synchronization '") + sync_type + "' completed", sync_metadata);
}

void PerformanceLogger::LogOptimizationResult(const std::string& optimization_name, const json& before_metrics,
                                            const json& after_metrics, const json& metadata) {
    json opt_metadata = metadata;
    opt_metadata["optimization_name"] = optimization_name;
    opt_metadata["before_metrics"] = before_metrics;
    opt_metadata["after_metrics"] = after_metrics;

    // Calculate improvement if possible
    if (before_metrics.contains("throughput_mkeys_per_sec") && after_metrics.contains("throughput_mkeys_per_sec")) {
        double before = before_metrics["throughput_mkeys_per_sec"];
        double after = after_metrics["throughput_mkeys_per_sec"];
        if (before > 0) {
            opt_metadata["improvement_factor"] = after / before;
            opt_metadata["improvement_percent"] = ((after - before) / before) * 100.0;
        }
    }

    Log(LogLevel::INFO, "optimization",
        std::string("Optimization '") + optimization_name + "' applied", opt_metadata);
}

void PerformanceLogger::SetLogLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    current_level_ = level;
}

void PerformanceLogger::EnableFileLogging(const std::string& log_file_path) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    file_logging_enabled_ = true;
    log_file_path_ = log_file_path;
}

void PerformanceLogger::DisableFileLogging() {
    std::lock_guard<std::mutex> lock(log_mutex_);
    file_logging_enabled_ = false;
}

void PerformanceLogger::EnableConsoleLogging(bool enabled) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    console_logging_enabled_ = enabled;
}

void PerformanceLogger::SetMaxMemoryEntries(size_t max_entries) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    max_memory_entries_ = max_entries;

    // Trim if necessary
    if (log_entries_.size() > max_memory_entries_) {
        log_entries_.erase(log_entries_.begin(), log_entries_.begin() + (log_entries_.size() - max_memory_entries_));
    }
}

void PerformanceLogger::Flush() {
    std::lock_guard<std::mutex> lock(log_mutex_);

    if (file_logging_enabled_) {
        std::ofstream file(log_file_path_, std::ios::app);
        if (file.is_open()) {
            for (const auto& entry : log_entries_) {
                file << FormatEntry(entry) << std::endl;
            }
        }
    }
}

void PerformanceLogger::Clear() {
    std::lock_guard<std::mutex> lock(log_mutex_);
    log_entries_.clear();
}

std::vector<PerformanceLogEntry> PerformanceLogger::GetEntries(LogLevel min_level) const {
    std::lock_guard<std::mutex> lock(log_mutex_);
    std::vector<PerformanceLogEntry> filtered_entries;

    for (const auto& entry : log_entries_) {
        if (entry.level >= min_level) {
            filtered_entries.push_back(entry);
        }
    }

    return filtered_entries;
}

std::vector<PerformanceLogEntry> PerformanceLogger::GetEntriesByCategory(const std::string& category, LogLevel min_level) const {
    std::lock_guard<std::mutex> lock(log_mutex_);
    std::vector<PerformanceLogEntry> filtered_entries;

    for (const auto& entry : log_entries_) {
        if (entry.level >= min_level && entry.category == category) {
            filtered_entries.push_back(entry);
        }
    }

    return filtered_entries;
}

json PerformanceLogger::GetEntriesAsJson(LogLevel min_level) const {
    std::lock_guard<std::mutex> lock(log_mutex_);
    json entries_json = json::array();

    for (const auto& entry : log_entries_) {
        if (entry.level >= min_level) {
            json entry_json;
            auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                entry.timestamp.time_since_epoch()).count();

            entry_json["timestamp_us"] = timestamp;
            entry_json["level"] = LogLevelToString(entry.level);
            entry_json["category"] = entry.category;
            entry_json["message"] = entry.message;
            entry_json["metadata"] = entry.metadata;

            entries_json.push_back(entry_json);
        }
    }

    return entries_json;
}

json PerformanceLogger::GetPerformanceSummary(const std::string& category) const {
    std::lock_guard<std::mutex> lock(log_mutex_);
    json summary;

    std::map<std::string, std::vector<double>> kernel_times;
    std::map<std::string, std::vector<size_t>> memory_ops;
    size_t total_sync_events = 0;
    double total_sync_overhead = 0.0;

    for (const auto& entry : log_entries_) {
        if (!category.empty() && entry.category != category) {
            continue;
        }

        if (entry.category == "kernel_launch" && entry.metadata.contains("kernel_name") &&
            entry.metadata.contains("execution_time_us")) {
            std::string kernel_name = entry.metadata["kernel_name"];
            double exec_time = entry.metadata["execution_time_us"];
            kernel_times[kernel_name].push_back(exec_time);
        }

        if (entry.category == "memory_operation" && entry.metadata.contains("operation") &&
            entry.metadata.contains("size_bytes")) {
            std::string operation = entry.metadata["operation"];
            size_t size = entry.metadata["size_bytes"];
            memory_ops[operation].push_back(size);
        }

        if (entry.category == "synchronization" && entry.metadata.contains("overhead_us")) {
            total_sync_events++;
            total_sync_overhead += entry.metadata["overhead_us"].get<double>();
        }
    }

    // Calculate kernel statistics
    for (const auto& [kernel_name, times] : kernel_times) {
        if (!times.empty()) {
            double sum = std::accumulate(times.begin(), times.end(), 0.0);
            double mean = sum / times.size();
            double min_time = *std::min_element(times.begin(), times.end());
            double max_time = *std::max_element(times.begin(), times.end());

            summary["kernel_stats"][kernel_name]["avg_time_us"] = mean;
            summary["kernel_stats"][kernel_name]["min_time_us"] = min_time;
            summary["kernel_stats"][kernel_name]["max_time_us"] = max_time;
            summary["kernel_stats"][kernel_name]["execution_count"] = times.size();
        }
    }

    // Calculate memory statistics
    for (const auto& [operation, sizes] : memory_ops) {
        if (!sizes.empty()) {
            size_t total_size = std::accumulate(sizes.begin(), sizes.end(), 0ULL);
            size_t avg_size = total_size / sizes.size();

            summary["memory_stats"][operation]["total_bytes"] = total_size;
            summary["memory_stats"][operation]["avg_bytes"] = avg_size;
            summary["memory_stats"][operation]["operation_count"] = sizes.size();
        }
    }

    summary["synchronization_stats"]["total_events"] = total_sync_events;
    summary["synchronization_stats"]["total_overhead_us"] = total_sync_overhead;
    if (total_sync_events > 0) {
        summary["synchronization_stats"]["avg_overhead_us"] = total_sync_overhead / total_sync_events;
    }

    return summary;
}

std::map<std::string, double> PerformanceLogger::GetAverageKernelTimes() const {
    std::lock_guard<std::mutex> lock(log_mutex_);
    std::map<std::string, double> avg_times;
    std::map<std::string, std::vector<double>> kernel_times;

    for (const auto& entry : log_entries_) {
        if (entry.category == "kernel_launch" && entry.metadata.contains("kernel_name") &&
            entry.metadata.contains("execution_time_us")) {
            std::string kernel_name = entry.metadata["kernel_name"];
            double exec_time = entry.metadata["execution_time_us"];
            kernel_times[kernel_name].push_back(exec_time);
        }
    }

    for (const auto& [kernel_name, times] : kernel_times) {
        if (!times.empty()) {
            double sum = std::accumulate(times.begin(), times.end(), 0.0);
            avg_times[kernel_name] = sum / times.size();
        }
    }

    return avg_times;
}

std::map<std::string, size_t> PerformanceLogger::GetMemoryOperationStats() const {
    std::lock_guard<std::mutex> lock(log_mutex_);
    std::map<std::string, size_t> stats;

    for (const auto& entry : log_entries_) {
        if (entry.category == "memory_operation" && entry.metadata.contains("operation")) {
            std::string operation = entry.metadata["operation"];
            stats[operation]++;
        }
    }

    return stats;
}

void PerformanceLogger::WriteToFile(const PerformanceLogEntry& entry) {
    std::ofstream file(log_file_path_, std::ios::app);
    if (file.is_open()) {
        file << FormatEntry(entry) << std::endl;
    }
}

void PerformanceLogger::WriteToConsole(const PerformanceLogEntry& entry) {
    std::cout << FormatEntry(entry) << std::endl;
}

std::string PerformanceLogger::FormatEntry(const PerformanceLogEntry& entry) const {
    auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        entry.timestamp.time_since_epoch()).count();

    std::ostringstream oss;
    oss << "[" << timestamp << "] "
        << "[" << LogLevelToString(entry.level) << "] "
        << "[" << entry.category << "] "
        << entry.message;

    if (!entry.metadata.empty()) {
        oss << " | " << entry.metadata.dump();
    }

    return oss.str();
}

std::string PerformanceLogger::LogLevelToString(LogLevel level) const {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO: return "INFO";
        case LogLevel::WARNING: return "WARN";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::CRITICAL: return "CRIT";
        default: return "UNKNOWN";
    }
}

// ScopedPerformanceLogger implementation
ScopedPerformanceLogger::ScopedPerformanceLogger(const std::string& category, const std::string& operation, const json& metadata)
    : category_(category), operation_(operation), metadata_(metadata) {
    start_time_ = std::chrono::high_resolution_clock::now();
    PerformanceLogger::GetInstance().Debug(category_, "Starting " + operation_, metadata_);
}

ScopedPerformanceLogger::~ScopedPerformanceLogger() {
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time_);

    json complete_metadata = metadata_;
    complete_metadata["duration_us"] = duration.count();

    PerformanceLogger::GetInstance().Info(category_, "Completed " + operation_, complete_metadata);
}

} // namespace puzzle71::gpu::performance