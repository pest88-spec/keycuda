#include "ComputeCore/gpu/performance/metrics_collector.h"
#include <iostream>
#include <algorithm>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <numeric>

namespace puzzle71::gpu::performance {

// Factory method implementation
std::unique_ptr<MetricsCollector> MetricsCollector::Create(int gpu_id) {
    return std::make_unique<CudaMetricsCollector>(gpu_id);
}

// CudaMetricsCollector implementation
CudaMetricsCollector::CudaMetricsCollector(int gpu_id) : gpu_id_(gpu_id) {
    // Initialize NVML for detailed GPU monitoring
    InitializeNvml();

    // Register default metrics
    RegisterMetric(MetricType::Throughput, CollectionInterval::Realtime, "throughput_mkeys_per_sec", "Mkeys/s");
    RegisterMetric(MetricType::Utilization, CollectionInterval::Frequent, "gpu_utilization", "%");
    RegisterMetric(MetricType::MemoryBandwidth, CollectionInterval::Frequent, "memory_bandwidth_utilization", "%");
    RegisterMetric(MetricType::MemoryBandwidth, CollectionInterval::Frequent, "memory_utilization", "%");
    RegisterMetric(MetricType::Temperature, CollectionInterval::Regular, "gpu_temperature", "°C");
    RegisterMetric(MetricType::PowerUsage, CollectionInterval::Regular, "gpu_power", "W");

    collection_start_time_ = std::chrono::steady_clock::now();
    cumulative_stats_.measurement_start = collection_start_time_;

    std::cout << "CudaMetricsCollector initialized for GPU " << gpu_id_ << std::endl;
}

CudaMetricsCollector::~CudaMetricsCollector() {
    StopCollection();
    ShutdownNvml();
}

// Core collection methods
void CudaMetricsCollector::StartCollection() {
    if (collecting_.load()) {
        return;  // Already collecting
    }

    should_stop_.store(false);
    collecting_.store(true);

    // Start collection thread
    collection_thread_ = std::thread(&CudaMetricsCollector::CollectionLoop, this);

    std::cout << "Started metrics collection for GPU " << gpu_id_ << std::endl;
}

void CudaMetricsCollector::StopCollection() {
    if (!collecting_.load()) {
        return;  // Not collecting
    }

    should_stop_.store(true);
    collecting_.store(false);

    if (collection_thread_.joinable()) {
        collection_thread_.join();
    }

    std::cout << "Stopped metrics collection for GPU " << gpu_id_ << std::endl;
}

bool CudaMetricsCollector::IsCollecting() const {
    return collecting_.load();
}

void CudaMetricsCollector::RecordMetric(const std::string& metric_name, double value) {
    RecordMetric(metric_name, value, {});
}

void CudaMetricsCollector::RecordMetric(const std::string& name, double value,
                                       const std::map<std::string, std::string>& metadata) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    auto it = registered_metrics_.find(name);
    if (it == registered_metrics_.end()) {
        // Auto-register unknown metrics as throughput type
        RegisterMetric(MetricType::Throughput, default_interval_, name, "");
        it = registered_metrics_.find(name);
    }

    PerformanceMetric metric(name, value, metric_units_[name], it->second);

    // Add metadata
    json metadata_json;
    for (const auto& [key, val] : metadata) {
        metadata_json[key] = val;
    }
    metric.metadata = metadata_json;

    metrics_data_.push_back(metric);

    // Update performance statistics if this is a throughput metric
    if (it->second == MetricType::Throughput && name.find("throughput") != std::string::npos) {
        UpdatePerformanceStatistics(value);
    }

    // Trim old data if needed
    TrimOldData();
}

std::vector<PerformanceMetric> CudaMetricsCollector::GetCollectedMetrics() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    return metrics_data_;
}

void CudaMetricsCollector::ClearMetrics() {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    metrics_data_.clear();
    kernel_metrics_.clear();
    gpu_snapshots_.clear();
    cumulative_stats_ = PerformanceStatistics{};
    cumulative_stats_.measurement_start = std::chrono::steady_clock::now();
}

void CudaMetricsCollector::EnableAutoCollection(bool enabled) {
    auto_collection_enabled_.store(enabled);
}

// Enhanced collection methods
void CudaMetricsCollector::RecordKernelExecution(const std::string& kernel_name,
                                                double execution_time_ms, size_t operations_count) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    auto& metrics = kernel_metrics_[kernel_name];
    metrics.kernel_name = kernel_name;
    metrics.UpdateExecution(execution_time_ms, operations_count);

    // Also record as a general metric
    PerformanceMetric metric("kernel_" + kernel_name + "_execution_time", execution_time_ms, "ms", MetricType::KernelPerformance);
    metric.metadata["kernel_name"] = kernel_name;
    metric.metadata["operations_count"] = std::to_string(operations_count);
    metrics_data_.push_back(metric);
}

