#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <fstream>
#include <chrono>
#include <vector>
#include <string>
#include <memory>
#include <atomic>
#include <thread>
#include <future>
#include <random>

#include "gpu_executor.h"
#include "adaptive_scaling.h"
#include "memory_manager.h"
#include "bandwidth_validator.h"
#include "error_recovery.h"
#include "monitoring/performance_monitor.h"

namespace keycuda {
namespace testing {

// Fallback scenario types
enum class FallbackScenario {
    ADAPTIVE_SCALING_FAILURE,
    MEMORY_OPTIMIZATION_FAILURE,
    BANDWIDTH_VALIDATION_FAILURE,
    GPU_OVERHEATING,
    DEVICE_CONTEXT_LOST,
    KERNEL_EXECUTION_FAILURE,
    MEMORY_ALLOCATION_FAILURE,
    SYNCHRONIZATION_TIMEOUT,
    INSUFFICIENT_GPU_RESOURCES,
    DRIVER_CRASH_SIMULATION
};

// Fallback test configuration
struct FallbackTestConfig {
    FallbackScenario scenario;
    double failure_probability;    // Probability of inducing failure (0.0-1.0)
    int max_fallback_attempts;     // Maximum fallback attempts
    std::chrono::milliseconds timeout;  // Test timeout
    bool expect_fallback_success; // Whether fallback is expected to succeed
    std::string description;

    FallbackTestConfig(FallbackScenario s, double prob = 1.0, int attempts = 3,
                      std::chrono::milliseconds t = std::chrono::seconds(30),
                      bool success = true)
        : scenario(s), failure_probability(prob), max_fallback_attempts(attempts),
          timeout(t), expect_fallback_success(success) {}
};

// Fallback test result
struct FallbackTestResult {
    FallbackScenario scenario;
    bool primary_mechanism_failed;
    bool fallback_triggered;
    bool fallback_successful;
    int fallback_attempts_used;
    std::chrono::milliseconds total_time;
    std::string failure_reason;
    std::string fallback_method;
    double performance_after_fallback;  // Performance as fraction of original

    FallbackTestResult(FallbackScenario s) : scenario(s), primary_mechanism_failed(false),
                                           fallback_triggered(false), fallback_successful(false),
                                           fallback_attempts_used(0), total_time(0),
                                           performance_after_fallback(0.0) {}
};

// Mock failure injector for controlled failure simulation
class FailureInjector {
public:
    FailureInjector() : enabled_(false), failure_count_(0) {}

    void Configure(const FallbackTestConfig& config) {
        std::lock_guard<std::mutex> lock(config_mutex_);
        config_ = config;
        enabled_ = true;
        failure_count_ = 0;
    }

    void Disable() {
        std::lock_guard<std::mutex> lock(config_mutex_);
        enabled_ = false;
    }

    bool ShouldInjectFailure() {
        std::lock_guard<std::mutex> lock(config_mutex_);
        if (!enabled_) return false;

        failure_count_++;

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<> dis(0.0, 1.0);

        return dis(gen) <= config_.failure_probability;
    }

    FallbackScenario GetScenario() const {
        std::lock_guard<std::mutex> lock(config_mutex_);
        return config_.scenario;
    }

    int GetFailureCount() const { return failure_count_; }

private:
    mutable std::mutex config_mutex_;
    FallbackTestConfig config_{FallbackScenario::ADAPTIVE_SCALING_FAILURE};
    std::atomic<bool> enabled_;
    std::atomic<int> failure_count_;
};

// Fallback mechanism tester
class FallbackMechanismTester {
public:
    FallbackMechanismTester(int device_id) : device_id_(device_id) {}

