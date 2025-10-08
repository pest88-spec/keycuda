/*
 * GPU Optimization Usage Examples
 *
 * This file demonstrates practical usage of keycuda's GPU optimization features
 * including adaptive parallelism scaling, memory optimization, and performance monitoring.
 */

#include <iostream>
#include <chrono>
#include <vector>
#include <memory>
#include <iomanip>

#include "gpu_executor.h"
#include "adaptive_scaling.h"
#include "memory_manager.h"
#include "bandwidth_validator.h"
#include "monitoring/performance_monitor.h"
#include "solver.h"

using namespace keycuda;
using namespace std::chrono;

// Example 1: Basic optimized key search
void Example1_BasicOptimizedSearch() {
    std::cout << "\n=== Example 1: Basic Optimized Key Search ===" << std::endl;

    try {
        // Create solver with optimizations enabled
        Solver solver;
        solver.EnableAdaptiveParallelism(true);
        solver.EnableMemoryOptimization(true);
        solver.EnableBandwidthValidation(true);

        // Target address (example)
        std::string target_address = "1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa";
        uint8_t public_key_hash[20];
        // Convert target address to hash (simplified for example)
        memset(public_key_hash, 0, 20);

        std::cout << "Starting optimized key search for address: " << target_address << std::endl;

        auto start_time = high_resolution_clock::now();

        // Run optimized search with automatic parameter tuning
        auto result = solver.FindPrivateKey(target_address);

        auto end_time = high_resolution_clock::now();
        auto duration = duration_cast<milliseconds>(end_time - start_time);

        if (result.success) {
            std::cout << "SUCCESS! Found private key:" << std::endl;
            std::cout << "Private key: " << result.private_key.GetHex() << std::endl;
            std::cout << "Search time: " << duration.count() << " ms" << std::endl;
            std::cout << "Keys searched: " << result.keys_searched << std::endl;
            std::cout << "Performance: " << (result.keys_searched / 1000000.0) / (duration.count() / 1000.0)
                      << " Mkeys/sec" << std::endl;
        } else {
            std::cout << "Key not found in search space" << std::endl;
            std::cout << "Keys searched: " << result.keys_searched << std::endl;
            std::cout << "Search time: " << duration.count() << " ms" << std::endl;
        }

    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << std::endl;
    }
}

// Example 2: Advanced configuration with custom parameters
void Example2_AdvancedConfiguration() {
    std::cout << "\n=== Example 2: Advanced Configuration ===" << std::endl;

    try {
        // Create GPU executor
        auto executor = std::make_unique<GpuExecutor>(0);

        // Configure adaptive scaling with custom parameters
        AdaptiveScalingConfig config;
        config.target_throughput = 50.0;              // 50 Mkeys/sec target
        config.min_batch_size = 2048;                 // 2K minimum batch
        config.max_batch_size = 2048 * 2048;          // 4M maximum batch
        config.block_size_range = {128, 256, 512, 1024}; // Test multiple block sizes
        config.performance_window = seconds(30);      // 30-second evaluation window
        config.scaling_threshold = 0.05;              // 5% performance change triggers scaling

        auto adaptive_scaling = CreateAdaptiveParallelismScaling(config);
        adaptive_scaling->EnableAutoScaling(true);

        // Create memory optimizer with aggressive settings
        auto memory_manager = std::make_unique<MemoryManager>(0);
        memory_manager->SetOptimizationLevel(MemoryOptimizationLevel::AGGRESSIVE);
        memory_manager->SetBandwidthTarget(0.90);     // 90% bandwidth target
        memory_manager->SetMemoryPoolSize(2ULL * 1024 * 1024 * 1024); // 2GB pool

        // Create bandwidth validator
        auto bandwidth_validator = std::make_unique<BandwidthValidator>(0);

        std::cout << "Running bandwidth validation..." << std::endl;
        auto validation_result = bandwidth_validator->ValidateBandwidth();

        if (validation_result.is_valid) {
            std::cout << "✓ Bandwidth validation PASSED" << std::endl;
            std::cout << "  Achieved: " << std::fixed << std::setprecision(2)
                      << validation_result.achieved_bandwidth << " GB/s" << std::endl;
            std::cout << "  Utilization: " << (validation_result.utilization * 100) << "%" << std::endl;
        } else {
            std::cout << "✗ Bandwidth validation FAILED" << std::endl;
            std::cout << "  Error: " << validation_result.error_message << std::endl;
        }

        // Test different batch sizes
        std::vector<size_t> batch_sizes = {1024, 4096, 16384, 65536, 262144};
        uint8_t test_hash[20];
        memset(test_hash, 0, 20);

        std::cout << "\nTesting performance with different batch sizes:" << std::endl;
        std::cout << std::setw(12) << "Batch Size" << std::setw(15) << "Throughput"
                  << std::setw(15) << "Latency" << std::setw(15) << "GPU Util%" << std::endl;

        for (size_t batch_size : batch_sizes) {
            auto start_time = high_resolution_clock::now();

            // Run search with current batch size
            auto results = executor->SearchKeysOptimized(test_hash, batch_size);

            auto end_time = high_resolution_clock::now();
            auto duration = duration_cast<microseconds>(end_time - start_time);

            double throughput = (batch_size / 1000000.0) / (duration.count() / 1000000.0);
            double latency = duration.count() / 1000.0;

            // Get current GPU metrics (simplified)
            double gpu_util = 0.85; // Would get from actual metrics

            std::cout << std::setw(12) << batch_size
                      << std::setw(14) << std::fixed << std::setprecision(2) << throughput << " M/s"
                      << std::setw(14) << std::setprecision(1) << latency << " ms"
                      << std::setw(14) << (gpu_util * 100) << "%" << std::endl;
        }

        // Show adaptive scaling recommendations
        auto optimal_config = adaptive_scaling->GetOptimalConfiguration();
        std::cout << "\nAdaptive scaling recommendations:" << std::endl;
        std::cout << "  Optimal block size: " << optimal_config.block_size << std::endl;
        std::cout << "  Optimal grid size: " << optimal_config.grid_size << std::endl;
        std::cout << "  Optimal batch size: " << optimal_config.batch_size << std::endl;
        std::cout << "  Expected throughput: " << optimal_config.expected_throughput << " Mkeys/sec" << std::endl;

    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << std::endl;
    }
}

