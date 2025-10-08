#pragma once

#include <chrono>
#include <vector>
#include <memory>
#include <string>
#include <functional>
#include <nlohmann/json.hpp>
#include "ComputeCore/gpu/performance/performance_logger.h"

namespace puzzle71::gpu::performance {

using json = nlohmann::json;

struct BaselineMetrics {
    std::string gpu_name;
    int compute_capability;
    size_t total_memory_mb;

    // Performance metrics
    double throughput_mkeys_per_sec;
    double gpu_utilization_percent;
    double memory_bandwidth_utilization_percent;
    double sync_overhead_percent;

    // Timing metrics
    std::chrono::microseconds total_execution_time;
    std::chrono::microseconds kernel_execution_time;
    std::chrono::microseconds memory_transfer_time;
    std::chrono::microseconds synchronization_time;

    // Configuration
    int block_size;
    int grid_size;
    int points_per_thread;
    size_t batch_size;

    // Quality metrics
    bool accuracy_verified;
    double accuracy_percentage;
    int num_validation_samples;

    // Metadata
    std::chrono::high_resolution_clock::time_point measurement_timestamp;
    std::string test_description;
    json additional_metadata;

    BaselineMetrics()
        : compute_capability(0), total_memory_mb(0), throughput_mkeys_per_sec(0.0),
          gpu_utilization_percent(0.0), memory_bandwidth_utilization_percent(0.0),
          sync_overhead_percent(0.0), total_execution_time(std::chrono::microseconds(0)),
          kernel_execution_time(std::chrono::microseconds(0)),
          memory_transfer_time(std::chrono::microseconds(0)),
          synchronization_time(std::chrono::microseconds(0)), block_size(0), grid_size(0),
          points_per_thread(0), batch_size(0), accuracy_verified(false),
          accuracy_percentage(0.0), num_validation_samples(0) {}
};

struct BenchmarkConfiguration {
    std::string benchmark_name;
    std::chrono::seconds warmup_time{30};
    std::chrono::seconds measurement_time{300};
    int num_iterations = 10;
    int num_validation_samples = 10000;
    bool enable_detailed_profiling = true;
    bool enable_accuracy_validation = true;
    double target_confidence_level = 0.95;
    double max_coefficient_of_variation = 0.05; // 5% max variance

    // Workload parameters
    core::UInt256 start_key;
    core::UInt256 end_key;
    int test_block_size = 384; // Updated to reflect flexible block sizing
    int test_grid_size = 4096;
    int test_points_per_thread = 1;

    json ToJson() const {
        json config;
        config["benchmark_name"] = benchmark_name;
        config["warmup_time_seconds"] = warmup_time.count();
        config["measurement_time_seconds"] = measurement_time.count();
        config["num_iterations"] = num_iterations;
        config["num_validation_samples"] = num_validation_samples;
        config["enable_detailed_profiling"] = enable_detailed_profiling;
        config["enable_accuracy_validation"] = enable_accuracy_validation;
        config["target_confidence_level"] = target_confidence_level;
        config["max_coefficient_of_variation"] = max_coefficient_of_variation;
        config["test_block_size"] = test_block_size;
        config["test_grid_size"] = test_grid_size;
        config["test_points_per_thread"] = test_points_per_thread;
        return config;
    }
};

class BaselineMeasurementFramework {
public:
    static BaselineMeasurementFramework& GetInstance();

    // Core measurement functionality
    BaselineMetrics MeasureBaselinePerformance(const BenchmarkConfiguration& config);
    std::vector<BaselineMetrics> MeasureBaselineMultipleRuns(const BenchmarkConfiguration& config, int num_runs = 5);

    // Baseline management
    void SaveBaseline(const BaselineMetrics& baseline, const std::string& baseline_name);
    bool LoadBaseline(const std::string& baseline_name, BaselineMetrics& baseline);
    std::vector<std::string> GetAvailableBaselines() const;
    bool RemoveBaseline(const std::string& baseline_name);

