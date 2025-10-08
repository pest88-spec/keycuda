#include "ComputeCore/gpu/performance/bandwidth_validator.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace puzzle71::gpu::performance {

BandwidthValidator::BandwidthValidator(int device_id)
    : target_device_id_(device_id)
    , target_bandwidth_utilization_(MIN_TARGET_UTILIZATION)
    , target_bandwidth_gb_per_sec_(0.0)
    , min_test_size_mb_(DEFAULT_TEST_SIZE_MB)
    , max_iterations_(DEFAULT_ITERATIONS)
    , monitoring_enabled_(false)
    , start_event_(nullptr)
    , end_event_(nullptr)
    , measurement_stream_(0)
    , theoretical_bandwidth_gb_per_sec_(0.0)
    , total_memory_bytes_(0)
    , l2_cache_size_(0)
    , sm_count_(0)
    , is_hbm_memory_(false) {

    InitializeCudaEvents();
    MeasureDeviceProperties();
    monitoring_start_time_ = std::chrono::high_resolution_clock::now();
}

BandwidthValidator::~BandwidthValidator() {
    DisableContinuousMonitoring();
    CleanupCudaEvents();
}

bool BandwidthValidator::ValidateBandwidthUtilization(double target_utilization_percent) {
    std::lock_guard<std::mutex> lock(measurement_mutex_);

    try {
        target_bandwidth_utilization_ = target_utilization_percent;

        std::vector<BandwidthMeasurement> measurements;
        measurements.reserve(max_iterations_);

        // Run multiple measurements for statistical accuracy
        for (int i = 0; i < max_iterations_; ++i) {
            BandwidthMeasurement measurement = PerformSingleMeasurement(
                min_test_size_mb_ * 1024 * 1024,
                [this]() {
                    // Simulate kernel memory access pattern
                    size_t test_size = min_test_size_mb_ * 1024 * 1024;
                    void* device_ptr = nullptr;
                    void* host_ptr = malloc(test_size);

                    if (!host_ptr) {
                        throw std::runtime_error("Failed to allocate host memory for bandwidth test");
                    }

                    cudaError_t err = cudaMalloc(&device_ptr, test_size);
                    if (err != cudaSuccess) {
                        free(host_ptr);
                        throw std::runtime_error("Failed to allocate device memory for bandwidth test");
                    }

                    // Perform memory copy to measure bandwidth
                    err = cudaMemcpyAsync(device_ptr, host_ptr, test_size, cudaMemcpyHostToDevice, measurement_stream_);
                    if (err != cudaSuccess) {
                        cudaFree(device_ptr);
                        free(host_ptr);
                        throw std::runtime_error("Failed to copy memory for bandwidth test");
                    }

                    cudaStreamSynchronize(measurement_stream_);
                    cudaFree(device_ptr);
                    free(host_ptr);
                },
                "bandwidth_validation"
            );

            measurements.push_back(measurement);
        }

        // Filter outliers
        measurements = FilterOutliers(measurements);

        if (measurements.empty()) {
            last_error_ = "No valid measurements obtained after filtering outliers";
            return false;
        }

        // Calculate average bandwidth
        double total_bandwidth = 0.0;
        for (const auto& measurement : measurements) {
            total_bandwidth += measurement.bandwidth_gb_per_sec;
        }
        double average_bandwidth = total_bandwidth / measurements.size();

        // Calculate utilization percentage
        double utilization_percentage = (average_bandwidth / theoretical_bandwidth_gb_per_sec_) * 100.0;

        bool meets_target = utilization_percentage >= target_utilization_percent;

        std::cout << "[BandwidthValidator] Validation Results:" << std::endl;
        std::cout << "  Theoretical Peak: " << theoretical_bandwidth_gb_per_sec_ << " GB/s" << std::endl;
        std::cout << "  Achieved: " << average_bandwidth << " GB/s" << std::endl;
        std::cout << "  Utilization: " << utilization_percentage << "%" << std::endl;
        std::cout << "  Target: " << target_utilization_percent << "%" << std::endl;
        std::cout << "  Meets Target: " << (meets_target ? "YES" : "NO") << std::endl;

        if (meets_target) {
            std::cout << "[BandwidthValidator] ✓ Target bandwidth utilization achieved" << std::endl;
        } else {
            std::cout << "[BandwidthValidator] ✗ Target bandwidth utilization not achieved" << std::endl;
            validation_warnings_.push_back("Bandwidth utilization below target threshold");
        }

        return meets_target;

    } catch (const std::exception& e) {
        last_error_ = std::string("Bandwidth validation failed: ") + e.what();
        std::cerr << "[BandwidthValidator] Error: " << last_error_ << std::endl;
        return false;
    }
}

