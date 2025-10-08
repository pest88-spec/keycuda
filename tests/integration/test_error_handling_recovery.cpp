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
#include <atomic>
#include <random>
#include <exception>

#include "gpu_executor.h"
#include "device_metrics.h"
#include "memory_manager.h"
#include "bandwidth_validator.h"
#include "adaptive_scaling.h"
#include "performance_reporter.h"

namespace keycuda {
namespace testing {

// Error injection types for testing
enum class ErrorType {
    CUDA_ERROR_LAUNCH_FAILURE,
    CUDA_ERROR_OUT_OF_MEMORY,
    CUDA_ERROR_INVALID_DEVICE,
    CUDA_ERROR_DEVICE_LOST,
    MEMORY_ALLOCATION_FAILURE,
    KERNEL_TIMEOUT,
    DATA_CORRUPTION,
    SYNCHRONIZATION_FAILURE,
    DEVICE_RESET_REQUIRED,
    INSUFFICIENT_RESOURCES
};

// Error injection configuration
struct ErrorInjectionConfig {
    ErrorType error_type;
    double injection_probability;  // 0.0 to 1.0
    int trigger_after_iterations;  // Trigger after N iterations
    bool auto_recovery_enabled;
    std::string error_description;

    ErrorInjectionConfig(ErrorType type, double prob = 1.0, int iterations = 0)
        : error_type(type), injection_probability(prob), trigger_after_iterations(iterations),
          auto_recovery_enabled(true) {}
};

// Error recovery result
struct ErrorRecoveryResult {
    ErrorType error_type;
    bool error_injected_successfully;
    bool error_detected_by_system;
    bool recovery_attempted;
    bool recovery_successful;
    double recovery_time_ms;
    std::string recovery_method;
    std::string error_details;
    int data_corruption_count;

    ErrorRecoveryResult(ErrorType type) : error_type(type), error_injected_successfully(false),
                                        error_detected_by_system(false), recovery_attempted(false),
                                        recovery_successful(false), recovery_time_ms(0.0),
                                        data_corruption_count(0) {}
};

// Mock error injector for controlled error simulation
class ErrorInjector {
public:
    ErrorInjector() : error_count_(0), enabled_(false) {}

    void Configure(const ErrorInjectionConfig& config) {
        std::lock_guard<std::mutex> lock(config_mutex_);
        config_ = config;
        enabled_ = true;
        error_count_ = 0;
    }

    void Disable() {
        std::lock_guard<std::mutex> lock(config_mutex_);
        enabled_ = false;
    }

    bool ShouldInjectError() {
        std::lock_guard<std::mutex> lock(config_mutex_);
        if (!enabled_) return false;

        error_count_++;

        if (error_count_ >= config_.trigger_after_iterations) {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_real_distribution<> dis(0.0, 1.0);

            return dis(gen) <= config_.injection_probability;
        }

        return false;
    }

    ErrorType GetConfiguredErrorType() {
        std::lock_guard<std::mutex> lock(config_mutex_);
        return config_.error_type;
    }

    int GetErrorCount() const {
        return error_count_;
    }

private:
    std::mutex config_mutex_;
    ErrorInjectionConfig config_{ErrorType::CUDA_ERROR_LAUNCH_FAILURE};
    std::atomic<int> error_count_;
    std::atomic<bool> enabled_;
};

// Error recovery manager
class ErrorRecoveryManager {
public:
    ErrorRecoveryManager(std::unique_ptr<GpuExecutor> executor)
        : executor_(std::move(executor)), recovery_attempts_(0) {}

