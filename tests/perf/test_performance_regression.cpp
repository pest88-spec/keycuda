#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <fstream>
#include <chrono>
#include <vector>
#include <string>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <future>
#include <iomanip>
#include <sstream>
#include <algorithm>

#include "gpu_executor.h"
#include "device_metrics.h"
#include "performance_reporter.h"
#include "bandwidth_validator.h"
#include "memory_manager.h"
#include "adaptive_scaling.h"

namespace keycuda {
namespace testing {

// Performance regression detection thresholds
constexpr double KEY_SEARCH_PERFORMANCE_REGRESSION_THRESHOLD = 0.15;  // 15% regression
constexpr double MEMORY_BANDWIDTH_REGRESSION_THRESHOLD = 0.20;       // 20% regression
constexpr double LATENCY_REGRESSION_THRESHOLD = 0.25;                // 25% regression
constexpr double THROUGHPUT_REGRESSION_THRESHOLD = 0.10;             // 10% regression

// Baseline performance targets (from previous optimal runs)
constexpr double BASELINE_KEY_SEARCH_MKEYS_PER_SEC = 40.0;
constexpr double BASELINE_MEMORY_BANDWIDTH_UTILIZATION = 0.80;       // 80% of theoretical max
constexpr double BASELINE_SYNC_LATENCY_US = 100.0;                    // 100 microseconds
constexpr double BASELINE_GPU_UTILIZATION = 0.90;                     // 90%

struct PerformanceBaseline {
    double key_search_mkeys_per_sec;
    double memory_bandwidth_utilization;
    double sync_latency_us;
    double gpu_utilization;
    std::chrono::system_clock::time_point timestamp;
    std::string git_commit;
    std::string gpu_architecture;
    std::string driver_version;

    PerformanceBaseline() : key_search_mkeys_per_sec(0.0), memory_bandwidth_utilization(0.0),
                           sync_latency_us(0.0), gpu_utilization(0.0) {}
};

struct RegressionTestResult {
    std::string test_name;
    double current_value;
    double baseline_value;
    double regression_percentage;
    bool passed_regression_test;
    std::string details;

    RegressionTestResult(const std::string& name) : test_name(name), current_value(0.0),
                                                   baseline_value(0.0), regression_percentage(0.0),
                                                   passed_regression_test(true) {}
};

struct PerformanceSnapshot {
    double key_search_throughput;
    double memory_bandwidth_utilization;
    double synchronization_latency;
    double gpu_utilization;
    double power_consumption;
    std::chrono::system_clock::time_point timestamp;

    PerformanceSnapshot() : key_search_throughput(0.0), memory_bandwidth_utilization(0.0),
                           synchronization_latency(0.0), gpu_utilization(0.0), power_consumption(0.0) {}
};

class PerformanceRegressionTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize GPU context
        ASSERT_TRUE(cudaSuccess == cudaSetDevice(0));

        // Load or create performance baseline
        baseline_ = LoadPerformanceBaseline();
        if (baseline_.key_search_mkeys_per_sec == 0.0) {
            // No baseline exists, create one
            baseline_ = CreatePerformanceBaseline();
            SavePerformanceBaseline(baseline_);
        }

        // Initialize performance monitoring components
        gpu_executor_ = std::make_unique<GpuExecutor>(0);
        device_metrics_ = std::make_unique<DeviceMetricsCollector>(0);
        bandwidth_validator_ = std::make_unique<BandwidthValidator>(0);
        memory_manager_ = std::make_unique<MemoryManager>(0);

        // Configure performance tracking
        performance_snapshots_.reserve(1000);
    }

    void TearDown() override {
        performance_snapshots_.clear();
    }

    // Load baseline from persistent storage
    PerformanceBaseline LoadPerformanceBaseline() {
        PerformanceBaseline baseline;
        std::ifstream baseline_file("performance_baseline.json");

        if (baseline_file.is_open()) {
            try {
                nlohmann::json json_data;
                baseline_file >> json_data;

                baseline.key_search_mkeys_per_sec = json_data["key_search_mkeys_per_sec"];
                baseline.memory_bandwidth_utilization = json_data["memory_bandwidth_utilization"];
                baseline.sync_latency_us = json_data["sync_latency_us"];
                baseline.gpu_utilization = json_data["gpu_utilization"];
                baseline.git_commit = json_data["git_commit"];
                baseline.gpu_architecture = json_data["gpu_architecture"];
                baseline.driver_version = json_data["driver_version"];

                // Parse timestamp
                auto ts = json_data["timestamp"].get<std::string>();
                std::tm tm = {};
                std::istringstream ss(ts);
                ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
                baseline.timestamp = std::chrono::system_clock::from_time_t(std::mktime(&tm));

            } catch (const std::exception& e) {
                std::cout << "Failed to load baseline: " << e.what() << std::endl;
                baseline = PerformanceBaseline{};
            }
        }

        return baseline;
    }

