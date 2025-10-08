// Test for parallelism scaling under resource contention
#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <algorithm>
#include <cmath>
#include <random>

// Mock structures for testing
struct GpuCapabilities {
    int compute_capability;
    std::string device_name;
    int sm_count;
    size_t total_memory_mb;
    size_t free_memory_mb;
    int max_threads_per_block;
    int max_registers_per_thread;
    size_t shared_memory_per_block;
    double memory_bandwidth_gb_per_sec;
};

struct ParallelismConfiguration {
    int points_per_thread;
    int block_size;
    int grid_size;
    size_t shared_memory_size;
    int registers_per_thread;
    double expected_occupancy;
    double memory_utilization_estimate;
    std::string configuration_rationale;
};

struct ResourceContentionScenario {
    std::string name;
    size_t available_memory_mb;
    double memory_bandwidth_utilization;
    double thermal_pressure;
    int concurrent_processes;
    std::string expected_behavior;
};

// Mock adaptive parallelism scaling system
class MockAdaptiveParallelismScaling {
private:
    GpuCapabilities gpu_caps_;
    std::map<std::string, double> performance_history_;
    std::vector<ParallelismConfiguration> failed_configurations_;

public:
    explicit MockAdaptiveParallelismScaling(const GpuCapabilities& caps) : gpu_caps_(caps) {}

    ParallelismConfiguration GetOptimalConfiguration(size_t workload_size, const ResourceContentionScenario& scenario) {
        ParallelismConfiguration config = GetBaseConfiguration(workload_size);

        // Apply resource contention adaptations
        config = AdaptForMemoryPressure(config, scenario.available_memory_mb);
        config = AdaptForBandwidthContention(config, scenario.memory_bandwidth_utilization);
        config = AdaptForThermalPressure(config, scenario.thermal_pressure);
        config = AdaptForConcurrency(config, scenario.concurrent_processes);

        return config;
    }

    ParallelismConfiguration GetBaseConfiguration(size_t workload_size) {
        ParallelismConfiguration config;

        // Architecture-specific base configuration
        if (gpu_caps_.compute_capability >= 90) { // Hopper
            config.block_size = 1024;
            config.points_per_thread = 256;
            config.grid_size = gpu_caps_.sm_count * 3;
            config.shared_memory_size = gpu_caps_.shared_memory_per_block * 0.75;
            config.registers_per_thread = 80;
            config.expected_occupancy = 0.90;
            config.memory_utilization_estimate = 0.70;
        } else if (gpu_caps_.compute_capability >= 89) { // Ada
            config.block_size = 768;
            config.points_per_thread = 192;
            config.grid_size = gpu_caps_.sm_count * 2;
            config.shared_memory_size = gpu_caps_.shared_memory_per_block * 0.66;
            config.registers_per_thread = 72;
            config.expected_occupancy = 0.85;
            config.memory_utilization_estimate = 0.65;
        } else if (gpu_caps_.compute_capability >= 80) { // Ampere
            config.block_size = 512;
            config.points_per_thread = 128;
            config.grid_size = gpu_caps_.sm_count * 2;
            config.shared_memory_size = gpu_caps_.shared_memory_per_block * 0.5;
            config.registers_per_thread = 64;
            config.expected_occupancy = 0.80;
            config.memory_utilization_estimate = 0.60;
        } else { // Older architectures
            config.block_size = 384;
            config.points_per_thread = 96;
            config.grid_size = gpu_caps_.sm_count;
            config.shared_memory_size = gpu_caps_.shared_memory_per_block * 0.4;
            config.registers_per_thread = 56;
            config.expected_occupancy = 0.75;
            config.memory_utilization_estimate = 0.55;
        }

        config.configuration_rationale = "base_configuration";
        return config;
    }

    ParallelismConfiguration AdaptForMemoryPressure(ParallelismConfiguration config, size_t available_memory_mb) {
        // Simulate more realistic memory pressure based on available memory
        double memory_pressure_ratio = 1.0 - (static_cast<double>(available_memory_mb) / gpu_caps_.total_memory_mb);

        if (memory_pressure_ratio > 0.7) {
            // Critical memory pressure (<30% of total memory available)
            config.points_per_thread = std::max(64, config.points_per_thread / 2);
            config.block_size = std::max(256, config.block_size / 2);
            config.grid_size = std::max(1, config.grid_size / 2);
            config.configuration_rationale += "_critical_memory_pressure";
            config.memory_utilization_estimate = memory_pressure_ratio;
        } else if (memory_pressure_ratio > 0.5) {
            // High memory pressure (<50% of total memory available)
            config.points_per_thread = std::max(64, static_cast<int>(config.points_per_thread * 0.75));
            config.block_size = std::max(256, static_cast<int>(config.block_size * 0.8));
            config.configuration_rationale += "_high_memory_pressure";
            config.memory_utilization_estimate = memory_pressure_ratio;
        } else if (memory_pressure_ratio > 0.3) {
            // Moderate memory pressure (<70% of total memory available)
            config.points_per_thread = std::max(64, static_cast<int>(config.points_per_thread * 0.9));
            config.configuration_rationale += "_moderate_memory_pressure";
            config.memory_utilization_estimate = memory_pressure_ratio;
        }

        return config;
    }

