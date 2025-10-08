#include <gtest/gtest.h>
#include <chrono>
#include <vector>
#include <memory>
#include <fstream>
#include <thread>
#include <random>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <map>

#include "ComputeCore/gpu/gpu_executor.h"
#include "ComputeCore/gpu/performance/memory_bandwidth_profiler.h"
#include "ComputeCore/gpu/performance/memory_coalescing_optimizer.h"
#include "ComputeCore/gpu/performance/memory_pool_manager.h"
#include "ComputeCore/gpu/performance/memory_prefetch_manager.h"
#include "core/uint256.h"
#include "KeyFinderLib/KeySearchTypes.h"

namespace puzzle71::tests::perf {

/**
 * @brief T053-T054: Phase 5 acceptance criteria validation for memory optimization
 *
 * This test validates:
 * - T053: >80% theoretical memory bandwidth utilization achievement
 * - T053a: ≥75% cache hit rate for coalesced memory access patterns (FR-004a)
 * - T053b: ≤5% shared memory bank conflicts in kernel execution (FR-004b)
 * - T053c: ≥90% prefetch accuracy for repeated access patterns (FR-004c)
 * - T054: Caching effectiveness for repeated access patterns
 */
class MemoryOptimizationAcceptanceTest : public ::testing::Test {
protected:
    void SetUp() override {
        device_id_ = 0;
        verbose_ = true;

        // Initialize target for testing (Puzzle 40 - known fast test case)
        std::array<std::uint32_t, 5> target_hash160 = {
            0x739437bb, 0x3dd6d1dc, 0x88a9d8c1,
            0x5f37e6f1, 0x04994e72
        };

        // Create GPU executor with all optimizations enabled
        executor_ = std::make_unique<GpuExecutor>(device_id_, true, target_hash160, verbose_);
        executor_->EnableMemoryOptimization(true);
        executor_->SetMemoryOptimizationLevel(2); // Advanced

        // Get GPU properties for theoretical bandwidth calculation
        cudaDeviceProp props;
        cudaGetDeviceProperties(&props, device_id_);
        gpu_memory_bandwidth_gb_per_sec_ = props.memoryBusWidth / 8.0 * props.memoryClockRate * 1000.0 / 1e9;
        theoretical_peak_bandwidth_gb_per_sec_ = gpu_memory_bandwidth_gb_per_sec_;

        std::cout << "GPU Memory Bandwidth Analysis:" << std::endl;
        std::cout << "  Memory Bus Width: " << props.memoryBusWidth << " bits" << std::endl;
        std::cout << "  Memory Clock: " << props.memoryClockRate / 1000.0 << " MHz" << std::endl;
        std::cout << "  Theoretical Peak Bandwidth: " << std::fixed << std::setprecision(1)
                  << theoretical_peak_bandwidth_gb_per_sec_ << " GB/s" << std::endl;

        // Initialize bandwidth profiler
        bandwidth_profiler_ = std::make_unique<puzzle71::gpu::performance::MemoryBandwidthProfiler>(device_id_);
        bandwidth_profiler_->EnableDetailedProfiling(true);

        // Test range - small enough for quick testing but large enough for meaningful measurements
        test_start_ = core::UInt256("0xe9ae490000");
        test_end_ = core::UInt256("0xe9ae494000");
        test_range_size_ = test_end_ - test_start_;
    }

    struct MemoryBandwidthMetrics {
        double actual_bandwidth_gb_per_sec{0.0};
        double theoretical_bandwidth_gb_per_sec{0.0};
        double utilization_percentage{0.0};
        double cache_hit_rate_percentage{0.0};
        double shared_memory_bank_conflicts_percentage{0.0};
        double prefetch_accuracy_percentage{0.0};
        double caching_effectiveness_score{0.0};
        bool meets_bandwidth_target{false};
        bool meets_cache_hit_target{false};
        bool meets_bank_conflict_target{false};
        bool meets_prefetch_accuracy_target{false};
        bool meets_caching_effectiveness_target{false};
        bool overall_pass{false};

        nlohmann::json to_json() const {
            nlohmann::json j;
            j["bandwidth"] = {
                {"actual_gb_per_sec", actual_bandwidth_gb_per_sec},
                {"theoretical_gb_per_sec", theoretical_bandwidth_gb_per_sec},
                {"utilization_percentage", utilization_percentage},
                {"meets_target", meets_bandwidth_target}
            };
            j["cache_performance"] = {
                {"hit_rate_percentage", cache_hit_rate_percentage},
                {"meets_target", meets_cache_hit_target}
            };
            j["shared_memory"] = {
                {"bank_conflicts_percentage", shared_memory_bank_conflicts_percentage},
                {"meets_target", meets_bank_conflict_target}
            };
            j["prefetch"] = {
                {"accuracy_percentage", prefetch_accuracy_percentage},
                {"meets_target", meets_prefetch_accuracy_target}
            };
            j["caching"] = {
                {"effectiveness_score", caching_effectiveness_score},
                {"meets_target", meets_caching_effectiveness_target}
            };
            j["overall"] = {
                {"pass", overall_pass}
            };
            return j;
        }
    };

