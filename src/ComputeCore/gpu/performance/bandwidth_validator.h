#pragma once

#include <chrono>
#include <vector>
#include <memory>
#include <string>
#include <map>
#include <mutex>
#include <functional>
#include <cuda_runtime.h>
#include <nlohmann/json.hpp>

namespace puzzle71::gpu::performance {

using json = nlohmann::json;

struct MemoryBandwidthMetrics {
    double theoretical_bandwidth_gb_per_sec = 0.0;
    double achieved_bandwidth_gb_per_sec = 0.0;
    double utilization_percentage = 0.0;
    size_t total_bytes_transferred = 0;
    double transfer_time_ms = 0.0;
    bool meets_target_utilization = false;  // Target: >80%
    std::vector<double> bandwidth_samples;
    double average_bandwidth_gb_per_sec = 0.0;
    double bandwidth_std_deviation = 0.0;
    std::chrono::microseconds total_measurement_time{0};
};

struct BandwidthMeasurement {
    size_t bytes_transferred;
    std::chrono::high_resolution_clock::time_point start_time;
    std::chrono::high_resolution_clock::time_point end_time;
    std::chrono::microseconds duration;
    double bandwidth_gb_per_sec;
    std::string operation_type;  // "kernel", "host_to_device", "device_to_host", "device_to_device"
    int memory_type;             // 0: global, 1: shared, 2: constant, 3: texture
    bool is_coalesced;
    size_t transaction_size;
    int streaming_multiprocessor;
    std::string optimization_notes;
};

struct MemoryAccessPattern {
    std::vector<size_t> access_sizes;
    std::vector<std::chrono::nanoseconds> access_latencies;
    double coalescing_efficiency;
    double cache_hit_rate;
    size_t total_transactions;
    size_t efficient_transactions;
    double memory_utilization_percentage;
    std::map<std::string, double> pattern_metrics;
};

struct CachePerformanceMetrics {
    double l1_cache_hit_rate;
    double l2_cache_hit_rate;
    double shared_memory_efficiency;
    size_t l1_cache_misses;
    size_t l2_cache_misses;
    size_t shared_memory_bank_conflicts;
    double texture_cache_hit_rate;
    double constant_cache_hit_rate;
};

class BandwidthValidator {
public:
    explicit BandwidthValidator(int device_id = 0);
    virtual ~BandwidthValidator();

    // Primary validation interface
    virtual bool ValidateBandwidthUtilization(double target_utilization_percent = 80.0);
    virtual MemoryBandwidthMetrics GetCurrentBandwidthMetrics() const;
    virtual bool RunBandwidthTest(size_t test_size_mb = 1024, double& achieved_utilization);

    // Enhanced measurement methods
    virtual BandwidthMeasurement MeasureKernelMemoryBandwidth(
        size_t data_size_bytes,
        const std::function<void(void)>& kernel_function
    );

    virtual BandwidthMeasurement MeasureTransferBandwidth(
        void* host_ptr,
        void* device_ptr,
        size_t size,
        bool is_host_to_device = true,
        cudaStream_t stream = 0
    );

    // Memory access pattern analysis
    virtual MemoryAccessPattern AnalyzeMemoryAccessPattern(
        const std::string& kernel_name,
        size_t workload_size
    );

    virtual bool ValidateCoalescedAccessPattern(
        const MemoryAccessPattern& pattern
    );

    // Cache performance analysis
    virtual CachePerformanceMetrics MeasureCachePerformance(
        const std::string& operation_type = "key_search"
    );

    virtual bool ValidateCacheEfficiencyTargets(
        const CachePerformanceMetrics& metrics
    );

    // Continuous monitoring
    virtual void EnableContinuousMonitoring();
    virtual void DisableContinuousMonitoring();
    virtual bool IsMonitoringEnabled() const;
    virtual std::vector<BandwidthMeasurement> GetRecentMeasurements(
        std::chrono::minutes time_window = std::chrono::minutes(5)
    );

    // GPU-specific optimizations
    virtual double GetTheoreticalPeakBandwidth(int device_id = -1) const;
    virtual bool IsHighBandwidthMemoryAvailable() const;
    virtual std::vector<std::string> GetMemoryOptimizationRecommendations();