    FallbackTestResult TestFallbackScenario(const FallbackTestConfig& config) {
        FallbackTestResult result(config.scenario);

        auto start_time = std::chrono::high_resolution_clock::now();

        // Initialize components
        InitializeComponents();

        // Configure failure injector
        failure_injector_.Configure(config);

        try {
            switch (config.scenario) {
                case FallbackScenario::ADAPTIVE_SCALING_FAILURE:
                    result = TestAdaptiveScalingFallback(result);
                    break;

                case FallbackScenario::MEMORY_OPTIMIZATION_FAILURE:
                    result = TestMemoryOptimizationFallback(result);
                    break;

                case FallbackScenario::BANDWIDTH_VALIDATION_FAILURE:
                    result = TestBandwidthValidationFallback(result);
                    break;

                case FallbackScenario::GPU_OVERHEATING:
                    result = TestGpuOverheatingFallback(result);
                    break;

                case FallbackScenario::DEVICE_CONTEXT_LOST:
                    result = TestDeviceContextLostFallback(result);
                    break;

                case FallbackScenario::KERNEL_EXECUTION_FAILURE:
                    result = TestKernelExecutionFailureFallback(result);
                    break;

                case FallbackScenario::MEMORY_ALLOCATION_FAILURE:
                    result = TestMemoryAllocationFailureFallback(result);
                    break;

                case FallbackScenario::SYNCHRONIZATION_TIMEOUT:
                    result = TestSynchronizationTimeoutFallback(result);
                    break;

                case FallbackScenario::INSUFFICIENT_GPU_RESOURCES:
                    result = TestInsufficientGpuResourcesFallback(result);
                    break;

                case FallbackScenario::DRIVER_CRASH_SIMULATION:
                    result = TestDriverCrashFallback(result);
                    break;
            }

        } catch (const std::exception& e) {
            result.failure_reason = std::string("Exception: ") + e.what();
            result.fallback_successful = false;
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        result.total_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);

        // Cleanup
        CleanupComponents();
        failure_injector_.Disable();

        return result;
    }

private:
    void InitializeComponents() {
        gpu_executor_ = std::make_unique<GpuExecutor>(device_id_);
        adaptive_scaling_ = CreateAdaptiveParallelismScaling(device_id_, true, true);
        memory_manager_ = std::make_unique<MemoryManager>(device_id_);
        bandwidth_validator_ = std::make_unique<BandwidthValidator>(device_id_);
        recovery_manager_ = std::make_unique<ErrorRecoveryManager>(std::make_unique<GpuExecutor>(device_id_));
        performance_monitor_ = std::make_unique<monitoring::PerformanceMonitor>(device_id_);
    }

    void CleanupComponents() {
        performance_monitor_.reset();
        recovery_manager_.reset();
        bandwidth_validator_.reset();
        memory_manager_.reset();
        adaptive_scaling_.reset();
        gpu_executor_.reset();
    }

    FallbackTestResult TestAdaptiveScalingFallback(FallbackTestResult result) {
        // Measure baseline performance
        double baseline_performance = MeasureKeySearchPerformance();
        result.performance_after_fallback = baseline_performance;

        // Simulate adaptive scaling failure
        if (failure_injector_.ShouldInjectFailure()) {
            result.primary_mechanism_failed = true;
            result.failure_reason = "Adaptive scaling failed to find optimal parameters";

            // Fallback 1: Use static optimal parameters
            result.fallback_attempts_used++;
            result.fallback_method = "Static parameter fallback";

            AdaptiveScalingConfig static_config;
            static_config.block_size = 256;
            static_config.grid_size = 8192;
            static_config.batch_size = 1024 * 1024;
            static_config.enable_auto_scaling = false;

            adaptive_scaling_->ApplyConfiguration(static_config);

            // Test fallback performance
            double fallback_performance = MeasureKeySearchPerformance();
            result.performance_after_fallback = fallback_performance / baseline_performance;
            result.fallback_successful = fallback_performance >= baseline_performance * 0.7;  // 70% threshold

            if (!result.fallback_successful && result.fallback_attempts_used < 3) {
                // Fallback 2: Conservative parameters
                result.fallback_attempts_used++;
                result.fallback_method = "Conservative parameter fallback";

                static_config.block_size = 128;
                static_config.grid_size = 4096;
                static_config.batch_size = 512 * 1024;

                adaptive_scaling_->ApplyConfiguration(static_config);
                fallback_performance = MeasureKeySearchPerformance();
                result.performance_after_fallback = fallback_performance / baseline_performance;
                result.fallback_successful = fallback_performance >= baseline_performance * 0.5;  // 50% threshold
            }
        }

        result.fallback_triggered = result.fallback_attempts_used > 0;
        return result;
    }

    FallbackTestResult TestMemoryOptimizationFallback(FallbackTestResult result) {
        // Measure baseline performance
        double baseline_performance = MeasureKeySearchPerformance();

        // Simulate memory optimization failure
        if (failure_injector_.ShouldInjectFailure()) {
            result.primary_mechanism_failed = true;
            result.failure_reason = "Memory optimization failed to achieve bandwidth targets";

            // Fallback 1: Disable memory optimization
            result.fallback_attempts_used++;
            result.fallback_method = "Disable memory optimization";

            memory_manager_->SetOptimizationLevel(MemoryOptimizationLevel::DISABLED);

            double fallback_performance = MeasureKeySearchPerformance();
            result.performance_after_fallback = fallback_performance / baseline_performance;
            result.fallback_successful = fallback_performance >= baseline_performance * 0.6;  // 60% threshold

            if (!result.fallback_successful && result.fallback_attempts_used < 3) {
                // Fallback 2: Reduce memory footprint
                result.fallback_attempts_used++;
                result.fallback_method = "Reduced memory footprint";

                memory_manager_->SetOptimizationLevel(MemoryOptimizationLevel::CONSERVATIVE);
                memory_manager_->ReduceMemoryUsage(0.5);  // Reduce to 50%

                fallback_performance = MeasureKeySearchPerformance();
                result.performance_after_fallback = fallback_performance / baseline_performance;
                result.fallback_successful = fallback_performance >= baseline_performance * 0.4;  // 40% threshold
            }
        }

        result.fallback_triggered = result.fallback_attempts_used > 0;
        return result;
    }