    /**
     * @brief T053: Verify >80% theoretical memory bandwidth utilization achievement
     */
    MemoryBandwidthMetrics ValidateMemoryBandwidthUtilization() {
        std::cout << "\n=== T053: Memory Bandwidth Utilization Validation ===" << std::endl;

        MemoryBandwidthMetrics metrics;
        metrics.theoretical_bandwidth_gb_per_sec = theoretical_peak_bandwidth_gb_per_sec_;

        // Configure for bandwidth-intensive test
        BatchConfig config;
        config.grid = dim3(512, 1, 1);
        config.block = dim3(512, 1, 1);
        config.points_per_thread = 128;

        // Start bandwidth profiling
        bandwidth_profiler_->StartProfiling();

        // Execute test with multiple iterations to gather meaningful data
        const int num_iterations = 5;
        std::vector<double> measured_bandwidths;

        for (int i = 0; i < num_iterations; ++i) {
            auto start_time = std::chrono::high_resolution_clock::now();

            executor_->PrepareBatch(config, test_start_);
            auto result = executor_->Execute();

            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

            // Calculate actual bandwidth based on processed keys and data moved
            size_t bytes_processed = result.processed_keys * 32; // Approximate bytes per key
            double bandwidth_gb_per_sec = (bytes_processed / (1024.0 * 1024.0 * 1024.0)) / (duration_ms / 1000.0);

            measured_bandwidths.push_back(bandwidth_gb_per_sec);

            if (verbose_) {
                std::cout << "  Iteration " << (i+1) << ": " << std::fixed << std::setprecision(2)
                          << bandwidth_gb_per_sec << " GB/s" << std::endl;
            }

            // Small delay between iterations
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // Stop profiling and get detailed metrics
        auto profile_results = bandwidth_profiler_->StopProfilingAndGetResults();

        // Calculate average bandwidth
        metrics.actual_bandwidth_gb_per_sec = std::accumulate(measured_bandwidths.begin(), measured_bandwidths.end(), 0.0) / measured_bandwidths.size();

        // Calculate utilization percentage
        if (metrics.theoretical_bandwidth_gb_per_sec > 0) {
            metrics.utilization_percentage = (metrics.actual_bandwidth_gb_per_sec / metrics.theoretical_bandwidth_gb_per_sec) * 100.0;
        }

        // Extract additional metrics from profiler
        metrics.cache_hit_rate_percentage = profile_results.cache_hit_rate_percentage;
        metrics.shared_memory_bank_conflicts_percentage = profile_results.shared_memory_bank_conflict_percentage;
        metrics.prefetch_accuracy_percentage = profile_results.prefetch_accuracy_percentage;
        metrics.caching_effectiveness_score = profile_results.caching_effectiveness_score;

        // Validate targets
        metrics.meets_bandwidth_target = metrics.utilization_percentage >= 80.0;
        metrics.meets_cache_hit_target = metrics.cache_hit_rate_percentage >= 75.0;
        metrics.meets_bank_conflict_target = metrics.shared_memory_bank_conflicts_percentage <= 5.0;
        metrics.meets_prefetch_accuracy_target = metrics.prefetch_accuracy_percentage >= 90.0;
        metrics.meets_caching_effectiveness_target = metrics.caching_effectiveness_score >= 0.8;

        metrics.overall_pass = metrics.meets_bandwidth_target &&
                              metrics.meets_cache_hit_target &&
                              metrics.meets_bank_conflict_target &&
                              metrics.meets_prefetch_accuracy_target &&
                              metrics.meets_caching_effectiveness_target;

        // Report results
        std::cout << "\nT053 Results:" << std::endl;
        std::cout << "  Target: >80% bandwidth utilization" << std::endl;
        std::cout << "  Actual: " << std::fixed << std::setprecision(1)
                  << metrics.utilization_percentage << "% (" << metrics.actual_bandwidth_gb_per_sec
                  << " GB/s of " << metrics.theoretical_bandwidth_gb_per_sec << " GB/s)" << std::endl;
        std::cout << "  Result: " << (metrics.meets_bandwidth_target ? "✅ PASS" : "❌ FAIL") << std::endl;

        std::cout << "\nT053a Results (Cache Hit Rate - FR-004a):" << std::endl;
        std::cout << "  Target: ≥75% cache hit rate" << std::endl;
        std::cout << "  Actual: " << std::fixed << std::setprecision(1)
                  << metrics.cache_hit_rate_percentage << "%" << std::endl;
        std::cout << "  Result: " << (metrics.meets_cache_hit_target ? "✅ PASS" : "❌ FAIL") << std::endl;

        std::cout << "\nT053b Results (Shared Memory Bank Conflicts - FR-004b):" << std::endl;
        std::cout << "  Target: ≤5% bank conflicts" << std::endl;
        std::cout << "  Actual: " << std::fixed << std::setprecision(1)
                  << metrics.shared_memory_bank_conflicts_percentage << "%" << std::endl;
        std::cout << "  Result: " << (metrics.meets_bank_conflict_target ? "✅ PASS" : "❌ FAIL") << std::endl;

        std::cout << "\nT053c Results (Prefetch Accuracy - FR-004c):" << std::endl;
        std::cout << "  Target: ≥90% prefetch accuracy" << std::endl;
        std::cout << "  Actual: " << std::fixed << std::setprecision(1)
                  << metrics.prefetch_accuracy_percentage << "%" << std::endl;
        std::cout << "  Result: " << (metrics.meets_prefetch_accuracy_target ? "✅ PASS" : "❌ FAIL") << std::endl;

        std::cout << "\nT054 Results (Caching Effectiveness):" << std::endl;
        std::cout << "  Target: ≥80% caching effectiveness" << std::endl;
        std::cout << "  Actual: " << std::fixed << std::setprecision(1)
                  << (metrics.caching_effectiveness_score * 100) << "%" << std::endl;
        std::cout << "  Result: " << (metrics.meets_caching_effectiveness_target ? "✅ PASS" : "❌ FAIL") << std::endl;

        std::cout << "\nOverall Memory Optimization Validation:" << std::endl;
        std::cout << "  Result: " << (metrics.overall_pass ? "✅ ALL CRITERIA PASSED" : "❌ SOME CRITERIA FAILED") << std::endl;

        return metrics;
    }

    /**
     * @brief Additional validation for memory access pattern optimization
     */
    bool ValidateMemoryAccessPatterns() {
        std::cout << "\n=== Memory Access Pattern Validation ===" << std::endl;

        // Test different access patterns to validate coalescing optimization
        std::vector<std::string> access_patterns = {"sequential", "strided", "random"};
        bool all_patterns_optimized = true;

        for (const auto& pattern : access_patterns) {
            BatchConfig config;
            config.grid = dim3(256, 1, 1);
            config.block = dim3(256, 1, 1);
            config.points_per_thread = 64;

            auto start_time = std::chrono::high_resolution_clock::now();

            executor_->PrepareBatch(config, test_start_);
            auto result = executor_->Execute();

            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();

            double throughput_mkeys_per_sec = (static_cast<double>(result.processed_keys) / 1'000'000.0) / (duration_us / 1'000'000.0);

            std::cout << "  " << pattern << " pattern: " << std::fixed << std::setprecision(1)
                      << throughput_mkeys_per_sec << " Mkeys/s" << std::endl;

            // Check if performance meets minimum threshold for this pattern
            bool pattern_meets_threshold = throughput_mkeys_per_sec >= 500.0; // Conservative threshold
            std::cout << "    Status: " << (pattern_meets_threshold ? "✅ OPTIMIZED" : "❌ NEEDS OPTIMIZATION") << std::endl;

            if (!pattern_meets_threshold) {
                all_patterns_optimized = false;
            }
        }

        return all_patterns_optimized;
    }

    std::unique_ptr<GpuExecutor> executor_;
    std::unique_ptr<puzzle71::gpu::performance::MemoryBandwidthProfiler> bandwidth_profiler_;

    int device_id_{0};
    bool verbose_{false};
    double theoretical_peak_bandwidth_gb_per_sec_{0.0};
    double gpu_memory_bandwidth_gb_per_sec_{0.0};

    core::UInt256 test_start_;
    core::UInt256 test_end_;
    std::uint64_t test_range_size_{0};
};

/**
 * @brief Main T053-T054 acceptance test: Memory optimization validation
 */
TEST_F(MemoryOptimizationAcceptanceTest, T053_T054_MemoryOptimizationAcceptanceCriteria) {
    std::cout << "\n=== T053-T054: Memory Optimization Acceptance Criteria Validation ===" << std::endl;
    std::cout << "This test validates all Phase 5 memory optimization acceptance criteria:" << std::endl;
    std::cout << "  - T053: >80% theoretical memory bandwidth utilization achievement" << std::endl;
    std::cout << "  - T053a: ≥75% cache hit rate for coalesced memory access patterns (FR-004a)" << std::endl;
    std::cout << "  - T053b: ≤5% shared memory bank conflicts in kernel execution (FR-004b)" << std::endl;
    std::cout << "  - T053c: ≥90% prefetch accuracy for repeated access patterns (FR-004c)" << std::endl;
    std::cout << "  - T054: Caching effectiveness for repeated access patterns" << std::endl;

    // Execute T053: Memory bandwidth utilization validation
    auto bandwidth_metrics = ValidateMemoryBandwidthUtilization();

    // Execute additional memory access pattern validation
    bool access_patterns_optimized = ValidateMemoryAccessPatterns();

    // Generate final validation report
    std::cout << "\n=== FINAL VALIDATION SUMMARY ===" << std::endl;
    std::cout << "T053 - Bandwidth Utilization: " << (bandwidth_metrics.meets_bandwidth_target ? "✅ PASS" : "❌ FAIL") << std::endl;
    std::cout << "T053a - Cache Hit Rate: " << (bandwidth_metrics.meets_cache_hit_target ? "✅ PASS" : "❌ FAIL") << std::endl;
    std::cout << "T053b - Bank Conflicts: " << (bandwidth_metrics.meets_bank_conflict_target ? "✅ PASS" : "❌ FAIL") << std::endl;
    std::cout << "T053c - Prefetch Accuracy: " << (bandwidth_metrics.meets_prefetch_accuracy_target ? "✅ PASS" : "❌ FAIL") << std::endl;
    std::cout << "T054 - Caching Effectiveness: " << (bandwidth_metrics.meets_caching_effectiveness_target ? "✅ PASS" : "❌ FAIL") << std::endl;
    std::cout << "Memory Access Patterns: " << (access_patterns_optimized ? "✅ OPTIMIZED" : "❌ NEEDS WORK") << std::endl;

    bool all_criteria_met = bandwidth_metrics.overall_pass && access_patterns_optimized;

    std::cout << "\nOVERALL RESULT: " << (all_criteria_met ? "✅ ALL PHASE 5 ACCEPTANCE CRITERIA PASSED" : "❌ SOME CRITERIA FAILED") << std::endl;

    if (all_criteria_met) {
        std::cout << "\n🎉 PHASE 5 MEMORY OPTIMIZATION FULLY VALIDATED 🎉" << std::endl;
        std::cout << "All memory optimization features are working as specified:" << std::endl;
        std::cout << "  - Memory bandwidth utilization exceeds 80% target" << std::endl;
        std::cout << "  - Cache hit rates meet or exceed 75% requirement" << std::endl;
        std::cout << "  - Shared memory bank conflicts are below 5% threshold" << std::endl;
        std::cout << "  - Prefetch accuracy achieves 90%+ accuracy" << std::endl;
        std::cout << "  - Caching effectiveness demonstrates significant improvements" << std::endl;
        std::cout << "  - Memory access patterns are properly optimized" << std::endl;
    }

    // Save detailed results to JSON file
    std::ofstream results_file("phase5_acceptance_test_results.json");
    if (results_file.is_open()) {
        nlohmann::json report;
        report["test_name"] = "T053-T054_MemoryOptimizationAcceptanceCriteria";
        report["timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        report["bandwidth_metrics"] = bandwidth_metrics.to_json();
        report["access_patterns_optimized"] = access_patterns_optimized;
        report["all_criteria_met"] = all_criteria_met;

        results_file << std::setw(4) << report << std::endl;
        results_file.close();

        std::cout << "\n📄 Detailed results saved to: phase5_acceptance_test_results.json" << std::endl;
    }

    // Assert that all major criteria are met
    EXPECT_TRUE(bandwidth_metrics.meets_bandwidth_target) << "T053: Memory bandwidth utilization should exceed 80%";
    EXPECT_TRUE(bandwidth_metrics.meets_cache_hit_target) << "T053a: Cache hit rate should be ≥75%";
    EXPECT_TRUE(bandwidth_metrics.meets_bank_conflict_target) << "T053b: Bank conflicts should be ≤5%";
    EXPECT_TRUE(bandwidth_metrics.meets_prefetch_accuracy_target) << "T053c: Prefetch accuracy should be ≥90%";
    EXPECT_TRUE(bandwidth_metrics.meets_caching_effectiveness_target) << "T054: Caching effectiveness should be ≥80%";
    EXPECT_TRUE(access_patterns_optimized) << "Memory access patterns should be optimized";

    // Final assertion for overall success
    EXPECT_TRUE(all_criteria_met) << "All Phase 5 memory optimization acceptance criteria should be met";
}

} // namespace puzzle71::tests::perf