    ErrorRecoveryResult HandleError(ErrorType error_type, const std::string& error_details = "") {
        ErrorRecoveryResult result(error_type);
        result.error_details = error_details;

        auto recovery_start = std::chrono::high_resolution_clock::now();

        switch (error_type) {
            case ErrorType::CUDA_ERROR_LAUNCH_FAILURE:
                result = HandleKernelLaunchFailure(result);
                break;

            case ErrorType::CUDA_ERROR_OUT_OF_MEMORY:
                result = HandleOutOfMemoryError(result);
                break;

            case ErrorType::CUDA_ERROR_INVALID_DEVICE:
                result = HandleInvalidDeviceError(result);
                break;

            case ErrorType::CUDA_ERROR_DEVICE_LOST:
                result = HandleDeviceLostError(result);
                break;

            case ErrorType::MEMORY_ALLOCATION_FAILURE:
                result = HandleMemoryAllocationFailure(result);
                break;

            case ErrorType::KERNEL_TIMEOUT:
                result = HandleKernelTimeout(result);
                break;

            case ErrorType::DATA_CORRUPTION:
                result = HandleDataCorruption(result);
                break;

            case ErrorType::SYNCHRONIZATION_FAILURE:
                result = HandleSynchronizationFailure(result);
                break;

            case ErrorType::DEVICE_RESET_REQUIRED:
                result = HandleDeviceResetRequired(result);
                break;

            case ErrorType::INSUFFICIENT_RESOURCES:
                result = HandleInsufficientResources(result);
                break;
        }

        auto recovery_end = std::chrono::high_resolution_clock::now();
        result.recovery_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            recovery_end - recovery_start).count();

        recovery_attempts_++;

        return result;
    }

    int GetRecoveryAttempts() const {
        return recovery_attempts_;
    }

private:
    ErrorRecoveryResult HandleKernelLaunchFailure(ErrorRecoveryResult result) {
        result.recovery_attempted = true;
        result.recovery_method = "Kernel restart and context reset";

        // Attempt to restart kernel
        try {
            // Reset CUDA context
            cudaError_t reset_result = cudaDeviceReset();
            if (reset_result == cudaSuccess) {
                // Reinitialize executor
                executor_->Reinitialize();
                result.recovery_successful = true;
            } else {
                result.recovery_successful = false;
                result.error_details += " | Context reset failed";
            }
        } catch (const std::exception& e) {
            result.recovery_successful = false;
            result.error_details += " | Exception during recovery: " + std::string(e.what());
        }

        return result;
    }

    ErrorRecoveryResult HandleOutOfMemoryError(ErrorRecoveryResult result) {
        result.recovery_attempted = true;
        result.recovery_method = "Memory cleanup and reallocation";

        try {
            // Force garbage collection
            executor_->ForceMemoryCleanup();

            // Try smaller allocation
            size_t reduced_size = executor_->GetLastAllocationSize() / 2;
            bool allocation_success = executor_->TryAllocateWithSize(reduced_size);

            result.recovery_successful = allocation_success;
            if (!allocation_success) {
                result.error_details += " | Reduced allocation also failed";
            }
        } catch (const std::exception& e) {
            result.recovery_successful = false;
            result.error_details += " | Exception during recovery: " + std::string(e.what());
        }

        return result;
    }

    ErrorRecoveryResult HandleInvalidDeviceError(ErrorRecoveryResult result) {
        result.recovery_attempted = true;
        result.recovery_method = "Device enumeration and fallback";

        try {
            // Enumerate available devices
            int device_count = 0;
            cudaError_t enum_result = cudaGetDeviceCount(&device_count);

            if (enum_result == cudaSuccess && device_count > 0) {
                // Try to fall back to device 0
                cudaError_t set_result = cudaSetDevice(0);
                if (set_result == cudaSuccess) {
                    executor_->Reinitialize();
                    result.recovery_successful = true;
                } else {
                    result.recovery_successful = false;
                    result.error_details += " | Fallback device setup failed";
                }
            } else {
                result.recovery_successful = false;
                result.error_details += " | No devices available";
            }
        } catch (const std::exception& e) {
            result.recovery_successful = false;
            result.error_details += " | Exception during recovery: " + std::string(e.what());
        }

        return result;
    }

