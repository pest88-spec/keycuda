#include "performance_monitor.h"
#include <algorithm>
#include <chrono>
#include <thread>
#include <fstream>
#include <sstream>
#include <iomanip>

namespace keycuda {
namespace monitoring {

// Performance alert implementation
PerformanceAlert::PerformanceAlert(AlertType type, AlertSeverity severity,
                                   const std::string& message, double value,
                                   double threshold)
    : type_(type), severity_(severity), message_(message),
      value_(value), threshold_(threshold),
      timestamp_(std::chrono::system_clock::now()),
      resolved_(false) {}

std::string PerformanceAlert::GetTypeString() const {
    switch (type_) {
        case AlertType::PERFORMANCE_DEGRADATION:
            return "Performance Degradation";
        case AlertType::MEMORY_PRESSURE:
            return "Memory Pressure";
        case AlertType::GPU_UTILIZATION_LOW:
            return "Low GPU Utilization";
        case AlertType::KERNEL_TIMEOUT:
            return "Kernel Timeout";
        case AlertType::TEMPERATURE_HIGH:
            return "High Temperature";
        case AlertType::BANDWIDTH_LOW:
            return "Low Bandwidth";
        case AlertType::ERROR_RATE_HIGH:
            return "High Error Rate";
        case AlertType::RESOURCE_EXHAUSTION:
            return "Resource Exhaustion";
        default:
            return "Unknown";
    }
}

std::string PerformanceAlert::GetSeverityString() const {
    switch (severity_) {
        case AlertSeverity::INFO:
            return "INFO";
        case AlertSeverity::WARNING:
            return "WARNING";
        case AlertSeverity::CRITICAL:
            return "CRITICAL";
        default:
            return "UNKNOWN";
    }
}

nlohmann::json PerformanceAlert::ToJson() const {
    nlohmann::json json;
    json["type"] = GetTypeString();
    json["severity"] = GetSeverityString();
    json["message"] = message_;
    json["value"] = value_;
    json["threshold"] = threshold_;
    json["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        timestamp_.time_since_epoch()).count();
    json["resolved"] = resolved_;
    return json;
}

// Alert channel implementation
ConsoleAlertChannel::ConsoleAlertChannel(bool color_output) : color_output_(color_output) {}

void ConsoleAlertChannel::SendAlert(const PerformanceAlert& alert) {
    std::string severity_color;
    std::string reset_color;

    if (color_output_) {
        switch (alert.GetSeverity()) {
            case AlertSeverity::INFO:
                severity_color = "\033[36m";  // Cyan
                break;
            case AlertSeverity::WARNING:
                severity_color = "\033[33m";  // Yellow
                break;
            case AlertSeverity::CRITICAL:
                severity_color = "\033[31m";  // Red
                break;
        }
        reset_color = "\033[0m";
    }

    std::cout << severity_color
              << "[" << alert.GetSeverityString() << "] "
              << alert.GetTypeString() << ": "
              << alert.GetMessage()
              << " (Value: " << alert.GetValue()
              << ", Threshold: " << alert.GetThreshold() << ")"
              << reset_color << std::endl;
}

FileAlertChannel::FileAlertChannel(const std::string& filename)
    : filename_(filename) {
    // Create directory if it doesn't exist
    size_t last_slash = filename.find_last_of('/');
    if (last_slash != std::string::npos) {
        std::string directory = filename.substr(0, last_slash);
        std::string mkdir_cmd = "mkdir -p " + directory;
        system(mkdir_cmd.c_str());
    }
}

void FileAlertChannel::SendAlert(const PerformanceAlert& alert) {
    std::ofstream file(filename_, std::ios::app);
    if (file.is_open()) {
        auto timestamp = std::chrono::system_clock::to_time_t(alert.GetTimestamp());
        file << std::put_time(std::localtime(&timestamp), "%Y-%m-%d %H:%M:%S")
             << " [" << alert.GetSeverityString() << "] "
             << alert.GetTypeString() << ": "
             << alert.GetMessage()
             << " (Value: " << alert.GetValue()
             << ", Threshold: " << alert.GetThreshold() << ")"
             << std::endl;
        file.close();
    }
}

NetworkAlertChannel::NetworkAlertChannel(const std::string& endpoint)
    : endpoint_(endpoint) {}

void NetworkAlertChannel::SendAlert(const PerformanceAlert& alert) {
    // This would implement HTTP/REST alert notification
    // For now, just log that we would send the alert
    std::cout << "Would send network alert to " << endpoint_ << ": "
              << alert.GetMessage() << std::endl;
}

// Metrics collector implementation
MetricsCollector::MetricsCollector(int device_id)
    : device_id_(device_id), collection_enabled_(false),
      collection_thread_running_(false) {}

MetricsCollector::~MetricsCollector() {
    StopCollection();
}

void MetricsCollector::StartCollection(std::chrono::milliseconds interval) {
    if (collection_enabled_) {
        return;
    }

    collection_enabled_ = true;
    collection_interval_ = interval;
    collection_thread_running_ = true;

    collection_thread_ = std::thread(&MetricsCollector::CollectionLoop, this);
}

void MetricsCollector::StopCollection() {
    collection_enabled_ = false;
    collection_thread_running_ = false;

    if (collection_thread_.joinable()) {
        collection_thread_.join();
    }
}

PerformanceSnapshot MetricsCollector::GetCurrentSnapshot() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    if (metrics_history_.empty()) {
        return PerformanceSnapshot{};
    }

    return metrics_history_.back();
}

std::vector<PerformanceSnapshot> MetricsCollector::GetRecentSnapshots(
    std::chrono::milliseconds duration) const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    auto cutoff_time = std::chrono::system_clock::now() - duration;
    std::vector<PerformanceSnapshot> recent_snapshots;