MemoryBandwidthMetrics BandwidthValidator::GetCurrentBandwidthMetrics() const {
    std::lock_guard<std::mutex> lock(measurement_mutex_);

    MemoryBandwidthMetrics metrics;

    if (!measurement_history_.empty()) {
        // Use recent measurements
        auto recent_end = measurement_history_.end();
        auto recent_start = std::max(measurement_history_.begin(), recent_end - 10);

        std::vector<double> recent_bandwidths;
        size_t total_bytes = 0;
        std::chrono::microseconds total_time{0};

        for (auto it = recent_start; it != recent_end; ++it) {
            recent_bandwidths.push_back(it->bandwidth_gb_per_sec);
            total_bytes += it->bytes_transferred;
            total_time += it->duration;
        }

        if (!recent_bandwidths.empty()) {
            metrics.bandwidth_samples = recent_bandwidths;
            metrics.average_bandwidth_gb_per_sec = std::accumulate(recent_bandwidths.begin(), recent_bandwidths.end(), 0.0) / recent_bandwidths.size();

            // Calculate standard deviation
            double variance = 0.0;
            for (double bandwidth : recent_bandwidths) {
                variance += std::pow(bandwidth - metrics.average_bandwidth_gb_per_sec, 2);
            }
            metrics.bandwidth_std_deviation = std::sqrt(variance / recent_bandwidths.size());

            metrics.achieved_bandwidth_gb_per_sec = metrics.average_bandwidth_gb_per_sec;
            metrics.total_bytes_transferred = total_bytes;
            metrics.transfer_time_ms = total_time.count() / 1000.0;

            if (theoretical_bandwidth_gb_per_sec_ > 0) {
                metrics.utilization_percentage = (metrics.achieved_bandwidth_gb_per_sec / theoretical_bandwidth_gb_per_sec_) * 100.0;
                metrics.meets_target_utilization = metrics.utilization_percentage >= target_bandwidth_utilization_;
            }

            // Calculate total measurement time
            auto now = std::chrono::high_resolution_clock::now();
            metrics.total_measurement_time = std::chrono::duration_cast<std::chrono::microseconds>(now - monitoring_start_time_);
        }
    }

    metrics.theoretical_bandwidth_gb_per_sec = theoretical_bandwidth_gb_per_sec_;

    return metrics;
}

bool BandwidthValidator::RunBandwidthTest(size_t test_size_mb, double& achieved_utilization) {
    try {
        BandwidthMeasurement measurement = PerformSingleMeasurement(
            test_size_mb * 1024 * 1024,
            [this, test_size_mb]() {
                size_t test_size = test_size_mb * 1024 * 1024;
                void* device_ptr = nullptr;
                void* host_ptr = malloc(test_size);

                if (!host_ptr) {
                    throw std::runtime_error("Failed to allocate host memory for bandwidth test");
                }

                // Initialize host memory with pattern
                memset(host_ptr, 0xAB, test_size);

                cudaError_t err = cudaMalloc(&device_ptr, test_size);
                if (err != cudaSuccess) {
                    free(host_ptr);
                    throw std::runtime_error("Failed to allocate device memory for bandwidth test");
                }

                // Perform bidirectional memory copy
                err = cudaMemcpyAsync(device_ptr, host_ptr, test_size, cudaMemcpyHostToDevice, measurement_stream_);
                if (err != cudaSuccess) {
                    cudaFree(device_ptr);
                    free(host_ptr);
                    throw std::runtime_error("Failed to copy memory (H2D) for bandwidth test");
                }

                cudaStreamSynchronize(measurement_stream_);

                err = cudaMemcpyAsync(host_ptr, device_ptr, test_size, cudaMemcpyDeviceToHost, measurement_stream_);
                if (err != cudaSuccess) {
                    cudaFree(device_ptr);
                    free(host_ptr);
                    throw std::runtime_error("Failed to copy memory (D2H) for bandwidth test");
                }

                cudaStreamSynchronize(measurement_stream_);
                cudaFree(device_ptr);
                free(host_ptr);
            },
            "bandwidth_test"
        );

        UpdateMeasurementHistory(measurement);

        achieved_utilization = (measurement.bandwidth_gb_per_sec / theoretical_bandwidth_gb_per_sec_) * 100.0;

        std::cout << "[BandwidthValidator] Test Results:" << std::endl;
        std::cout << "  Test Size: " << test_size_mb << " MB" << std::endl;
        std::cout << "  Bandwidth: " << measurement.bandwidth_gb_per_sec << " GB/s" << std::endl;
        std::cout << "  Utilization: " << achieved_utilization << "%" << std::endl;

        return true;

    } catch (const std::exception& e) {
        last_error_ = std::string("Bandwidth test failed: ") + e.what();
        std::cerr << "[BandwidthValidator] Error: " << last_error_ << std::endl;
        achieved_utilization = 0.0;
        return false;
    }
}

