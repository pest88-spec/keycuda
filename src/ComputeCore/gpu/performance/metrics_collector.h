#pragma once

#include <chrono>
#include <vector>
#include <map>
#include <string>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include <cuda_runtime.h>
// NVML support is optional
#ifdef HAS_NVML
#include <nvidia/ml/ml.h>
#else
// Define NVML types and constants when NVML is not available
// Avoid conflicts with other headers by using include guards
#ifndef NVML_FALLBACK_TYPES_DEFINED
#define NVML_FALLBACK_TYPES_DEFINED
typedef void* nvmlDevice_t;
typedef enum {
    NVML_SUCCESS = 0,
    NVML_ERROR_NOT_SUPPORTED = 8
} nvmlReturn_t;
typedef enum {
    NVML_TEMPERATURE_GPU = 0
} nvmlTemperatureSensors_t;
typedef struct {
    unsigned int gpu;
    unsigned int memory;
} nvmlUtilization_t;
#endif // NVML_FALLBACK_TYPES_DEFINED
#endif
#include <nlohmann/json.hpp>

namespace puzzle71::gpu::performance {

using json = nlohmann::json;

/**
 * Performance Metrics Types
 */
enum class MetricType {
    Throughput,          // Operations per second
    Latency,             // Operation latency
    Utilization,         // Resource utilization percentage
    MemoryBandwidth,     // Memory bandwidth utilization
    PowerUsage,          // Power consumption
    Temperature,         // Thermal metrics
    KernelPerformance,   // Kernel-specific metrics
    ErrorRate,           // Error occurrence rate
    QueueDepth,          // GPU queue depth
    CacheHitRate         // Cache performance
};

/**
 * Metric collection intervals
 */
enum class CollectionInterval {
    Realtime,     // Every second
    Frequent,     // Every 10 seconds
    Regular,      // Every minute
    Periodic,     // Every 5 minutes
    OnDemand      // Only when requested
};

/**
 * Performance metric data point (enhanced version of original)
 */
struct PerformanceMetric {
    std::string name;
    double value;
    std::chrono::steady_clock::time_point timestamp;
    std::string unit;
    MetricType type;
    json metadata;

    // Default constructor
    PerformanceMetric() : name(""), value(0.0), unit(""), type(MetricType::Throughput), metadata(json::object()) {}

    // Parameterized constructor
    PerformanceMetric(const std::string& n, double v, const std::string& u, MetricType t = MetricType::Throughput)
        : name(n), value(v), timestamp(std::chrono::steady_clock::now()),
          unit(u), type(t), metadata(json::object()) {}
};

/**
 * Kernel execution metrics
 */
struct KernelExecutionMetrics {
    std::string kernel_name;
    double avg_execution_time_ms = 0.0;
    double min_execution_time_ms = 0.0;
    double max_execution_time_ms = 0.0;
    size_t total_launches = 0;
    double throughput_ops_per_sec = 0.0;
    double gpu_utilization_percent = 0.0;
    double memory_bandwidth_utilization_percent = 0.0;
    std::chrono::steady_clock::time_point last_execution;

    void UpdateExecution(double execution_time_ms, size_t operations_count) {
        avg_execution_time_ms = (avg_execution_time_ms * total_launches + execution_time_ms) / (total_launches + 1);
        min_execution_time_ms = (total_launches == 0) ? execution_time_ms :
                               std::min(min_execution_time_ms, execution_time_ms);
        max_execution_time_ms = (total_launches == 0) ? execution_time_ms :
                               std::max(max_execution_time_ms, execution_time_ms);
        total_launches++;
        last_execution = std::chrono::steady_clock::now();

        if (execution_time_ms > 0) {
            throughput_ops_per_sec = (operations_count * 1000.0) / execution_time_ms;
        }
    }
};

/**
 * GPU performance snapshot
 */
struct GpuPerformanceSnapshot {
    int gpu_id;
    std::chrono::steady_clock::time_point timestamp;

    // Utilization metrics
    double gpu_utilization_percent = 0.0;
    double memory_utilization_percent = 0.0;
    double memory_bandwidth_utilization_percent = 0.0;

    // Thermal and power
    double temperature_celsius = 0.0;
    double power_usage_watts = 0.0;
    bool thermal_throttling_active = false;

    // Performance metrics
    double current_throughput_mkeys_per_sec = 0.0;
    double avg_throughput_mkeys_per_sec = 0.0;
    size_t total_operations_processed = 0;

    // Memory metrics
    size_t memory_used_mb = 0;
    size_t memory_total_mb = 0;
    size_t memory_free_mb = 0;