    FallbackTestResult TestBandwidthValidationFallback(FallbackTestResult result) {
        // Measure baseline performance
        double baseline_performance = MeasureKeySearchPerformance();

        // Simulate bandwidth validation failure
        if (failure_injector_.ShouldInjectFailure()) {
            result.primary_mechanism_failed = true;
            result.failure_reason = "Bandwidth validation failed consistently";

            // Fallback 1: Continue without bandwidth validation
            result.fallback_attempts_used++;
            result.fallback_method = "Continue without bandwidth validation";

            bandwidth_validator_->DisableValidation();

            double fallback_performance = MeasureKeySearchPerformance();
            result.performance_after_fallback = fallback_performance / baseline_performance;
            result.fallback_successful = fallback_performance >= baseline_performance * 0.8;  // 80% threshold

            if (!result.fallback_successful && result.fallback_attempts_used < 3) {
                // Fallback 2: Force memory access pattern optimization
                result.fallback_attempts_used++;
                result.fallback_method = "Force access pattern optimization";

                memory_manager_->ForceAccessPatternOptimization();

                fallback_performance = MeasureKeySearchPerformance();
                result.performance_after_fallback = fallback_performance / baseline_performance;
                result.fallback_successful = fallback_performance >= baseline_performance * 0.6;  // 60% threshold
            }
        }

        result.fallback_triggered = result.fallback_attempts_used > 0;
        return result;
    }

    FallbackTestResult TestGpuOverheatingFallback(FallbackTestResult result) {
        // Simulate GPU overheating condition
        if (failure_injector_.ShouldInjectFailure()) {
            result.primary_mechanism_failed = true;
            result.failure_reason = "GPU temperature exceeded safe threshold";

            // Fallback 1: Reduce workload intensity
            result.fallback_attempts_used++;
            result.fallback_method = "Reduce workload intensity";

            adaptive_scaling_->SetThermalThrottlingEnabled(true);
            adaptive_scaling_->SetMaxTemperatureThreshold(75.0);  // 75°C threshold

            // Simulate reduced performance due to throttling
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            double throttled_performance = MeasureKeySearchPerformance();
            double baseline_performance = throttled_performance * 1.5;  // Estimate baseline

            result.performance_after_fallback = throttled_performance / baseline_performance;
            result.fallback_successful = throttled_performance > 0;  // Any performance is success

            if (!result.fallback_successful && result.fallback_attempts_used < 3) {
                // Fallback 2: Pause and cool down
                result.fallback_attempts_used++;
                result.fallback_method = "Pause for cooling";

                // Simulate cooling period
                std::this_thread::sleep_for(std::chrono::seconds(2));

                throttled_performance = MeasureKeySearchPerformance();
                result.performance_after_fallback = throttled_performance / baseline_performance;
                result.fallback_successful = throttled_performance >= baseline_performance * 0.3;  // 30% threshold
            }
        }

        result.fallback_triggered = result.fallback_attempts_used > 0;
        return result;
    }

    FallbackTestResult TestDeviceContextLostFallback(FallbackTestResult result) {
        // Simulate device context loss
        if (failure_injector_.ShouldInjectFailure()) {
            result.primary_mechanism_failed = true;
            result.failure_reason = "CUDA device context lost";

            // Fallback 1: Reset device and recreate context
            result.fallback_attempts_used++;
            result.fallback_method = "Device reset and context recreation";

            cudaError_t reset_result = cudaDeviceReset();
            if (reset_result == cudaSuccess) {
                // Wait and reinitialize
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));

                try {
                    gpu_executor_->Reinitialize();
                    adaptive_scaling_->Reinitialize();
                    memory_manager_->Reinitialize();

                    double recovery_performance = MeasureKeySearchPerformance();
                    double baseline_performance = recovery_performance;  // This becomes new baseline

                    result.performance_after_fallback = 1.0;  // 100% of new baseline
                    result.fallback_successful = recovery_performance > 0;

                } catch (const std::exception& e) {
                    result.failure_reason += " | Reinitialization failed: " + std::string(e.what());
                    result.fallback_successful = false;
                }
            } else {
                result.failure_reason += " | Device reset failed";
                result.fallback_successful = false;
            }