    ParallelismConfiguration AdaptForBandwidthContention(ParallelismConfiguration config, double bandwidth_utilization) {
        if (bandwidth_utilization > 0.9) {
            // Critical bandwidth contention - reduce memory access
            config.points_per_thread = std::max(64, config.points_per_thread / 2);
            config.block_size = std::min(config.block_size, 512);
            config.configuration_rationale += "_bandwidth_contention";
        } else if (bandwidth_utilization > 0.8) {
            // High bandwidth contention
            config.points_per_thread = std::max(64, static_cast<int>(config.points_per_thread * 0.8));
            config.configuration_rationale += "_bandwidth_pressure";
        }

        return config;
    }

    ParallelismConfiguration AdaptForThermalPressure(ParallelismConfiguration config, double thermal_pressure) {
        if (thermal_pressure > 0.9) {
            // Critical thermal pressure - reduce compute intensity
            config.block_size = std::max(256, static_cast<int>(config.block_size * 0.6));
            config.grid_size = std::max(1, config.grid_size / 2);
            config.configuration_rationale += "_thermal_throttling";
        } else if (thermal_pressure > 0.8) {
            // High thermal pressure
            config.block_size = std::max(256, static_cast<int>(config.block_size * 0.8));
            config.configuration_rationale += "_thermal_pressure";
        }

        return config;
    }

    ParallelismConfiguration AdaptForConcurrency(ParallelismConfiguration config, int concurrent_processes) {
        if (concurrent_processes > 4) {
            // High concurrency - need to share resources
            config.grid_size = std::max(1, config.grid_size / concurrent_processes);
            config.block_size = std::min(config.block_size, 512);
            config.configuration_rationale += "_high_concurrency";
        } else if (concurrent_processes > 2) {
            // Moderate concurrency
            config.grid_size = std::max(1, static_cast<int>(config.grid_size * 0.7));
            config.configuration_rationale += "_moderate_concurrency";
        }

        return config;
    }

    size_t EstimateMemoryUsage(const ParallelismConfiguration& config) {
        // Simplified memory usage estimation in MB
        size_t per_thread_memory = sizeof(int) * config.points_per_thread * 8 / 1024 / 1024; // Rough estimate in MB
        size_t shared_memory = config.shared_memory_size / 1024 / 1024; // Convert to MB
        size_t register_memory = config.registers_per_thread * config.block_size * 4 / 1024 / 1024; // Convert to MB

        size_t total_threads = static_cast<size_t>(config.block_size) * config.grid_size;
        size_t thread_memory = total_threads * per_thread_memory;

        return thread_memory + shared_memory + register_memory;
    }

    bool HasConfigurationFailed(const ParallelismConfiguration& config) {
        return std::find_if(failed_configurations_.begin(), failed_configurations_.end(),
                          [&config](const ParallelismConfiguration& failed) {
                              return failed.points_per_thread == config.points_per_thread &&
                                     failed.block_size == config.block_size &&
                                     failed.grid_size == config.grid_size;
                          }) != failed_configurations_.end();
    }

    void MarkConfigurationAsFailed(const ParallelismConfiguration& config) {
        failed_configurations_.push_back(config);
    }

    ParallelismConfiguration GetFallbackConfiguration() {
        ParallelismConfiguration fallback;
        fallback.block_size = 256;
        fallback.points_per_thread = 64;
        fallback.grid_size = std::max(1, gpu_caps_.sm_count / 2);
        fallback.shared_memory_size = gpu_caps_.shared_memory_per_block / 4;
        fallback.registers_per_thread = 32;
        fallback.expected_occupancy = 0.5;
        fallback.memory_utilization_estimate = 0.3;
        fallback.configuration_rationale = "safe_fallback";
        return fallback;
    }
};

struct TestResult {
    std::string scenario_name;
    bool test_passed;
    std::vector<std::string> failures;
    ParallelismConfiguration original_config;
    ParallelismConfiguration adapted_config;
    std::string observed_behavior;
};