// Example 3: Performance monitoring and alerting
void Example3_PerformanceMonitoring() {
    std::cout << "\n=== Example 3: Performance Monitoring and Alerting ===" << std::endl;

    try {
        // Create performance monitor
        auto monitor = std::make_unique<monitoring::PerformanceMonitor>(0);

        // Add multiple alert channels
        monitor->AddAlertChannel(std::make_unique<monitoring::ConsoleAlertChannel>(true));
        monitor->AddAlertChannel(std::make_unique<monitoring::FileAlertChannel>("/tmp/keycuda_alerts.log"));

        // Configure alert thresholds
        monitor->SetAlertThreshold(monitoring::AlertType::PERFORMANCE_DEGRADATION, 0.15,
                                 monitoring::AlertSeverity::WARNING);
        monitor->SetAlertThreshold(monitoring::AlertType::MEMORY_PRESSURE, 0.85,
                                 monitoring::AlertSeverity::WARNING);
        monitor->SetAlertThreshold(monitoring::AlertType::TEMPERATURE_HIGH, 80.0,
                                 monitoring::AlertSeverity::WARNING);
        monitor->SetAlertThreshold(monitoring::AlertType::GPU_UTILIZATION_LOW, 0.60,
                                 monitoring::AlertSeverity::INFO);

        std::cout << "Starting performance monitoring..." << std::endl;
        monitor->StartMonitoring(milliseconds(500));  // Collect every 500ms

        // Create executor and run workload
        auto executor = std::make_unique<GpuExecutor>(0);
        uint8_t test_hash[20];
        memset(test_hash, 0x42, 20);

        // Run extended workload to generate monitoring data
        std::cout << "Running extended workload for monitoring..." << std::endl;
        const int iterations = 50;
        std::vector<double> throughputs;

        for (int i = 0; i < iterations; ++i) {
            auto start_time = high_resolution_clock::now();

            // Vary batch size to create performance changes
            size_t batch_size = 1024 * (1 + (i % 16));
            auto results = executor->SearchKeysOptimized(test_hash, batch_size);

            auto end_time = high_resolution_clock::now();
            auto duration = duration_cast<microseconds>(end_time - start_time);

            double throughput = (batch_size / 1000000.0) / (duration.count() / 1000000.0);
            throughputs.push_back(throughput);

            if (i % 10 == 0) {
                std::cout << "Iteration " << i << ": " << std::fixed << std::setprecision(2)
                          << throughput << " Mkeys/sec" << std::endl;
            }

            // Small delay to allow monitoring to collect data
            std::this_thread::sleep_for(milliseconds(100));
        }

        // Get monitoring statistics
        auto status = monitor->GetMonitoringStatus();
        std::cout << "\nMonitoring Statistics:" << std::endl;
        std::cout << "  Monitoring enabled: " << (status["monitoring_enabled"] ? "Yes" : "No") << std::endl;
        std::cout << "  Total alerts generated: " << status["total_alerts"] << std::endl;
        std::cout << "  Alert channels: " << status["alert_channels_count"] << std::endl;

        // Get current metrics
        auto snapshot = monitor->GetCurrentSnapshot();
        std::cout << "\nCurrent Performance Metrics:" << std::endl;
        std::cout << "  GPU utilization: " << (snapshot.gpu_utilization * 100) << "%" << std::endl;
        std::cout << "  Memory usage: " << snapshot.memory_used_mb << " MB" << std::endl;
        std::cout << "  Memory utilization: " << (snapshot.memory_utilization * 100) << "%" << std::endl;
        std::cout << "  Temperature: " << snapshot.temperature_c << "°C" << std::endl;
        std::cout << "  Key search throughput: " << snapshot.key_search_throughput << " Mkeys/sec" << std::endl;
        std::cout << "  Memory bandwidth utilization: " << (snapshot.memory_bandwidth_utilization * 100) << "%" << std::endl;

        // Calculate average performance
        double avg_throughput = 0.0;
        for (double t : throughputs) {
            avg_throughput += t;
        }
        avg_throughput /= throughputs.size();

        std::cout << "\nWorkload Performance Summary:" << std::endl;
        std::cout << "  Average throughput: " << std::fixed << std::setprecision(2)
                  << avg_throughput << " Mkeys/sec" << std::endl;
        std::cout << "  Total iterations: " << iterations << std::endl;

        // Export monitoring data
        monitor->ExportMonitoringData("/tmp/monitoring_export.json");
        std::cout << "  Monitoring data exported to /tmp/monitoring_export.json" << std::endl;

        // Show alert history
        const auto& alert_history = monitor->GetAlertHistory();
        if (!alert_history.empty()) {
            std::cout << "\nRecent Alerts:" << std::endl;
            int shown = 0;
            for (auto it = alert_history.rbegin(); it != alert_history.rend() && shown < 5; ++it, ++shown) {
                std::cout << "  [" << it->GetSeverityString() << "] "
                          << it->GetTypeString() << ": " << it->GetMessage() << std::endl;
            }
        } else {
            std::cout << "\nNo alerts generated during monitoring period" << std::endl;
        }

        monitor->StopMonitoring();

    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << std::endl;
    }
}