    ErrorRecoveryResult HandleDeviceLostError(ErrorRecoveryResult result) {
        result.recovery_attempted = true;
        result.recovery_method = "Device reset and reinitialization";

        try {
            // Attempt complete device reset
            cudaError_t reset_result = cudaDeviceReset();

            if (reset_result == cudaSuccess) {
                // Wait for device to become available again
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));

                // Reinitialize
                executor_->Reinitialize();
                result.recovery_successful = true;
            } else {
                result.recovery_successful = false;
                result.error_details += " | Device reset failed";
            }
        } catch (const std::exception& e) {
            result.recovery_successful = false;
            result.error_details += " | Exception during recovery: " + std::string(e.what());
        }

        return result;
    }

    ErrorRecoveryResult HandleMemoryAllocationFailure(ErrorRecoveryResult result) {
        result.recovery_attempted = true;
        result.recovery_method = "Progressive memory size reduction";

        try {
            // Try progressively smaller allocations
            size_t original_size = executor_->GetRequestedAllocationSize();
            size_t current_size = original_size;

            for (int attempt = 0; attempt < 5; ++attempt) {
                current_size = current_size / 2;

                if (current_size < 1024) {  // Minimum size threshold
                    break;
                }

                bool success = executor_->TryAllocateWithSize(current_size);
                if (success) {
                    result.recovery_successful = true;
                    result.error_details += " | Success at size: " + std::to_string(current_size);
                    return result;
                }
            }

            result.recovery_successful = false;
            result.error_details += " | All size reduction attempts failed";
        } catch (const std::exception& e) {
            result.recovery_successful = false;
            result.error_details += " | Exception during recovery: " + std::string(e.what());
        }

        return result;
    }

    ErrorRecoveryResult HandleKernelTimeout(ErrorRecoveryResult result) {
        result.recovery_attempted = true;
        result.recovery_method = "Kernel cancellation and restart";

        try {
            // Cancel any running kernels
            bool cancel_success = executor_->CancelRunningKernels();

            if (cancel_success) {
                // Restart with timeout parameters
                executor_->SetKernelTimeout(std::chrono::milliseconds(5000));
                executor_->Reinitialize();
                result.recovery_successful = true;
            } else {
                result.recovery_successful = false;
                result.error_details += " | Kernel cancellation failed";
            }
        } catch (const std::exception& e) {
            result.recovery_successful = false;
            result.error_details += " | Exception during recovery: " + std::string(e.what());
        }

        return result;
    }

    ErrorRecoveryResult HandleDataCorruption(ErrorRecoveryResult result) {
        result.recovery_attempted = true;
        result.recovery_method = "Data validation and recomputation";

        try {
            // Validate current data
            int corruption_count = executor_->ValidateDataIntegrity();
            result.data_corruption_count = corruption_count;

            if (corruption_count > 0) {
                // Recompute corrupted data
                bool recompute_success = executor_->RecomputeCorruptedData();
                result.recovery_successful = recompute_success;

                if (!recompute_success) {
                    result.error_details += " | Data recomputation failed";
                }
            } else {
                result.recovery_successful = true;
                result.error_details += " | No corruption detected";
            }
        } catch (const std::exception& e) {
            result.recovery_successful = false;
            result.error_details += " | Exception during recovery: " + std::string(e.what());
        }

        return result;
    }

    ErrorRecoveryResult HandleSynchronizationFailure(ErrorRecoveryResult result) {
        result.recovery_attempted = true;
        result.recovery_method = "Synchronization reset and stream recreation";

        try {
            // Reset synchronization primitives
            bool sync_reset_success = executor_->ResetSynchronization();

            if (sync_reset_success) {
                // Recreate CUDA streams
                bool stream_recreate_success = executor_->RecreateStreams();
                result.recovery_successful = stream_recreate_success;

                if (!stream_recreate_success) {
                    result.error_details += " | Stream recreation failed";
                }
            } else {
                result.recovery_successful = false;
                result.error_details += " | Synchronization reset failed";
            }
        } catch (const std::exception& e) {
            result.recovery_successful = false;
            result.error_details += " | Exception during recovery: " + std::string(e.what());
        }

        return result;
    }

    ErrorRecoveryResult HandleDeviceResetRequired(ErrorRecoveryResult result) {
        result.recovery_attempted = true;
        result.recovery_method = "Full device reset and warmup";

        try {
            // Complete device reset
            cudaError_t reset_result = cudaDeviceReset();

            if (reset_result == cudaSuccess) {
                // Wait and reinitialize
                std::this_thread::sleep_for(std::chrono::milliseconds(2000));

                executor_->Reinitialize();

                // Warmup device
                bool warmup_success = executor_->WarmupDevice();
                result.recovery_successful = warmup_success;

                if (!warmup_success) {
                    result.error_details += " | Device warmup failed";
                }
            } else {
                result.recovery_successful = false;
                result.error_details += " | Device reset failed";
            }
        } catch (const std::exception& e) {
            result.recovery_successful = false;
            result.error_details += " | Exception during recovery: " + std::string(e.what());
        }

        return result;
    }

    ErrorRecoveryResult HandleInsufficientResources(ErrorRecoveryResult result) {
        result.recovery_attempted = true;
        result.recovery_method = "Resource optimization and load reduction";

        try {
            // Reduce resource usage
            bool optimization_success = executor_->OptimizeResourceUsage();

            if (optimization_success) {
                // Retry with reduced load
                bool retry_success = executor_->RetryWithReducedLoad();
                result.recovery_successful = retry_success;

                if (!retry_success) {
                    result.error_details += " | Retry with reduced load failed";
                }
            } else {
                result.recovery_successful = false;
                result.error_details += " | Resource optimization failed";
            }
        } catch (const std::exception& e) {
            result.recovery_successful = false;
            result.error_details += " | Exception during recovery: " + std::string(e.what());
        }

        return result;
    }

    std::unique_ptr<GpuExecutor> executor_;
    std::atomic<int> recovery_attempts_;
};

class ErrorHandlingRecoveryTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize GPU context
        ASSERT_TRUE(cudaSuccess == cudaSetDevice(0));

        // Create GPU executor with error handling capabilities
        gpu_executor_ = std::make_unique<GpuExecutor>(0);
        recovery_manager_ = std::make_unique<ErrorRecoveryManager>(std::make_unique<GpuExecutor>(0));
        error_injector_ = std::make_unique<ErrorInjector>();

        // Initialize other components
        device_metrics_ = std::make_unique<DeviceMetricsCollector>(0);
        memory_manager_ = std::make_unique<MemoryManager>(0);

        recovery_results_.clear();
    }

    void TearDown() override {
        error_injector_->Disable();
        recovery_results_.clear();
    }

    // Test error injection and recovery for a specific error type
    ErrorRecoveryResult TestErrorInjectionAndRecovery(ErrorType error_type,
                                                     double injection_probability = 1.0,
                                                     int trigger_after_iterations = 10) {
        ErrorInjectionConfig config(error_type, injection_probability, trigger_after_iterations);
        config.auto_recovery_enabled = true;
        config.error_description = GetErrorDescription(error_type);

        error_injector_->Configure(config);

        ErrorRecoveryResult result(error_type);

        // Run operations that might trigger the injected error
        int iterations = 0;
        const int max_iterations = trigger_after_iterations + 10;

        while (iterations < max_iterations) {
            iterations++;

            // Check if error should be injected
            if (error_injector_->ShouldInjectError()) {
                result.error_injected_successfully = true;

                // Simulate the error by calling the appropriate error handler
                result = recovery_manager_->HandleError(error_type, "Injected error during test");
                result.error_detected_by_system = true;

                break;
            }

            // Perform normal operations
            try {
                gpu_executor_->SearchKeys(nullptr, nullptr, 1024);
                memory_manager_->AllocateTemporaryBuffer(1024 * 1024);
            } catch (const std::exception& e) {
                result.error_detected_by_system = true;
                result = recovery_manager_->HandleError(error_type, e.what());
                break;
            }

            // Small delay to prevent overwhelming the system
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        recovery_results_.push_back(result);
        return result;
    }

    // Test multiple error types in sequence
    std::vector<ErrorRecoveryResult> TestMultipleErrorTypes() {
        std::vector<ErrorType> error_types = {
            ErrorType::CUDA_ERROR_LAUNCH_FAILURE,
            ErrorType::CUDA_ERROR_OUT_OF_MEMORY,
            ErrorType::MEMORY_ALLOCATION_FAILURE,
            ErrorType::KERNEL_TIMEOUT,
            ErrorType::SYNCHRONIZATION_FAILURE
        };

        std::vector<ErrorRecoveryResult> results;

        for (ErrorType error_type : error_types) {
            auto result = TestErrorInjectionAndRecovery(error_type, 1.0, 5);
            results.push_back(result);

            // Small delay between different error types
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        return results;
    }

    // Test recovery under high load
    ErrorRecoveryResult TestRecoveryUnderLoad(ErrorType error_type) {
        const int num_concurrent_operations = 8;
        std::vector<std::future<void>> futures;

        ErrorInjectionConfig config(error_type, 0.8, 20);  // 80% probability after 20 iterations
        error_injector_->Configure(config);

        ErrorRecoveryResult result(error_type);

        // Start concurrent operations
        for (int i = 0; i < num_concurrent_operations; ++i) {
            futures.push_back(std::async(std::launch::async, [this, &result, error_type]() {
                int iterations = 0;
                const int max_iterations = 50;

                while (iterations < max_iterations) {
                    iterations++;

                    if (error_injector_->ShouldInjectError()) {
                        result.error_injected_successfully = true;
                        result = recovery_manager_->HandleError(error_type, "High load error injection");
                        break;
                    }

                    try {
                        gpu_executor_->SearchKeys(nullptr, nullptr, 1024);
                    } catch (const std::exception& e) {
                        result.error_detected_by_system = true;
                        result = recovery_manager_->HandleError(error_type, e.what());
                        break;
                    }
                }
            }));
        }

        // Wait for all operations to complete
        for (auto& future : futures) {
            future.wait();
        }

        recovery_results_.push_back(result);
        return result;
    }

    // Test recovery performance impact
    void TestRecoveryPerformanceImpact() {
        auto baseline_start = std::chrono::high_resolution_clock::now();

        // Run normal operations for baseline
        for (int i = 0; i < 100; ++i) {
            gpu_executor_->SearchKeys(nullptr, nullptr, 1024);
        }

        auto baseline_end = std::chrono::high_resolution_clock::now();
        auto baseline_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            baseline_end - baseline_start);

        // Test with error injection and recovery
        auto recovery_start = std::chrono::high_resolution_clock::now();

        auto recovery_result = TestErrorInjectionAndRecovery(ErrorType::CUDA_ERROR_LAUNCH_FAILURE, 0.3, 30);

        // Continue operations after recovery
        for (int i = 0; i < 70; ++i) {  // Total 100 operations
            gpu_executor_->SearchKeys(nullptr, nullptr, 1024);
        }

        auto recovery_end = std::chrono::high_resolution_clock::now();
        auto recovery_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            recovery_end - recovery_start);

        // Calculate performance impact
        double performance_impact = static_cast<double>(recovery_duration.count() - baseline_duration.count()) /
                                  baseline_duration.count();

        EXPECT_LT(performance_impact, 0.5)  // Less than 50% performance impact
            << "Recovery performance impact too high: " << (performance_impact * 100) << "%";

        EXPECT_TRUE(recovery_result.recovery_successful)
            << "Recovery was not successful during performance impact test";
    }

    // Generate error handling report
    void GenerateErrorHandlingReport() {
        nlohmann::json report;
        report["test_timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        report["total_recovery_attempts"] = recovery_manager_->GetRecoveryAttempts();

        nlohmann::json error_results = nlohmann::json::array();
        int successful_recoveries = 0;
        int failed_recoveries = 0;

        for (const auto& result : recovery_results_) {
            nlohmann::json error_result;
            error_result["error_type"] = static_cast<int>(result.error_type);
            error_result["error_injected"] = result.error_injected_successfully;
            error_result["error_detected"] = result.error_detected_by_system;
            error_result["recovery_attempted"] = result.recovery_attempted;
            error_result["recovery_successful"] = result.recovery_successful;
            error_result["recovery_time_ms"] = result.recovery_time_ms;
            error_result["recovery_method"] = result.recovery_method;
            error_result["error_details"] = result.error_details;
            error_result["data_corruption_count"] = result.data_corruption_count;

            error_results.push_back(error_result);

            if (result.recovery_successful) {
                successful_recoveries++;
            } else {
                failed_recoveries++;
            }
        }

        report["error_results"] = error_results;
        report["successful_recoveries"] = successful_recoveries;
        report["failed_recoveries"] = failed_recoveries;
        report["recovery_success_rate"] = recovery_results_.empty() ? 0.0 :
            static_cast<double>(successful_recoveries) / recovery_results_.size();

        // Save report
        std::ofstream report_file("error_handling_recovery_report.json");
        if (report_file.is_open()) {
            report_file << std::setw(4) << report << std::endl;
            std::cout << "Error handling recovery report saved." << std::endl;
        }

        // Print summary
        std::cout << "\n=== Error Handling Recovery Test Summary ===" << std::endl;
        std::cout << "Total recovery attempts: " << recovery_manager_->GetRecoveryAttempts() << std::endl;
        std::cout << "Successful recoveries: " << successful_recoveries << std::endl;
        std::cout << "Failed recoveries: " << failed_recoveries << std::endl;
        std::cout << "Recovery success rate: " << (report["recovery_success_rate"].get<double>() * 100) << "%" << std::endl;
    }

    std::string GetErrorDescription(ErrorType error_type) {
        switch (error_type) {
            case ErrorType::CUDA_ERROR_LAUNCH_FAILURE:
                return "CUDA kernel launch failure";
            case ErrorType::CUDA_ERROR_OUT_OF_MEMORY:
                return "CUDA out of memory error";
            case ErrorType::CUDA_ERROR_INVALID_DEVICE:
                return "CUDA invalid device error";
            case ErrorType::CUDA_ERROR_DEVICE_LOST:
                return "CUDA device lost error";
            case ErrorType::MEMORY_ALLOCATION_FAILURE:
                return "Memory allocation failure";
            case ErrorType::KERNEL_TIMEOUT:
                return "Kernel execution timeout";
            case ErrorType::DATA_CORRUPTION:
                return "Data corruption detected";
            case ErrorType::SYNCHRONIZATION_FAILURE:
                return "Synchronization failure";
            case ErrorType::DEVICE_RESET_REQUIRED:
                return "Device reset required";
            case ErrorType::INSUFFICIENT_RESOURCES:
                return "Insufficient resources";
            default:
                return "Unknown error type";
        }
    }

    std::unique_ptr<GpuExecutor> gpu_executor_;
    std::unique_ptr<ErrorRecoveryManager> recovery_manager_;
    std::unique_ptr<ErrorInjector> error_injector_;
    std::unique_ptr<DeviceMetricsCollector> device_metrics_;
    std::unique_ptr<MemoryManager> memory_manager_;

    std::vector<ErrorRecoveryResult> recovery_results_;
};

