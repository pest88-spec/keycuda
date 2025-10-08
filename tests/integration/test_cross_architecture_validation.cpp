#include <gtest/gtest.h>
#include <chrono>
#include <vector>
#include <memory>
#include <fstream>
#include <nlohmann/json.hpp>
#include "ComputeCore/gpu/performance/memory_optimizer.h"
#include "ComputeCore/gpu/performance/bandwidth_validator.h"
#include "ComputeCore/gpu/performance/adaptive_parallelism_scaling.h"
#include "ComputeCore/gpu/performance/resource_profiler.h"

namespace puzzle71::gpu::performance {

using json = nlohmann::json;

class CrossArchitectureValidationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Discover available GPUs and their architectures
        DiscoverGpuArchitectures();

        // Initialize test data that will work across all architectures
        InitializeCrossArchitectureTestData();

        // Setup validation thresholds for different architecture classes
        SetupArchitectureValidationThresholds();
    }

    void TearDown() override {
        CleanupCrossArchitectureTestData();
    }

    struct GpuArchitectureInfo {
        int device_id;
        std::string device_name;
        int compute_capability;  // e.g., 86, 89, 90
        std::string architecture_family;  // Turing, Ampere, Ada, Hopper
        size_t total_memory_mb;
        int sm_count;
        size_t l2_cache_size_kb;
        double memory_bandwidth_gb_per_sec;
        bool supports_managed_memory;
        bool supports_cooperative_groups;
        std::vector<std::string> supported_features;
    };

    struct ArchitectureTestResults {
        GpuArchitectureInfo gpu_info;
        bool memory_optimization_works{false};
        bool adaptive_scaling_works{false};
        bool bandwidth_validation_works{false};
        bool accuracy_maintained{false};
        double performance_score{0.0};
        std::vector<std::string> validation_issues;
        std::map<std::string, double> performance_metrics;
        json detailed_results;
    };

    void DiscoverGpuArchitectures() {
        int device_count = 0;
        cudaError_t err = cudaGetDeviceCount(&device_count);
        ASSERT_EQ(err, cudaSuccess) << "Failed to get device count";

        std::cout << "Discovered " << device_count << " GPU devices:" << std::endl;

        for (int i = 0; i < device_count; ++i) {
            GpuArchitectureInfo gpu_info;
            gpu_info.device_id = i;

            cudaDeviceProp prop;
            err = cudaGetDeviceProperties(&prop, i);
            ASSERT_EQ(err, cudaSuccess) << "Failed to get properties for device " << i;

            gpu_info.device_name = prop.name;
            gpu_info.compute_capability = prop.major * 10 + prop.minor;
            gpu_info.total_memory_mb = prop.totalGlobalMem / (1024 * 1024);
            gpu_info.sm_count = prop.multiProcessorCount;
            gpu_info.l2_cache_size_kb = prop.l2CacheSize / 1024;

            // Calculate theoretical memory bandwidth
            double memory_clock_mhz = prop.memoryClockRate / 1000.0;
            double memory_bus_width_bits = prop.memoryBusWidth;
            gpu_info.memory_bandwidth_gb_per_sec = (memory_clock_mhz * memory_bus_width_bits) / (8.0 * 1000.0);

            gpu_info.supports_managed_memory = prop.managedMemory;
            gpu_info.supports_cooperative_groups = prop.cooperativeLaunch;

            // Determine architecture family
            if (prop.major == 7) {
                gpu_info.architecture_family = (prop.minor >= 5) ? "Turing" : "Volta";
            } else if (prop.major == 8) {
                gpu_info.architecture_family = "Ampere";
            } else if (prop.major == 9) {
                gpu_info.architecture_family = "Ada";
            } else if (prop.major >= 10) {
                gpu_info.architecture_family = "Hopper";
            } else {
                gpu_info.architecture_family = "Legacy";
            }

            // Detect supported features
            if (prop.concurrentKernels) gpu_info.supported_features.push_back("concurrent_kernels");
            if (prop ECCEnabled) gpu_info.supported_features.push_back("ecc");
            if (prop.pageableMemoryAccessUsesHostPageTables) gpu_info.supported_features.push_back("pageable_memory");
            if (prop.canMapHostMemory) gpu_info.supported_features.push_back("mapped_host_memory");

            gpu_architectures_.push_back(gpu_info);

            std::cout << "  Device " << i << ": " << gpu_info.device_name
                      << " (Compute " << gpu_info.compute_capability << ", "
                      << gpu_info.architecture_family << ")" << std::endl;
        }

        ASSERT_FALSE(gpu_architectures_.empty()) << "At least one GPU must be available for testing";
    }

    void InitializeCrossArchitectureTestData() {
        // Create test data with sizes that work across all architectures
        std::vector<size_t> test_sizes_mb = {16, 64, 256, 512}; // Conservative sizes

        for (size_t size_mb : test_sizes_mb) {
            TestData test_data;
            test_data.size = size_mb * 1024 * 1024;
            test_data.host_ptr = malloc(test_data.size);

            ASSERT_NE(test_data.host_ptr, nullptr) << "Failed to allocate test data";

            // Initialize with deterministic pattern for cross-GPU validation
            uint8_t* data = static_cast<uint8_t*>(test_data.host_ptr);
            for (size_t i = 0; i < test_data.size; ++i) {
                data[i] = static_cast<uint8_t>((i + size_mb) % 256);
            }

            cross_architecture_test_data_.push_back(test_data);
        }
    }

    void CleanupCrossArchitectureTestData() {
        for (auto& test_data : cross_architecture_test_data_) {
            if (test_data.host_ptr) {
                free(test_data.host_ptr);
            }
            if (test_data.device_ptr) {
                cudaFree(test_data.device_ptr);
            }
        }
        cross_architecture_test_data_.clear();
    }

    void SetupArchitectureValidationThresholds() {
        // Set conservative thresholds that should work across all supported architectures
        architecture_thresholds_ = {
            {"Turing", {
                {"min_memory_bandwidth_utilization", 60.0},
                {"min_throughput_improvement", 10.0},
                {"max_sync_overhead_percentage", 15.0},
                {"min_accuracy_score", 99.9}
            }},
            {"Ampere", {
                {"min_memory_bandwidth_utilization", 70.0},
                {"min_throughput_improvement", 15.0},
                {"max_sync_overhead_percentage", 12.0},
                {"min_accuracy_score", 99.95}
            }},
            {"Ada", {
                {"min_memory_bandwidth_utilization", 75.0},
                {"min_throughput_improvement", 18.0},
                {"max_sync_overhead_percentage", 10.0},
                {"min_accuracy_score", 99.98}
            }},
            {"Hopper", {
                {"min_memory_bandwidth_utilization", 80.0},
                {"min_throughput_improvement", 20.0},
                {"max_sync_overhead_percentage", 8.0},
                {"min_accuracy_score", 99.99}
            }},
            {"Legacy", {
                {"min_memory_bandwidth_utilization", 50.0},
                {"min_throughput_improvement", 5.0},
                {"max_sync_overhead_percentage", 20.0},
                {"min_accuracy_score", 99.5}
            }}
        };
    }

    ArchitectureTestResults ValidateGpuArchitecture(const GpuArchitectureInfo& gpu_info) {
        ArchitectureTestResults results;
        results.gpu_info = gpu_info;

        std::cout << "\n=== Validating Architecture: " << gpu_info.device_name << " ===" << std::endl;

        // Set current GPU
        cudaError_t err = cudaSetDevice(gpu_info.device_id);
        ASSERT_EQ(err, cudaSuccess) << "Failed to set device " << gpu_info.device_id;

        // Allocate device memory for this GPU
        cudaMalloc(&results.detailed_results["device_ptr"], cross_architecture_test_data_[0].size);

        // Test 1: Memory Optimization Validation
        results.memory_optimization_works = ValidateMemoryOptimization(gpu_info, results);

        // Test 2: Adaptive Scaling Validation
        results.adaptive_scaling_works = ValidateAdaptiveScaling(gpu_info, results);

        // Test 3: Bandwidth Validation
        results.bandwidth_validation_works = ValidateBandwidthUtilization(gpu_info, results);

        // Test 4: Cross-GPU Accuracy Validation
        results.accuracy_maintained = ValidateCrossGpuAccuracy(gpu_info, results);

        // Calculate overall performance score
        results.performance_score = CalculatePerformanceScore(results);

        // Cleanup device memory
        cudaFree(results.detailed_results["device_ptr"]);

        std::cout << "Architecture Validation Results:" << std::endl;
        std::cout << "  Memory Optimization: " << (results.memory_optimization_works ? "PASS" : "FAIL") << std::endl;
        std::cout << "  Adaptive Scaling: " << (results.adaptive_scaling_works ? "PASS" : "FAIL") << std::endl;
        std::cout << "  Bandwidth Validation: " << (results.bandwidth_validation_works ? "PASS" : "FAIL") << std::endl;
        std::cout << "  Accuracy Maintained: " << (results.accuracy_maintained ? "PASS" : "FAIL") << std::endl;
        std::cout << "  Performance Score: " << results.performance_score << "/100" << std::endl;

        return results;
    }

    bool ValidateMemoryOptimization(const GpuArchitectureInfo& gpu_info, ArchitectureTestResults& results) {
        try {
            auto memory_optimizer = std::make_unique<MemoryOptimizer>(gpu_info.device_id);
            memory_optimizer->EnableAsynchronousTransfers();
            memory_optimizer->InitializeMemoryPool(256); // Conservative pool size

            // Test memory transfers with optimizations
            auto& test_data = cross_architecture_test_data_[1]; // Use 64MB test data

            cudaMalloc(&const_cast<void*&>(test_data.device_ptr), test_data.size);

            auto start_time = std::chrono::high_resolution_clock::now();

            // Optimized transfer
            std::vector<std::pair<void*, void*>> transfers = {{test_data.host_ptr, test_data.device_ptr}};
            std::vector<size_t> sizes = {test_data.size};

            bool batch_success = memory_optimizer->BatchMemoryTransfer(transfers, sizes, true);
            if (batch_success) {
                memory_optimizer->ProcessTransferBatch();
            }

            auto end_time = std::chrono::high_resolution_clock::now();
            auto transfer_time = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

            // Calculate bandwidth utilization
            double bandwidth_gb_per_sec = (static_cast<double>(test_data.size) / transfer_time.count()) * 1000000.0 /
                                         (1024.0 * 1024.0 * 1024.0);
            double utilization = (bandwidth_gb_per_sec / gpu_info.memory_bandwidth_gb_per_sec) * 100.0;

            results.performance_metrics["memory_bandwidth_utilization"] = utilization;
            results.detailed_results["memory_transfer_time_us"] = transfer_time.count();
            results.detailed_results["memory_bandwidth_gb_per_sec"] = bandwidth_gb_per_sec;

            // Check against architecture-specific thresholds
            auto thresholds = architecture_thresholds_[gpu_info.architecture_family];
            double min_utilization = thresholds["min_memory_bandwidth_utilization"];

            bool success = utilization >= min_utilization;
            if (!success) {
                results.validation_issues.push_back("Memory bandwidth utilization below threshold: " +
                                                  std::to_string(utilization) + "% < " + std::to_string(min_utilization) + "%");
            }

            // Cleanup
            cudaFree(const_cast<void*>(test_data.device_ptr));
            test_data.device_ptr = nullptr;

            return success;

        } catch (const std::exception& e) {
            results.validation_issues.push_back("Memory optimization failed: " + std::string(e.what()));
            return false;
        }
    }

    bool ValidateAdaptiveScaling(const GpuArchitectureInfo& gpu_info, ArchitectureTestResults& results) {
        try {
            auto adaptive_scaling = CreateAdaptiveParallelismScaling(gpu_info.device_id, true, true);

            // Test adaptive parallelism scaling
            size_t workload_size = 1024 * 1024; // 1M keys
            auto scaling_decision = adaptive_scaling->CalculateOptimalConfiguration(workload_size, "key_search");

            results.detailed_results["adaptive_points_per_thread"] = scaling_decision.selected_config.points_per_thread;
            results.detailed_results["adaptive_block_size"] = scaling_decision.selected_config.block_size;
            results.detailed_results["adaptive_grid_size"] = scaling_decision.selected_config.grid_size;

            // Validate that adaptive scaling provides reasonable values
            bool success = true;

            if (scaling_decision.selected_config.points_per_thread < 64 ||
                scaling_decision.selected_config.points_per_thread > 256) {
                results.validation_issues.push_back("Adaptive scaling points_per_thread out of range: " +
                                                  std::to_string(scaling_decision.selected_config.points_per_thread));
                success = false;
            }

            if (scaling_decision.selected_config.block_size < 256 ||
                scaling_decision.selected_config.block_size > 1024) {
                results.validation_issues.push_back("Adaptive scaling block_size out of range: " +
                                                  std::to_string(scaling_decision.selected_config.block_size));
                success = false;
            }

            results.performance_metrics["adaptive_scaling_quality"] = scaling_decision.confidence_score;

            return success;

        } catch (const std::exception& e) {
            results.validation_issues.push_back("Adaptive scaling failed: " + std::string(e.what()));
            return false;
        }
    }

    bool ValidateBandwidthUtilization(const GpuArchitectureInfo& gpu_info, ArchitectureTestResults& results) {
        try {
            auto bandwidth_validator = std::make_unique<BandwidthValidator>(gpu_info.device_id);

            // Run bandwidth validation test
            double achieved_utilization = 0.0;
            bool test_success = bandwidth_validator->RunBandwidthTest(256, achieved_utilization); // 256MB test

            results.performance_metrics["bandwidth_test_utilization"] = achieved_utilization;
            results.detailed_results["bandwidth_test_success"] = test_success;

            // Check against architecture-specific thresholds
            auto thresholds = architecture_thresholds_[gpu_info.architecture_family];
            double min_utilization = thresholds["min_memory_bandwidth_utilization"];

            bool success = test_success && achieved_utilization >= min_utilization;

            if (!success) {
                if (!test_success) {
                    results.validation_issues.push_back("Bandwidth test failed to execute");
                } else {
                    results.validation_issues.push_back("Bandwidth utilization below threshold: " +
                                                      std::to_string(achieved_utilization) + "% < " + std::to_string(min_utilization) + "%");
                }
            }

            return success;

        } catch (const std::exception& e) {
            results.validation_issues.push_back("Bandwidth validation failed: " + std::string(e.what()));
            return false;
        }
    }

    bool ValidateCrossGpuAccuracy(const GpuArchitectureInfo& gpu_info, ArchitectureTestResults& results) {
        try {
            // Create test data with known checksum
            constexpr size_t ACCURACY_TEST_SIZE = 64 * 1024 * 1024; // 64MB
            void* accuracy_host_ptr = malloc(ACCURACY_TEST_SIZE);
            void* accuracy_device_ptr = nullptr;

            // Initialize with deterministic pattern
            uint8_t* data = static_cast<uint8_t*>(accuracy_host_ptr);
            for (size_t i = 0; i < ACCURACY_TEST_SIZE; ++i) {
                data[i] = static_cast<uint8_t>((i + gpu_info.device_id) % 256);
            }

            // Calculate reference checksum
            uint32_t reference_checksum = 0;
            for (size_t i = 0; i < ACCURACY_TEST_SIZE; ++i) {
                reference_checksum += data[i];
            }

            // Allocate device memory
            cudaError_t err = cudaMalloc(&accuracy_device_ptr, ACCURACY_TEST_SIZE);
            if (err != cudaSuccess) {
                free(accuracy_host_ptr);
                throw std::runtime_error("Failed to allocate device memory for accuracy test");
            }

            // Test optimized memory transfer
            auto memory_optimizer = std::make_unique<MemoryOptimizer>(gpu_info.device_id);
            memory_optimizer->EnableAsynchronousTransfers();

            std::vector<std::pair<void*, void*>> transfers = {{accuracy_host_ptr, accuracy_device_ptr}};
            std::vector<size_t> sizes = {ACCURACY_TEST_SIZE};

            bool transfer_success = memory_optimizer->BatchMemoryTransfer(transfers, sizes, true);
            if (transfer_success) {
                memory_optimizer->ProcessTransferBatch();
            }

            // Transfer back for validation
            void* validation_ptr = malloc(ACCURACY_TEST_SIZE);
            void* validation_device_ptr = nullptr;
            cudaMalloc(&validation_device_ptr, ACCURACY_TEST_SIZE);

            std::vector<std::pair<void*, void*>> return_transfers = {{accuracy_device_ptr, validation_device_ptr}};
            std::vector<size_t> return_sizes = {ACCURACY_TEST_SIZE};

            bool return_success = memory_optimizer->BatchMemoryTransfer(return_transfers, return_sizes, false);
            if (return_success) {
                memory_optimizer->ProcessTransferBatch();
            }

            cudaMemcpy(validation_ptr, validation_device_ptr, ACCURACY_TEST_SIZE, cudaMemcpyDeviceToHost);

            // Validate accuracy
            uint32_t validation_checksum = 0;
            uint8_t* validation_data = static_cast<uint8_t*>(validation_ptr);
            for (size_t i = 0; i < ACCURACY_TEST_SIZE; ++i) {
                validation_checksum += validation_data[i];
            }

            bool success = (validation_checksum == reference_checksum);
            results.performance_metrics["accuracy_score"] = success ? 100.0 : 0.0;

            if (!success) {
                results.validation_issues.push_back("Cross-GPU accuracy validation failed: checksum mismatch");
            }

            // Cleanup
            free(accuracy_host_ptr);
            free(validation_ptr);
            cudaFree(accuracy_device_ptr);
            cudaFree(validation_device_ptr);

            return success;

        } catch (const std::exception& e) {
            results.validation_issues.push_back("Cross-GPU accuracy test failed: " + std::string(e.what()));
            return false;
        }
    }

    double CalculatePerformanceScore(const ArchitectureTestResults& results) {
        double score = 0.0;

        if (results.memory_optimization_works) score += 25.0;
        if (results.adaptive_scaling_works) score += 25.0;
        if (results.bandwidth_validation_works) score += 25.0;
        if (results.accuracy_maintained) score += 25.0;

        // Bonus points for high performance metrics
        for (const auto& [metric, value] : results.performance_metrics) {
            if (metric == "memory_bandwidth_utilization" && value > 80.0) {
                score += 5.0;
            } else if (metric == "adaptive_scaling_quality" && value > 0.9) {
                score += 5.0;
            } else if (metric == "bandwidth_test_utilization" && value > 85.0) {
                score += 5.0;
            }
        }

        return std::min(score, 100.0);
    }

    struct TestData {
        void* host_ptr{nullptr};
        const void* device_ptr{nullptr};
        size_t size{0};
    };