    for (const auto& snapshot : metrics_history_) {
        if (snapshot.timestamp >= cutoff_time) {
            recent_snapshots.push_back(snapshot);
        }
    }

    return recent_snapshots;
}

double MetricsCollector::CalculateAverageMetric(
    std::function<double(const PerformanceSnapshot&)> metric_extractor,
    std::chrono::milliseconds duration) const {
    auto snapshots = GetRecentSnapshots(duration);

    if (snapshots.empty()) {
        return 0.0;
    }

    double sum = 0.0;
    for (const auto& snapshot : snapshots) {
        sum += metric_extractor(snapshot);
    }

    return sum / snapshots.size();
}

void MetricsCollector::CollectionLoop() {
    while (collection_thread_running_) {
        if (collection_enabled_) {
            PerformanceSnapshot snapshot = CollectCurrentMetrics();

            {
                std::lock_guard<std::mutex> lock(metrics_mutex_);
                metrics_history_.push_back(snapshot);

                // Keep only recent history (last 10 minutes)
                auto cutoff_time = std::chrono::system_clock::now() - std::chrono::minutes(10);
                metrics_history_.erase(
                    std::remove_if(metrics_history_.begin(), metrics_history_.end(),
                        [cutoff_time](const PerformanceSnapshot& s) {
                            return s.timestamp < cutoff_time;
                        }),
                    metrics_history_.end());
            }
        }

        std::this_thread::sleep_for(collection_interval_);
    }
}

PerformanceSnapshot MetricsCollector::CollectCurrentMetrics() {
    PerformanceSnapshot snapshot;
    snapshot.timestamp = std::chrono::system_clock::now();

    // Collect GPU metrics
    cudaDeviceProp prop;
    if (cudaGetDeviceProperties(&prop, device_id_) == cudaSuccess) {
        snapshot.gpu_name = prop.name;
        snapshot.compute_capability = prop.major * 10 + prop.minor;
        snapshot.total_memory_mb = prop.totalGlobalMem / (1024 * 1024);
    }

    // Get utilization and memory info
    size_t free_mem = 0, total_mem = 0;
    if (cudaMemGetInfo(&free_mem, &total_mem) == cudaSuccess) {
        snapshot.memory_used_mb = (total_mem - free_mem) / (1024 * 1024);
        snapshot.memory_utilization = static_cast<double>(total_mem - free_mem) / total_mem;
    }

    // Get utilization (this would use NVIDIA Management Library or similar)
    snapshot.gpu_utilization = GetGpuUtilization();
    snapshot.power_consumption_w = GetPowerConsumption();
    snapshot.temperature_c = GetTemperature();

    // Collect performance metrics
    snapshot.key_search_throughput = GetKeySearchThroughput();
    snapshot.memory_bandwidth_utilization = GetMemoryBandwidthUtilization();
    snapshot.synchronization_latency_us = GetSynchronizationLatency();
    snapshot.error_rate = GetErrorRate();

    return snapshot;
}

double MetricsCollector::GetGpuUtilization() {
    // This would typically use NVML or similar
    // For now, return a placeholder value
    return 0.85;  // 85% utilization
}

double MetricsCollector::GetPowerConsumption() {
    // This would typically use NVML
    // For now, return a placeholder value
    return 250.0;  // 250W
}

double MetricsCollector::GetTemperature() {
    // This would typically use NVML
    // For now, return a placeholder value
    return 75.0;  // 75°C
}

double MetricsCollector::GetKeySearchThroughput() {
    // This would measure actual key search performance
    // For now, return a placeholder value
    return 45.0;  // 45 Mkeys/sec
}

double MetricsCollector::GetMemoryBandwidthUtilization() {
    // This would measure actual memory bandwidth utilization
    // For now, return a placeholder value
    return 0.82;  // 82% of theoretical max
}

double MetricsCollector::GetSynchronizationLatency() {
    // This would measure actual synchronization latency
    // For now, return a placeholder value
    return 95.0;  // 95 μs
}

double MetricsCollector::GetErrorRate() {
    // This would measure actual error rate
    // For now, return a placeholder value
    return 0.001;  // 0.1% error rate
}

// Performance monitor implementation
PerformanceMonitor::PerformanceMonitor(int device_id)
    : device_id_(device_id), monitoring_enabled_(false),
      alert_cooldown_duration_(std::chrono::minutes(5)) {

    metrics_collector_ = std::make_unique<MetricsCollector>(device_id);

    // Add default alert channels
    AddAlertChannel(std::make_unique<ConsoleAlertChannel>(true));

    // Set default alert thresholds
    SetDefaultAlertThresholds();
}

PerformanceMonitor::~PerformanceMonitor() {
    StopMonitoring();
}

void PerformanceMonitor::StartMonitoring(std::chrono::milliseconds collection_interval) {
    if (monitoring_enabled_) {
        return;
    }

    monitoring_enabled_ = true;
    metrics_collector_->StartCollection(collection_interval);

    // Start alert processing thread
    alert_thread_running_ = true;
    alert_thread_ = std::thread(&PerformanceMonitor::AlertProcessingLoop, this);
}

void PerformanceMonitor::StopMonitoring() {
    monitoring_enabled_ = false;
    alert_thread_running_ = false;

    metrics_collector_->StopCollection();

    if (alert_thread_.joinable()) {
        alert_thread_.join();
    }
}

void PerformanceMonitor::AddAlertChannel(std::unique_ptr<AlertChannel> channel) {
    alert_channels_.push_back(std::move(channel));
}

void PerformanceMonitor::SetAlertThreshold(AlertType type, double threshold,
                                          AlertSeverity severity) {
    std::lock_guard<std::mutex> lock(thresholds_mutex_);
    alert_thresholds_[type] = {threshold, severity};
}

void PerformanceMonitor::CheckAndTriggerAlerts() {
    auto snapshot = metrics_collector_->GetCurrentSnapshot();

    std::lock_guard<std::mutex> lock(thresholds_mutex_);

    // Check each alert condition
    CheckPerformanceDegradationAlert(snapshot);
    CheckMemoryPressureAlert(snapshot);
    CheckGpuUtilizationAlert(snapshot);
    CheckTemperatureAlert(snapshot);
    CheckBandwidthAlert(snapshot);
    CheckErrorRateAlert(snapshot);
}

void PerformanceMonitor::CheckPerformanceDegradationAlert(const PerformanceSnapshot& snapshot) {
    auto it = alert_thresholds_.find(AlertType::PERFORMANCE_DEGRADATION);
    if (it == alert_thresholds_.end()) return;

    const auto& threshold_info = it->second;

    // Calculate average throughput over last 5 minutes
    double avg_throughput = metrics_collector_->CalculateAverageMetric(
        [](const PerformanceSnapshot& s) { return s.key_search_throughput; },
        std::chrono::minutes(5));

    double expected_throughput = 50.0;  // Expected throughput baseline
    double degradation_percentage = (expected_throughput - avg_throughput) / expected_throughput;

    if (degradation_percentage > threshold_info.first) {
        TriggerAlert(AlertType::PERFORMANCE_DEGRADATION, threshold_info.second,
                    "Key search throughput degraded by " +
                    std::to_string(degradation_percentage * 100) + "%",
                    avg_throughput, expected_throughput);
    }
}

void PerformanceMonitor::CheckMemoryPressureAlert(const PerformanceSnapshot& snapshot) {
    auto it = alert_thresholds_.find(AlertType::MEMORY_PRESSURE);
    if (it == alert_thresholds_.end()) return;

    const auto& threshold_info = it->second;

    if (snapshot.memory_utilization > threshold_info.first) {
        TriggerAlert(AlertType::MEMORY_PRESSURE, threshold_info.second,
                    "Memory utilization at " + std::to_string(snapshot.memory_utilization * 100) + "%",
                    snapshot.memory_utilization, threshold_info.first);
    }
}

void PerformanceMonitor::CheckGpuUtilizationAlert(const PerformanceSnapshot& snapshot) {
    auto it = alert_thresholds_.find(AlertType::GPU_UTILIZATION_LOW);
    if (it == alert_thresholds_.end()) return;

    const auto& threshold_info = it->second;

    // Check average GPU utilization over last 2 minutes
    double avg_utilization = metrics_collector_->CalculateAverageMetric(
        [](const PerformanceSnapshot& s) { return s.gpu_utilization; },
        std::chrono::minutes(2));

    if (avg_utilization < threshold_info.first) {
        TriggerAlert(AlertType::GPU_UTILIZATION_LOW, threshold_info.second,
                    "GPU utilization at " + std::to_string(avg_utilization * 100) + "%",
                    avg_utilization, threshold_info.first);
    }
}

void PerformanceMonitor::CheckTemperatureAlert(const PerformanceSnapshot& snapshot) {
    auto it = alert_thresholds_.find(AlertType::TEMPERATURE_HIGH);
    if (it == alert_thresholds_.end()) return;

    const auto& threshold_info = it->second;

    if (snapshot.temperature_c > threshold_info.first) {
        TriggerAlert(AlertType::TEMPERATURE_HIGH, threshold_info.second,
                    "GPU temperature at " + std::to_string(snapshot.temperature_c) + "°C",
                    snapshot.temperature_c, threshold_info.first);
    }
}

void PerformanceMonitor::CheckBandwidthAlert(const PerformanceSnapshot& snapshot) {
    auto it = alert_thresholds_.find(AlertType::BANDWIDTH_LOW);
    if (it == alert_thresholds_.end()) return;

    const auto& threshold_info = it->second;

    if (snapshot.memory_bandwidth_utilization < threshold_info.first) {
        TriggerAlert(AlertType::BANDWIDTH_LOW, threshold_info.second,
                    "Memory bandwidth utilization at " +
                    std::to_string(snapshot.memory_bandwidth_utilization * 100) + "%",
                    snapshot.memory_bandwidth_utilization, threshold_info.first);
    }
}

void PerformanceMonitor::CheckErrorRateAlert(const PerformanceSnapshot& snapshot) {
    auto it = alert_thresholds_.find(AlertType::ERROR_RATE_HIGH);
    if (it == alert_thresholds_.end()) return;

    const auto& threshold_info = it->second;

    if (snapshot.error_rate > threshold_info.first) {
        TriggerAlert(AlertType::ERROR_RATE_HIGH, threshold_info.second,
                    "Error rate at " + std::to_string(snapshot.error_rate * 100) + "%",
                    snapshot.error_rate, threshold_info.first);
    }
}

void PerformanceMonitor::TriggerAlert(AlertType type, AlertSeverity severity,
                                     const std::string& message,
                                     double value, double threshold) {
    // Check cooldown period to avoid alert spam
    auto now = std::chrono::system_clock::now();
    auto last_alert_time = last_alert_times_[type];

    if (now - last_alert_time < alert_cooldown_duration_) {
        return;  // Still in cooldown period
    }

    // Create and send alert
    auto alert = std::make_unique<PerformanceAlert>(type, severity, message, value, threshold);

    for (auto& channel : alert_channels_) {
        channel->SendAlert(*alert);
    }

    // Store alert and update cooldown
    alert_history_.push_back(*alert);
    last_alert_times_[type] = now;

    // Keep alert history manageable (last 1000 alerts)
    if (alert_history_.size() > 1000) {
        alert_history_.erase(alert_history_.begin(), alert_history_.begin() + 100);
    }
}

void PerformanceMonitor::AlertProcessingLoop() {
    while (alert_thread_running_) {
        if (monitoring_enabled_) {
            CheckAndTriggerAlerts();
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void PerformanceMonitor::SetDefaultAlertThresholds() {
    // Performance degradation (>20% drop = WARNING, >40% drop = CRITICAL)
    SetAlertThreshold(AlertType::PERFORMANCE_DEGRADATION, 0.20, AlertSeverity::WARNING);

    // Memory pressure (>85% = WARNING, >95% = CRITICAL)
    SetAlertThreshold(AlertType::MEMORY_PRESSURE, 0.85, AlertSeverity::WARNING);

    // Low GPU utilization (<60% = WARNING, <40% = CRITICAL)
    SetAlertThreshold(AlertType::GPU_UTILIZATION_LOW, 0.60, AlertSeverity::WARNING);

    // High temperature (>80°C = WARNING, >90°C = CRITICAL)
    SetAlertThreshold(AlertType::TEMPERATURE_HIGH, 80.0, AlertSeverity::WARNING);

    // Low bandwidth (<60% = WARNING, <40% = CRITICAL)
    SetAlertThreshold(AlertType::BANDWIDTH_LOW, 0.60, AlertSeverity::WARNING);

    // High error rate (>1% = WARNING, >5% = CRITICAL)
    SetAlertThreshold(AlertType::ERROR_RATE_HIGH, 0.01, AlertSeverity::WARNING);
}

nlohmann::json PerformanceMonitor::GetMonitoringStatus() const {
    nlohmann::json status;
    status["monitoring_enabled"] = monitoring_enabled_;
    status["device_id"] = device_id_;
    status["collection_interval_ms"] = metrics_collector_->GetCollectionInterval().count();
    status["total_alerts"] = alert_history_.size();
    status["alert_channels_count"] = alert_channels_.size();

    // Current metrics
    auto current_snapshot = metrics_collector_->GetCurrentSnapshot();
    status["current_metrics"] = current_snapshot.ToJson();

    // Recent alerts
    nlohmann::json recent_alerts = nlohmann::json::array();
    int count = 0;
    for (auto it = alert_history_.rbegin(); it != alert_history_.rend() && count < 10; ++it, ++count) {
        recent_alerts.push_back(it->ToJson());
    }
    status["recent_alerts"] = recent_alerts;

    return status;
}

void PerformanceMonitor::ExportMonitoringData(const std::string& filename) const {
    nlohmann::json export_data;
    export_data["export_timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    export_data["monitoring_status"] = GetMonitoringStatus();

    // Export metrics history
    auto recent_snapshots = metrics_collector_->GetRecentSnapshots(std::chrono::hours(1));
    nlohmann::json metrics_history = nlohmann::json::array();
    for (const auto& snapshot : recent_snapshots) {
        metrics_history.push_back(snapshot.ToJson());
    }
    export_data["metrics_history"] = metrics_history;

    // Export alert history
    nlohmann::json alert_history = nlohmann::json::array();
    for (const auto& alert : alert_history_) {
        alert_history.push_back(alert.ToJson());
    }
    export_data["alert_history"] = alert_history;

    // Save to file
    std::ofstream file(filename);
    if (file.is_open()) {
        file << std::setw(4) << export_data << std::endl;
    }
}

}  // namespace monitoring
}  // namespace keycuda