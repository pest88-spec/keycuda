#pragma once

#include <memory>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <functional>
#include <string>
#include <map>

#include <cuda_runtime.h>
#include "nlohmann/json.hpp"

namespace keycuda {
namespace monitoring {

// Alert type enumeration
enum class AlertType {
    PERFORMANCE_DEGRADATION,
    MEMORY_PRESSURE,
    GPU_UTILIZATION_LOW,
    KERNEL_TIMEOUT,
    TEMPERATURE_HIGH,
    BANDWIDTH_LOW,
    ERROR_RATE_HIGH,
    RESOURCE_EXHAUSTION
};

// Alert severity enumeration
enum class AlertSeverity {
    INFO,
    WARNING,
    CRITICAL
};

// Forward declarations
class PerformanceAlert;
class AlertChannel;
class MetricsCollector;

// Performance metrics snapshot
struct PerformanceSnapshot {
    std::chrono::system_clock::time_point timestamp;
    std::string gpu_name;
    int compute_capability;
    size_t total_memory_mb;
    size_t memory_used_mb;
    double memory_utilization;
    double gpu_utilization;
    double power_consumption_w;
    double temperature_c;
    double key_search_throughput;        // Mkeys/sec
    double memory_bandwidth_utilization; // Fraction of theoretical max
    double synchronization_latency_us;
    double error_rate;                   // Fraction of operations that fail

    PerformanceSnapshot() : compute_capability(0), total_memory_mb(0), memory_used_mb(0),
                           memory_utilization(0.0), gpu_utilization(0.0), power_consumption_w(0.0),
                           temperature_c(0.0), key_search_throughput(0.0), memory_bandwidth_utilization(0.0),
                           synchronization_latency_us(0.0), error_rate(0.0) {}

    nlohmann::json ToJson() const {
        nlohmann::json json;
        json["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            timestamp.time_since_epoch()).count();
        json["gpu_name"] = gpu_name;
        json["compute_capability"] = compute_capability;
        json["total_memory_mb"] = total_memory_mb;
        json["memory_used_mb"] = memory_used_mb;
        json["memory_utilization"] = memory_utilization;
        json["gpu_utilization"] = gpu_utilization;
        json["power_consumption_w"] = power_consumption_w;
        json["temperature_c"] = temperature_c;
        json["key_search_throughput"] = key_search_throughput;
        json["memory_bandwidth_utilization"] = memory_bandwidth_utilization;
        json["synchronization_latency_us"] = synchronization_latency_us;
        json["error_rate"] = error_rate;
        return json;
    }
};

// Performance alert class
class PerformanceAlert {
public:
    PerformanceAlert(AlertType type, AlertSeverity severity, const std::string& message,
                    double value, double threshold);

    AlertType GetType() const { return type_; }
    AlertSeverity GetSeverity() const { return severity_; }
    const std::string& GetMessage() const { return message_; }
    double GetValue() const { return value_; }
    double GetThreshold() const { return threshold_; }
    std::chrono::system_clock::time_point GetTimestamp() const { return timestamp_; }
    bool IsResolved() const { return resolved_; }

    void SetResolved(bool resolved) { resolved_ = resolved; }

    std::string GetTypeString() const;
    std::string GetSeverityString() const;
    nlohmann::json ToJson() const;

private:
    AlertType type_;
    AlertSeverity severity_;
    std::string message_;
    double value_;
    double threshold_;
    std::chrono::system_clock::time_point timestamp_;
    bool resolved_;
};

// Alert channel interface
class AlertChannel {
public:
    virtual ~AlertChannel() = default;
    virtual void SendAlert(const PerformanceAlert& alert) = 0;
};

// Console alert channel
class ConsoleAlertChannel : public AlertChannel {
public:
    explicit ConsoleAlertChannel(bool color_output = true);

    void SendAlert(const PerformanceAlert& alert) override;

private:
    bool color_output_;
};

// File alert channel
class FileAlertChannel : public AlertChannel {
public:
    explicit FileAlertChannel(const std::string& filename);

    void SendAlert(const PerformanceAlert& alert) override;

private:
    std::string filename_;
};

// Network alert channel (for REST/HTTP notifications)
class NetworkAlertChannel : public AlertChannel {
public:
    explicit NetworkAlertChannel(const std::string& endpoint);