    // Save baseline to persistent storage
    void SavePerformanceBaseline(const PerformanceBaseline& baseline) {
        nlohmann::json json_data;
        json_data["key_search_mkeys_per_sec"] = baseline.key_search_mkeys_per_sec;
        json_data["memory_bandwidth_utilization"] = baseline.memory_bandwidth_utilization;
        json_data["sync_latency_us"] = baseline.sync_latency_us;
        json_data["gpu_utilization"] = baseline.gpu_utilization;
        json_data["git_commit"] = baseline.git_commit;
        json_data["gpu_architecture"] = baseline.gpu_architecture;
        json_data["driver_version"] = baseline.driver_version;

        // Format timestamp
        auto time_t = std::chrono::system_clock::to_time_t(baseline.timestamp);
        std::ostringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        json_data["timestamp"] = ss.str();

        std::ofstream baseline_file("performance_baseline.json");
        if (baseline_file.is_open()) {
            baseline_file << std::setw(4) << json_data << std::endl;
            std::cout << "Performance baseline saved successfully." << std::endl;
        }
    }

    // Create new performance baseline by running comprehensive tests
    PerformanceBaseline CreatePerformanceBaseline() {
        std::cout << "Creating new performance baseline..." << std::endl;

        PerformanceBaseline baseline;

        // Get GPU information
        cudaDeviceProp prop;
        cudaGetDeviceProperties(&prop, 0);
        baseline.gpu_architecture = std::string(prop.name) + " (SM " +
                                   std::to_string(prop.major) + "." +
                                   std::to_string(prop.minor) + ")";

        // Get driver version
        int driver_version;
        cudaDriverGetVersion(&driver_version);
        baseline.driver_version = std::to_string(driver_version);

        // Get current git commit (if available)
        baseline.git_commit = GetCurrentGitCommit();

        // Run baseline performance measurements
        baseline.key_search_mkeys_per_sec = MeasureKeySearchPerformance();
        baseline.memory_bandwidth_utilization = MeasureMemoryBandwidthUtilization();
        baseline.sync_latency_us = MeasureSynchronizationLatency();
        baseline.gpu_utilization = MeasureGpuUtilization();

        baseline.timestamp = std::chrono::system_clock::now();

        std::cout << "Baseline performance metrics:" << std::endl;
        std::cout << "  Key search: " << baseline.key_search_mkeys_per_sec << " Mkeys/sec" << std::endl;
        std::cout << "  Memory bandwidth utilization: " << (baseline.memory_bandwidth_utilization * 100) << "%" << std::endl;
        std::cout << "  Sync latency: " << baseline.sync_latency_us << " μs" << std::endl;
        std::cout << "  GPU utilization: " << (baseline.gpu_utilization * 100) << "%" << std::endl;

        return baseline;
    }