void CudaMetricsCollector::RecordThroughputSample(double mkeys_per_sec) {
    RecordMetric("throughput_mkeys_per_sec", mkeys_per_sec,
                {{"gpu_id", std::to_string(gpu_id_)},
                 {"sample_type", "realtime"}});
}

void CudaMetricsCollector::RecordError(const std::string& error_type, const std::string& context) {
    RecordMetric("error_count", 1.0,
                {{"error_type", error_type},
                 {"context", context},
                 {"gpu_id", std::to_string(gpu_id_)}});

    // Update error statistics
    cumulative_stats_.total_errors++;
}

// Data retrieval methods
std::vector<PerformanceMetric> CudaMetricsCollector::GetMetricHistory(const std::string& name,
                                                                     std::chrono::seconds duration) const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    std::vector<PerformanceMetric> history;
    auto cutoff_time = std::chrono::steady_clock::now() - duration;

    for (const auto& metric : metrics_data_) {
        if (metric.name == name &&
            (duration.count() == 0 || metric.timestamp >= cutoff_time)) {
            history.push_back(metric);
        }
    }

    return history;
}

KernelExecutionMetrics CudaMetricsCollector::GetKernelMetrics(const std::string& kernel_name) const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    auto it = kernel_metrics_.find(kernel_name);
    if (it != kernel_metrics_.end()) {
        return it->second;
    }

    return KernelExecutionMetrics{kernel_name};
}

GpuPerformanceSnapshot CudaMetricsCollector::GetLatestSnapshot(int gpu_id) const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    // Find the latest snapshot for the requested GPU
    for (auto it = gpu_snapshots_.rbegin(); it != gpu_snapshots_.rend(); ++it) {
        if (it->gpu_id == gpu_id) {
            return *it;
        }
    }

    // Return empty snapshot if none found
    return GpuPerformanceSnapshot{gpu_id};
}

PerformanceStatistics CudaMetricsCollector::GetStatistics(std::chrono::seconds duration) const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    if (duration.count() == 0) {
        return cumulative_stats_;
    }

    // Calculate statistics for the specified duration
    PerformanceStatistics window_stats;
    auto cutoff_time = std::chrono::steady_clock::now() - duration;

    std::vector<double> throughput_samples;
    for (const auto& metric : metrics_data_) {
        if (metric.name == "throughput_mkeys_per_sec" && metric.timestamp >= cutoff_time) {
            throughput_samples.push_back(metric.value);
        }
    }

    if (!throughput_samples.empty()) {
        window_stats.mean_throughput_mkeys_per_sec =
            std::accumulate(throughput_samples.begin(), throughput_samples.end(), 0.0) / throughput_samples.size();

        auto max_it = std::max_element(throughput_samples.begin(), throughput_samples.end());
        window_stats.peak_throughput_mkeys_per_sec = *max_it;

        // Calculate variance
        double sum_sq_diff = 0.0;
        for (double sample : throughput_samples) {
            double diff = sample - window_stats.mean_throughput_mkeys_per_sec;
            sum_sq_diff += diff * diff;
        }
        window_stats.performance_variance = sum_sq_diff / throughput_samples.size();
    }

    return window_stats;
}

// Metric registration
void CudaMetricsCollector::RegisterMetric(MetricType type, CollectionInterval interval,
                                         const std::string& name, const std::string& unit) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    registered_metrics_[name] = type;
    metric_intervals_[name] = interval;
    metric_units_[name] = unit;
}

void CudaMetricsCollector::UnregisterMetric(const std::string& name) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    registered_metrics_.erase(name);
    metric_intervals_.erase(name);
    metric_units_.erase(name);
}

// Configuration
void CudaMetricsCollector::SetCollectionInterval(CollectionInterval interval) {
    default_interval_ = interval;
}

void CudaMetricsCollector::SetMaxHistorySize(size_t max_size) {
    max_history_size_ = max_size;
}

void CudaMetricsCollector::EnableAutoOptimization(bool enable) {
    auto_optimization_enabled_ = enable;
}