BandwidthMeasurement BandwidthValidator::MeasureKernelMemoryBandwidth(
    size_t data_size_bytes,
    const std::function<void(void)>& kernel_function) {

    return PerformSingleMeasurement(data_size_bytes, kernel_function, "kernel_execution");
}

BandwidthMeasurement BandwidthValidator::MeasureTransferBandwidth(
    void* host_ptr,
    void* device_ptr,
    size_t size,
    bool is_host_to_device,
    cudaStream_t stream) {

    cudaStream_t target_stream = (stream == 0) ? measurement_stream_ : stream;

    return PerformSingleMeasurement(size, [this, host_ptr, device_ptr, size, is_host_to_device, target_stream]() {
        cudaMemcpyKind kind = is_host_to_device ? cudaMemcpyHostToDevice : cudaMemcpyDeviceToHost;
        cudaError_t err = cudaMemcpyAsync(device_ptr, host_ptr, size, kind, target_stream);
        if (err != cudaSuccess) {
            throw std::runtime_error("Failed to execute memory transfer for bandwidth measurement");
        }
        cudaStreamSynchronize(target_stream);
    }, is_host_to_device ? "host_to_device" : "device_to_host");
}

void BandwidthValidator::SetTargetBandwidth(double target_gb_per_sec) {
    target_bandwidth_gb_per_sec_ = target_gb_per_sec;
    std::cout << "[BandwidthValidator] Target bandwidth set to " << target_gb_per_sec << " GB/s" << std::endl;
}

void BandwidthValidator::SetValidationParameters(size_t min_test_size_mb, int max_iterations) {
    min_test_size_mb_ = min_test_size_mb;
    max_iterations_ = max_iterations;
    std::cout << "[BandwidthValidator] Validation parameters updated: min_size=" << min_test_size_mb
              << "MB, iterations=" << max_iterations << std::endl;
}

bool BandwidthValidator::ValidateMeasurementAccuracy(const MemoryBandwidthMetrics& metrics) {
    if (metrics.bandwidth_samples.empty()) {
        return false;
    }

    // Check if standard deviation is reasonable (< 10% of mean)
    double coefficient_of_variation = metrics.bandwidth_std_deviation / metrics.average_bandwidth_gb_per_sec;
    bool is_accurate = coefficient_of_variation < 0.1;  // 10% threshold

    if (!is_accurate) {
        validation_warnings_.push_back("High variance in bandwidth measurements detected");
    }

    return is_accurate;
}