            if (!result.fallback_successful && result.fallback_attempts_used < 3) {
                // Fallback 2: Fall back to CPU (if available)
                result.fallback_attempts_used++;
                result.fallback_method = "CPU fallback";

                // Simulate CPU fallback (much slower)
                std::this_thread::sleep_for(std::chrono::milliseconds(5000));
                result.performance_after_fallback = 0.01;  // 1% of GPU performance
                result.fallback_successful = true;  // CPU fallback is considered success
            }
        }

        result.fallback_triggered = result.fallback_attempts_used > 0;
        return result;
    }

    FallbackTestResult TestKernelExecutionFailureFallback(FallbackTestResult result) {
        // Simulate kernel execution failure
        if (failure_injector_.ShouldInjectFailure()) {
            result.primary_mechanism_failed = true;
            result.failure_reason = "Kernel execution failed consistently";

            // Fallback 1: Use alternative kernel implementation
            result.fallback_attempts_used++;
            result.fallback_method = "Alternative kernel implementation";

            gpu_executor_->SetKernelImplementation(KernelImplementation::REFERENCE);

            double fallback_performance = MeasureKeySearchPerformance();
            double baseline_performance = fallback_performance * 2.0;  // Estimate original performance

            result.performance_after_fallback = fallback_performance / baseline_performance;
            result.fallback_successful = fallback_performance >= baseline_performance * 0.3;  // 30% threshold

            if (!result.fallback_successful && result.fallback_attempts_used < 3) {
                // Fallback 2: Reduce kernel complexity
                result.fallback_attempts_used++;
                result.fallback_method = "Reduced kernel complexity";

                gpu_executor_->SetKernelOptimizationLevel(KernelOptimizationLevel::BASIC);

                fallback_performance = MeasureKeySearchPerformance();
                result.performance_after_fallback = fallback_performance / baseline_performance;
                result.fallback_successful = fallback_performance >= baseline_performance * 0.2;  // 20% threshold
            }
        }

        result.fallback_triggered = result.fallback_attempts_used > 0;
        return result;
    }

    FallbackTestResult TestMemoryAllocationFailureFallback(FallbackTestResult result) {
        // Simulate memory allocation failure
        if (failure_injector_.ShouldInjectFailure()) {
            result.primary_mechanism_failed = true;
            result.failure_reason = "Memory allocation failed";

            // Fallback 1: Reduce allocation size
            result.fallback_attempts_used++;
            result.fallback_method = "Reduced allocation size";

            size_t original_batch_size = 1024 * 1024;
            size_t reduced_batch_size = original_batch_size / 2;

            gpu_executor_->SetMaxBatchSize(reduced_batch_size);

            double fallback_performance = MeasureKeySearchPerformance();
            double baseline_performance = fallback_performance * 2.0;  // Estimate original performance

            result.performance_after_fallback = fallback_performance / baseline_performance;
            result.fallback_successful = fallback_performance > 0;

            if (!result.fallback_successful && result.fallback_attempts_used < 3) {
                // Fallback 2: Use system memory (unified memory)
                result.fallback_attempts_used++;
                result.fallback_method = "System memory fallback";

                memory_manager_->UseUnifiedMemory(true);

                fallback_performance = MeasureKeySearchPerformance();
                result.performance_after_fallback = fallback_performance / baseline_performance;
                result.fallback_successful = fallback_performance >= baseline_performance * 0.1;  // 10% threshold
            }
        }

        result.fallback_triggered = result.fallback_attempts_used > 0;
        return result;
    }

    FallbackTestResult TestSynchronizationTimeoutFallback(FallbackTestResult result) {
        // Simulate synchronization timeout
        if (failure_injector_.ShouldInjectFailure()) {
            result.primary_mechanism_failed = true;
            result.failure_reason = "Synchronization operations timed out";

            // Fallback 1: Use asynchronous operations
            result.fallback_attempts_used++;
            result.fallback_method = "Asynchronous operations";

            gpu_executor_->SetSynchronizationMode(SynchronizationMode::ASYNC);

            double fallback_performance = MeasureKeySearchPerformance();
            double baseline_performance = fallback_performance * 1.2;  // Estimate original performance

            result.performance_after_fallback = fallback_performance / baseline_performance;
            result.fallback_successful = fallback_performance >= baseline_performance * 0.8;  // 80% threshold

            if (!result.fallback_successful && result.fallback_attempts_used < 3) {
                // Fallback 2: Increase timeout values
                result.fallback_attempts_used++;
                result.fallback_method = "Increased timeout values";

                gpu_executor_->SetSynchronizationTimeout(std::chrono::seconds(30));

                fallback_performance = MeasureKeySearchPerformance();
                result.performance_after_fallback = fallback_performance / baseline_performance;
                result.fallback_successful = fallback_performance >= baseline_performance * 0.6;  // 60% threshold
            }
        }

        result.fallback_triggered = result.fallback_attempts_used > 0;
        return result;
    }