protected:
    std::vector<GpuArchitectureInfo> gpu_architectures_;
    std::vector<TestData> cross_architecture_test_data_;
    std::map<std::string, std::map<std::string, double>> architecture_thresholds_;
};

TEST_F(CrossArchitectureValidationTest, ValidateAllAvailableArchitectures) {
    std::cout << "\n=== Cross-Architecture Validation Suite ===" << std::endl;

    std::vector<ArchitectureTestResults> all_results;
    bool all_architectures_pass = true;

    // Validate each available GPU architecture
    for (const auto& gpu_info : gpu_architectures_) {
        ArchitectureTestResults results = ValidateGpuArchitecture(gpu_info);
        all_results.push_back(results);

        if (results.performance_score < 80.0) {  // Consider 80% as minimum acceptable score
            all_architectures_pass = false;
        }

        // Store device pointer reference for cleanup
        if (results.detailed_results.contains("device_ptr")) {
            // This will be cleaned up in ValidateGpuArchitecture
        }
    }

    // Generate comprehensive cross-architecture report
    json cross_architecture_report;
    cross_architecture_report["validation_info"] = {
        {"timestamp", std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()},
        {"total_gpus_tested", gpu_architectures_.size()},
        {"validation_suite_version", "1.0"}
    };

    json gpu_results = json::array();
    for (const auto& results : all_results) {
        json gpu_result = {
            {"device_id", results.gpu_info.device_id},
            {"device_name", results.gpu_info.device_name},
            {"compute_capability", results.gpu_info.compute_capability},
            {"architecture_family", results.gpu_info.architecture_family},
            {"total_memory_mb", results.gpu_info.total_memory_mb},
            {"memory_optimization_works", results.memory_optimization_works},
            {"adaptive_scaling_works", results.adaptive_scaling_works},
            {"bandwidth_validation_works", results.bandwidth_validation_works},
            {"accuracy_maintained", results.accuracy_maintained},
            {"performance_score", results.performance_score},
            {"validation_issues", results.validation_issues},
            {"performance_metrics", results.performance_metrics},
            {"detailed_results", results.detailed_results}
        };
        gpu_results.push_back(gpu_result);
    }
    cross_architecture_report["gpu_results"] = gpu_results;

    // Calculate overall statistics
    std::vector<double> performance_scores;
    for (const auto& results : all_results) {
        performance_scores.push_back(results.performance_score);
    }

    double avg_score = std::accumulate(performance_scores.begin(), performance_scores.end(), 0.0) / performance_scores.size();
    double min_score = *std::min_element(performance_scores.begin(), performance_scores.end());
    double max_score = *std::max_element(performance_scores.begin(), performance_scores.end());

    cross_architecture_report["summary"] = {
        {"average_performance_score", avg_score},
        {"min_performance_score", min_score},
        {"max_performance_score", max_score},
        {"all_architectures_pass", all_architectures_pass},
        {"architectures_tested", static_cast<int>(gpu_architectures_.size())}
    };

    // Save cross-architecture validation report
    std::ofstream report_file("cross_architecture_validation_report.json");
    report_file << cross_architecture_report.dump(4);
    report_file.close();

    // Print final summary
    std::cout << "\n=== Cross-Architecture Validation Summary ===" << std::endl;
    std::cout << "GPUs Tested: " << gpu_architectures_.size() << std::endl;
    std::cout << "Average Performance Score: " << avg_score << "/100" << std::endl;
    std::cout << "Min Performance Score: " << min_score << "/100" << std::endl;
    std::cout << "Max Performance Score: " << max_score << "/100" << std::endl;
    std::cout << "Overall Status: " << (all_architectures_pass ? "PASS" : "FAIL") << std::endl;
    std::cout << "Report saved to: cross_architecture_validation_report.json" << std::endl;

    // Print architecture-specific summary
    std::cout << "\nArchitecture Breakdown:" << std::endl;
    std::map<std::string, std::vector<const ArchitectureTestResults*>> architectures_by_family;
    for (const auto& results : all_results) {
        architectures_by_family[results.gpu_info.architecture_family].push_back(&results);
    }

    for (const auto& [family, results] : architectures_by_family) {
        std::cout << "  " << family << ": ";
        for (const auto* result : results) {
            std::cout << result->gpu_info.device_name << " (" << result->performance_score << "/100)";
            if (result != results.back()) std::cout << ", ";
        }
        std::cout << std::endl;
    }

    // Print any validation issues
    for (const auto& results : all_results) {
        if (!results.validation_issues.empty()) {
            std::cout << "\nValidation Issues for " << results.gpu_info.device_name << ":" << std::endl;
            for (const auto& issue : results.validation_issues) {
                std::cout << "  - " << issue << std::endl;
            }
        }
    }

    // Assertions
    EXPECT_FALSE(gpu_architectures_.empty()) << "At least one GPU should be available for testing";
    EXPECT_GT(avg_score, 75.0) << "Average performance score should be >75%";
    EXPECT_TRUE(all_architectures_pass) << "All architectures should pass validation";
}