    // Measure key search performance
    double MeasureKeySearchPerformance() {
        const size_t search_iterations = 1000000;
        const int warmup_iterations = 100;

        // Warmup
        for (int i = 0; i < warmup_iterations; ++i) {
            gpu_executor_->SearchKeys(nullptr, nullptr, 1024);
        }

        auto start_time = std::chrono::high_resolution_clock::now();

        for (size_t i = 0; i < search_iterations; ++i) {
            gpu_executor_->SearchKeys(nullptr, nullptr, 1024);
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

        double keys_per_sec = (search_iterations * 1024.0) / (duration.count() / 1000000.0);
        return keys_per_sec / 1000000.0;  // Convert to Mkeys/sec
    }

    // Measure memory bandwidth utilization
    double MeasureMemoryBandwidthUtilization() {
        const size_t buffer_size = 1024 * 1024 * 1024;  // 1GB
        const int iterations = 100;

        void* device_buffer;
        ASSERT_TRUE(cudaSuccess == cudaMalloc(&device_buffer, buffer_size));

        std::vector<uint8_t> host_buffer(buffer_size);
        std::fill(host_buffer.begin(), host_buffer.end(), 0x42);

        auto start_time = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < iterations; ++i) {
            ASSERT_TRUE(cudaSuccess == cudaMemcpy(device_buffer, host_buffer.data(),
                                                buffer_size, cudaMemcpyHostToDevice));
            ASSERT_TRUE(cudaSuccess == cudaMemcpy(host_buffer.data(), device_buffer,
                                                buffer_size, cudaMemcpyDeviceToHost));
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

        // Calculate achieved bandwidth
        size_t bytes_transferred = buffer_size * 2 * iterations;  // Host->Device + Device->Host
        double achieved_bandwidth = bytes_transferred / (duration.count() / 1000000.0) / (1024.0 * 1024.0 * 1024.0);  // GB/s

        cudaFree(device_buffer);

        // Get theoretical peak bandwidth (this would be GPU-specific)
        double theoretical_bandwidth = bandwidth_validator_->GetTheoreticalBandwidthGBps();

        return achieved_bandwidth / theoretical_bandwidth;
    }

    // Measure synchronization latency
    double MeasureSynchronizationLatency() {
        const int iterations = 10000;
        std::vector<double> latencies;
        latencies.reserve(iterations);

        for (int i = 0; i < iterations; ++i) {
            auto start_time = std::chrono::high_resolution_clock::now();
            ASSERT_TRUE(cudaSuccess == cudaDeviceSynchronize());
            auto end_time = std::chrono::high_resolution_clock::now();

            double latency_us = std::chrono::duration_cast<std::chrono::nanoseconds>(
                end_time - start_time).count() / 1000.0;
            latencies.push_back(latency_us);
        }

        // Return median latency
        std::sort(latencies.begin(), latencies.end());
        return latencies[latencies.size() / 2];
    }

    // Measure GPU utilization
    double MeasureGpuUtilization() {
        const int measurement_duration_ms = 1000;
        auto start_time = std::chrono::steady_clock::now();

        device_metrics_->StartCollection();

        // Run some workload to keep GPU busy
        std::vector<std::future<void>> futures;
        for (int i = 0; i < 8; ++i) {
            futures.push_back(std::async(std::launch::async, [this]() {
                for (int j = 0; j < 100; ++j) {
                    gpu_executor_->SearchKeys(nullptr, nullptr, 1024);
                }
            }));
        }

        for (auto& future : futures) {
            future.wait();
        }

        auto end_time = std::chrono::steady_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

        auto metrics = device_metrics_->GetMetrics();
        device_metrics_->StopCollection();

        // Calculate average utilization during the measurement period
        double total_utilization = 0.0;
        int sample_count = 0;

        for (const auto& sample : samples) {
            if (sample.timestamp >= start_time && sample.timestamp <= end_time) {
                total_utilization += sample.gpu_utilization;
                sample_count++;
            }
        }

        return sample_count > 0 ? total_utilization / sample_count : 0.0;
    }

    // Test for performance regressions in key search
    RegressionTestResult TestKeySearchRegression() {
        RegressionTestResult result("Key Search Performance");

        double current_performance = MeasureKeySearchPerformance();
        result.current_value = current_performance;
        result.baseline_value = baseline_.key_search_mkeys_per_sec;

        if (baseline_.key_search_mkeys_per_sec > 0) {
            result.regression_percentage = (baseline_.key_search_mkeys_per_sec - current_performance) /
                                         baseline_.key_search_mkeys_per_sec;
            result.passed_regression_test = result.regression_percentage <= KEY_SEARCH_PERFORMANCE_REGRESSION_THRESHOLD;
        }

        result.details = "Current: " + std::to_string(current_performance) + " Mkeys/sec, " +
                        "Baseline: " + std::to_string(baseline_.key_search_mkeys_per_sec) + " Mkeys/sec, " +
                        "Regression: " + std::to_string(result.regression_percentage * 100) + "%";

        return result;
    }

    // Test for memory bandwidth regression
    RegressionTestResult TestMemoryBandwidthRegression() {
        RegressionTestResult result("Memory Bandwidth Utilization");

        double current_utilization = MeasureMemoryBandwidthUtilization();
        result.current_value = current_utilization;
        result.baseline_value = baseline_.memory_bandwidth_utilization;

        if (baseline_.memory_bandwidth_utilization > 0) {
            result.regression_percentage = (baseline_.memory_bandwidth_utilization - current_utilization) /
                                         baseline_.memory_bandwidth_utilization;
            result.passed_regression_test = result.regression_percentage <= MEMORY_BANDWIDTH_REGRESSION_THRESHOLD;
        }

        result.details = "Current: " + std::to_string(current_utilization * 100) + "%, " +
                        "Baseline: " + std::to_string(baseline_.memory_bandwidth_utilization * 100) + "%, " +
                        "Regression: " + std::to_string(result.regression_percentage * 100) + "%";

        return result;
    }

    // Test for synchronization latency regression
    RegressionTestResult TestSynchronizationLatencyRegression() {
        RegressionTestResult result("Synchronization Latency");

        double current_latency = MeasureSynchronizationLatency();
        result.current_value = current_latency;
        result.baseline_value = baseline_.sync_latency_us;

        if (baseline_.sync_latency_us > 0) {
            result.regression_percentage = (current_latency - baseline_.sync_latency_us) /
                                         baseline_.sync_latency_us;
            result.passed_regression_test = result.regression_percentage <= LATENCY_REGRESSION_THRESHOLD;
        }

        result.details = "Current: " + std::to_string(current_latency) + " μs, " +
                        "Baseline: " + std::to_string(baseline_.sync_latency_us) + " μs, " +
                        "Regression: " + std::to_string(result.regression_percentage * 100) + "%";

        return result;
    }

    // Test for GPU utilization regression
    RegressionTestResult TestGpuUtilizationRegression() {
        RegressionTestResult result("GPU Utilization");

        double current_utilization = MeasureGpuUtilization();
        result.current_value = current_utilization;
        result.baseline_value = baseline_.gpu_utilization;

        if (baseline_.gpu_utilization > 0) {
            result.regression_percentage = (baseline_.gpu_utilization - current_utilization) /
                                         baseline_.gpu_utilization;
            result.passed_regression_test = result.regression_percentage <= THROUGHPUT_REGRESSION_THRESHOLD;
        }

        result.details = "Current: " + std::to_string(current_utilization * 100) + "%, " +
                        "Baseline: " + std::to_string(baseline_.gpu_utilization * 100) + "%, " +
                        "Regression: " + std::to_string(result.regression_percentage * 100) + "%";

        return result;
    }

    // Get current git commit hash
    std::string GetCurrentGitCommit() {
        // This would typically execute git command
        // For now, return placeholder
        return "unknown";
    }

    // Generate regression test report
    void GenerateRegressionReport(const std::vector<RegressionTestResult>& results) {
        nlohmann::json report;
        report["test_timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        report["baseline_commit"] = baseline_.git_commit;
        report["gpu_architecture"] = baseline_.gpu_architecture;

        nlohmann::json test_results = nlohmann::json::array();
        bool all_passed = true;

        for (const auto& result : results) {
            nlohmann::json test_result;
            test_result["test_name"] = result.test_name;
            test_result["current_value"] = result.current_value;
            test_result["baseline_value"] = result.baseline_value;
            test_result["regression_percentage"] = result.regression_percentage;
            test_result["passed"] = result.passed_regression_test;
            test_result["details"] = result.details;

            test_results.push_back(test_result);
            all_passed &= result.passed_regression_test;
        }

        report["test_results"] = test_results;
        report["all_tests_passed"] = all_passed;

        // Save report
        std::ofstream report_file("performance_regression_report.json");
        if (report_file.is_open()) {
            report_file << std::setw(4) << report << std::endl;
            std::cout << "Performance regression report saved." << std::endl;
        }

        // Print summary
        std::cout << "\n=== Performance Regression Test Summary ===" << std::endl;
        std::cout << "Overall result: " << (all_passed ? "PASSED" : "FAILED") << std::endl;
        std::cout << "Baseline commit: " << baseline_.git_commit << std::endl;
        std::cout << "GPU architecture: " << baseline_.gpu_architecture << std::endl;

        for (const auto& result : results) {
            std::cout << result.test_name << ": " <<
                (result.passed_regression_test ? "PASS" : "FAIL") << std::endl;
            std::cout << "  " << result.details << std::endl;
        }
    }

    std::unique_ptr<GpuExecutor> gpu_executor_;
    std::unique_ptr<DeviceMetricsCollector> device_metrics_;
    std::unique_ptr<BandwidthValidator> bandwidth_validator_;
    std::unique_ptr<MemoryManager> memory_manager_;

    PerformanceBaseline baseline_;
    std::vector<PerformanceSnapshot> performance_snapshots_;
    std::vector<DeviceMetricsSample> samples;
};

// Main regression test
TEST_F(PerformanceRegressionTest, ComprehensivePerformanceRegression) {
    std::vector<RegressionTestResult> results;

    // Run all regression tests
    results.push_back(TestKeySearchRegression());
    results.push_back(TestMemoryBandwidthRegression());
    results.push_back(TestSynchronizationLatencyRegression());
    results.push_back(TestGpuUtilizationRegression());

    // Generate comprehensive report
    GenerateRegressionReport(results);

    // Check that all tests passed
    for (const auto& result : results) {
        EXPECT_TRUE(result.passed_regression_test)
            << "Performance regression detected in " << result.test_name
            << ": " << result.details;
    }
}

// Specific regression test for key search performance
TEST_F(PerformanceRegressionTest, KeySearchPerformanceRegression) {
    auto result = TestKeySearchRegression();

    EXPECT_TRUE(result.passed_regression_test)
        << "Key search performance regression detected: " << result.details;

    // Also check against absolute minimum threshold
    EXPECT_GE(result.current_value, BASELINE_KEY_SEARCH_MKEYS_PER_SEC * 0.5)
        << "Key search performance below absolute minimum threshold";
}

// Specific regression test for memory bandwidth
TEST_F(PerformanceRegressionTest, MemoryBandwidthRegression) {
    auto result = TestMemoryBandwidthRegression();

    EXPECT_TRUE(result.passed_regression_test)
        << "Memory bandwidth regression detected: " << result.details;

    // Check absolute minimum
    EXPECT_GE(result.current_value, BASELINE_MEMORY_BANDWIDTH_UTILIZATION * 0.5)
        << "Memory bandwidth utilization below absolute minimum threshold";
}

// Performance trend analysis test
TEST_F(PerformanceRegressionTest, PerformanceTrendAnalysis) {
    // Collect multiple performance snapshots over time
    const int num_snapshots = 10;

    for (int i = 0; i < num_snapshots; ++i) {
        PerformanceSnapshot snapshot;
        snapshot.key_search_throughput = MeasureKeySearchPerformance();
        snapshot.memory_bandwidth_utilization = MeasureMemoryBandwidthUtilization();
        snapshot.synchronization_latency = MeasureSynchronizationLatency();
        snapshot.gpu_utilization = MeasureGpuUtilization();
        snapshot.timestamp = std::chrono::system_clock::now();

        performance_snapshots_.push_back(snapshot);

        // Small delay between measurements
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Analyze trends
    ASSERT_GE(performance_snapshots_.size(), 2);

    // Calculate performance variance
    double throughput_variance = CalculateVariance(
        performance_snapshots_, &PerformanceSnapshot::key_search_throughput);

    double bandwidth_variance = CalculateVariance(
        performance_snapshots_, &PerformanceSnapshot::memory_bandwidth_utilization);

    // Performance should be relatively stable (low variance)
    EXPECT_LE(throughput_variance, BASELINE_KEY_SEARCH_MKEYS_PER_SEC * 0.1)
        << "Key search performance variance too high: " << throughput_variance;

    EXPECT_LE(bandwidth_variance, 0.05)
        << "Memory bandwidth utilization variance too high: " << bandwidth_variance;
}

// Baseline update test (for manual baseline updates)
TEST_F(PerformanceRegressionTest, UpdateBaselineOnImprovement) {
    double current_performance = MeasureKeySearchPerformance();

    // If current performance is significantly better than baseline, update it
    if (baseline_.key_search_mkeys_per_sec > 0 &&
        current_performance > baseline_.key_search_mkeys_per_sec * 1.10) {  // 10% improvement

        std::cout << "Significant performance improvement detected. Updating baseline..." << std::endl;

        baseline_.key_search_mkeys_per_sec = current_performance;
        baseline_.memory_bandwidth_utilization = MeasureMemoryBandwidthUtilization();
        baseline_.sync_latency_us = MeasureSynchronizationLatency();
        baseline_.gpu_utilization = MeasureGpuUtilization();
        baseline_.timestamp = std::chrono::system_clock::now();
        baseline_.git_commit = GetCurrentGitCommit();

        SavePerformanceBaseline(baseline_);

        std::cout << "Baseline updated with new performance metrics." << std::endl;
    }
}

}  // namespace testing
}  // namespace keycuda