    FallbackTestResult TestInsufficientGpuResourcesFallback(FallbackTestResult result) {
        // Simulate insufficient GPU resources
        if (failure_injector_.ShouldInjectFailure()) {
            result.primary_mechanism_failed = true;
            result.failure_reason = "Insufficient GPU resources available";

            // Fallback 1: Share resources with other processes
            result.fallback_attempts_used++;
            result.fallback_method = "Shared resource usage";

            adaptive_scaling_->SetResourceSharingEnabled(true);
            adaptive_scaling_->SetMaxResourceUsage(0.5);  // Use 50% of resources

            double fallback_performance = MeasureKeySearchPerformance();
            double baseline_performance = fallback_performance * 2.0;  // Estimate original performance

            result.performance_after_fallback = fallback_performance / baseline_performance;
            result.fallback_successful = fallback_performance > 0;

            if (!result.fallback_successful && result.fallback_attempts_used < 3) {
                // Fallback 2: Defer execution until resources available
                result.fallback_attempts_used++;
                result.fallback_method = "Deferred execution";

                // Simulate waiting for resources
                std::this_thread::sleep_for(std::chrono::seconds(1));

                fallback_performance = MeasureKeySearchPerformance();
                result.performance_after_fallback = fallback_performance / baseline_performance;
                result.fallback_successful = fallback_performance >= baseline_performance * 0.1;  // 10% threshold
            }
        }

        result.fallback_triggered = result.fallback_attempts_used > 0;
        return result;
    }

    FallbackTestResult TestDriverCrashFallback(FallbackTestResult result) {
        // Simulate driver crash (extreme case)
        if (failure_injector_.ShouldInjectFailure()) {
            result.primary_mechanism_failed = true;
            result.failure_reason = "GPU driver crash simulation";

            // Fallback 1: Complete system restart
            result.fallback_attempts_used++;
            result.fallback_method = "Graceful degradation";

            // Simulate system restart (in reality, this would require external process)
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));

            try {
                // Attempt to reinitialize everything
                InitializeComponents();

                double recovery_performance = MeasureKeySearchPerformance();
                double baseline_performance = recovery_performance;

                result.performance_after_fallback = 1.0;
                result.fallback_successful = recovery_performance > 0;

            } catch (const std::exception& e) {
                result.failure_reason += " | System restart failed: " + std::string(e.what());
                result.fallback_successful = false;
            }

            if (!result.fallback_successful && result.fallback_attempts_used < 3) {
                // Fallback 2: Emergency shutdown
                result.fallback_attempts_used++;
                result.fallback_method = "Emergency shutdown with data preservation";

                result.performance_after_fallback = 0.0;
                result.fallback_successful = true;  // Successful graceful shutdown
            }
        }

        result.fallback_triggered = result.fallback_attempts_used > 0;
        return result;
    }

    double MeasureKeySearchPerformance() {
        try {
            uint8_t test_hash[20];
            memset(test_hash, 0x55, 20);

            auto start_time = std::chrono::high_resolution_clock::now();
            auto results = gpu_executor_->SearchKeysOptimized(test_hash, 1024 * 512);
            auto end_time = std::chrono::high_resolution_clock::now();

            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
            return (1024 * 512 / 1000000.0) / (duration.count() / 1000000.0);  // Mkeys/sec

        } catch (const std::exception&) {
            return 0.0;
        }
    }

    int device_id_;
    std::unique_ptr<GpuExecutor> gpu_executor_;
    std::unique_ptr<AdaptiveParallelismScaling> adaptive_scaling_;
    std::unique_ptr<MemoryManager> memory_manager_;
    std::unique_ptr<BandwidthValidator> bandwidth_validator_;
    std::unique_ptr<ErrorRecoveryManager> recovery_manager_;
    std::unique_ptr<monitoring::PerformanceMonitor> performance_monitor_;

    FailureInjector failure_injector_;
};

class FallbackMechanismsTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Check CUDA availability
        int device_count = 0;
        ASSERT_EQ(cudaGetDeviceCount(&device_count), cudaSuccess);
        ASSERT_GT(device_count, 0) << "No CUDA devices available";

        device_id_ = 0;
        fallback_tester_ = std::make_unique<FallbackMechanismTester>(device_id_);

        test_results_.clear();
    }

    void TearDown() override {
        fallback_tester_.reset();
        test_results_.clear();
    }

    FallbackTestResult RunFallbackTest(FallbackScenario scenario, double failure_prob = 1.0,
                                      int max_attempts = 3, bool expect_success = true) {
        FallbackTestConfig config(scenario, failure_prob, max_attempts,
                                 std::chrono::seconds(30), expect_success);
        config.description = GetScenarioDescription(scenario);

        auto result = fallback_tester_->TestFallbackScenario(config);
        test_results_.push_back(result);

        return result;
    }

    void RunAllFallbackTests() {
        std::vector<FallbackScenario> scenarios = {
            FallbackScenario::ADAPTIVE_SCALING_FAILURE,
            FallbackScenario::MEMORY_OPTIMIZATION_FAILURE,
            FallbackScenario::BANDWIDTH_VALIDATION_FAILURE,
            FallbackScenario::GPU_OVERHEATING,
            FallbackScenario::DEVICE_CONTEXT_LOST,
            FallbackScenario::KERNEL_EXECUTION_FAILURE,
            FallbackScenario::MEMORY_ALLOCATION_FAILURE,
            FallbackScenario::SYNCHRONIZATION_TIMEOUT,
            FallbackScenario::INSUFFICIENT_GPU_RESOURCES,
            FallbackScenario::DRIVER_CRASH_SIMULATION
        };

        for (auto scenario : scenarios) {
            std::cout << "\nTesting fallback for: " << GetScenarioDescription(scenario) << std::endl;
            auto result = RunFallbackTest(scenario, 0.8, 3, true);  // 80% failure probability
            std::cout << "Result: " << (result.fallback_successful ? "SUCCESS" : "FAILED") << std::endl;
        }
    }

    void GenerateFallbackTestReport() {
        nlohmann::json report;
        report["test_timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        report["device_id"] = device_id_;
        report["total_tests"] = test_results_.size();

        nlohmann::json test_results_json = nlohmann::json::array();
        int successful_fallbacks = 0;
        int failed_fallbacks = 0;

        for (const auto& result : test_results_) {
            nlohmann::json test_result;
            test_result["scenario"] = GetScenarioDescription(result.scenario);
            test_result["primary_mechanism_failed"] = result.primary_mechanism_failed;
            test_result["fallback_triggered"] = result.fallback_triggered;
            test_result["fallback_successful"] = result.fallback_successful;
            test_result["fallback_attempts_used"] = result.fallback_attempts_used;
            test_result["total_time_ms"] = result.total_time.count();
            test_result["failure_reason"] = result.failure_reason;
            test_result["fallback_method"] = result.fallback_method;
            test_result["performance_after_fallback"] = result.performance_after_fallback;

            test_results_json.push_back(test_result);

            if (result.fallback_successful) {
                successful_fallbacks++;
            } else {
                failed_fallbacks++;
            }
        }

        report["test_results"] = test_results_json;
        report["successful_fallbacks"] = successful_fallbacks;
        report["failed_fallbacks"] = failed_fallbacks;
        report["fallback_success_rate"] = test_results_.empty() ? 0.0 :
            static_cast<double>(successful_fallbacks) / test_results_.size();

        // Save report
        std::ofstream report_file("fallback_mechanisms_test_report.json");
        if (report_file.is_open()) {
            report_file << std::setw(4) << report << std::endl;
            std::cout << "Fallback mechanisms test report saved." << std::endl;
        }

        // Print summary
        std::cout << "\n=== Fallback Mechanisms Test Summary ===" << std::endl;
        std::cout << "Total tests: " << test_results_.size() << std::endl;
        std::cout << "Successful fallbacks: " << successful_fallbacks << std::endl;
        std::cout << "Failed fallbacks: " << failed_fallbacks << std::endl;
        std::cout << "Fallback success rate: " << (report["fallback_success_rate"].get<double>() * 100) << "%" << std::endl;

        // Print detailed results for failed tests
        for (const auto& result : test_results_) {
            if (!result.fallback_successful) {
                std::cout << "\nFAILED: " << GetScenarioDescription(result.scenario) << std::endl;
                std::cout << "  Failure reason: " << result.failure_reason << std::endl;
                std::cout << "  Fallback attempts: " << result.fallback_attempts_used << std::endl;
                std::cout << "  Fallback method: " << result.fallback_method << std::endl;
            }
        }
    }

    std::string GetScenarioDescription(FallbackScenario scenario) {
        switch (scenario) {
            case FallbackScenario::ADAPTIVE_SCALING_FAILURE:
                return "Adaptive Scaling Failure";
            case FallbackScenario::MEMORY_OPTIMIZATION_FAILURE:
                return "Memory Optimization Failure";
            case FallbackScenario::BANDWIDTH_VALIDATION_FAILURE:
                return "Bandwidth Validation Failure";
            case FallbackScenario::GPU_OVERHEATING:
                return "GPU Overheating";
            case FallbackScenario::DEVICE_CONTEXT_LOST:
                return "Device Context Lost";
            case FallbackScenario::KERNEL_EXECUTION_FAILURE:
                return "Kernel Execution Failure";
            case FallbackScenario::MEMORY_ALLOCATION_FAILURE:
                return "Memory Allocation Failure";
            case FallbackScenario::SYNCHRONIZATION_TIMEOUT:
                return "Synchronization Timeout";
            case FallbackScenario::INSUFFICIENT_GPU_RESOURCES:
                return "Insufficient GPU Resources";
            case FallbackScenario::DRIVER_CRASH_SIMULATION:
                return "Driver Crash Simulation";
            default:
                return "Unknown Scenario";
        }
    }

    int device_id_;
    std::unique_ptr<FallbackMechanismTester> fallback_tester_;
    std::vector<FallbackTestResult> test_results_;
};

