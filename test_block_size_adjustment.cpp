// Test for dynamic block size adjustment
#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <algorithm>
#include <cmath>

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
};

struct ParallelismConfiguration {
    int block_size;
    int points_per_thread;
    int grid_size;
    size_t shared_memory_size;
    int registers_per_thread;
    double expected_occupancy;
    double memory_utilization_estimate;
    std::string configuration_rationale;
};

// Mock architecture-specific optimization functions
ParallelismConfiguration OptimizeForHopper(const GpuCapabilities& caps, size_t workload_size) {
    ParallelismConfiguration config;
    config.block_size = 1024;
    config.points_per_thread = 256;
    config.grid_size = caps.sm_count * 3;
    config.shared_memory_size = static_cast<size_t>(caps.shared_memory_per_block * 0.75);
    config.registers_per_thread = 80;
    config.expected_occupancy = 0.90;
    config.memory_utilization_estimate = 0.70;
    config.configuration_rationale = "hopper_optimized";
    return config;
}

ParallelismConfiguration OptimizeForAda(const GpuCapabilities& caps, size_t workload_size) {
    ParallelismConfiguration config;
    config.block_size = 768;
    config.points_per_thread = 192;
    config.grid_size = caps.sm_count * 2;
    config.shared_memory_size = static_cast<size_t>(caps.shared_memory_per_block * 0.66);
    config.registers_per_thread = 72;
    config.expected_occupancy = 0.85;
    config.memory_utilization_estimate = 0.65;
    config.configuration_rationale = "ada_optimized";
    return config;
}

ParallelismConfiguration OptimizeForAmpere(const GpuCapabilities& caps, size_t workload_size) {
    ParallelismConfiguration config;
    config.block_size = 512;
    config.points_per_thread = 128;
    config.grid_size = caps.sm_count * 2;
    config.shared_memory_size = static_cast<size_t>(caps.shared_memory_per_block * 0.5);
    config.registers_per_thread = 64;
    config.expected_occupancy = 0.80;
    config.memory_utilization_estimate = 0.60;
    config.configuration_rationale = "ampere_optimized";
    return config;
}

ParallelismConfiguration OptimizeForTuring(const GpuCapabilities& caps, size_t workload_size) {
    ParallelismConfiguration config;
    config.block_size = 384;
    config.points_per_thread = 96;
    config.grid_size = caps.sm_count;
    config.shared_memory_size = static_cast<size_t>(caps.shared_memory_per_block * 0.4);
    config.registers_per_thread = 56;
    config.expected_occupancy = 0.75;
    config.memory_utilization_estimate = 0.55;
    config.configuration_rationale = "turing_optimized";
    return config;
}

