// T016: Integration Metrics Collection System
// Collects, aggregates, and reports metrics for integration operations

#include "metrics_collector.h"
#include <fstream>
#include <iomanip>
#include <numeric>

namespace integration {
namespace metrics_collector {

MetricsCollector::MetricsCollector()
    : collection_enabled_(true)
    , aggregation_interval_ms_(5000) {
    start_collection_thread();
}

MetricsCollector::~MetricsCollector() {
    stop_collection();
}

void MetricsCollector::record_metric(const std::string& name, double value) {
    if (!collection_enabled_) return;

    std::lock_guard<std::mutex> lock(metrics_mutex_);

    auto now = std::chrono::high_resolution_clock::now();
    MetricPoint point;
    point.timestamp = now;
    point.value = value;

    metrics_[name].points.push_back(point);

    // Maintain rolling window (last 1000 points)
    if (metrics_[name].points.size() > 1000) {
        metrics_[name].points.erase(metrics_[name].points.begin());
    }
}

void MetricsCollector::increment_counter(const std::string& name) {
    std::lock_guard<std::mutex> lock(counters_mutex_);
    counters_[name]++;
}

void MetricsCollector::record_timing(const std::string& operation, double duration_ms) {
    record_metric(operation + "_duration_ms", duration_ms);
    record_metric(operation + "_timestamp",
                   std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::high_resolution_clock::now().time_since_epoch()).count());
}

MetricStats MetricsCollector::calculate_statistics(const std::string& metric_name) const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    MetricStats stats;
    stats.name = metric_name;
    stats.count = 0;

    auto it = metrics_.find(metric_name);
    if (it == metrics_.end() || it->second.points.empty()) {
        return stats;
    }

    const auto& points = it->second.points;
    stats.count = points.size();

    // Calculate min, max, mean
    auto min_it = std::min_element(points.begin(), points.end(),
        [](const MetricPoint& a, const MetricPoint& b) { return a.value < b.value; });
    auto max_it = std::max_element(points.begin(), points.end(),
        [](const MetricPoint& a, const MetricPoint& b) { return a.value < b.value; });

    stats.min_value = min_it->value;
    stats.max_value = max_it->value;
    stats.mean_value = std::accumulate(points.begin(), points.end(), 0.0,
        [](double sum, const MetricPoint& p) { return sum + p.value; }) / points.size();

    // Calculate standard deviation
    double variance = 0.0;
    for (const auto& point : points) {
        variance += (point.value - stats.mean_value) * (point.value - stats.mean_value);
    }
    stats.std_deviation = std::sqrt(variance / points.size());

    // Calculate percentiles
    std::vector<double> sorted_values;
    for (const auto& point : points) {
        sorted_values.push_back(point.value);
    }
    std::sort(sorted_values.begin(), sorted_values.end());

    size_t n = sorted_values.size();
    if (n > 0) {
        stats.p50_index = n * 0.5;
        stats.p95_index = n * 0.95;
        stats.p99_index = n * 0.99;

        stats.p50_value = sorted_values[std::min(stats.p50_index, n - 1)];
        stats.p95_value = sorted_values[std::min(stats.p95_index, n - 1)];
        stats.p99_value = sorted_values[std::min(stats.p99_index, n - 1)];
    }

    // Rate calculation (if timestamp metric)
    if (metric_name.find("_timestamp") != std::string::npos && sorted_values.size() > 1) {
        double time_span = sorted_values.back() - sorted_values.front();
        if (time_span > 0) {
            stats.rate_per_second = (stats.count - 1) / (time_span / 1000.0);
        }
    }

    return stats;
}

nlohmann::json MetricsCollector::generate_metrics_report() const {
    nlohmann::json report;
    report["generated"] = get_current_timestamp();
    report["collection_interval_ms"] = aggregation_interval_ms_;
    report["metrics"] = nlohmann::json::object();

    // Add metric statistics
    {
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        for (const auto& pair : metrics_) {
            if (!pair.second.points.empty()) {
                MetricStats stats = calculate_statistics(pair.first);
                report["metrics"][pair.first] = {
                    {"count", stats.count},
                    {"min", stats.min_value},
                    {"max", stats.max_value},
                    {"mean", stats.mean_value},
                    {"std_dev", stats.std_deviation},
                    {"p50", stats.p50_value},
                    {"p95", stats.p95_value},
                    {"p99", stats.p99_value},
                    {"rate_per_second", stats.rate_per_second}
                };
            }
        }
    }

    // Add counters
    {
        std::lock_guard<std::mutex> lock(counters_mutex_);
        report["counters"] = counters_;
    }

    // Add system metrics
    report["system"] = {
        {"uptime_seconds", get_uptime_seconds()},
        {"memory_usage_mb", get_memory_usage_mb()},
        {"cpu_usage_percent", get_cpu_usage_percent()}
    };

    return report;
}