// Export and analysis
std::map<std::string, double> CudaMetricsCollector::GetRealtimeMetrics() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    std::map<std::string, double> realtime_metrics;

    // Get latest values for key metrics
    std::map<std::string, PerformanceMetric> latest_metrics;
    for (const auto& metric : metrics_data_) {
        auto it = latest_metrics.find(metric.name);
        if (it == latest_metrics.end() || metric.timestamp > it->second.timestamp) {
            latest_metrics[metric.name] = metric;
        }
    }

    // Extract values
    for (const auto& [name, metric] : latest_metrics) {
        realtime_metrics[name] = metric.value;
    }

    return realtime_metrics;
}

std::string CudaMetricsCollector::ExportMetrics(const std::string& format) const {
    if (format == "json") {
        return ExportToJson();
    } else if (format == "csv") {
        return ExportToCsv();
    } else {
        return "Unsupported format: " + format;
    }
}

// GPU-specific methods
void CudaMetricsCollector::SetTargetThroughput(double target_mkeys_per_sec) {
    target_throughput_mkeys_per_sec_ = target_mkeys_per_sec;
}

double CudaMetricsCollector::GetPerformanceEfficiency() const {
    if (target_throughput_mkeys_per_sec_ <= 0.0) {
        return 0.0;
    }

    auto current_throughput = GetRealtimeMetrics();
    auto it = current_throughput.find("throughput_mkeys_per_sec");
    if (it != current_throughput.end()) {
        return (it->second / target_throughput_mkeys_per_sec_) * 100.0;
    }

    return 0.0;
}

bool CudaMetricsCollector::IsThermalThrottlingDetected() const {
    auto latest_snapshot = GetLatestSnapshot(gpu_id_);
    return latest_snapshot.thermal_throttling_active;
}

// Private methods
void CudaMetricsCollector::CollectionLoop() {
    auto interval = GetIntervalDuration(default_interval_);

    while (!should_stop_.load()) {
        if (auto_collection_enabled_.load()) {
            CollectGpuMetrics();
            CollectSystemMetrics();
            UpdateStatistics();
            CheckAutoOptimization();
        }

        std::this_thread::sleep_for(interval);
    }
}

void CudaMetricsCollector::CollectGpuMetrics() {
    if (!nvml_initialized_) {
        return;
    }

    GpuPerformanceSnapshot snapshot;
    snapshot.gpu_id = gpu_id_;
    snapshot.timestamp = std::chrono::steady_clock::now();

    // Collect utilization metrics
    snapshot.gpu_utilization_percent = GetGpuUtilization();
    snapshot.memory_utilization_percent = GetMemoryUtilization();
    snapshot.memory_bandwidth_utilization_percent = GetMemoryBandwidthUtilization();

    // Collect thermal and power metrics
    snapshot.temperature_celsius = GetTemperature();
    snapshot.power_usage_watts = GetPowerUsage();
    snapshot.thermal_throttling_active = IsThermalThrottling();

    // Collect memory metrics
    size_t free_mem, total_mem;
    if (cudaMemGetInfo(&free_mem, &total_mem) == cudaSuccess) {
        snapshot.memory_free_mb = free_mem / (1024 * 1024);
        snapshot.memory_total_mb = total_mem / (1024 * 1024);
        snapshot.memory_used_mb = snapshot.memory_total_mb - snapshot.memory_free_mb;
    }

    // Get current throughput
    auto realtime_metrics = GetRealtimeMetrics();
    auto throughput_it = realtime_metrics.find("throughput_mkeys_per_sec");
    if (throughput_it != realtime_metrics.end()) {
        snapshot.current_throughput_mkeys_per_sec = throughput_it->second;
    }

    std::lock_guard<std::mutex> lock(metrics_mutex_);
    gpu_snapshots_.push_back(snapshot);

    // Trim old snapshots
    if (gpu_snapshots_.size() > 1000) {  // Keep last 1000 snapshots
        gpu_snapshots_.erase(gpu_snapshots_.begin());
    }
}

void CudaMetricsCollector::CollectSystemMetrics() {
    // Record system-wide metrics
    RecordMetric("collection_timestamp",
                std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(),
                {{"type", "system"}, {"gpu_id", std::to_string(gpu_id_)}});
}