// Example 4: Error handling and recovery
void Example4_ErrorHandlingAndRecovery() {
    std::cout << "\n=== Example 4: Error Handling and Recovery ===" << std::endl;

    try {
        // Create executor with error recovery
        auto executor = std::make_unique<GpuExecutor>(0);

        // Create error recovery manager
        auto recovery_manager = std::make_unique<ErrorRecoveryManager>(std::move(executor));

        // Configure recovery strategies
        recovery_manager->SetRecoveryStrategy(ErrorType::CUDA_ERROR_OUT_OF_MEMORY,
                                             RecoveryStrategy::REDUCE_BATCH_SIZE);
        recovery_manager->SetRecoveryStrategy(ErrorType::CUDA_ERROR_DEVICE_LOST,
                                             RecoveryStrategy::DEVICE_RESET);
        recovery_manager->SetRecoveryStrategy(ErrorType::KERNEL_TIMEOUT,
                                             RecoveryStrategy::CANCEL_AND_RETRY);

        recovery_manager->EnableAutoRecovery(true);

        std::cout << "Testing error handling scenarios..." << std::endl;

        uint8_t test_hash[20];
        memset(test_hash, 0x33, 20);

        // Scenario 1: Normal operation
        std::cout << "\n1. Testing normal operation:" << std::endl;
        try {
            auto results = recovery_manager->GetExecutor()->SearchKeysOptimized(test_hash, 1024*1024);
            std::cout << "   ✓ Normal operation successful" << std::endl;
        } catch (const std::exception& e) {
            std::cout << "   ✗ Normal operation failed: " << e.what() << std::endl;
        }

        // Scenario 2: Simulate memory pressure
        std::cout << "\n2. Testing memory pressure handling:" << std::endl;
        try {
            // Try to allocate a very large batch to trigger OOM
            size_t huge_batch = 1024 * 1024 * 1024;  // Very large
            auto results = recovery_manager->GetExecutor()->SearchKeysOptimized(test_hash, huge_batch);
            std::cout << "   ✓ Large allocation successful (unexpected)" << std::endl;
        } catch (const std::exception& e) {
            std::cout << "   Memory error detected: " << e.what() << std::endl;

            // Attempt recovery
            if (recovery_manager->HandleError(ErrorType::CUDA_ERROR_OUT_OF_MEMORY)) {
                std::cout << "   ✓ Recovery successful, retrying with smaller batch..." << std::endl;
                auto results = recovery_manager->GetExecutor()->SearchKeysOptimized(test_hash, 1024*256);
                std::cout << "   ✓ Retry successful" << std::endl;
            } else {
                std::cout << "   ✗ Recovery failed" << std::endl;
            }
        }

        // Scenario 3: Simulate device issues
        std::cout << "\n3. Testing device issue handling:" << std::endl;
        try {
            // Reset device to simulate device lost
            cudaDeviceReset();

            // Try to use the device
            auto results = recovery_manager->GetExecutor()->SearchKeysOptimized(test_hash, 1024);
            std::cout << "   ✓ Device reset handled successfully" << std::endl;
        } catch (const std::exception& e) {
            std::cout << "   Device error detected: " << e.what() << std::endl;

            // Attempt device recovery
            if (recovery_manager->HandleError(ErrorType::CUDA_ERROR_DEVICE_LOST)) {
                std::cout << "   ✓ Device recovery successful" << std::endl;
            } else {
                std::cout << "   ✗ Device recovery failed" << std::endl;
            }
        }

        // Show recovery statistics
        std::cout << "\nRecovery Statistics:" << std::endl;
        std::cout << "  Total recovery attempts: " << recovery_manager->GetRecoveryAttempts() << std::endl;
        std::cout << "  Auto-recovery enabled: " << (recovery_manager->IsAutoRecoveryEnabled() ? "Yes" : "No") << std::endl;

    } catch (const std::exception& e) {
        std::cout << "Error in error handling example: " << e.what() << std::endl;
    }
}