json BandwidthValidator::GenerateBandwidthReport() const {
    std::lock_guard<std::mutex> lock(measurement_mutex_);

    json report;

    MemoryBandwidthMetrics metrics = GetCurrentBandwidthMetrics();

    report["device_name"] = device_name_;
    report["device_id"] = target_device_id_;
    report["theoretical_bandwidth_gb_per_sec"] = theoretical_bandwidth_gb_per_sec_;
    report["achieved_bandwidth_gb_per_sec"] = metrics.achieved_bandwidth_gb_per_sec;
    report["utilization_percentage"] = metrics.utilization_percentage;
    report["meets_target_utilization"] = metrics.meets_target_utilization;
    report["target_utilization_percentage"] = target_bandwidth_utilization_;

    report["measurement_statistics"] = {
        {"average_bandwidth_gb_per_sec", metrics.average_bandwidth_gb_per_sec},
        {"bandwidth_std_deviation", metrics.bandwidth_std_deviation},
        {"total_bytes_transferred", metrics.total_bytes_transferred},
        {"total_measurement_time_us", metrics.total_measurement_time.count()},
        {"sample_count", metrics.bandwidth_samples.size()}
    };

    // Include recent measurements
    json recent_measurements = json::array();
    for (const auto& measurement : measurement_history_) {
        recent_measurements.push_back(SerializeMeasurement(measurement));
    }
    report["recent_measurements"] = recent_measurements;

    // Include performance trends
    json trends = json::object();
    for (const auto& [operation, values] : bandwidth_trends_) {
        trends[operation] = values;
    }
    report["performance_trends"] = trends;

    // Include validation warnings
    if (!validation_warnings_.empty()) {
        report["validation_warnings"] = validation_warnings_;
    }

    report["hbm_memory_available"] = is_hbm_memory_;
    report["total_memory_gb"] = total_memory_bytes_ / (1024.0 * 1024.0 * 1024.0);
    report["l2_cache_size_kb"] = l2_cache_size_ / 1024;
    report["sm_count"] = sm_count_;

    return report;
}

std::string BandwidthValidator::GeneratePerformanceSummary() const {
    MemoryBandwidthMetrics metrics = GetCurrentBandwidthMetrics();

    std::stringstream ss;
    ss << "=== Bandwidth Performance Summary ===" << std::endl;
    ss << "Device: " << device_name_ << " (ID: " << target_device_id_ << ")" << std::endl;
    ss << "Theoretical Peak: " << theoretical_bandwidth_gb_per_sec_ << " GB/s" << std::endl;
    ss << "Achieved: " << metrics.achieved_bandwidth_gb_per_sec << " GB/s" << std::endl;
    ss << "Utilization: " << std::fixed << std::setprecision(2) << metrics.utilization_percentage << "%" << std::endl;
    ss << "Target: " << target_bandwidth_utilization_ << "%" << std::endl;
    ss << "Status: " << (metrics.meets_target_utilization ? "✓ MEETS TARGET" : "✗ BELOW TARGET") << std::endl;

    if (!metrics.bandwidth_samples.empty()) {
        ss << std::endl;
        ss << "Statistics (based on " << metrics.bandwidth_samples.size() << " samples):" << std::endl;
        ss << "  Average: " << metrics.average_bandwidth_gb_per_sec << " GB/s" << std::endl;
        ss << "  Std Dev: " << metrics.bandwidth_std_deviation << " GB/s" << std::endl;
        ss << "  Total Data: " << (metrics.total_bytes_transferred / (1024.0 * 1024.0 * 1024.0)) << " GB" << std::endl;
        ss << "  Measurement Time: " << (metrics.total_measurement_time.count() / 1000000.0) << " s" << std::endl;
    }

    if (!validation_warnings_.empty()) {
        ss << std::endl;
        ss << "Warnings:" << std::endl;
        for (const auto& warning : validation_warnings_) {
            ss << "  - " << warning << std::endl;
        }
    }

    return ss.str();
}

std::string BandwidthValidator::GetLastError() const {
    std::lock_guard<std::mutex> lock(measurement_mutex_);
    return last_error_;
}

bool BandwidthValidator::HasValidationErrors() const {
    std::lock_guard<std::mutex> lock(measurement_mutex_);
    return !last_error_.empty();
}

std::vector<std::string> BandwidthValidator::GetValidationWarnings() const {
    std::lock_guard<std::mutex> lock(measurement_mutex_);
    return validation_warnings_;
}

void BandwidthValidator::EnableContinuousMonitoring() {
    monitoring_enabled_ = true;
    std::cout << "[BandwidthValidator] Continuous monitoring enabled" << std::endl;
}

void BandwidthValidator::DisableContinuousMonitoring() {
    monitoring_enabled_ = false;
    std::cout << "[BandwidthValidator] Continuous monitoring disabled" << std::endl;
}

bool BandwidthValidator::IsMonitoringEnabled() const {
    return monitoring_enabled_;
}