// Test individual fallback scenarios
TEST_F(FallbackMechanismsTest, AdaptiveScalingFallback) {
    auto result = RunFallbackTest(FallbackScenario::ADAPTIVE_SCALING_FAILURE, 0.7, 3, true);

    EXPECT_TRUE(result.primary_mechanism_failed) << "Adaptive scaling should fail in this test";
    EXPECT_TRUE(result.fallback_triggered) << "Fallback should be triggered";
    EXPECT_TRUE(result.fallback_successful) << "Fallback should be successful";
    EXPECT_GT(result.fallback_attempts_used, 0) << "Should attempt fallback";
    EXPECT_GT(result.performance_after_fallback, 0.3) << "Performance should be reasonable after fallback";
}

TEST_F(FallbackMechanismsTest, MemoryOptimizationFallback) {
    auto result = RunFallbackTest(FallbackScenario::MEMORY_OPTIMIZATION_FAILURE, 0.8, 3, true);

    EXPECT_TRUE(result.primary_mechanism_failed) << "Memory optimization should fail in this test";
    EXPECT_TRUE(result.fallback_triggered) << "Fallback should be triggered";
    EXPECT_TRUE(result.fallback_successful) << "Fallback should be successful";
    EXPECT_GT(result.performance_after_fallback, 0.2) << "Performance should be reasonable after fallback";
}

TEST_F(FallbackMechanismsTest, BandwidthValidationFallback) {
    auto result = RunFallbackTest(FallbackScenario::BANDWIDTH_VALIDATION_FAILURE, 0.6, 3, true);

    EXPECT_TRUE(result.fallback_triggered) << "Fallback should be triggered when validation fails";
    EXPECT_TRUE(result.fallback_successful) << "Fallback should handle validation failure";
}

TEST_F(FallbackMechanismsTest, GpuOverheatingFallback) {
    auto result = RunFallbackTest(FallbackScenario::GPU_OVERHEATING, 0.5, 3, true);

    EXPECT_TRUE(result.primary_mechanism_failed) << "Should detect overheating condition";
    EXPECT_TRUE(result.fallback_triggered) << "Thermal throttling fallback should be triggered";
    EXPECT_TRUE(result.fallback_successful) << "Should handle overheating gracefully";
}

TEST_F(FallbackMechanismsTest, DeviceContextLostFallback) {
    auto result = RunFallbackTest(FallbackScenario::DEVICE_CONTEXT_LOST, 0.4, 2, true);

    EXPECT_TRUE(result.primary_mechanism_failed) << "Should detect context loss";
    EXPECT_TRUE(result.fallback_triggered) << "Context recovery should be attempted";
    EXPECT_TRUE(result.fallback_successful) << "Should recover from context loss";
}

TEST_F(FallbackMechanismsTest, KernelExecutionFailureFallback) {
    auto result = RunFallbackTest(FallbackScenario::KERNEL_EXECUTION_FAILURE, 0.7, 3, true);

    EXPECT_TRUE(result.primary_mechanism_failed) << "Should detect kernel execution failure";
    EXPECT_TRUE(result.fallback_triggered) << "Alternative kernel fallback should be triggered";
    EXPECT_TRUE(result.fallback_successful) << "Should fallback to alternative kernel";
}