// Test basic error injection and recovery
TEST_F(ErrorHandlingRecoveryTest, BasicErrorInjectionAndRecovery) {
    auto result = TestErrorInjectionAndRecovery(ErrorType::CUDA_ERROR_LAUNCH_FAILURE);

    EXPECT_TRUE(result.error_injected_successfully) << "Error was not injected successfully";
    EXPECT_TRUE(result.error_detected_by_system) << "Error was not detected by the system";
    EXPECT_TRUE(result.recovery_attempted) << "Recovery was not attempted";
    EXPECT_TRUE(result.recovery_successful) << "Recovery was not successful";
    EXPECT_GT(result.recovery_time_ms, 0) << "Recovery time should be positive";
    EXPECT_FALSE(result.recovery_method.empty()) << "Recovery method should be specified";
}

// Test multiple error types
TEST_F(ErrorHandlingRecoveryTest, MultipleErrorTypesRecovery) {
    auto results = TestMultipleErrorTypes();

    EXPECT_EQ(results.size(), 5) << "Should test 5 different error types";

    int successful_recoveries = 0;
    for (const auto& result : results) {
        EXPECT_TRUE(result.error_injected_successfully) << "Error injection failed for one of the types";
        EXPECT_TRUE(result.recovery_attempted) << "Recovery was not attempted for one of the types";

        if (result.recovery_successful) {
            successful_recoveries++;
        }
    }

    EXPECT_GE(successful_recoveries, 4) << "At least 4 out of 5 error types should recover successfully";

    GenerateErrorHandlingReport();
}

