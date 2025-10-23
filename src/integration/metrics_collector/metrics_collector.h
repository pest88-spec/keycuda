// T016: Integration Metrics Collection System
// Collects, aggregates, and reports metrics for integration operations

#pragma once

#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <nlohmann/json.hpp>

namespace integration {
namespace metrics_collector {

struct MetricPoint {
    std::chrono::high_resolution_clock::time_point timestamp;
    double value;
};

struct MetricStats {
    std::string name;
    size_t count = 0;
    double min_value = 0.0;
    double max_value = 0.0;
    double mean_value = 0.0;
    double std_deviation = 0.0;
    double p50_value = 0.0;
    double p95_value = 0.0;
    double p99_value = 0.0;
    size_t p50_index = 0;
    size_t p95_index = 0;
    size_t p99_index = 0;
    double rate_per_second = 0.0;
};

class MetricsCollector {
public:
    MetricsCollector();
    ~MetricsCollector();

    // Recording methods
    void record_metric(const std::string& name, double value);
    void increment_counter(const std::string& name);
    void record_timing(const std::string& operation, double duration_ms);

    // Statistics and reporting
    MetricStats calculate_statistics(const std::string& metric_name) const;
    nlohmann::json generate_metrics_report() const;
    bool export_metrics(const std::string& filename) const;

    // Accessors
    double get_metric_value(const std::string& name) const;
    long long get_counter_value(const std::string& name) const;

    // Configuration
    void reset_metrics();
    void enable_collection(bool enabled);
    void set_aggregation_interval(int interval_ms);

private:
    void start_collection_thread();
    void stop_collection_thread();

    void collect_system_metrics();
    double get_memory_usage_mb() const;
    double get_cpu_usage_percent() const;
    double get_uptime_seconds() const;
    std::string get_current_timestamp() const;

    // Data storage
    std::map<std::string, std::vector<MetricPoint>> metrics_;
    std::map<std::string, long long> counters_;

    // Thread safety
    mutable std::mutex metrics_mutex_;
    mutable std::mutex counters_mutex_;

    // Collection thread
    std::thread collection_thread_;
    std::atomic<bool> collection_active_{true};
    std::atomic<bool> collection_enabled_{true};
    int aggregation_interval_ms_;

    // System metrics cache
    mutable std::chrono::steady_clock::time_point last_system_check_;
    double cached_memory_usage_ = 0.0;
    double cached_cpu_usage_ = 0.0;
};

// RAII helper for automatic timing measurement
class ScopedMetric {
public:
    ScopedMetric(MetricsCollector& collector, const std::string& name);
    ~ScopedMetric();

private:
    MetricsCollector& collector_;
    std::string name_;
    std::chrono::high_resolution_clock::time_point start_time_;
};

// Global accessor
MetricsCollector& get_metrics_collector();

// Convenience macros
#define RECORD_METRIC(name, value) \
    integration::metrics_collector::get_metrics_collector().record_metric(name, value)

#define INCREMENT_COUNTER(name) \
    integration::metrics_collector::get_metrics_collector().increment_counter(name)

#define METRICS_TIMER_SCOPE(op) \
    integration::metrics_collector::ScopedMetric _metrics_timer( \
        integration::metrics_collector::get_metrics_collector(), op)

} // namespace metrics_collector
} // namespace integration