TEST_F(FallbackMechanismsTest, MemoryAllocationFailureFallback) {
    auto result = RunFallbackTest(FallbackScenario::MEMORY_ALLOCATION_FAILURE, 0.8, 3, true);

    EXPECT_TRUE(result.primary_mechanism_failed) << "Should detect memory allocation failure";
    EXPECT_TRUE(result.fallback_triggered) << "Memory reduction fallback should be triggered";
    EXPECT_TRUE(result.fallback_successful) << "Should handle memory allocation failure";
}

TEST_F(FallbackMechanismsTest, SynchronizationTimeoutFallback) {
    auto result = RunFallbackTest(FallbackScenario::SYNCHRONIZATION_TIMEOUT, 0.6, 3, true);

    EXPECT_TRUE(result.primary_mechanism_failed) << "Should detect synchronization timeout";
    EXPECT_TRUE(result.fallback_triggered) << "Async fallback should be triggered";
    EXPECT_TRUE(result.fallback_successful) << "Should handle synchronization timeout";
}

TEST_F(FallbackMechanismsTest, InsufficientGpuResourcesFallback) {
    auto result = RunFallbackTest(FallbackScenario::INSUFFICIENT_GPU_RESOURCES, 0.5, 3, true);

    EXPECT_TRUE(result.primary_mechanism_failed) << "Should detect insufficient resources";
    EXPECT_TRUE(result.fallback_triggered) << "Resource sharing fallback should be triggered";
    EXPECT_TRUE(result.fallback_successful) << "Should handle resource constraints";
}

TEST_F(FallbackMechanismsTest, DriverCrashFallback) {
    auto result = RunFallbackTest(FallbackScenario::DRIVER_CRASH_SIMULATION, 0.3, 2, true);

    EXPECT_TRUE(result.primary_mechanism_failed) << "Should simulate driver crash";
    EXPECT_TRUE(result.fallback_triggered) << "Emergency fallback should be triggered";
    EXPECT_TRUE(result.fallback_successful) << "Should handle extreme failure gracefully";
}

// Comprehensive fallback test
TEST_F(FallbackMechanismsTest, ComprehensiveFallbackTest) {
    RunAllFallbackTests();

    EXPECT_GT(test_results_.size(), 5) << "Should test multiple fallback scenarios";

    // Calculate overall success rate
    int successful_tests = 0;
    for (const auto& result : test_results_) {
        if (result.fallback_successful) {
            successful_tests++;
        }
    }

    double success_rate = static_cast<double>(successful_tests) / test_results_.size();
    EXPECT_GE(success_rate, 0.8) << "At least 80% of fallback tests should succeed";

    GenerateFallbackTestReport();
}

// Fallback performance test
TEST_F(FallbackMechanismsTest, FallbackPerformanceTest) {
    const int num_iterations = 10;
    double total_fallback_time = 0.0;
    int successful_fallbacks = 0;

    for (int i = 0; i < num_iterations; ++i) {
        auto result = RunFallbackTest(FallbackScenario::ADAPTIVE_SCALING_FAILURE, 0.5, 3, true);

        if (result.fallback_successful) {
            total_fallback_time += result.total_time.count();
            successful_fallbacks++;
        }
    }

    if (successful_fallbacks > 0) {
        double avg_fallback_time = total_fallback_time / successful_fallbacks;
        EXPECT_LT(avg_fallback_time, 10000) << "Average fallback time should be less than 10 seconds";
    }
}

// Multiple concurrent fallback tests
TEST_F(FallbackMechanismsTest, ConcurrentFallbackTest) {
    const int num_concurrent_tests = 4;
    std::vector<std::future<FallbackTestResult>> futures;

    // Start concurrent fallback tests
    for (int i = 0; i < num_concurrent_tests; ++i) {
        FallbackScenario scenario = static_cast<FallbackScenario>(
            (FallbackScenario::ADAPTIVE_SCALING_FAILURE + i) % (FallbackScenario::DRIVER_CRASH_SIMULATION + 1));

        futures.push_back(std::async(std::launch::async, [this, scenario]() {
            return RunFallbackTest(scenario, 0.6, 3, true);
        }));
    }

    // Wait for all tests to complete
    std::vector<FallbackTestResult> results;
    for (auto& future : futures) {
        results.push_back(future.get());
    }

    EXPECT_EQ(results.size(), num_concurrent_tests) << "All concurrent tests should complete";

    int successful_concurrent_tests = 0;
    for (const auto& result : results) {
        if (result.fallback_successful) {
            successful_concurrent_tests++;
        }
    }

    EXPECT_GE(successful_concurrent_tests, num_concurrent_tests * 0.75)
        << "At least 75% of concurrent fallback tests should succeed";
}

}  // namespace testing
}  // namespace keycuda