    // Queue and scheduling
    double active_streams_ratio = 0.0;
    size_t pending_operations = 0;
};

/**
 * Performance statistics
 */
struct PerformanceStatistics {
    double mean_throughput_mkeys_per_sec = 0.0;
    double peak_throughput_mkeys_per_sec = 0.0;
    double sustained_throughput_mkeys_per_sec = 0.0;  // 5-minute average

    std::chrono::steady_clock::time_point peak_throughput_time;
    std::chrono::steady_clock::time_point measurement_start;

    // Reliability metrics
    double error_rate_percent = 0.0;
    size_t total_errors = 0;
    size_t total_operations = 0;

    // Efficiency metrics
    double gpu_efficiency_percent = 0.0;      // Actual vs theoretical throughput
    double memory_efficiency_percent = 0.0;   // Memory bandwidth utilization
    double power_efficiency_mkeys_per_watt = 0.0;

    // Stability metrics
    double performance_variance = 0.0;
    double uptime_percentage = 0.0;
    size_t recovery_events = 0;
};

/**
 * Enhanced Metrics Collector Interface
 */
class MetricsCollector {
public:
    MetricsCollector() = default;
    virtual ~MetricsCollector() = default;

    // Core collection methods (original interface preserved)
    virtual void StartCollection() = 0;
    virtual void StopCollection() = 0;
    virtual bool IsCollecting() const = 0;
    virtual void RecordMetric(const std::string& metric_name, double value) = 0;
    virtual std::vector<PerformanceMetric> GetCollectedMetrics() const = 0;
    virtual void ClearMetrics() = 0;
    virtual void EnableAutoCollection(bool enabled) = 0;

    // Enhanced collection methods
    virtual void RecordMetric(const std::string& name, double value,
                            const std::map<std::string, std::string>& metadata = {}) = 0;
    virtual void RecordKernelExecution(const std::string& kernel_name,
                                     double execution_time_ms, size_t operations_count) = 0;
    virtual void RecordThroughputSample(double mkeys_per_sec) = 0;
    virtual void RecordError(const std::string& error_type, const std::string& context) = 0;

    // Data retrieval
    virtual std::vector<PerformanceMetric> GetMetricHistory(const std::string& name,
                                                           std::chrono::seconds duration = {}) const = 0;
    virtual KernelExecutionMetrics GetKernelMetrics(const std::string& kernel_name) const = 0;
    virtual GpuPerformanceSnapshot GetLatestSnapshot(int gpu_id) const = 0;
    virtual PerformanceStatistics GetStatistics(std::chrono::seconds duration = {}) const = 0;

    // Metric registration
    virtual void RegisterMetric(MetricType type, CollectionInterval interval,
                              const std::string& name, const std::string& unit) = 0;
    virtual void UnregisterMetric(const std::string& name) = 0;

    // Configuration
    virtual void SetCollectionInterval(CollectionInterval interval) = 0;
    virtual void SetMaxHistorySize(size_t max_size) = 0;
    virtual void EnableAutoOptimization(bool enable) = 0;

    // Export and analysis
    virtual std::map<std::string, double> GetRealtimeMetrics() const = 0;
    virtual std::string ExportMetrics(const std::string& format = "json") const = 0;

    // Factory method
    static std::unique_ptr<MetricsCollector> Create(int gpu_id);
};

/**
 * CUDA-specific Metrics Collector Implementation
 */
class CudaMetricsCollector : public MetricsCollector {
public:
    explicit CudaMetricsCollector(int gpu_id);
    ~CudaMetricsCollector() override;

    // Core collection methods (original interface)
    void StartCollection() override;
    void StopCollection() override;
    bool IsCollecting() const override;
    void RecordMetric(const std::string& metric_name, double value) override;
    std::vector<PerformanceMetric> GetCollectedMetrics() const override;
    void ClearMetrics() override;
    void EnableAutoCollection(bool enabled) override;

    // Enhanced collection methods
    void RecordMetric(const std::string& name, double value,
                     const std::map<std::string, std::string>& metadata) override;
    void RecordKernelExecution(const std::string& kernel_name,
                             double execution_time_ms, size_t operations_count) override;
    void RecordThroughputSample(double mkeys_per_sec) override;
    void RecordError(const std::string& error_type, const std::string& context) override;

    // Data retrieval
    std::vector<PerformanceMetric> GetMetricHistory(const std::string& name,
                                                   std::chrono::seconds duration) const override;
    KernelExecutionMetrics GetKernelMetrics(const std::string& kernel_name) const override;
    GpuPerformanceSnapshot GetLatestSnapshot(int gpu_id) const override;
    PerformanceStatistics GetStatistics(std::chrono::seconds duration) const override;