bool MetricsCollector::export_metrics(const std::string& filename) const {
    nlohmann::json report = generate_metrics_report();

    std::ofstream file(filename);
    if (!file.is_open()) {
        return false;
    }

    file << report.dump(2) << std::endl;
    return true;
}

void MetricsCollector::reset_metrics() {
    std::lock_guard<std::mutex> lock1(metrics_mutex_);
    std::lock_guard<std::mutex> lock2(counters_mutex_);

    metrics_.clear();
    counters_.clear();
}

void MetricsCollector::enable_collection(bool enabled) {
    collection_enabled_ = enabled;
}

void MetricsCollector::set_aggregation_interval(int interval_ms) {
    aggregation_interval_ms_ = interval_ms;
}

double MetricsCollector::get_metric_value(const std::string& name) const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    auto it = metrics_.find(name);
    if (it == metrics_.end() || it->second.points.empty()) {
        return 0.0;
    }

    return it->second.points.back().value;
}

long long MetricsCollector::get_counter_value(const std::string& name) const {
    std::lock_guard<std::mutex> lock(counters_mutex_);

    auto it = counters_.find(name);
    return it != counters_.end() ? it->second : 0;
}

void MetricsCollector::start_collection_thread() {
    collection_thread_ = std::thread([this]() {
        while (collection_active_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(aggregation_interval_ms_));

            if (collection_enabled_) {
                collect_system_metrics();
            }
        }
    });
}

void MetricsCollector::stop_collection() {
    collection_active_ = false;
    if (collection_thread_.joinable()) {
        collection_thread_.join();
    }
}

void MetricsCollector::collect_system_metrics() {
    // Record system metrics periodically
    record_metric("system_memory_mb", get_memory_usage_mb());
    record_metric("system_cpu_percent", get_cpu_usage_percent());
    record_metric("uptime_seconds", get_uptime_seconds());
}

double MetricsCollector::get_memory_usage_mb() const {
    // Simplified implementation
    std::ifstream status_file("/proc/self/status");
    std::string line;

    while (std::getline(status_file, line)) {
        if (line.substr(0, 6) == "VmRSS:") {
            std::istringstream iss(line);
            std::string label, value, unit;
            iss >> label >> value >> unit;

            if (unit == "kB") {
                return std::stod(value) / 1024.0;
            }
        }
    }

    return 0.0;
}

double MetricsCollector::get_cpu_usage_percent() const {
    // Simplified CPU usage calculation
    static long long prev_idle = 0, prev_total = 0;

    std::ifstream stat_file("/proc/stat");
    std::string line;

    if (std::getline(stat_file, line) && line.substr(0, 3) == "cpu") {
        std::istringstream iss(line);
        std::string cpu;
        long long user, nice, system, idle, iowait, irq, softirq, steal;

        if (iss >> cpu >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal) {
            long long total = user + nice + system + idle + iowait + irq + softirq + steal;
            long long idle_diff = idle - prev_idle;
            long long total_diff = total - prev_total;

            if (total_diff > 0) {
                double usage = 100.0 * (1.0 - static_cast<double>(idle_diff) / total_diff);
                prev_idle = idle;
                prev_total = total;
                return usage;
            }
        }
    }

    return 0.0;
}

double MetricsCollector::get_uptime_seconds() const {
    std::ifstream uptime_file("/proc/uptime");
    double uptime;
    if (uptime_file >> uptime) {
        return uptime;
    }
    return 0.0;
}

std::string MetricsCollector::get_current_timestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

// RAII Metrics Helper
ScopedMetric::ScopedMetric(MetricsCollector& collector,
                             const std::string& name)
    : collector_(collector)
    , name_(name)
    , start_time_(std::chrono::high_resolution_clock::now()) {
}

ScopedMetric::~ScopedMetric() {
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time_);

    collector_.record_timing(name_, duration.count());
}

// Global instance
static std::unique_ptr<MetricsCollector> g_metrics_collector;

MetricsCollector& get_metrics_collector() {
    if (!g_metrics_collector) {
        g_metrics_collector = std::make_unique<MetricsCollector>();
    }
    return *g_metrics_collector;
}

} // namespace metrics_collector
} // namespace integration