    // Configuration and validation
    virtual void SetTargetBandwidth(double target_gb_per_sec);
    virtual void SetValidationParameters(size_t min_test_size_mb, int max_iterations);
    virtual bool ValidateMeasurementAccuracy(const MemoryBandwidthMetrics& metrics);

    // Reporting and analytics
    virtual json GenerateBandwidthReport() const;
    virtual std::string GeneratePerformanceSummary() const;
    virtual std::map<std::string, double> GetBandwidthTrends() const;
    virtual void ExportMeasurements(const std::string& filename) const;

    // Comparison and benchmarking
    virtual bool CompareAgainstBaseline(
        const MemoryBandwidthMetrics& current,
        const MemoryBandwidthMetrics& baseline
    );

    virtual double CalculatePerformanceImprovement(
        const MemoryBandwidthMetrics& before,
        const MemoryBandwidthMetrics& after
    );

    // Error handling
    virtual std::string GetLastError() const;
    virtual bool HasValidationErrors() const;
    virtual std::vector<std::string> GetValidationWarnings() const;

protected:
    // Internal measurement methods
    void InitializeCudaEvents();
    void CleanupCudaEvents();
    void MeasureDeviceProperties();
    BandwidthMeasurement PerformSingleMeasurement(
        size_t size,
        const std::function<void(void)>& operation,
        const std::string& operation_type
    );

    // Memory pattern analysis
    void AnalyzeAccessSizes(std::vector<size_t>& access_sizes) const;
    void MeasureAccessLatencies(std::vector<std::chrono::nanoseconds>& latencies) const;
    double CalculateCoalescingEfficiency(const std::vector<size_t>& access_sizes) const;

    // Cache measurement utilities
    void SetupCacheCounters();
    void ReadCacheCounters(CachePerformanceMetrics& metrics);
    void ResetCacheCounters();

    // Validation helpers
    bool ValidateMeasurementConsistency(const std::vector<BandwidthMeasurement>& measurements) const;
    bool IsOutlierMeasurement(const BandwidthMeasurement& measurement, double mean, double std_dev) const;
    std::vector<BandwidthMeasurement> FilterOutliers(const std::vector<BandwidthMeasurement>& measurements) const;

    // Report generation
    void UpdateMeasurementHistory(const BandwidthMeasurement& measurement);
    void CalculateTrends();
    json SerializeMeasurement(const BandwidthMeasurement& measurement) const;

private:
    int target_device_id_;
    double target_bandwidth_utilization_;
    double target_bandwidth_gb_per_sec_;
    size_t min_test_size_mb_;
    int max_iterations_;
    bool monitoring_enabled_;

    mutable std::mutex measurement_mutex_;
    std::string last_error_;
    std::vector<std::string> validation_warnings_;

    // CUDA resources for measurement
    cudaEvent_t start_event_;
    cudaEvent_t end_event_;
    cudaStream_t measurement_stream_;

    // Device properties
    double theoretical_bandwidth_gb_per_sec_;
    size_t total_memory_bytes_;
    size_t l2_cache_size_;
    int sm_count_;
    std::string device_name_;
    bool is_hbm_memory_;

    // Measurement history
    std::vector<BandwidthMeasurement> measurement_history_;
    std::map<std::string, std::vector<BandwidthMeasurement>> operation_measurements_;
    std::chrono::high_resolution_clock::time_point monitoring_start_time_;

    // Performance trends
    std::map<std::string, std::vector<double>> bandwidth_trends_;
    std::map<std::string, double> baseline_bandwidths_;

    // Validation constants
    static constexpr double MIN_TARGET_UTILIZATION = 80.0;  // 80% target
    static constexpr size_t DEFAULT_TEST_SIZE_MB = 1024;
    static constexpr int DEFAULT_ITERATIONS = 10;
    static constexpr double OUTLIER_THRESHOLD = 2.0;  // 2 standard deviations
};

// Factory function
std::unique_ptr<BandwidthValidator> CreateBandwidthValidator(int device_id = 0);

} // namespace puzzle71::gpu::performance