    // Metric registration
    void RegisterMetric(MetricType type, CollectionInterval interval,
                      const std::string& name, const std::string& unit) override;
    void UnregisterMetric(const std::string& name) override;

    // Configuration
    void SetCollectionInterval(CollectionInterval interval) override;
    void SetMaxHistorySize(size_t max_size) override;
    void EnableAutoOptimization(bool enable) override;

    // Export and analysis
    std::map<std::string, double> GetRealtimeMetrics() const override;
    std::string ExportMetrics(const std::string& format) const override;

    // GPU-specific methods
    void SetTargetThroughput(double target_mkeys_per_sec);
    double GetPerformanceEfficiency() const;
    bool IsThermalThrottlingDetected() const;

private:
    int gpu_id_;
    std::atomic<bool> collecting_{false};
    std::atomic<bool> should_stop_{false};
    std::atomic<bool> auto_collection_enabled_{true};
    std::thread collection_thread_;

    // Metric storage
    mutable std::mutex metrics_mutex_;
    std::vector<PerformanceMetric> metrics_data_;
    std::map<std::string, MetricType> registered_metrics_;
    std::map<std::string, CollectionInterval> metric_intervals_;
    std::map<std::string, std::string> metric_units_;

    // Kernel metrics
    std::map<std::string, KernelExecutionMetrics> kernel_metrics_;

    // Performance snapshots
    std::vector<GpuPerformanceSnapshot> gpu_snapshots_;

    // Configuration
    CollectionInterval default_interval_ = CollectionInterval::Frequent;
    size_t max_history_size_ = 10000;
    bool auto_optimization_enabled_ = false;
    double target_throughput_mkeys_per_sec_ = 50.0;  // Default target

    // NVML integration
    bool nvml_initialized_ = false;
    nvmlDevice_t nvml_device_;

    // Statistics tracking
    PerformanceStatistics cumulative_stats_;
    std::chrono::steady_clock::time_point collection_start_time_;

    // Private methods
    void CollectionLoop();
    void CollectGpuMetrics();
    void CollectSystemMetrics();
    void UpdateStatistics();
    void CheckAutoOptimization();

    // NVML helpers
    bool InitializeNvml();
    void ShutdownNvml();
    double GetGpuUtilization() const;
    double GetMemoryUtilization() const;
    double GetMemoryBandwidthUtilization() const;
    double GetTemperature() const;
    double GetPowerUsage() const;
    bool IsThermalThrottling() const;

    // Metric helpers
    std::chrono::milliseconds GetIntervalDuration(CollectionInterval interval) const;
    bool ShouldCollectMetric(const std::string& name) const;
    void TrimOldData();
    void UpdatePerformanceStatistics(double throughput_sample);

    // Export helpers
    std::string ExportToJson() const;
    std::string ExportToCsv() const;
};

/**
 * RAII Metrics Timer for automatic performance measurement
 */
class MetricsTimer {
public:
    MetricsTimer(CudaMetricsCollector& collector, const std::string& operation_name,
                 size_t operations_count = 1)
        : collector_(collector), operation_name_(operation_name),
          operations_count_(operations_count), start_time_(std::chrono::steady_clock::now()) {}

    ~MetricsTimer() {
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time_);
        double execution_time_ms = duration.count() / 1000.0;

        collector_.RecordKernelExecution(operation_name_, execution_time_ms, operations_count_);
    }

    // Disable copying
    MetricsTimer(const MetricsTimer&) = delete;
    MetricsTimer& operator=(const MetricsTimer&) = delete;

    // Enable moving
    MetricsTimer(MetricsTimer&& other) noexcept
        : collector_(other.collector_), operation_name_(std::move(other.operation_name_)),
          operations_count_(other.operations_count_), start_time_(other.start_time_) {}

private:
    CudaMetricsCollector& collector_;
    std::string operation_name_;
    size_t operations_count_;
    std::chrono::steady_clock::time_point start_time_;
};

// Utility macros for automatic metrics collection
#define MEASURE_KERNEL_PERFORMANCE(collector, kernel_name, ops_count) \
    MetricsTimer timer(collector, kernel_name, ops_count)

#define RECORD_THROUGHPUT_SAMPLE(collector, mkeys_per_sec) \
    collector.RecordThroughputSample(mkeys_per_sec)

#define RECORD_METRIC(collector, name, value) \
    collector.RecordMetric(name, value)

} // namespace puzzle71::gpu::performance