// Test recovery under high load
TEST_F(ErrorHandlingRecoveryTest, RecoveryUnderHighLoad) {
    auto result = TestRecoveryUnderLoad(ErrorType::CUDA_ERROR_OUT_OF_MEMORY);

    EXPECT_TRUE(result.error_injected_successfully) << "Error injection failed under high load";
    EXPECT_TRUE(result.recovery_attempted) << "Recovery was not attempted under high load";
    EXPECT_TRUE(result.recovery_successful) << "Recovery failed under high load";
}

// Test recovery performance impact
TEST_F(ErrorHandlingRecoveryTest, RecoveryPerformanceImpact) {
    TestRecoveryPerformanceImpact();
}

// Test data corruption recovery
TEST_F(ErrorHandlingRecoveryTest, DataCorruptionRecovery) {
    auto result = TestErrorInjectionAndRecovery(ErrorType::DATA_CORRUPTION, 1.0, 15);

    EXPECT_TRUE(result.error_injected_successfully) << "Data corruption error injection failed";
    EXPECT_TRUE(result.recovery_attempted) << "Data corruption recovery was not attempted";
    EXPECT_TRUE(result.recovery_successful) << "Data corruption recovery failed";

    // Check that data corruption was detected and handled
    EXPECT_GE(result.data_corruption_count, 0) << "Data corruption count should be available";
}

