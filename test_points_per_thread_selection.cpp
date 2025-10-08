// Test file to verify automatic points_per_thread selection
#include <iostream>
#include <vector>
#include <map>
#include "src/ComputeCore/gpu/performance/adaptive_parallelism_scaling.h"

using namespace keycuda::gpu::performance;

struct TestCase {
    std::string name;
    int compute_capability;
    int expected_points_per_thread;
    std::string expected_rationale;
};

int main() {
    std::cout << "=== Testing Automatic Points Per Thread Selection ===" << std::endl;

    // Define test cases for different GPU architectures
    std::vector<TestCase> test_cases = {
        {"Hopper H100", 90, 256, "hopper_optimized"},
        {"Ada Lovelace RTX 4090", 89, 192, "ada_optimized"},
        {"Ada Lovelace RTX 4080", 87, 192, "ada_optimized"},
        {"Ampere RTX 3090", 86, 128, "ampere_optimized"},
        {"Ampere RTX 3080", 80, 128, "ampere_optimized"},
        {"Turing RTX 2080 Ti", 75, 96, "turing_optimized"},
        {"Volta V100", 70, 64, "volta_optimized"}
    };

    // Mock GPU capabilities for testing
    std::map<int, GpuCapabilities> mock_gpu_caps = {
        {90, {0, "Hopper", 90, 80000, 75000, 132, 16384, 1024, 102400, 51200, 2000, 3000, 32, 16, 64, 255, true, true, true}},
        {89, {0, "Ada", 89, 24000, 22000, 128, 16384, 1024, 102400, 51200, 1000, 2500, 32, 16, 64, 255, true, true, true}},
        {87, {0, "Ada", 87, 16000, 15000, 76, 15360, 1024, 102400, 32768, 1000, 2400, 32, 16, 64, 255, true, true, true}},
        {86, {0, "Ampere", 86, 24000, 22000, 108, 16384, 1024, 102400, 51200, 1000, 1700, 32, 16, 64, 255, true, true, true}},
        {80, {0, "Ampere", 80, 10000, 9000, 82, 10240, 1024, 65536, 32768, 780, 1410, 32, 16, 64, 255, true, true, true}},
        {75, {0, "Turing", 75, 11000, 10000, 82, 10240, 1024, 65536, 32768, 670, 1545, 32, 16, 64, 255, true, true, false}},
        {70, {0, "Volta", 70, 16000, 15000, 80, 10240, 1024, 65536, 32768, 900, 1375, 32, 16, 64, 255, true, false, false}}
    };

    bool all_tests_passed = true;

    for (const auto& test_case : test_cases) {
        std::cout << "\nTesting: " << test_case.name << " (Compute Capability " << test_case.compute_capability << ")" << std::endl;

        // Create adaptive scaling instance
        auto scaling = std::make_unique<AdaptiveParallelismScaling>(0);

        // Get the recommended configuration
        const auto& gpu_caps = mock_gpu_caps[test_case.compute_capability];
        auto config = scaling->GetRecommendedConfiguration(gpu_caps, 1000000);

        // Verify points_per_thread selection
        bool test_passed = true;
        std::cout << "  Expected points_per_thread: " << test_case.expected_points_per_thread << std::endl;
        std::cout << "  Actual points_per_thread: " << config.points_per_thread << std::endl;

        if (config.points_per_thread != test_case.expected_points_per_thread) {
            std::cout << "  ❌ FAILED: Points per thread mismatch!" << std::endl;
            test_passed = false;
        } else {
            std::cout << "  ✅ PASSED: Points per thread selection correct" << std::endl;
        }

        // Verify rationale
        std::cout << "  Expected rationale: " << test_case.expected_rationale << std::endl;
        std::cout << "  Actual rationale: " << config.configuration_rationale << std::endl;

        if (config.configuration_rationale != test_case.expected_rationale) {
            std::cout << "  ❌ FAILED: Rationale mismatch!" << std::endl;
            test_passed = false;
        } else {
            std::cout << "  ✅ PASSED: Rationale correct" << std::endl;
        }

        // Verify points_per_thread is within valid range (64-256)
        if (config.points_per_thread < 64 || config.points_per_thread > 256) {
            std::cout << "  ❌ FAILED: Points per thread out of valid range [64, 256]!" << std::endl;
            test_passed = false;
        } else {
            std::cout << "  ✅ PASSED: Points per thread within valid range" << std::endl;
        }

        // Test memory constraint scaling
        std::cout << "  Testing memory constraint scaling..." << std::endl;
        auto memory_constrained_config = scaling->ScaleForMemoryConstraints(config, gpu_caps.total_memory_mb * 0.1); // 10% of memory

        if (memory_constrained_config.points_per_thread <= config.points_per_thread) {
            std::cout << "  ✅ PASSED: Memory constraint scaling reduces points_per_thread appropriately" << std::endl;
        } else {
            std::cout << "  ❌ FAILED: Memory constraint scaling should not increase points_per_thread!" << std::endl;
            test_passed = false;
        }

        if (test_passed) {
            std::cout << "  🎯 Overall: PASSED" << std::endl;
        } else {
            std::cout << "  ❌ Overall: FAILED" << std::endl;
            all_tests_passed = false;
        }
    }

    // Test automatic fallback scenarios
    std::cout << "\n=== Testing Automatic Fallback Scenarios ===" << std::endl;

    auto scaling = std::make_unique<AdaptiveParallelismScaling>(0);

    // Test 1: Invalid configuration fallback
    ParallelismConfiguration invalid_config;
    invalid_config.points_per_thread = 1000; // Too high
    invalid_config.block_size = 2000; // Too high
    invalid_config.grid_size = 10000; // Too high
    invalid_config.configuration_rationale = "invalid_test_config";

    std::cout << "Testing invalid configuration fallback..." << std::endl;
    std::string constraint_type;
    bool has_constraints = scaling->DetectResourceConstraints(invalid_config, 1000000, constraint_type);

    if (has_constraints) {
        std::cout << "  ✅ PASSED: Resource constraints detected: " << constraint_type << std::endl;

        // Test constraint-aware fallback
        auto fallback_config = scaling->GetResourceConstraintAwareFallback(constraint_type, 1000000);
        std::cout << "  Fallback points_per_thread: " << fallback_config.points_per_thread << std::endl;
        std::cout << "  Fallback rationale: " << fallback_config.configuration_rationale << std::endl;

        if (fallback_config.points_per_thread >= 32 && fallback_config.points_per_thread <= 256) {
            std::cout << "  ✅ PASSED: Fallback points_per_thread within valid range" << std::endl;
        } else {
            std::cout << "  ❌ FAILED: Fallback points_per_thread out of range!" << std::endl;
            all_tests_passed = false;
        }
    } else {
        std::cout << "  ❌ FAILED: Should have detected resource constraints!" << std::endl;
        all_tests_passed = false;
    }

    // Final result
    std::cout << "\n=== FINAL TEST RESULT ===" << std::endl;
    if (all_tests_passed) {
        std::cout << "🎉 ALL TESTS PASSED! Automatic points_per_thread selection is working correctly." << std::endl;
        return 0;
    } else {
        std::cout << "❌ SOME TESTS FAILED! Please review the automatic points_per_thread selection logic." << std::endl;
        return 1;
    }
}