std::vector<BandwidthMeasurement> BandwidthValidator::GetRecentMeasurements(std::chrono::minutes time_window) {
    std::lock_guard<std::mutex> lock(measurement_mutex_);

    std::vector<BandwidthMeasurement> recent_measurements;
    auto cutoff_time = std::chrono::high_resolution_clock::now() - time_window;

    for (const auto& measurement : measurement_history_) {
        if (measurement.start_time >= cutoff_time) {
            recent_measurements.push_back(measurement);
        }
    }

    return recent_measurements;
}

double BandwidthValidator::GetTheoreticalPeakBandwidth(int device_id) const {
    if (device_id == -1) {
        device_id = target_device_id_;
    }

    if (device_id == target_device_id_) {
        return theoretical_bandwidth_gb_per_sec_;
    }

    // Query properties for different device
    cudaDeviceProp prop;
    cudaError_t err = cudaGetDeviceProperties(&prop, device_id);
    if (err != cudaSuccess) {
        return 0.0;
    }

    double memory_clock_mhz = prop.memoryClockRate / 1000.0;
    double memory_bus_width_bits = prop.memoryBusWidth;
    return (memory_clock_mhz * memory_bus_width_bits) / (8.0 * 1000.0);
}

bool BandwidthValidator::IsHighBandwidthMemoryAvailable() const {
    return is_hbm_memory_;
}

std::vector<std::string> BandwidthValidator::GetMemoryOptimizationRecommendations() {
    std::vector<std::string> recommendations;

    MemoryBandwidthMetrics metrics = GetCurrentBandwidthMetrics();

    if (metrics.utilization_percentage < 60.0) {
        recommendations.push_back("Consider implementing memory coalescing optimizations");
        recommendations.push_back("Increase batch sizes for better memory utilization");
    }

    if (metrics.utilization_percentage < 40.0) {
        recommendations.push_back("Enable shared memory usage for data reuse");
        recommendations.push_back("Implement memory prefetching strategies");
        recommendations.push_back("Consider using texture memory for read-only data");
    }

    if (!is_hbm_memory_) {
        recommendations.push_back("Consider upgrading to HBM memory for higher bandwidth");
    }

    if (l2_cache_size_ < 6 * 1024 * 1024) {  // Less than 6MB
        recommendations.push_back("Small L2 cache detected - optimize for cache locality");
    }

    return recommendations;
}

MemoryAccessPattern BandwidthValidator::AnalyzeMemoryAccessPattern(
    const std::string& kernel_name,
    size_t workload_size) {

    MemoryAccessPattern pattern;

    // This is a simplified implementation - in a real system, you would use
    // profiling tools like NVIDIA Nsight to get actual access patterns

    pattern.coalescing_efficiency = 0.85;  // Assumed
    pattern.cache_hit_rate = 0.75;         // Assumed
    pattern.total_transactions = workload_size / 128;  // Assumed 128-byte transactions
    pattern.efficient_transactions = static_cast<size_t>(pattern.total_transactions * pattern.coalescing_efficiency);
    pattern.memory_utilization_percentage = pattern.coalescing_efficiency * 100.0;

    pattern.pattern_metrics = {
        {"avg_access_size", 128.0},
        {"sequential_access_ratio", 0.90},
        {"random_access_ratio", 0.10},
        {"stride_efficiency", 0.95}
    };

    return pattern;
}

bool BandwidthValidator::ValidateCoalescedAccessPattern(const MemoryAccessPattern& pattern) {
    // Consider coalesced if efficiency > 80%
    return pattern.coalescing_efficiency > 0.80;
}

CachePerformanceMetrics BandwidthValidator::MeasureCachePerformance(const std::string& operation_type) {
    CachePerformanceMetrics metrics;

    // This is a simplified implementation - in a real system, you would use
    // hardware performance counters or profiling tools

    metrics.l1_cache_hit_rate = 0.85;
    metrics.l2_cache_hit_rate = 0.90;
    metrics.shared_memory_efficiency = 0.88;
    metrics.l1_cache_misses = 1500;
    metrics.l2_cache_misses = 800;
    metrics.shared_memory_bank_conflicts = 50;
    metrics.texture_cache_hit_rate = 0.92;
    metrics.constant_cache_hit_rate = 0.95;

    return metrics;
}

bool BandwidthValidator::ValidateCacheEfficiencyTargets(const CachePerformanceMetrics& metrics) {
    return metrics.l1_cache_hit_rate > 0.75 &&
           metrics.l2_cache_hit_rate > 0.75 &&
           metrics.shared_memory_efficiency > 0.75;
}