// Example 5: Performance benchmarking
void Example5_PerformanceBenchmarking() {
    std::cout << "\n=== Example 5: Performance Benchmarking ===" << std::endl;

    try {
        // Create benchmark runner
        auto benchmark_runner = std::make_unique<BenchmarkRunner>(0);

        // Configure comprehensive benchmark
        BenchmarkConfig config;
        config.key_search_iterations = 100;
        config.memory_bandwidth_passes = 50;
        config.synchronization_tests = 25;
        config.warmup_iterations = 5;
        config.enable_detailed_profiling = true;

        std::cout << "Running comprehensive performance benchmark..." << std::endl;
        std::cout << "This may take a few minutes..." << std::endl;

        auto start_time = high_resolution_clock::now();

        // Run benchmark suite
        auto results = benchmark_runner->RunComprehensiveBenchmark(config);

        auto end_time = high_resolution_clock::now();
        auto benchmark_duration = duration_cast<seconds>(end_time - start_time);

        // Display results
        std::cout << "\nBenchmark Results (completed in " << benchmark_duration.count() << "s):" << std::endl;
        std::cout << std::string(60, '=') << std::endl;

        std::cout << "\nKey Search Performance:" << std::endl;
        std::cout << "  Throughput: " << std::fixed << std::setprecision(2)
                  << results.key_search_throughput << " Mkeys/sec" << std::endl;
        std::cout << "  Latency: " << std::setprecision(1)
                  << results.key_search_latency_us << " μs" << std::endl;
        std::cout << "  Success rate: " << (results.key_search_success_rate * 100) << "%" << std::endl;

        std::cout << "\nMemory Performance:" << std::endl;
        std::cout << "  Bandwidth: " << std::setprecision(2)
                  << results.memory_bandwidth_gbps << " GB/s" << std::endl;
        std::cout << "  Utilization: " << std::setprecision(1)
                  << (results.memory_bandwidth_utilization * 100) << "%" << std::endl;
        std::cout << "  Read speed: " << std::setprecision(2)
                  << results.memory_read_speed_gbps << " GB/s" << std::endl;
        std::cout << "  Write speed: " << std::setprecision(2)
                  << results.memory_write_speed_gbps << " GB/s" << std::endl;

        std::cout << "\nGPU Utilization:" << std::endl;
        std::cout << "  Average utilization: " << std::setprecision(1)
                  << (results.gpu_utilization * 100) << "%" << std::endl;
        std::cout << "  Peak utilization: " << std::setprecision(1)
                  << (results.peak_gpu_utilization * 100) << "%" << std::endl;
        std::cout << "  Power consumption: " << std::setprecision(1)
                  << results.power_consumption_w << " W" << std::endl;

        std::cout << "\nSynchronization Performance:" << std::endl;
        std::cout << "  Sync latency: " << std::setprecision(1)
                  << results.synchronization_latency_us << " μs" << std::endl;
        std::cout << "  Kernel launch overhead: " << std::setprecision(1)
                  << results.kernel_launch_overhead_us << " μs" << std::endl;

        // Performance rating
        std::cout << "\nPerformance Rating:" << std::endl;
        std::string rating;
        if (results.key_search_throughput >= 50.0) {
            rating = "EXCELLENT";
        } else if (results.key_search_throughput >= 40.0) {
            rating = "GOOD";
        } else if (results.key_search_throughput >= 30.0) {
            rating = "FAIR";
        } else {
            rating = "POOR";
        }
        std::cout << "  Overall: " << rating << std::endl;

        // Export detailed results
        results.ExportToJson("/tmp/benchmark_results.json");
        results.ExportToCsv("/tmp/benchmark_results.csv");

        std::cout << "\nDetailed results exported:" << std::endl;
        std::cout << "  JSON: /tmp/benchmark_results.json" << std::endl;
        std::cout << "  CSV: /tmp/benchmark_results.csv" << std::endl;

        // Performance recommendations
        std::cout << "\nPerformance Recommendations:" << std::endl;
        if (results.key_search_throughput < 40.0) {
            std::cout << "  - Consider increasing batch size for better throughput" << std::endl;
        }
        if (results.memory_bandwidth_utilization < 0.80) {
            std::cout << "  - Memory bandwidth utilization is low, check access patterns" << std::endl;
        }
        if (results.gpu_utilization < 0.90) {
            std::cout << "  - GPU utilization could be improved, adjust block/grid sizes" << std::endl;
        }
        if (results.power_consumption_w > 300.0) {
            std::cout << "  - High power consumption detected, monitor thermal throttling" << std::endl;
        }

    } catch (const std::exception& e) {
        std::cout << "Benchmark error: " << e.what() << std::endl;
    }
}