void CudaMetricsCollector::UpdateStatistics() {
    // Update cumulative statistics
    auto realtime_metrics = GetRealtimeMetrics();

    auto throughput_it = realtime_metrics.find("throughput_mkeys_per_sec");
    if (throughput_it != realtime_metrics.end()) {
        double current_throughput = throughput_it->second;

        // Update peak throughput
        if (current_throughput > cumulative_stats_.peak_throughput_mkeys_per_sec) {
            cumulative_stats_.peak_throughput_mkeys_per_sec = current_throughput;
            cumulative_stats_.peak_throughput_time = std::chrono::steady_clock::now();
        }

        // Update mean throughput (simple moving average)
        static const double ALPHA = 0.1;  // Smoothing factor
        if (cumulative_stats_.mean_throughput_mkeys_per_sec == 0.0) {
            cumulative_stats_.mean_throughput_mkeys_per_sec = current_throughput;
        } else {
            cumulative_stats_.mean_throughput_mkeys_per_sec =
                ALPHA * current_throughput + (1.0 - ALPHA) * cumulative_stats_.mean_throughput_mkeys_per_sec;
        }
    }

    // Update total operations
    auto ops_it = realtime_metrics.find("total_operations");
    if (ops_it != realtime_metrics.end()) {
        cumulative_stats_.total_operations = static_cast<size_t>(ops_it->second);
    }

    // Update error rate
    if (cumulative_stats_.total_operations > 0) {
        cumulative_stats_.error_rate_percent =
            (static_cast<double>(cumulative_stats_.total_errors) / cumulative_stats_.total_operations) * 100.0;
    }
}

void CudaMetricsCollector::CheckAutoOptimization() {
    if (!auto_optimization_enabled_) {
        return;
    }

    // Check if performance is below target
    double current_efficiency = GetPerformanceEfficiency();
    if (current_efficiency < 80.0) {  // Below 80% efficiency
        // This would trigger optimization callbacks in a real implementation
        std::cout << "Performance efficiency below target: " << current_efficiency << "%" << std::endl;
    }

    // Check for thermal throttling
    if (IsThermalThrottlingDetected()) {
        std::cout << "Thermal throttling detected on GPU " << gpu_id_ << std::endl;
    }
}

// NVML helpers
bool CudaMetricsCollector::InitializeNvml() {
#ifdef HAS_NVML
    nvmlReturn_t result = nvmlInit();
    if (result == NVML_SUCCESS) {
        result = nvmlDeviceGetHandleByIndex(gpu_id_, &nvml_device_);
        if (result == NVML_SUCCESS) {
            nvml_initialized_ = true;
            std::cout << "NVML initialized for GPU " << gpu_id_ << std::endl;
            return true;
        }
    }

    std::cerr << "Failed to initialize NVML for GPU " << gpu_id_ << " (fallback mode enabled)" << std::endl;
#else
    std::cout << "NVML not available - using fallback metrics collection for GPU " << gpu_id_ << std::endl;
#endif
    return false;
}

void CudaMetricsCollector::ShutdownNvml() {
#ifdef HAS_NVML
    if (nvml_initialized_) {
        nvmlShutdown();
        nvml_initialized_ = false;
    }
#endif
}

double CudaMetricsCollector::GetGpuUtilization() const {
#ifdef HAS_NVML
    if (nvml_initialized_) {
        nvmlUtilization_t utilization;
        nvmlReturn_t result = nvmlDeviceGetUtilizationRates(nvml_device_, &utilization);
        if (result == NVML_SUCCESS) {
            return static_cast<double>(utilization.gpu);
        }
    }
#endif
    // Fallback: use CUDA runtime to estimate utilization
    // This is a simplified estimation
    return 0.0;  // Would be implemented with CUDA events in a real system
}

double CudaMetricsCollector::GetMemoryUtilization() const {
#ifdef HAS_NVML
    if (nvml_initialized_) {
        nvmlUtilization_t utilization;
        nvmlReturn_t result = nvmlDeviceGetUtilizationRates(nvml_device_, &utilization);
        if (result == NVML_SUCCESS) {
            return static_cast<double>(utilization.memory);
        }
    }
#endif
    // Fallback: use CUDA runtime memory info
    size_t free_mem, total_mem;
    if (cudaMemGetInfo(&free_mem, &total_mem) == cudaSuccess) {
        size_t used_mem = total_mem - free_mem;
        return (static_cast<double>(used_mem) / total_mem) * 100.0;
    }
    return 0.0;
}

double CudaMetricsCollector::GetMemoryBandwidthUtilization() const {
    // This is a simplified estimation
    // In a real implementation, we'd use NVML bandwidth counters or CUDA profiling
    double memory_util = GetMemoryUtilization();
    return memory_util * 0.8;  // Rough estimate
}

double CudaMetricsCollector::GetTemperature() const {
#ifdef HAS_NVML
    if (nvml_initialized_) {
        unsigned int temp;
        nvmlReturn_t result = nvmlDeviceGetTemperature(nvml_device_, NVML_TEMPERATURE_GPU, &temp);
        if (result == NVML_SUCCESS) {
            return static_cast<double>(temp);
        }
    }
#endif
    // Fallback: return a reasonable default temperature
    return 30.0;  // Default temperature estimate
}