std::map<std::string, double> BandwidthValidator::GetBandwidthTrends() const {
    std::lock_guard<std::mutex> lock(measurement_mutex_);

    std::map<std::string, double> trends;

    for (const auto& [operation, values] : bandwidth_trends_) {
        if (!values.empty()) {
            // Calculate trend (slope) using simple linear regression
            double sum_x = 0.0, sum_y = 0.0, sum_xy = 0.0, sum_x2 = 0.0;
            size_t n = values.size();

            for (size_t i = 0; i < n; ++i) {
                sum_x += i;
                sum_y += values[i];
                sum_xy += i * values[i];
                sum_x2 += i * i;
            }

            double slope = (n * sum_xy - sum_x * sum_y) / (n * sum_x2 - sum_x * sum_x);
            trends[operation] = slope;
        }
    }

    return trends;
}

bool BandwidthValidator::CompareAgainstBaseline(
    const MemoryBandwidthMetrics& current,
    const MemoryBandwidthMetrics& baseline) {

    return current.achieved_bandwidth_gb_per_sec > baseline.achieved_bandwidth_gb_per_sec;
}

double BandwidthValidator::CalculatePerformanceImprovement(
    const MemoryBandwidthMetrics& before,
    const MemoryBandwidthMetrics& after) {

    if (before.achieved_bandwidth_gb_per_sec == 0.0) {
        return 0.0;
    }

    return ((after.achieved_bandwidth_gb_per_sec - before.achieved_bandwidth_gb_per_sec) /
            before.achieved_bandwidth_gb_per_sec) * 100.0;
}

// Protected methods implementation

void BandwidthValidator::InitializeCudaEvents() {
    cudaError_t err = cudaEventCreate(&start_event_);
    if (err != cudaSuccess) {
        throw std::runtime_error("Failed to create CUDA start event");
    }

    err = cudaEventCreate(&end_event_);
    if (err != cudaSuccess) {
        cudaEventDestroy(start_event_);
        throw std::runtime_error("Failed to create CUDA end event");
    }

    err = cudaStreamCreate(&measurement_stream_);
    if (err != cudaSuccess) {
        cudaEventDestroy(start_event_);
        cudaEventDestroy(end_event_);
        throw std::runtime_error("Failed to create CUDA measurement stream");
    }
}

void BandwidthValidator::CleanupCudaEvents() {
    if (start_event_) {
        cudaEventDestroy(start_event_);
        start_event_ = nullptr;
    }
    if (end_event_) {
        cudaEventDestroy(end_event_);
        end_event_ = nullptr;
    }
    if (measurement_stream_) {
        cudaStreamDestroy(measurement_stream_);
        measurement_stream_ = 0;
    }
}

void BandwidthValidator::MeasureDeviceProperties() {
    cudaDeviceProp prop;
    cudaError_t err = cudaGetDeviceProperties(&prop, target_device_id_);
    if (err != cudaSuccess) {
        throw std::runtime_error("Failed to get device properties");
    }

    device_name_ = prop.name;
    sm_count_ = prop.multiProcessorCount;
    total_memory_bytes_ = prop.totalGlobalMem;
    l2_cache_size_ = prop.l2CacheSize;

    // Calculate theoretical bandwidth
    // Bandwidth = memory clock * memory bus width / 8
    double memory_clock_mhz = prop.memoryClockRate / 1000.0;
    double memory_bus_width_bits = prop.memoryBusWidth;
    theoretical_bandwidth_gb_per_sec_ = (memory_clock_mhz * memory_bus_width_bits) / (8.0 * 1000.0);

    // Check for HBM memory
    is_hbm_memory_ = device_name_.find("HBM") != std::string::npos;

    std::cout << "[BandwidthValidator] Device Properties:" << std::endl;
    std::cout << "  Name: " << device_name_ << std::endl;
    std::cout << "  SM Count: " << sm_count_ << std::endl;
    std::cout << "  Total Memory: " << (total_memory_bytes_ / (1024.0 * 1024.0)) << " MB" << std::endl;
    std::cout << "  L2 Cache: " << (l2_cache_size_ / 1024) << " KB" << std::endl;
    std::cout << "  Theoretical Bandwidth: " << theoretical_bandwidth_gb_per_sec_ << " GB/s" << std::endl;
    std::cout << "  HBM Memory: " << (is_hbm_memory_ ? "Yes" : "No") << std::endl;
}