// Example 6: Multi-GPU optimization (if available)
void Example6_MultiGpuOptimization() {
    std::cout << "\n=== Example 6: Multi-GPU Optimization ===" << std::endl;

    int device_count = 0;
    cudaError_t error = cudaGetDeviceCount(&device_count);

    if (error != cudaSuccess || device_count <= 1) {
        std::cout << "Multi-GPU not available, skipping this example" << std::endl;
        return;
    }

    std::cout << "Found " << device_count << " GPUs, setting up multi-GPU optimization..." << std::endl;

    try {
        std::vector<std::unique_ptr<GpuExecutor>> executors;
        std::vector<std::unique_ptr<monitoring::PerformanceMonitor>> monitors;

        // Initialize all GPUs
        for (int i = 0; i < device_count; ++i) {
            cudaDeviceProp prop;
            cudaGetDeviceProperties(&prop, i);

            std::cout << "Initializing GPU " << i << ": " << prop.name << std::endl;

            auto executor = std::make_unique<GpuExecutor>(i);
            auto monitor = std::make_unique<monitoring::PerformanceMonitor>(i);

            // Configure per-GPU optimization based on capabilities
            if (prop.major >= 8) {  // Ampere and newer
                executor->SetOptimizationLevel(OptimizationLevel::AGGRESSIVE);
            } else if (prop.major == 7) {  // Turing/Volta
                executor->SetOptimizationLevel(OptimizationLevel::BALANCED);
            } else {  // Older architectures
                executor->SetOptimizationLevel(OptimizationLevel::CONSERVATIVE);
            }

            executors.push_back(std::move(executor));
            monitors.push_back(std::move(monitor));
        }

        // Start monitoring on all GPUs
        for (auto& monitor : monitors) {
            monitor->StartMonitoring(milliseconds(1000));
        }

        uint8_t test_hash[20];
        memset(test_hash, 0x44, 20);

        std::cout << "\nRunning distributed search across " << device_count << " GPUs..." << std::endl;

        auto start_time = high_resolution_clock::now();

        // Distribute workload across GPUs
        std::vector<std::future<std::vector<uint256_t>>> futures;
        const size_t batch_size_per_gpu = 1024 * 1024;

        for (int i = 0; i < device_count; ++i) {
            futures.push_back(std::async(std::launch::async, [&executors, i, &test_hash, batch_size_per_gpu]() {
                cudaSetDevice(i);  // Ensure we're using the right device
                return executors[i]->SearchKeysOptimized(test_hash, batch_size_per_gpu);
            }));
        }

        // Collect results
        std::vector<std::vector<uint256_t>> all_results;
        size_t total_keys_searched = 0;

        for (auto& future : futures) {
            auto results = future.get();
            all_results.push_back(results);
            total_keys_searched += batch_size_per_gpu;
        }

        auto end_time = high_resolution_clock::now();
        auto duration = duration_cast<milliseconds>(end_time - start_time);

        std::cout << "Multi-GPU search completed:" << std::endl;
        std::cout << "  Total keys searched: " << total_keys_searched << std::endl;
        std::cout << "  Total time: " << duration.count() << " ms" << std::endl;
        std::cout << "  Combined throughput: " << std::fixed << std::setprecision(2)
                  << (total_keys_searched / 1000000.0) / (duration.count() / 1000.0) << " Mkeys/sec" << std::endl;

        // Show per-GPU performance
        std::cout << "\nPer-GPU Performance:" << std::endl;
        for (int i = 0; i < device_count; ++i) {
            auto snapshot = monitors[i]->GetCurrentSnapshot();
            cudaDeviceProp prop;
            cudaGetDeviceProperties(&prop, i);

            std::cout << "  GPU " << i << " (" << prop.name << "):" << std::endl;
            std::cout << "    Throughput: " << std::setprecision(2)
                      << snapshot.key_search_throughput << " Mkeys/sec" << std::endl;
            std::cout << "    GPU utilization: " << std::setprecision(1)
                      << (snapshot.gpu_utilization * 100) << "%" << std::endl;
            std::cout << "    Memory bandwidth: " << std::setprecision(1)
                      << (snapshot.memory_bandwidth_utilization * 100) << "%" << std::endl;
        }

        // Stop monitoring
        for (auto& monitor : monitors) {
            monitor->StopMonitoring();
        }

        // Calculate efficiency
        double single_gpu_estimate = 40.0;  // Expected single GPU throughput
        double actual_multi_gpu = (total_keys_searched / 1000000.0) / (duration.count() / 1000.0);
        double scaling_efficiency = actual_multi_gpu / (single_gpu_estimate * device_count);

        std::cout << "\nMulti-GPU Scaling Efficiency:" << std::endl;
        std::cout << "  Expected " << device_count << "x GPU throughput: " << (single_gpu_estimate * device_count) << " Mkeys/sec" << std::endl;
        std::cout << "  Actual combined throughput: " << actual_multi_gpu << " Mkeys/sec" << std::endl;
        std::cout << "  Scaling efficiency: " << std::setprecision(1) << (scaling_efficiency * 100) << "%" << std::endl;

    } catch (const std::exception& e) {
        std::cout << "Multi-GPU error: " << e.what() << std::endl;
    }
}

int main() {
    std::cout << "Keycuda GPU Optimization Examples" << std::endl;
    std::cout << "=================================" << std::endl;

    // Check CUDA availability
    int device_count = 0;
    cudaError_t error = cudaGetDeviceCount(&device_count);
    if (error != cudaSuccess || device_count == 0) {
        std::cout << "No CUDA devices available. Exiting." << std::endl;
        return 1;
    }

    std::cout << "Found " << device_count << " CUDA device(s)" << std::endl;

    // Run all examples
    try {
        Example1_BasicOptimizedSearch();
        Example2_AdvancedConfiguration();
        Example3_PerformanceMonitoring();
        Example4_ErrorHandlingAndRecovery();
        Example5_PerformanceBenchmarking();
        Example6_MultiGpuOptimization();

        std::cout << "\n=== All Examples Completed Successfully ===" << std::endl;

    } catch (const std::exception& e) {
        std::cout << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}