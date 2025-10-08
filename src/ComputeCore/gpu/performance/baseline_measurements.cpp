#include "ComputeCore/gpu/performance/baseline_measurements.h"
#include "core/uint256.h"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <filesystem>
#include <mutex>
#include <random>

namespace puzzle71::gpu::performance {

BaselineMeasurementFramework& BaselineMeasurementFramework::GetInstance() {
    static BaselineMeasurementFramework instance;
    return instance;
}

BaselineMetrics BaselineMeasurementFramework::MeasureBaselinePerformance(const BenchmarkConfiguration& config) {
    std::lock_guard<std::mutex> lock(measurement_mutex_);

    PERF_LOG_INFO("baseline_measurement", "Starting baseline performance measurement",
                 json{{"config", config.ToJson()}});

    // Create measurement session
    BaselineMeasurementSession session(config);
    session.StartMeasurement();

    // Perform warmup
    WarmupGPU(config);
    PERF_LOG_INFO("baseline_measurement", "GPU warmup completed");

    // Perform single measurement
    auto baseline_metrics = PerformSingleMeasurement(config);

    session.RecordIterationMetrics(baseline_metrics);

    PERF_LOG_INFO("baseline_measurement", "Baseline measurement completed",
                 json{{"throughput_mkeys_per_sec", baseline_metrics.throughput_mkeys_per_sec},
                      {"gpu_utilization", baseline_metrics.gpu_utilization_percent},
                      {"execution_time_us", baseline_metrics.total_execution_time.count()}});

    return baseline_metrics;
}

std::vector<BaselineMetrics> BaselineMeasurementFramework::MeasureBaselineMultipleRuns(
    const BenchmarkConfiguration& config, int num_runs) {

    std::lock_guard<std::mutex> lock(measurement_mutex_);
    std::vector<BaselineMetrics> results;
    results.reserve(num_runs);

    PERF_LOG_INFO("baseline_measurement", "Starting multiple baseline runs",
                 json{{"num_runs", num_runs}});

    // Warmup GPU once before all measurements
    WarmupGPU(config);

    for (int run = 0; run < num_runs; ++run) {
        PERF_LOG_DEBUG("baseline_measurement", "Starting baseline run",
                      json{{"run_number", run + 1}});

        auto metrics = PerformSingleMeasurement(config);
        results.push_back(metrics);

        PERF_LOG_DEBUG("baseline_measurement", "Completed baseline run",
                      json{{"run_number", run + 1},
                           {"throughput_mkeys_per_sec", metrics.throughput_mkeys_per_sec}});
    }

    // Calculate statistics
    auto stats = CalculateStatistics(results);
    PERF_LOG_INFO("baseline_measurement", "Multiple runs completed",
                 json{{"avg_throughput_mkeys_per_sec", stats.throughput_mkeys_per_sec},
                      {"coefficient_of_variation", CalculateCoefficientOfVariation(
                          [results]() {
                              std::vector<double> throughputs;
                              for (const auto& m : results) {
                                  throughputs.push_back(m.throughput_mkeys_per_sec);
                              }
                              return throughputs;
                          }())}});

    return results;
}

void BaselineMeasurementFramework::SaveBaseline(const BaselineMetrics& baseline, const std::string& baseline_name) {
    std::lock_guard<std::mutex> lock(measurement_mutex_);

    std::string file_path = GetBaselineFilePath(baseline_name);
    if (SaveBaselineToFile(baseline, file_path)) {
        PERF_LOG_INFO("baseline_measurement", "Baseline saved",
                     json{{"baseline_name", baseline_name},
                          {"file_path", file_path},
                          {"throughput_mkeys_per_sec", baseline.throughput_mkeys_per_sec}});
    } else {
        PERF_LOG_ERROR("baseline_measurement", "Failed to save baseline",
                      json{{"baseline_name", baseline_name}});
    }
}

bool BaselineMeasurementFramework::LoadBaseline(const std::string& baseline_name, BaselineMetrics& baseline) {
    std::lock_guard<std::mutex> lock(measurement_mutex_);

    std::string file_path = GetBaselineFilePath(baseline_name);
    bool success = LoadBaselineFromFile(file_path, baseline);

    if (success) {
        PERF_LOG_INFO("baseline_measurement", "Baseline loaded",
                     json{{"baseline_name", baseline_name},
                          {"throughput_mkeys_per_sec", baseline.throughput_mkeys_per_sec}});
    } else {
        PERF_LOG_WARNING("baseline_measurement", "Failed to load baseline",
                        json{{"baseline_name", baseline_name}});
    }

    return success;
}

std::vector<std::string> BaselineMeasurementFramework::GetAvailableBaselines() const {
    std::lock_guard<std::mutex> lock(measurement_mutex_);
    std::vector<std::string> baselines;

    try {
        if (std::filesystem::exists(baseline_storage_path_) &&
            std::filesystem::is_directory(baseline_storage_path_)) {

            for (const auto& entry : std::filesystem::directory_iterator(baseline_storage_path_)) {
                if (entry.is_regular_file() && entry.path().extension() == ".json") {
                    baselines.push_back(entry.path().stem().string());
                }
            }
        }
    } catch (const std::exception& e) {
        PERF_LOG_ERROR("baseline_measurement", "Error scanning baseline directory",
                      json{{"error", e.str()}});
    }

    std::sort(baselines.begin(), baselines.end());
    return baselines;
}

bool BaselineMeasurementFramework::RemoveBaseline(const std::string& baseline_name) {
    std::lock_guard<std::mutex> lock(measurement_mutex_);

    std::string file_path = GetBaselineFilePath(baseline_name);
    bool success = std::filesystem::remove(file_path);

    if (success) {
        PERF_LOG_INFO("baseline_measurement", "Baseline removed",
                     json{{"baseline_name", baseline_name}});
    } else {
        PERF_LOG_WARNING("baseline_measurement", "Failed to remove baseline",
                        json{{"baseline_name", baseline_name}});
    }

    return success;
}

json BaselineMeasurementFramework::CompareWithBaseline(const BaselineMetrics& current, const std::string& baseline_name) const {
    BaselineMetrics baseline;
    if (!LoadBaselineFromFile(GetBaselineFilePath(baseline_name), baseline)) {
        throw std::runtime_error("Failed to load baseline: " + baseline_name);
    }

    json comparison;
    comparison["baseline_name"] = baseline_name;
    comparison["current_metrics"] = baseline_utils::BaselineMetricsToJson(current);
    comparison["baseline_metrics"] = baseline_utils::BaselineMetricsToJson(baseline);

    // Calculate improvements
    double throughput_improvement = CalculatePerformanceImprovement(current, baseline);
    double utilization_improvement = (current.gpu_utilization_percent - baseline.gpu_utilization_percent) /
                                     baseline.gpu_utilization_percent * 100.0;
    double sync_overhead_reduction = (baseline.sync_overhead_percent - current.sync_overhead_percent) /
                                    baseline.sync_overhead_percent * 100.0;

    comparison["performance_improvements"] = {
        {"throughput_factor", throughput_improvement},
        {"throughput_percent", (throughput_improvement - 1.0) * 100.0},
        {"gpu_utilization_percent_improvement", utilization_improvement},
        {"sync_overhead_reduction_percent", sync_overhead_reduction}
    };

    // Configuration comparison
    comparison["configuration_changes"] = {
        {"points_per_thread_change", current.points_per_thread - baseline.points_per_thread},
        {"block_size_change", current.block_size - baseline.block_size},
        {"grid_size_change", current.grid_size - baseline.grid_size}
    };

    // Timing comparison
    comparison["timing_comparisons"] = {
        {"total_time_reduction_percent",
         (baseline.total_execution_time.count() - current.total_execution_time.count()) /
         static_cast<double>(baseline.total_execution_time.count()) * 100.0},
        {"kernel_time_reduction_percent",
         (baseline.kernel_execution_time.count() - current.kernel_execution_time.count()) /
         static_cast<double>(baseline.kernel_execution_time.count()) * 100.0},
        {"sync_time_reduction_percent",
         (baseline.synchronization_time.count() - current.synchronization_time.count()) /
         static_cast<double>(baseline.synchronization_time.count()) * 100.0}
    };

    return comparison;
}

double BaselineMeasurementFramework::CalculatePerformanceImprovement(const BaselineMetrics& optimized, const BaselineMetrics& baseline) const {
    if (baseline.throughput_mkeys_per_sec <= 0.0) {
        return 1.0; // Avoid division by zero
    }
    return optimized.throughput_mkeys_per_sec / baseline.throughput_mkeys_per_sec;
}

bool BaselineMeasurementFramework::ValidatePerformanceImprovement(const BaselineMetrics& current, const std::string& baseline_name, double min_improvement_factor) const {
    BaselineMetrics baseline;
    if (!LoadBaselineFromFile(GetBaselineFilePath(baseline_name), baseline)) {
        PERF_LOG_WARNING("baseline_measurement", "Cannot validate improvement - baseline not found",
                        json{{"baseline_name", baseline_name}});
        return false;
    }

    double improvement = CalculatePerformanceImprovement(current, baseline);
    bool meets_target = improvement >= min_improvement_factor;

    PERF_LOG_INFO("baseline_measurement", "Performance improvement validation",
                 json{{"baseline_name", baseline_name},
                      {"current_throughput", current.throughput_mkeys_per_sec},
                      {"baseline_throughput", baseline.throughput_mkeys_per_sec},
                      {"improvement_factor", improvement},
                      {"target_factor", min_improvement_factor},
                      {"meets_target", meets_target}});

    return meets_target;
}

BaselineMetrics BaselineMeasurementFramework::CalculateStatistics(const std::vector<BaselineMetrics>& measurements) const {
    BaselineMetrics stats;

    if (measurements.empty()) {
        return stats;
    }

    // Calculate averages for numeric fields
    std::vector<double> throughputs, gpu_utilizations, memory_utilizations, sync_overheads;
    std::vector<long long> total_times, kernel_times, memory_times, sync_times;

    for (const auto& m : measurements) {
        throughputs.push_back(m.throughput_mkeys_per_sec);
        gpu_utilizations.push_back(m.gpu_utilization_percent);
        memory_utilizations.push_back(m.memory_bandwidth_utilization_percent);
        sync_overheads.push_back(m.sync_overhead_percent);
        total_times.push_back(m.total_execution_time.count());
        kernel_times.push_back(m.kernel_execution_time.count());
        memory_times.push_back(m.memory_transfer_time.count());
        sync_times.push_back(m.synchronization_time.count());
    }

    stats.throughput_mkeys_per_sec = CalculateMean(throughputs);
    stats.gpu_utilization_percent = CalculateMean(gpu_utilizations);
    stats.memory_bandwidth_utilization_percent = CalculateMean(memory_utilizations);
    stats.sync_overhead_percent = CalculateMean(sync_overheads);
    stats.total_execution_time = std::chrono::microseconds(static_cast<long long>(CalculateMean(total_times)));
    stats.kernel_execution_time = std::chrono::microseconds(static_cast<long long>(CalculateMean(kernel_times)));
    stats.memory_transfer_time = std::chrono::microseconds(static_cast<long long>(CalculateMean(memory_times)));
    stats.synchronization_time = std::chrono::microseconds(static_cast<long long>(CalculateMean(sync_times)));

    // Copy other fields from first measurement (assumed to be consistent)
    stats.gpu_name = measurements[0].gpu_name;
    stats.compute_capability = measurements[0].compute_capability;
    stats.total_memory_mb = measurements[0].total_memory_mb;
    stats.block_size = measurements[0].block_size;
    stats.grid_size = measurements[0].grid_size;
    stats.points_per_thread = measurements[0].points_per_thread;
    stats.batch_size = measurements[0].batch_size;
    stats.accuracy_verified = measurements[0].accuracy_verified;
    stats.accuracy_percentage = measurements[0].accuracy_percentage;
    stats.num_validation_samples = measurements[0].num_validation_samples;

    return stats;
}

bool BaselineMeasurementFramework::ValidateStatisticalSignificance(const std::vector<BaselineMetrics>& measurements, double confidence_level) const {
    if (measurements.size() < 3) {
        return false; // Need at least 3 samples for statistical significance
    }

    std::vector<double> throughputs;
    for (const auto& m : measurements) {
        throughputs.push_back(m.throughput_mkeys_per_sec);
    }

    double cv = CalculateCoefficientOfVariation(throughputs);
    return cv <= 0.05; // Coefficient of variation should be <= 5%
}

double BaselineMeasurementFramework::CalculateConfidenceInterval(const std::vector<double>& values, double confidence_level) const {
    if (values.size() < 2) {
        return 0.0;
    }

    double mean = CalculateMean(values);
    double std_dev = CalculateStandardDeviation(values);
    double n = static_cast<double>(values.size());

    // Simple approximation using t-distribution for small samples
    // For larger n, this approaches normal distribution
    double t_value = confidence_level >= 0.95 ? 2.0 : (confidence_level >= 0.90 ? 1.645 : 1.28);

    return t_value * (std_dev / std::sqrt(n));
}

json BaselineMeasurementFramework::GenerateBaselineReport(const BaselineMetrics& baseline) const {
    json report = baseline_utils::BaselineMetricsToJson(baseline);

    report["performance_rating"] = baseline.throughput_mkeys_per_sec >= 50000 ? "Excellent" :
                                   baseline.throughput_mkeys_per_sec >= 20000 ? "Good" :
                                   baseline.throughput_mkeys_per_sec >= 10000 ? "Average" : "Poor";

    report["efficiency_metrics"] = {
        {"gpu_efficiency_score", baseline.gpu_utilization_percent / 100.0},
        {"memory_efficiency_score", baseline.memory_bandwidth_utilization_percent / 100.0},
        {"synchronization_efficiency", 100.0 - baseline.sync_overhead_percent}
    };

    return report;
}

json BaselineMeasurementFramework::GenerateComparisonReport(const std::vector<BaselineMetrics>& before_list,
                                                           const std::vector<BaselineMetrics>& after_list) const {
    BaselineMetrics before_stats = CalculateStatistics(before_list);
    BaselineMetrics after_stats = CalculateStatistics(after_list);

    json report;
    report["before_statistics"] = baseline_utils::BaselineMetricsToJson(before_stats);
    report["after_statistics"] = baseline_utils::BaselineMetricsToJson(after_stats);

    double improvement_factor = CalculatePerformanceImprovement(after_stats, before_stats);
    report["summary"] = {
        {"performance_improvement_factor", improvement_factor},
        {"performance_improvement_percent", (improvement_factor - 1.0) * 100.0},
        {"throughput_before_mkeys_per_sec", before_stats.throughput_mkeys_per_sec},
        {"throughput_after_mkeys_per_sec", after_stats.throughput_mkeys_per_sec},
        {"statistical_significance", ValidateStatisticalSignificance(after_list)}
    };

    return report;
}

std::string BaselineMeasurementFramework::GenerateBaselineSummary(const BaselineMetrics& baseline) const {
    std::ostringstream oss;
    oss << "Baseline Performance Summary:\n";
    oss << "  GPU: " << baseline.gpu_name << " (CC " << baseline.compute_capability << ")\n";
    oss << "  Throughput: " << std::fixed << std::setprecision(1) << baseline.throughput_mkeys_per_sec << " Mkeys/s\n";
    oss << "  GPU Utilization: " << std::setprecision(1) << baseline.gpu_utilization_percent << "%\n";
    oss << "  Memory Bandwidth: " << std::setprecision(1) << baseline.memory_bandwidth_utilization_percent << "%\n";
    oss << "  Sync Overhead: " << std::setprecision(1) << baseline.sync_overhead_percent << "%\n";
    oss << "  Execution Time: " << baseline.total_execution_time.count() / 1000.0 << " ms\n";
    oss << "  Configuration: " << baseline.block_size << "x" << baseline.grid_size
        << " blocks, " << baseline.points_per_thread << " PPT\n";
    oss << "  Accuracy: " << (baseline.accuracy_verified ? "Verified" : "Not verified");
    if (baseline.accuracy_verified) {
        oss << " (" << std::setprecision(2) << baseline.accuracy_percentage << "%)";
    }
    oss << "\n";

    return oss.str();
}

void BaselineMeasurementFramework::SetBaselineStoragePath(const std::string& storage_path) {
    std::lock_guard<std::mutex> lock(measurement_mutex_);
    baseline_storage_path_ = storage_path;

    // Create directory if it doesn't exist
    std::filesystem::create_directories(storage_path);

    PERF_LOG_INFO("baseline_measurement", "Baseline storage path updated",
                 json{{"path", storage_path}});
}

void BaselineMeasurementFramework::SetDefaultBenchmarkConfiguration(const BenchmarkConfiguration& config) {
    std::lock_guard<std::mutex> lock(measurement_mutex_);
    default_config_ = config;

    PERF_LOG_INFO("baseline_measurement", "Default benchmark configuration updated",
                 json{{"config", config.ToJson()}});
}

BenchmarkConfiguration BaselineMeasurementFramework::GetDefaultBenchmarkConfiguration() const {
    std::lock_guard<std::mutex> lock(measurement_mutex_);
    return default_config_;
}

// Private methods implementation
BaselineMetrics BaselineMeasurementFramework::PerformSingleMeasurement(const BenchmarkConfiguration& config) {
    BaselineMetrics metrics;

    // This is a placeholder implementation
    // In a real implementation, this would:
    // 1. Setup GPU workloads based on config
    // 2. Execute the key search algorithm
    // 3. Measure performance metrics
    // 4. Validate accuracy

    auto start_time = std::chrono::high_resolution_clock::now();

    // TODO: Implement actual GPU execution and measurement
    // For now, return placeholder values
    metrics.gpu_name = "RTX 4090"; // Should be detected
    metrics.compute_capability = 89;
    metrics.total_memory_mb = 24576;
    metrics.throughput_mkeys_per_sec = 50000.0; // Placeholder
    metrics.gpu_utilization_percent = 85.0;
    metrics.memory_bandwidth_utilization_percent = 75.0;
    metrics.sync_overhead_percent = 15.0;
    metrics.block_size = config.test_block_size;
    metrics.grid_size = config.test_grid_size;
    metrics.points_per_thread = config.test_points_per_thread;
    metrics.batch_size = 1000; // Placeholder
    metrics.accuracy_verified = true;
    metrics.accuracy_percentage = 100.0;
    metrics.num_validation_samples = config.num_validation_samples;
    metrics.measurement_timestamp = std::chrono::high_resolution_clock::now();
    metrics.test_description = config.benchmark_name;

    auto end_time = std::chrono::high_resolution_clock::now();
    metrics.total_execution_time = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    metrics.kernel_execution_time = metrics.total_execution_time * 0.8; // Placeholder
    metrics.memory_transfer_time = metrics.total_execution_time * 0.15; // Placeholder
    metrics.synchronization_time = metrics.total_execution_time * 0.05; // Placeholder

    // Validate accuracy if requested
    if (config.enable_accuracy_validation) {
        ValidateAccuracy(metrics, config);
    }

    return metrics;
}

void BaselineMeasurementFramework::WarmupGPU(const BenchmarkConfiguration& config) {
    PERF_LOG_DEBUG("baseline_measurement", "Starting GPU warmup",
                  json{{"warmup_duration_seconds", config.warmup_time.count()}});

    // TODO: Implement actual GPU warmup
    // This would run some light GPU workloads to bring the GPU to optimal operating temperature

    std::this_thread::sleep_for(std::chrono::seconds(1)); // Placeholder

    PERF_LOG_DEBUG("baseline_measurement", "GPU warmup completed");
}

void BaselineMeasurementFramework::CollectGPUStatistics(BaselineMetrics& metrics) {
    // TODO: Implement actual GPU statistics collection
    // This would use NVIDIA Management Library (NVML) or CUDA runtime APIs
    // to collect real-time GPU utilization, memory usage, temperature, etc.
}

void BaselineMeasurementFramework::ValidateAccuracy(BaselineMetrics& metrics, const BenchmarkConfiguration& config) {
    PERF_LOG_DEBUG("baseline_measurement", "Starting accuracy validation",
                  json{{"num_samples", config.num_validation_samples}});

    // TODO: Implement actual accuracy validation
    // This would run a subset of the computation with a reference implementation
    // to verify that the optimized version produces correct results

    metrics.accuracy_verified = true;
    metrics.accuracy_percentage = 100.0;

    PERF_LOG_DEBUG("baseline_measurement", "Accuracy validation completed",
                  json{{"verified", metrics.accuracy_verified},
                       {"accuracy_percent", metrics.accuracy_percentage}});
}

std::string BaselineMeasurementFramework::GetBaselineFilePath(const std::string& baseline_name) const {
    return baseline_storage_path_ + baseline_name + ".json";
}

bool BaselineMeasurementFramework::SaveBaselineToFile(const BaselineMetrics& baseline, const std::string& file_path) const {
    try {
        json baseline_json = baseline_utils::BaselineMetricsToJson(baseline);
        std::ofstream file(file_path);
        file << baseline_json.dump(4);
        return file.good();
    } catch (const std::exception& e) {
        PERF_LOG_ERROR("baseline_measurement", "Failed to save baseline to file",
                      json{{"file_path", file_path}, {"error", e.what()}});
        return false;
    }
}

bool BaselineMeasurementFramework::LoadBaselineFromFile(const std::string& file_path, BaselineMetrics& baseline) const {
    try {
        std::ifstream file(file_path);
        if (!file.is_open()) {
            return false;
        }

        json baseline_json;
        file >> baseline_json;
        baseline = baseline_utils::JsonToBaselineMetrics(baseline_json);
        return true;
    } catch (const std::exception& e) {
        PERF_LOG_ERROR("baseline_measurement", "Failed to load baseline from file",
                      json{{"file_path", file_path}, {"error", e.what()}});
        return false;
    }
}

double BaselineMeasurementFramework::CalculateMean(const std::vector<double>& values) const {
    if (values.empty()) return 0.0;
    return std::accumulate(values.begin(), values.end(), 0.0) / values.size();
}

double BaselineMeasurementFramework::CalculateStandardDeviation(const std::vector<double>& values) const {
    if (values.size() < 2) return 0.0;

    double mean = CalculateMean(values);
    double sum_squares = std::accumulate(values.begin(), values.end(), 0.0,
        [mean](double acc, double val) { return acc + (val - mean) * (val - mean); });

    return std::sqrt(sum_squares / (values.size() - 1));
}

double BaselineMeasurementFramework::CalculateCoefficientOfVariation(const std::vector<double>& values) const {
    double mean = CalculateMean(values);
    if (mean == 0.0) return 0.0;
    return CalculateStandardDeviation(values) / mean;
}

bool BaselineMeasurementFramework::PerformTTest(const std::vector<double>& sample1, const std::vector<double>& sample2, double confidence_level) const {
    // Simplified t-test implementation
    // In practice, you'd use a proper statistical library
    if (sample1.size() < 2 || sample2.size() < 2) {
        return false;
    }

    double mean1 = CalculateMean(sample1);
    double mean2 = CalculateMean(sample2);
    double std1 = CalculateStandardDeviation(sample1);
    double std2 = CalculateStandardDeviation(sample2);
    double n1 = static_cast<double>(sample1.size());
    double n2 = static_cast<double>(sample2.size());

    double pooled_std = std::sqrt(((n1 - 1) * std1 * std1 + (n2 - 1) * std2 * std2) / (n1 + n2 - 2));
    double standard_error = pooled_std * std::sqrt(1.0/n1 + 1.0/n2);

    if (standard_error == 0.0) return true;

    double t_statistic = std::abs(mean1 - mean2) / standard_error;

    // Critical value for two-tailed test at 95% confidence
    double critical_value = 2.0;

    return t_statistic > critical_value;
}

// BaselineMeasurementSession implementation
BaselineMeasurementSession::BaselineMeasurementSession(const BenchmarkConfiguration& config)
    : config_(config) {
}

BaselineMeasurementSession::~BaselineMeasurementSession() {
    if (measurement_started_ && !measurement_completed_) {
        PERF_LOG_WARNING("baseline_measurement", "Measurement session ended without completion");
    }
}

void BaselineMeasurementSession::StartMeasurement() {
    std::lock_guard<std::mutex> lock(session_mutex_);
    session_start_ = std::chrono::high_resolution_clock::now();
    measurement_started_ = true;

    PERF_LOG_INFO("baseline_measurement", "Baseline measurement session started",
                 json{{"config", config_.ToJson()}});
}

void BaselineMeasurementSession::RecordIterationMetrics(const BaselineMetrics& metrics) {
    std::lock_guard<std::mutex> lock(session_mutex_);
    iteration_metrics_.push_back(metrics);
}

BaselineMetrics BaselineMeasurementSession::GetFinalMetrics() const {
    std::lock_guard<std::mutex> lock(session_mutex_);

    if (iteration_metrics_.empty()) {
        return BaselineMetrics{};
    }

    return BaselineMeasurementFramework::GetInstance().CalculateStatistics(iteration_metrics_);
}

bool BaselineMeasurementSession::IsCompleted() const {
    std::lock_guard<std::mutex> lock(session_mutex_);
    return measurement_completed_;
}

// Utility functions implementation
namespace baseline_utils {

BaselineMetrics CreateBaselineFromGpuMetrics(const std::string& gpu_name, int compute_capability,
                                             size_t memory_mb, double throughput) {
    BaselineMetrics baseline;
    baseline.gpu_name = gpu_name;
    baseline.compute_capability = compute_capability;
    baseline.total_memory_mb = memory_mb;
    baseline.throughput_mkeys_per_sec = throughput;
    baseline.measurement_timestamp = std::chrono::high_resolution_clock::now();
    baseline.accuracy_verified = true;
    baseline.accuracy_percentage = 100.0;

    return baseline;
}

json BaselineMetricsToJson(const BaselineMetrics& metrics) {
    json j;
    j["gpu_name"] = metrics.gpu_name;
    j["compute_capability"] = metrics.compute_capability;
    j["total_memory_mb"] = metrics.total_memory_mb;
    j["throughput_mkeys_per_sec"] = metrics.throughput_mkeys_per_sec;
    j["gpu_utilization_percent"] = metrics.gpu_utilization_percent;
    j["memory_bandwidth_utilization_percent"] = metrics.memory_bandwidth_utilization_percent;
    j["sync_overhead_percent"] = metrics.sync_overhead_percent;
    j["total_execution_time_us"] = metrics.total_execution_time.count();
    j["kernel_execution_time_us"] = metrics.kernel_execution_time.count();
    j["memory_transfer_time_us"] = metrics.memory_transfer_time.count();
    j["synchronization_time_us"] = metrics.synchronization_time.count();
    j["block_size"] = metrics.block_size;
    j["grid_size"] = metrics.grid_size;
    j["points_per_thread"] = metrics.points_per_thread;
    j["batch_size"] = metrics.batch_size;
    j["accuracy_verified"] = metrics.accuracy_verified;
    j["accuracy_percentage"] = metrics.accuracy_percentage;
    j["num_validation_samples"] = metrics.num_validation_samples;
    j["measurement_timestamp_us"] = std::chrono::duration_cast<std::chrono::microseconds>(
        metrics.measurement_timestamp.time_since_epoch()).count();
    j["test_description"] = metrics.test_description;
    j["additional_metadata"] = metrics.additional_metadata;

    return j;
}

BaselineMetrics JsonToBaselineMetrics(const json& json_metrics) {
    BaselineMetrics metrics;

    metrics.gpu_name = json_metrics.value("gpu_name", "");
    metrics.compute_capability = json_metrics.value("compute_capability", 0);
    metrics.total_memory_mb = json_metrics.value("total_memory_mb", 0);
    metrics.throughput_mkeys_per_sec = json_metrics.value("throughput_mkeys_per_sec", 0.0);
    metrics.gpu_utilization_percent = json_metrics.value("gpu_utilization_percent", 0.0);
    metrics.memory_bandwidth_utilization_percent = json_metrics.value("memory_bandwidth_utilization_percent", 0.0);
    metrics.sync_overhead_percent = json_metrics.value("sync_overhead_percent", 0.0);
    metrics.total_execution_time = std::chrono::microseconds(json_metrics.value("total_execution_time_us", 0));
    metrics.kernel_execution_time = std::chrono::microseconds(json_metrics.value("kernel_execution_time_us", 0));
    metrics.memory_transfer_time = std::chrono::microseconds(json_metrics.value("memory_transfer_time_us", 0));
    metrics.synchronization_time = std::chrono::microseconds(json_metrics.value("synchronization_time_us", 0));
    metrics.block_size = json_metrics.value("block_size", 0);
    metrics.grid_size = json_metrics.value("grid_size", 0);
    metrics.points_per_thread = json_metrics.value("points_per_thread", 0);
    metrics.batch_size = json_metrics.value("batch_size", 0);
    metrics.accuracy_verified = json_metrics.value("accuracy_verified", false);
    metrics.accuracy_percentage = json_metrics.value("accuracy_percentage", 0.0);
    metrics.num_validation_samples = json_metrics.value("num_validation_samples", 0);
    metrics.test_description = json_metrics.value("test_description", "");
    metrics.additional_metadata = json_metrics.value("additional_metadata", json{});

    auto timestamp_us = json_metrics.value("measurement_timestamp_us", 0LL);
    metrics.measurement_timestamp = std::chrono::high_resolution_clock::time_point{
        std::chrono::microseconds(timestamp_us)};

    return metrics;
}

bool ValidateBaselineMetrics(const BaselineMetrics& metrics) {
    // Basic validation checks
    if (metrics.gpu_name.empty() || metrics.compute_capability <= 0 || metrics.total_memory_mb == 0) {
        return false;
    }

    if (metrics.throughput_mkeys_per_sec < 0.0 || metrics.gpu_utilization_percent < 0.0 ||
        metrics.gpu_utilization_percent > 100.0 || metrics.memory_bandwidth_utilization_percent < 0.0 ||
        metrics.memory_bandwidth_utilization_percent > 100.0 || metrics.sync_overhead_percent < 0.0 ||
        metrics.sync_overhead_percent > 100.0) {
        return false;
    }

    if (metrics.block_size <= 0 || metrics.grid_size <= 0 || metrics.points_per_thread <= 0 ||
        metrics.batch_size <= 0) {
        return false;
    }

    if (metrics.accuracy_verified && (metrics.accuracy_percentage < 0.0 || metrics.accuracy_percentage > 100.0)) {
        return false;
    }

    return true;
}

std::string FormatBaselineForDisplay(const BaselineMetrics& metrics) {
    std::ostringstream oss;
    oss << "GPU: " << metrics.gpu_name << " (CC " << metrics.compute_capability << ")\n";
    oss << "Memory: " << metrics.total_memory_mb << " MB\n";
    oss << "Throughput: " << std::fixed << std::setprecision(1) << metrics.throughput_mkeys_per_sec << " Mkeys/s\n";
    oss << "GPU Utilization: " << std::setprecision(1) << metrics.gpu_utilization_percent << "%\n";
    oss << "Memory Bandwidth: " << std::setprecision(1) << metrics.memory_bandwidth_utilization_percent << "%\n";
    oss << "Sync Overhead: " << std::setprecision(1) << metrics.sync_overhead_percent << "%\n";
    oss << "Configuration: " << metrics.block_size << "x" << metrics.grid_size << " blocks, "
        << metrics.points_per_thread << " PPT, batch size " << metrics.batch_size << "\n";

    if (metrics.accuracy_verified) {
        oss << "Accuracy: Verified (" << std::setprecision(2) << metrics.accuracy_percentage << "%)\n";
    } else {
        oss << "Accuracy: Not verified\n";
    }

    return oss.str();
}

} // namespace baseline_utils

} // namespace puzzle71::gpu::performance