    // Comparison functionality
    json CompareWithBaseline(const BaselineMetrics& current, const std::string& baseline_name) const;
    double CalculatePerformanceImprovement(const BaselineMetrics& optimized, const BaselineMetrics& baseline) const;
    bool ValidatePerformanceImprovement(const BaselineMetrics& current, const std::string& baseline_name,
                                       double min_improvement_factor = 1.2) const;

    // Statistical analysis
    BaselineMetrics CalculateStatistics(const std::vector<BaselineMetrics>& measurements) const;
    bool ValidateStatisticalSignificance(const std::vector<BaselineMetrics>& measurements,
                                       double confidence_level = 0.95) const;
    double CalculateConfidenceInterval(const std::vector<double>& values, double confidence_level = 0.95) const;

    // Reporting
    json GenerateBaselineReport(const BaselineMetrics& baseline) const;
    json GenerateComparisonReport(const std::vector<BaselineMetrics>& before_list,
                                 const std::vector<BaselineMetrics>& after_list) const;
    std::string GenerateBaselineSummary(const BaselineMetrics& baseline) const;

    // Configuration
    void SetBaselineStoragePath(const std::string& storage_path);
    void SetDefaultBenchmarkConfiguration(const BenchmarkConfiguration& config);
    BenchmarkConfiguration GetDefaultBenchmarkConfiguration() const;

private:
    BaselineMeasurementFramework() = default;
    ~BaselineMeasurementFramework() = default;
    BaselineMeasurementFramework(const BaselineMeasurementFramework&) = delete;
    BaselineMeasurementFramework& operator=(const BaselineMeasurementFramework&) = delete;

    // Internal measurement methods
    BaselineMetrics PerformSingleMeasurement(const BenchmarkConfiguration& config);
    void WarmupGPU(const BenchmarkConfiguration& config);
    void CollectGPUStatistics(BaselineMetrics& metrics);
    void ValidateAccuracy(BaselineMetrics& metrics, const BenchmarkConfiguration& config);

    // File operations
    std::string GetBaselineFilePath(const std::string& baseline_name) const;
    bool SaveBaselineToFile(const BaselineMetrics& baseline, const std::string& file_path) const;
    bool LoadBaselineFromFile(const std::string& file_path, BaselineMetrics& baseline) const;

    // Statistical utilities
    double CalculateMean(const std::vector<double>& values) const;
    double CalculateStandardDeviation(const std::vector<double>& values) const;
    double CalculateCoefficientOfVariation(const std::vector<double>& values) const;
    bool PerformTTest(const std::vector<double>& sample1, const std::vector<double>& sample2,
                     double confidence_level) const;

    std::string baseline_storage_path_ = "./baselines/";
    BenchmarkConfiguration default_config_;
    mutable std::mutex measurement_mutex_;
};

// RAII class for baseline measurement sessions
class BaselineMeasurementSession {
public:
    explicit BaselineMeasurementSession(const BenchmarkConfiguration& config);
    ~BaselineMeasurementSession();

    void StartMeasurement();
    void RecordIterationMetrics(const BaselineMetrics& metrics);
    BaselineMetrics GetFinalMetrics() const;
    bool IsCompleted() const;

private:
    BenchmarkConfiguration config_;
    std::vector<BaselineMetrics> iteration_metrics_;
    std::chrono::high_resolution_clock::time_point session_start_;
    bool measurement_started_ = false;
    bool measurement_completed_ = false;
    mutable std::mutex session_mutex_;
};

// Utility functions for baseline operations
namespace baseline_utils {
    BaselineMetrics CreateBaselineFromGpuMetrics(const std::string& gpu_name, int compute_capability,
                                                size_t memory_mb, double throughput);
    json BaselineMetricsToJson(const BaselineMetrics& metrics);
    BaselineMetrics JsonToBaselineMetrics(const json& json_metrics);
    bool ValidateBaselineMetrics(const BaselineMetrics& metrics);
    std::string FormatBaselineForDisplay(const BaselineMetrics& metrics);
}

} // namespace puzzle71::gpu::performance