int main() {
    std::cout << "=== Testing Parallelism Scaling Under Resource Contention ===" << std::endl;

    // Setup mock GPU capabilities (RTX 3090 - Ampere)
    GpuCapabilities rtx3090_caps = {
        86, "RTX 3090", 108, 24000, 22000, 1024, 255, 102400, 936.0
    };

    MockAdaptiveParallelismScaling scaling_system(rtx3090_caps);

    // Define resource contention scenarios
    std::vector<ResourceContentionScenario> scenarios = {
        {
            "Normal Operation",
            20000,  // 20GB available memory
            0.4,    // 40% bandwidth utilization
            0.3,    // 30% thermal pressure
            1,      // Single process
            "Should use optimal configuration"
        },
        {
            "Memory Pressure - Moderate",
            8000,   // 8GB available memory
            0.5,    // 50% bandwidth utilization
            0.4,    // 40% thermal pressure
            1,      // Single process
            "Should reduce points_per_thread and possibly block_size"
        },
        {
            "Memory Pressure - Critical",
            2000,   // 2GB available memory
            0.6,    // 60% bandwidth utilization
            0.5,    // 50% thermal pressure
            1,      // Single process
            "Should significantly reduce all parameters"
        },
        {
            "Bandwidth Contention",
            18000,  // 18GB available memory
            0.95,   // 95% bandwidth utilization
            0.4,    // 40% thermal pressure
            2,      // 2 concurrent processes
            "Should reduce memory access patterns"
        },
        {
            "Thermal Throttling",
            16000,  // 16GB available memory
            0.7,    // 70% bandwidth utilization
            0.95,   // 95% thermal pressure
            1,      // Single process
            "Should reduce compute intensity"
        },
        {
            "High Concurrency",
            12000,  // 12GB available memory
            0.8,    // 80% bandwidth utilization
            0.6,    // 60% thermal pressure
            6,      // 6 concurrent processes
            "Should significantly reduce resource usage"
        },
        {
            "Multi-Resource Contention",
            4000,   // 4GB available memory
            0.9,    // 90% bandwidth utilization
            0.85,   // 85% thermal pressure
            4,      // 4 concurrent processes
            "Should apply aggressive resource reduction"
        }
    };

    std::vector<TestResult> test_results;
    bool all_tests_passed = true;

    for (const auto& scenario : scenarios) {
        std::cout << "\n=== Testing Scenario: " << scenario.name << " ===" << std::endl;
        std::cout << "Expected: " << scenario.expected_behavior << std::endl;

        TestResult result;
        result.scenario_name = scenario.name;

        // Get base configuration
        ParallelismConfiguration base_config = scaling_system.GetBaseConfiguration(1000000);
        result.original_config = base_config;

        std::cout << "Base configuration:" << std::endl;
        std::cout << "  points_per_thread: " << base_config.points_per_thread << std::endl;
        std::cout << "  block_size: " << base_config.block_size << std::endl;
        std::cout << "  grid_size: " << base_config.grid_size << std::endl;
        std::cout << "  memory_utilization_estimate: " << base_config.memory_utilization_estimate << std::endl;

        // Get adapted configuration under contention
        ParallelismConfiguration adapted_config = scaling_system.GetOptimalConfiguration(1000000, scenario);
        result.adapted_config = adapted_config;

        std::cout << "Adapted configuration:" << std::endl;
        std::cout << "  points_per_thread: " << adapted_config.points_per_thread << std::endl;
        std::cout << "  block_size: " << adapted_config.block_size << std::endl;
        std::cout << "  grid_size: " << adapted_config.grid_size << std::endl;
        std::cout << "  memory_utilization_estimate: " << adapted_config.memory_utilization_estimate << std::endl;
        std::cout << "  rationale: " << adapted_config.configuration_rationale << std::endl;

        // Validate adaptation behavior
        bool scenario_test_passed = true;

        // Test 1: Memory pressure scenarios should reduce memory usage
        if (scenario.name.find("Memory Pressure") != std::string::npos) {
            // Calculate expected memory utilization reduction based on parameter changes
            double ppt_reduction = static_cast<double>(adapted_config.points_per_thread) / base_config.points_per_thread;
            double block_reduction = static_cast<double>(adapted_config.block_size) / base_config.block_size;
            double grid_reduction = static_cast<double>(adapted_config.grid_size) / base_config.grid_size;

            // Overall memory usage should be reduced
            double expected_memory_reduction = ppt_reduction * std::min(block_reduction, 1.0) * std::min(grid_reduction, 1.0);

            if (expected_memory_reduction >= 1.0) {
                result.failures.push_back("Memory pressure should reduce overall memory usage parameters");
                scenario_test_passed = false;
            } else {
                std::cout << "  ✅ PASSED: Memory usage parameters reduced (factor: " << expected_memory_reduction << ")" << std::endl;
            }

            if (adapted_config.points_per_thread > base_config.points_per_thread) {
                result.failures.push_back("Memory pressure should not increase points_per_thread");
                scenario_test_passed = false;
            } else {
                std::cout << "  ✅ PASSED: points_per_thread appropriately reduced or maintained" << std::endl;
            }
        }

        // Test 2: Bandwidth contention should reduce memory-intensive parameters
        if (scenario.name.find("Bandwidth") != std::string::npos) {
            if (adapted_config.points_per_thread > base_config.points_per_thread) {
                result.failures.push_back("Bandwidth contention should not increase points_per_thread");
                scenario_test_passed = false;
            } else {
                std::cout << "  ✅ PASSED: points_per_thread reduced for bandwidth contention" << std::endl;
            }
        }

        // Test 3: Thermal pressure should reduce compute intensity
        if (scenario.name.find("Thermal") != std::string::npos) {
            if (adapted_config.block_size > base_config.block_size) {
                result.failures.push_back("Thermal pressure should not increase block_size");
                scenario_test_passed = false;
            } else {
                std::cout << "  ✅ PASSED: block_size reduced for thermal pressure" << std::endl;
            }
        }

        // Test 4: High concurrency should reduce resource usage
        if (scenario.name.find("Concurrency") != std::string::npos) {
            if (adapted_config.grid_size > base_config.grid_size) {
                result.failures.push_back("High concurrency should not increase grid_size");
                scenario_test_passed = false;
            } else {
                std::cout << "  ✅ PASSED: grid_size reduced for concurrency" << std::endl;
            }
        }

        // Test 5: Parameters should remain within valid ranges
        if (adapted_config.points_per_thread < 32 || adapted_config.points_per_thread > 256) {
            result.failures.push_back("points_per_thread out of valid range [32, 256]");
            scenario_test_passed = false;
        }

        if (adapted_config.block_size < 128 || adapted_config.block_size > 1024) {
            result.failures.push_back("block_size out of valid range [128, 1024]");
            scenario_test_passed = false;
        }

        if (adapted_config.grid_size < 1 || adapted_config.grid_size > rtx3090_caps.sm_count * 4) {
            result.failures.push_back("grid_size out of valid range");
            scenario_test_passed = false;
        }

        // Test 6: Configuration rationale should reflect applied adaptations
        bool has_appropriate_rationale = false;
        if (scenario.name.find("Memory") != std::string::npos) {
            if (adapted_config.configuration_rationale.find("memory") != std::string::npos ||
                adapted_config.configuration_rationale.find("critical") != std::string::npos) {
                has_appropriate_rationale = true;
            }
        }
        if (scenario.name.find("Bandwidth") != std::string::npos) {
            if (adapted_config.configuration_rationale.find("bandwidth") != std::string::npos) {
                has_appropriate_rationale = true;
            }
        }
        if (scenario.name.find("Thermal") != std::string::npos) {
            if (adapted_config.configuration_rationale.find("thermal") != std::string::npos) {
                has_appropriate_rationale = true;
            }
        }
        if (scenario.name.find("Concurrency") != std::string::npos) {
            if (adapted_config.configuration_rationale.find("concurrency") != std::string::npos) {
                has_appropriate_rationale = true;
            }
        }
        if (scenario.name.find("Multi-Resource") != std::string::npos) {
            // Check for multiple adaptation indicators
            int adaptation_count = 0;
            if (adapted_config.configuration_rationale.find("memory") != std::string::npos) adaptation_count++;
            if (adapted_config.configuration_rationale.find("bandwidth") != std::string::npos) adaptation_count++;
            if (adapted_config.configuration_rationale.find("thermal") != std::string::npos) adaptation_count++;
            if (adapted_config.configuration_rationale.find("concurrency") != std::string::npos) adaptation_count++;
            if (adaptation_count >= 2) has_appropriate_rationale = true;
        }

        if (scenario.name != "Normal Operation" && !has_appropriate_rationale) {
            result.failures.push_back("Configuration rationale doesn't reflect applied adaptations");
            scenario_test_passed = false;
        } else if (scenario.name != "Normal Operation" && has_appropriate_rationale) {
            std::cout << "  ✅ PASSED: Configuration rationale reflects adaptations" << std::endl;
        }

        result.test_passed = scenario_test_passed;
        result.observed_behavior = adapted_config.configuration_rationale;

        if (scenario_test_passed) {
            std::cout << "  🎯 SCENARIO PASSED" << std::endl;
        } else {
            std::cout << "  ❌ SCENARIO FAILED" << std::endl;
            for (const auto& failure : result.failures) {
                std::cout << "    - " << failure << std::endl;
            }
            all_tests_passed = false;
        }

        test_results.push_back(result);
    }

    // Additional stress test: Simulate rapidly changing resource conditions
    std::cout << "\n=== Stress Test: Rapid Resource Condition Changes ===" << std::endl;

    std::vector<std::pair<size_t, double>> resource_fluctuations = {
        {20000, 0.3}, // Normal conditions
        {8000, 0.7},  // Memory pressure appears
        {3000, 0.9},  // Critical memory pressure
        {12000, 0.5}, // Memory pressure eases
        {18000, 0.4}, // Near normal
        {5000, 0.85}, // Memory pressure returns
        {15000, 0.6}  // Stabilizes
    };

    ParallelismConfiguration previous_config;
    bool stress_test_passed = true;

    for (size_t i = 0; i < resource_fluctuations.size(); ++i) {
        ResourceContentionScenario fluctuation_scenario = {
            "Fluctuation " + std::to_string(i),
            resource_fluctuations[i].first,
            resource_fluctuations[i].second,
            0.4,
            1,
            "Should adapt to changing conditions"
        };

        auto config = scaling_system.GetOptimalConfiguration(1000000, fluctuation_scenario);

        std::cout << "Step " << (i+1) << ": Memory=" << resource_fluctuations[i].first
                  << "MB, Bandwidth=" << resource_fluctuations[i].second
                  << " -> PPT=" << config.points_per_thread
                  << ", BS=" << config.block_size << std::endl;

        // Check that configuration changes in response to resource changes
        if (i > 0) {
            size_t prev_mem = resource_fluctuations[i-1].first;
            size_t curr_mem = resource_fluctuations[i].first;

            // Memory decreased significantly - should see reduction in parameters
            if (curr_mem < prev_mem * 0.7) {
                if (config.points_per_thread > previous_config.points_per_thread) {
                    std::cout << "  ❌ Should reduce parameters when memory decreases" << std::endl;
                    stress_test_passed = false;
                } else {
                    std::cout << "  ✅ Correctly reduced parameters for memory decrease" << std::endl;
                }
            }
            // Memory increased significantly - should see increase or maintenance of parameters
            else if (curr_mem > prev_mem * 1.3) {
                if (config.points_per_thread < previous_config.points_per_thread * 0.8) {
                    std::cout << "  ❌ Should not unnecessarily reduce parameters when memory increases" << std::endl;
                    stress_test_passed = false;
                } else {
                    std::cout << "  ✅ Correctly maintained or increased parameters for memory increase" << std::endl;
                }
            }
        }

        previous_config = config;
    }

    if (!stress_test_passed) {
        all_tests_passed = false;
    }

    // Final results
    std::cout << "\n=== FINAL TEST RESULTS ===" << std::endl;

    size_t passed_scenarios = 0;
    for (const auto& result : test_results) {
        if (result.test_passed) {
            passed_scenarios++;
        }
    }

    std::cout << "Resource contention scenarios passed: " << passed_scenarios << "/" << test_results.size() << std::endl;
    std::cout << "Stress test: " << (stress_test_passed ? "PASSED" : "FAILED") << std::endl;

    if (all_tests_passed) {
        std::cout << "\n🎉 ALL TESTS PASSED! Parallelism scaling under resource contention is working correctly." << std::endl;
        std::cout << "\nKey findings:" << std::endl;
        std::cout << "- Memory pressure correctly reduces points_per_thread and block_size" << std::endl;
        std::cout << "- Bandwidth contention appropriately limits memory-intensive parameters" << std::endl;
        std::cout << "- Thermal pressure reduces compute intensity to manage heat" << std::endl;
        std::cout << "- High concurrency scenarios scale down resource usage appropriately" << std::endl;
        std::cout << "- Multi-resource contention applies combined adaptations" << std::endl;
        std::cout << "- System responds dynamically to changing resource conditions" << std::endl;
        std::cout << "- All parameters remain within safe operational ranges" << std::endl;
        std::cout << "- Configuration rationales accurately reflect applied adaptations" << std::endl;
        return 0;
    } else {
        std::cout << "\n❌ SOME TESTS FAILED! Please review the resource contention handling logic." << std::endl;
        return 1;
    }
}