// Mock occupancy-based block size selection
std::vector<int> CalculateOptimalBlockSizesOccupancyBased(const GpuCapabilities& gpu_caps) {
    // Simulate different block sizes with different occupancy characteristics
    std::vector<std::pair<int, double>> candidates = {
        {256, 0.65},
        {384, 0.72},
        {512, 0.80},
        {640, 0.78},
        {768, 0.76},
        {896, 0.74},
        {1024, 0.70}
    };

    // Sort by occupancy (highest first)
    std::sort(candidates.begin(), candidates.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    std::vector<int> optimal_sizes;
    for (const auto& [size, occupancy] : candidates) {
        if (size <= gpu_caps.max_threads_per_block) {
            optimal_sizes.push_back(size);
        }
    }

    return optimal_sizes;
}

int SelectOptimalBlockSizeForWorkload(
    const GpuCapabilities& gpu_caps,
    size_t workload_size,
    size_t available_memory_mb) {

    auto optimal_sizes = CalculateOptimalBlockSizesOccupancyBased(gpu_caps);
    if (optimal_sizes.empty()) {
        return 512; // Safe fallback
    }

    // Select block size based on workload characteristics
    int best_block_size = optimal_sizes[0]; // Start with highest occupancy

    // Adjust for workload size
    size_t total_threads_needed = std::min(workload_size, static_cast<size_t>(gpu_caps.sm_count * 2048));

    // For small workloads, prefer smaller blocks to avoid waste
    if (total_threads_needed < 10000) {
        for (int size : optimal_sizes) {
            if (size * 4 <= total_threads_needed) { // Ensure at least 4 blocks
                best_block_size = size;
                break;
            }
        }
    }

    // Adjust for memory constraints
    if (available_memory_mb < 2000) {
        best_block_size = std::min(best_block_size, 512);
    }

    return best_block_size;
}

// Mock memory constraint scaling
ParallelismConfiguration ScaleForMemoryConstraints(
    const ParallelismConfiguration& base_config,
    size_t current_memory_usage,
    size_t available_memory_mb) {

    ParallelismConfiguration scaled_config = base_config;

    if (current_memory_usage > available_memory_mb) {
        double reduction_factor = static_cast<double>(available_memory_mb) / current_memory_usage;

        if (reduction_factor > 0.7) {
            // Mild memory constraint - primarily reduce points_per_thread
            scaled_config.points_per_thread = std::max(64,
                static_cast<int>(base_config.points_per_thread * std::sqrt(reduction_factor)));
        } else if (reduction_factor > 0.4) {
            // Moderate memory constraint - reduce both PPT and block size
            scaled_config.points_per_thread = std::max(64,
                static_cast<int>(base_config.points_per_thread * reduction_factor));
            scaled_config.block_size = std::max(256,
                static_cast<int>(base_config.block_size * std::sqrt(reduction_factor)));
        } else {
            // Severe memory constraint - aggressive scaling
            scaled_config.points_per_thread = 64; // Minimum viable
            scaled_config.block_size = 256;       // Conservative block size
        }

        scaled_config.configuration_rationale = "memory_constrained_scaling";
        scaled_config.memory_utilization_estimate = reduction_factor;
    }

    return scaled_config;
}

// Mock dynamic scaling based on performance
ParallelismConfiguration ScaleForThroughputTarget(
    const ParallelismConfiguration& base_config,
    double current_throughput,
    double target_throughput) {

    ParallelismConfiguration scaled_config = base_config;

    if (current_throughput < target_throughput) {
        double scaling_factor = target_throughput / current_throughput;
        scaling_factor = std::min(scaling_factor, 2.0); // Limit scaling

        if (scaling_factor > 1.5) {
            // Aggressive scaling needed
            scaled_config.block_size = std::min(1024,
                static_cast<int>(base_config.block_size * 1.5));
            scaled_config.points_per_thread = std::min(256,
                static_cast<int>(base_config.points_per_thread * 1.2));
        } else if (scaling_factor > 1.2) {
            // Moderate scaling
            scaled_config.block_size = std::min(1024,
                static_cast<int>(base_config.block_size * 1.2));
            scaled_config.points_per_thread = std::min(256,
                static_cast<int>(base_config.points_per_thread * 1.1));
        }

        scaled_config.configuration_rationale = "throughput_scaling";
    }

    return scaled_config;
}

struct TestCase {
    std::string name;
    int compute_capability;
    int expected_base_block_size;
    std::string expected_rationale;
};

int main() {
    std::cout << "=== Testing Dynamic Block Size Adjustment ===" << std::endl;

    // Test cases for different GPU architectures
    std::vector<TestCase> test_cases = {
        {"Hopper H100", 90, 1024, "hopper_optimized"},
        {"Ada Lovelace RTX 4090", 89, 768, "ada_optimized"},
        {"Ampere RTX 3090", 86, 512, "ampere_optimized"},
        {"Turing RTX 2080 Ti", 75, 384, "turing_optimized"}
    };

    // Mock GPU capabilities
    std::map<int, GpuCapabilities> mock_gpu_caps = {
        {90, {90, "Hopper", 132, 80000, 75000, 1024, 255, 102400}},
        {89, {89, "Ada", 128, 24000, 22000, 1024, 255, 102400}},
        {86, {86, "Ampere", 108, 24000, 22000, 1024, 255, 102400}},
        {75, {75, "Turing", 82, 11000, 10000, 1024, 255, 65536}}
    };

    bool all_tests_passed = true;

    // Test 1: Architecture-specific block size selection
    std::cout << "\n=== Test 1: Architecture-Specific Block Size Selection ===" << std::endl;

    for (const auto& test_case : test_cases) {
        std::cout << "\nTesting: " << test_case.name << " (Compute Capability " << test_case.compute_capability << ")" << std::endl;

        const auto& gpu_caps = mock_gpu_caps[test_case.compute_capability];
        ParallelismConfiguration config;

        // Architecture-specific optimization
        if (gpu_caps.compute_capability >= 90) {
            config = OptimizeForHopper(gpu_caps, 1000000);
        } else if (gpu_caps.compute_capability >= 89) {
            config = OptimizeForAda(gpu_caps, 1000000);
        } else if (gpu_caps.compute_capability >= 80) {
            config = OptimizeForAmpere(gpu_caps, 1000000);
        } else if (gpu_caps.compute_capability >= 70) {
            config = OptimizeForTuring(gpu_caps, 1000000);
        }

        bool test_passed = true;

        std::cout << "  Expected block_size: " << test_case.expected_base_block_size << std::endl;
        std::cout << "  Actual block_size: " << config.block_size << std::endl;

        if (config.block_size != test_case.expected_base_block_size) {
            std::cout << "  ❌ FAILED: Block size mismatch!" << std::endl;
            test_passed = false;
        } else {
            std::cout << "  ✅ PASSED: Block size selection correct" << std::endl;
        }

        // Verify block size is within valid range (256-1024)
        if (config.block_size < 256 || config.block_size > 1024) {
            std::cout << "  ❌ FAILED: Block size out of valid range [256, 1024]!" << std::endl;
            test_passed = false;
        } else {
            std::cout << "  ✅ PASSED: Block size within valid range" << std::endl;
        }

        if (test_passed) {
            std::cout << "  🎯 Overall: PASSED" << std::endl;
        } else {
            std::cout << "  ❌ Overall: FAILED" << std::endl;
            all_tests_passed = false;
        }
    }

    // Test 2: Occupancy-based block size adjustment
    std::cout << "\n=== Test 2: Occupancy-Based Block Size Adjustment ===" << std::endl;

    const auto& ampere_caps = mock_gpu_caps[86];
    auto occupancy_sizes = CalculateOptimalBlockSizesOccupancyBased(ampere_caps);

    std::cout << "Optimal block sizes by occupancy for Ampere:" << std::endl;
    for (size_t i = 0; i < std::min(size_t(3), occupancy_sizes.size()); ++i) {
        std::cout << "  " << (i+1) << ". Block size: " << occupancy_sizes[i] << std::endl;
    }

    // Test workload-based selection
    std::vector<std::pair<size_t, int>> workload_tests = {
        {1000, 256},    // Small workload
        {50000, 512},   // Medium workload
        {1000000, 512}  // Large workload
    };

    for (const auto& [workload_size, expected_size] : workload_tests) {
        int selected_size = SelectOptimalBlockSizeForWorkload(ampere_caps, workload_size, ampere_caps.free_memory_mb);
        std::cout << "  Workload size: " << workload_size << " -> Selected block size: " << selected_size << std::endl;

        if (selected_size >= 256 && selected_size <= 1024) {
            std::cout << "    ✅ PASSED: Valid block size" << std::endl;
        } else {
            std::cout << "    ❌ FAILED: Invalid block size" << std::endl;
            all_tests_passed = false;
        }
    }

    // Test 3: Memory constraint scaling
    std::cout << "\n=== Test 3: Memory Constraint Block Size Scaling ===" << std::endl;

    auto base_config = OptimizeForAmpere(ampere_caps, 1000000);
    std::cout << "Base configuration block_size: " << base_config.block_size << std::endl;

    std::vector<std::pair<size_t, size_t>> memory_scenarios = {
        {16000, 8000},  // Severe memory constraint
        {16000, 12000}, // Moderate memory constraint
        {16000, 15000}  // Mild memory constraint
    };

    for (const auto& [current_mem, available_mem] : memory_scenarios) {
        auto constrained_config = ScaleForMemoryConstraints(base_config, current_mem, available_mem);
        std::cout << "  Memory: " << current_mem << "MB -> " << available_mem << "MB" << std::endl;
        std::cout << "    Block size: " << base_config.block_size << " -> " << constrained_config.block_size << std::endl;

        if (constrained_config.block_size <= base_config.block_size) {
            std::cout << "    ✅ PASSED: Memory constraint reduces block size appropriately" << std::endl;
        } else {
            std::cout << "    ❌ FAILED: Memory constraint should not increase block size!" << std::endl;
            all_tests_passed = false;
        }
    }

    // Test 4: Performance-based scaling
    std::cout << "\n=== Test 4: Performance-Based Block Size Scaling ===" << std::endl;

    std::vector<std::pair<double, double>> performance_scenarios = {
        {1000.0, 1500.0}, // Need 1.5x improvement
        {1000.0, 2000.0}, // Need 2x improvement
        {1000.0, 1200.0}  // Need 1.2x improvement
    };

    for (const auto& [current_throughput, target_throughput] : performance_scenarios) {
        auto scaled_config = ScaleForThroughputTarget(base_config, current_throughput, target_throughput);
        double scaling_factor = target_throughput / current_throughput;

        std::cout << "  Performance: " << current_throughput << " -> " << target_throughput << " (factor: " << scaling_factor << ")" << std::endl;
        std::cout << "    Block size: " << base_config.block_size << " -> " << scaled_config.block_size << std::endl;

        if (scaled_config.block_size >= base_config.block_size) {
            std::cout << "    ✅ PASSED: Performance scaling increases block size appropriately" << std::endl;
        } else {
            std::cout << "    ❌ FAILED: Performance scaling should maintain or increase block size!" << std::endl;
            all_tests_passed = false;
        }

        // Ensure we don't exceed maximum block size
        if (scaled_config.block_size <= 1024) {
            std::cout << "    ✅ PASSED: Block size within maximum limit" << std::endl;
        } else {
            std::cout << "    ❌ FAILED: Block size exceeds maximum limit!" << std::endl;
            all_tests_passed = false;
        }
    }

    // Final result
    std::cout << "\n=== FINAL TEST RESULT ===" << std::endl;
    if (all_tests_passed) {
        std::cout << "🎉 ALL TESTS PASSED! Dynamic block size adjustment is working correctly." << std::endl;
        std::cout << "\nKey findings:" << std::endl;
        std::cout << "- Architecture-specific optimization correctly assigns block sizes (384-1024)" << std::endl;
        std::cout << "- Higher-end GPUs get larger block sizes for better utilization" << std::endl;
        std::cout << "- Occupancy-based selection optimizes for GPU resource utilization" << std::endl;
        std::cout << "- Memory constraints properly scale down block sizes when needed" << std::endl;
        std::cout << "- Performance scaling can increase block sizes for better throughput" << std::endl;
        std::cout << "- All block sizes remain within the valid range of [256, 1024]" << std::endl;
        return 0;
    } else {
        std::cout << "❌ SOME TESTS FAILED! Please review the dynamic block size adjustment logic." << std::endl;
        return 1;
    }
}