    void SendAlert(const PerformanceAlert& alert) override;

private:
    std::string endpoint_;
};

// Metrics collector for gathering performance data
class MetricsCollector {
public:
    explicit MetricsCollector(int device_id);
    ~MetricsCollector();

    void StartCollection(std::chrono::milliseconds interval = std::chrono::seconds(1));
    void StopCollection();

    PerformanceSnapshot GetCurrentSnapshot() const;
    std::vector<PerformanceSnapshot> GetRecentSnapshots(std::chrono::milliseconds duration) const;

    double CalculateAverageMetric(std::function<double(const PerformanceSnapshot&)> metric_extractor,
                                 std::chrono::milliseconds duration) const;

    std::chrono::milliseconds GetCollectionInterval() const { return collection_interval_; }

private:
    void CollectionLoop();
    PerformanceSnapshot CollectCurrentMetrics();

    // GPU metric collection methods
    double GetGpuUtilization();
    double GetPowerConsumption();
    double GetTemperature();
    double GetKeySearchThroughput();
    double GetMemoryBandwidthUtilization();
    double GetSynchronizationLatency();
    double GetErrorRate();

    int device_id_;
    std::atomic<bool> collection_enabled_;
    std::atomic<bool> collection_thread_running_;
    std::chrono::milliseconds collection_interval_;
    std::thread collection_thread_;

    mutable std::mutex metrics_mutex_;
    std::vector<PerformanceSnapshot> metrics_history_;
};

// Main performance monitor class
class PerformanceMonitor {
public:
    explicit PerformanceMonitor(int device_id);
    ~PerformanceMonitor();

    void StartMonitoring(std::chrono::milliseconds collection_interval = std::chrono::seconds(1));
    void StopMonitoring();

    bool IsMonitoringEnabled() const { return monitoring_enabled_; }

    // Alert management
    void AddAlertChannel(std::unique_ptr<AlertChannel> channel);
    void SetAlertThreshold(AlertType type, double threshold, AlertSeverity severity);
    void SetAlertCooldownDuration(std::chrono::milliseconds duration) {
        alert_cooldown_duration_ = duration;
    }

    // Metrics access
    PerformanceSnapshot GetCurrentSnapshot() const {
        return metrics_collector_->GetCurrentSnapshot();
    }

    std::vector<PerformanceSnapshot> GetRecentSnapshots(std::chrono::milliseconds duration) const {
        return metrics_collector_->GetRecentSnapshots(duration);
    }

    // Status and reporting
    nlohmann::json GetMonitoringStatus() const;
    void ExportMonitoringData(const std::string& filename) const;

    const std::vector<PerformanceAlert>& GetAlertHistory() const { return alert_history_; }

private:
    void AlertProcessingLoop();
    void CheckAndTriggerAlerts();

    // Individual alert checking methods
    void CheckPerformanceDegradationAlert(const PerformanceSnapshot& snapshot);
    void CheckMemoryPressureAlert(const PerformanceSnapshot& snapshot);
    void CheckGpuUtilizationAlert(const PerformanceSnapshot& snapshot);
    void CheckTemperatureAlert(const PerformanceSnapshot& snapshot);
    void CheckBandwidthAlert(const PerformanceSnapshot& snapshot);
    void CheckErrorRateAlert(const PerformanceSnapshot& snapshot);

    void TriggerAlert(AlertType type, AlertSeverity severity, const std::string& message,
                     double value, double threshold);

    void SetDefaultAlertThresholds();

    int device_id_;
    std::atomic<bool> monitoring_enabled_;
    std::atomic<bool> alert_thread_running_;
    std::thread alert_thread_;

    std::unique_ptr<MetricsCollector> metrics_collector_;
    std::vector<std::unique_ptr<AlertChannel>> alert_channels_;

    struct AlertThresholdInfo {
        double threshold;
        AlertSeverity severity;
    };

    std::mutex thresholds_mutex_;
    std::map<AlertType, AlertThresholdInfo> alert_thresholds_;

    std::chrono::milliseconds alert_cooldown_duration_;
    std::map<AlertType, std::chrono::system_clock::time_point> last_alert_times_;

    std::vector<PerformanceAlert> alert_history_;
};

}  // namespace monitoring
}  // namespace keycuda