double CudaMetricsCollector::GetPowerUsage() const {
#ifdef HAS_NVML
    if (nvml_initialized_) {
        unsigned int power;
        nvmlReturn_t result = nvmlDeviceGetPowerUsage(nvml_device_, &power);
        if (result == NVML_SUCCESS) {
            return static_cast<double>(power) / 1000.0;  // Convert mW to W
        }
    }
#endif
    // Fallback: estimate power based on utilization
    double utilization = GetGpuUtilization();
    return 37.0 + (utilization / 100.0) * 200.0;  // Rough estimate: 37W idle + scaling
}

bool CudaMetricsCollector::IsThermalThrottling() const {
    double temp = GetTemperature();
    return temp > 85.0;  // Assume 85°C as throttling threshold
}

// Metric helpers
std::chrono::milliseconds CudaMetricsCollector::GetIntervalDuration(CollectionInterval interval) const {
    switch (interval) {
        case CollectionInterval::Realtime: return std::chrono::seconds(1);
        case CollectionInterval::Frequent: return std::chrono::seconds(10);
        case CollectionInterval::Regular: return std::chrono::minutes(1);
        case CollectionInterval::Periodic: return std::chrono::minutes(5);
        case CollectionInterval::OnDemand: return std::chrono::minutes(10);
        default: return std::chrono::seconds(10);
    }
}

bool CudaMetricsCollector::ShouldCollectMetric(const std::string& name) const {
    auto it = metric_intervals_.find(name);
    if (it != metric_intervals_.end()) {
        // Simple time-based collection logic
        // In a real implementation, this would be more sophisticated
        return true;
    }
    return false;
}

void CudaMetricsCollector::TrimOldData() {
    if (metrics_data_.size() > max_history_size_) {
        size_t excess = metrics_data_.size() - max_history_size_;
        metrics_data_.erase(metrics_data_.begin(), metrics_data_.begin() + excess);
    }
}

void CudaMetricsCollector::UpdatePerformanceStatistics(double throughput_sample) {
    cumulative_stats_.total_operations++;

    // Update efficiency metrics
    if (target_throughput_mkeys_per_sec_ > 0.0) {
        cumulative_stats_.gpu_efficiency_percent =
            (throughput_sample / target_throughput_mkeys_per_sec_) * 100.0;
    }

    // Update power efficiency if power data is available
    auto power_metric = GetRealtimeMetrics().find("gpu_power");
    if (power_metric != GetRealtimeMetrics().end() && power_metric->second > 0.0) {
        cumulative_stats_.power_efficiency_mkeys_per_watt =
            throughput_sample / power_metric->second;
    }
}

// Export helpers
std::string CudaMetricsCollector::ExportToJson() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    json export_data;
    export_data["gpu_id"] = gpu_id_;
    export_data["export_timestamp"] = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    // Export metrics
    json metrics_array = json::array();
    for (const auto& metric : metrics_data_) {
        json metric_obj;
        metric_obj["name"] = metric.name;
        metric_obj["value"] = metric.value;
        metric_obj["unit"] = metric.unit;
        metric_obj["timestamp"] = std::chrono::duration<double>(
            metric.timestamp.time_since_epoch()).count();
        metric_obj["metadata"] = metric.metadata;
        metrics_array.push_back(metric_obj);
    }
    export_data["metrics"] = metrics_array;

    // Export statistics
    json stats_obj;
    stats_obj["peak_throughput_mkeys_per_sec"] = cumulative_stats_.peak_throughput_mkeys_per_sec;
    stats_obj["mean_throughput_mkeys_per_sec"] = cumulative_stats_.mean_throughput_mkeys_per_sec;
    stats_obj["total_errors"] = cumulative_stats_.total_errors;
    stats_obj["total_operations"] = cumulative_stats_.total_operations;
    stats_obj["error_rate_percent"] = cumulative_stats_.error_rate_percent;
    stats_obj["gpu_efficiency_percent"] = cumulative_stats_.gpu_efficiency_percent;
    export_data["statistics"] = stats_obj;

    return export_data.dump(2);
}

std::string CudaMetricsCollector::ExportToCsv() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);

    std::ostringstream csv;
    csv << "timestamp,name,value,unit,metadata\n";

    for (const auto& metric : metrics_data_) {
        csv << std::chrono::duration<double>(metric.timestamp.time_since_epoch()).count()
            << "," << metric.name << "," << metric.value << "," << metric.unit << ","
            << "\"" << metric.metadata.dump() << "\"\n";
    }

    return csv.str();
}

} // namespace puzzle71::gpu::performance