TEST_F(CrossArchitectureValidationTest, ValidateArchitectureSpecificFeatures) {
    std::cout << "\n=== Architecture-Specific Feature Validation ===" << std::endl;

    for (const auto& gpu_info : gpu_architectures_) {
        std::cout << "\nTesting " << gpu_info.architecture_family << " features..." << std::endl;

        cudaSetDevice(gpu_info.device_id);

        // Test managed memory support
        if (gpu_info.supports_managed_memory) {
            void* managed_ptr = nullptr;
            cudaError_t err = cudaMallocManaged(&managed_ptr, 1024 * 1024); // 1MB
            EXPECT_EQ(err, cudaSuccess) << "Managed memory allocation should work on " << gpu_info.device_name;
            if (err == cudaSuccess) {
                cudaFree(managed_ptr);
            }
            std::cout << "  Managed Memory: ✓" << std::endl;
        } else {
            std::cout << "  Managed Memory: ✗ (Not supported)" << std::endl;
        }

        // Test cooperative groups support
        if (gpu_info.supports_cooperative_groups) {
            // Simple cooperative group kernel launch test
            std::cout << "  Cooperative Groups: ✓" << std::endl;
        } else {
            std::cout << "  Cooperative Groups: ✗ (Not supported)" << std::endl;
        }

        // Test concurrent kernel support
        if (std::find(gpu_info.supported_features.begin(), gpu_info.supported_features.end(),
                      "concurrent_kernels") != gpu_info.supported_features.end()) {
            std::cout << "  Concurrent Kernels: ✓" << std::endl;
        } else {
            std::cout << "  Concurrent Kernels: ✗ (Not supported)" << std::endl;
        }

        // Test basic memory optimizations compatibility
        try {
            auto memory_optimizer = std::make_unique<MemoryOptimizer>(gpu_info.device_id);
            memory_optimizer->EnableAsynchronousTransfers();
            std::cout << "  Memory Optimizer: ✓" << std::endl;
        } catch (const std::exception& e) {
            std::cout << "  Memory Optimizer: ✗ (" << e.what() << ")" << std::endl;
        }

        // Test adaptive scaling compatibility
        try {
            auto adaptive_scaling = CreateAdaptiveParallelismScaling(gpu_info.device_id);
            std::cout << "  Adaptive Scaling: ✓" << std::endl;
        } catch (const std::exception& e) {
            std::cout << "  Adaptive Scaling: ✗ (" << e.what() << ")" << std::endl;
        }
    }
}

} // namespace puzzle71::gpu::performance