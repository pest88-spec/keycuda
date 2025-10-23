// T003: Integration Logging Infrastructure
// Provides JSON-capable logging for integration operations

#pragma once

#include <chrono>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace integration {
namespace metrics {

class MetricsCollector {
public:
    MetricsCollector();

    // Log operations with JSON details
    void log_operation(const std::string& operation,
                      const nlohmann::json& details);

    // Log timing information
    void log_timing(const std::string& operation,
                   double duration_ms);

    // Log integrity check results
    void log_integrity_check(const std::string& path,
                           const std::string& expected_sha256,
                           const std::string& actual_sha256,
                           bool passed);

    // Log attribution check results
    void log_attribution_check(const std::string& file_path,
                             bool has_spdx,
                             bool has_copyright,
                             bool has_origin);

    // Export logs to JSON file
    void export_to_json(const std::string& filename) const;

    // Configuration
    void set_verbose(bool verbose) { verbose_ = verbose; }
    bool is_verbose() const { return verbose_; }

private:
    nlohmann::json generate_summary() const;
    std::string get_timestamp() const;
    std::string generate_session_id() const;

    nlohmann::json log_entry_;
    std::vector<nlohmann::json> operations_log_;
    std::chrono::high_resolution_clock::time_point start_time_;
    bool verbose_ = false;
};

// Global accessor
MetricsCollector& get_metrics_collector();

// RAII helper for timing operations
class ScopedTimer {
public:
    ScopedTimer(const std::string& operation);
    ~ScopedTimer();

private:
    std::string operation_;
    std::chrono::high_resolution_clock::time_point start_;
};

// Convenience macros
#define LOG_INTEGRATION_OP(op, details) \
    integration::metrics::get_metrics_collector().log_operation(op, details)

#define LOG_INTEGRATION_TIMING(op, duration) \
    integration::metrics::get_metrics_collector().log_timing(op, duration)

#define INTEGRATION_TIMER_SCOPE(op) \
    integration::metrics::ScopedTimer _timer(op)

} // namespace metrics
} // namespace integration