BandwidthMeasurement BandwidthValidator::PerformSingleMeasurement(
    size_t size,
    const std::function<void(void)>& operation,
    const std::string& operation_type) {

    BandwidthMeasurement measurement;
    measurement.bytes_transferred = size;
    measurement.operation_type = operation_type;
    measurement.is_coalesced = true;  // Assume coalesced for basic operations
    measurement.transaction_size = size;
    measurement.streaming_multiprocessor = -1;  // Not applicable for basic transfers

    cudaEventRecord(start_event_, measurement_stream_);
    auto start_time = std::chrono::high_resolution_clock::now();

    try {
        operation();
    } catch (const std::exception& e) {
        throw;  // Re-throw to handle at higher level
    }

    cudaEventRecord(end_event_, measurement_stream_);
    cudaEventSynchronize(end_event_);

    auto end_time = std::chrono::high_resolution_clock::now();

    float milliseconds = 0.0f;
    cudaEventElapsedTime(&milliseconds, start_event_, end_event_);

    measurement.start_time = start_time;
    measurement.end_time = end_time;
    measurement.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

    // Calculate bandwidth in GB/s
    double seconds = milliseconds / 1000.0;
    double gigabytes = size / (1024.0 * 1024.0 * 1024.0);
    measurement.bandwidth_gb_per_sec = gigabytes / seconds;

    return measurement;
}

std::vector<BandwidthMeasurement> BandwidthValidator::FilterOutliers(const std::vector<BandwidthMeasurement>& measurements) const {
    if (measurements.size() <= 2) {
        return measurements;
    }

    // Calculate mean and standard deviation
    double mean = 0.0;
    for (const auto& measurement : measurements) {
        mean += measurement.bandwidth_gb_per_sec;
    }
    mean /= measurements.size();

    double variance = 0.0;
    for (const auto& measurement : measurements) {
        variance += std::pow(measurement.bandwidth_gb_per_sec - mean, 2);
    }
    double std_dev = std::sqrt(variance / measurements.size());

    // Filter outliers (> 2 standard deviations from mean)
    std::vector<BandwidthMeasurement> filtered;
    for (const auto& measurement : measurements) {
        if (std::abs(measurement.bandwidth_gb_per_sec - mean) <= OUTLIER_THRESHOLD * std_dev) {
            filtered.push_back(measurement);
        }
    }

    std::cout << "[BandwidthValidator] Filtered " << (measurements.size() - filtered.size())
              << " outliers from " << measurements.size() << " measurements" << std::endl;

    return filtered;
}

void BandwidthValidator::UpdateMeasurementHistory(const BandwidthMeasurement& measurement) {
    measurement_history_.push_back(measurement);
    operation_measurements_[measurement.operation_type].push_back(measurement);

    // Keep history size manageable
    const size_t MAX_HISTORY_SIZE = 1000;
    if (measurement_history_.size() > MAX_HISTORY_SIZE) {
        measurement_history_.erase(measurement_history_.begin());
    }

    // Update trends
    bandwidth_trends_[measurement.operation_type].push_back(measurement.bandwidth_gb_per_sec);

    const size_t MAX_TREND_SIZE = 100;
    if (bandwidth_trends_[measurement.operation_type].size() > MAX_TREND_SIZE) {
        bandwidth_trends_[measurement.operation_type].erase(bandwidth_trends_[measurement.operation_type].begin());
    }
}

json BandwidthValidator::SerializeMeasurement(const BandwidthMeasurement& measurement) const {
    json j;
    j["bytes_transferred"] = measurement.bytes_transferred;
    j["duration_us"] = measurement.duration.count();
    j["bandwidth_gb_per_sec"] = measurement.bandwidth_gb_per_sec;
    j["operation_type"] = measurement.operation_type;
    j["is_coalesced"] = measurement.is_coalesced;
    j["transaction_size"] = measurement.transaction_size;
    j["timestamp_ms"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        measurement.start_time.time_since_epoch()).count();
    return j;
}

// Factory function implementation
std::unique_ptr<BandwidthValidator> CreateBandwidthValidator(int device_id) {
    return std::make_unique<BandwidthValidator>(device_id);
}

} // namespace puzzle71::gpu::performance