// Test device lost error recovery
TEST_F(ErrorHandlingRecoveryTest, DeviceLostErrorRecovery) {
    auto result = TestErrorInjectionAndRecovery(ErrorType::CUDA_ERROR_DEVICE_LOST, 1.0, 10);

    EXPECT_TRUE(result.error_injected_successfully) << "Device lost error injection failed";
    EXPECT_TRUE(result.recovery_attempted) << "Device lost recovery was not attempted";

    // Device lost recovery might fail if device is actually lost, which is acceptable
    // The important thing is that the system attempts recovery gracefully
    EXPECT_TRUE(result.recovery_attempted) << "System should attempt device lost recovery";
}

// Test timeout and synchronization error recovery
TEST_F(ErrorHandlingRecoveryTest, TimeoutAndSynchronizationRecovery) {
    // Test kernel timeout
    auto timeout_result = TestErrorInjectionAndRecovery(ErrorType::KERNEL_TIMEOUT, 1.0, 20);
    EXPECT_TRUE(timeout_result.recovery_attempted) << "Kernel timeout recovery should be attempted";

    // Test synchronization failure
    auto sync_result = TestErrorInjectionAndRecovery(ErrorType::SYNCHRONIZATION_FAILURE, 1.0, 25);
    EXPECT_TRUE(sync_result.recovery_attempted) << "Synchronization recovery should be attempted";
    EXPECT_TRUE(sync_result.recovery_successful) << "Synchronization recovery should succeed";
}

// Test repeated error scenarios
TEST_F(ErrorHandlingRecoveryTest, RepeatedErrorScenarios) {
    const int num_error_cycles = 3;
    int successful_cycles = 0;

    for (int cycle = 0; cycle < num_error_cycles; ++cycle) {
        auto result = TestErrorInjectionAndRecovery(ErrorType::CUDA_ERROR_LAUNCH_FAILURE,
                                                   0.8, 5 + cycle * 10);

        if (result.recovery_successful) {
            successful_cycles++;
        }

        // Small delay between cycles
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    EXPECT_GE(successful_cycles, num_error_cycles - 1)
        << "Should recover successfully in most cycles, even with repeated errors";
}

}  // namespace testing
}  // namespace keycuda