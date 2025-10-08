#include <gtest/gtest.h>
#include <chrono>
#include <vector>
#include <memory>
#include <random>
#include <algorithm>
#include "ComputeCore/gpu/performance/memory_optimizer.h"
#include "ComputeCore/gpu/performance/bandwidth_validator.h"

namespace puzzle71::gpu::performance {

class MemoryAccessPatternTest : public ::testing::Test {
protected:
    void SetUp() override {
        device_id_ = 0;
        memory_optimizer_ = std::make_unique<MemoryOptimizer>(device_id_);
        bandwidth_validator_ = std::make_unique<BandwidthValidator>(device_id_);

        // Initialize with optimization settings
        memory_optimizer_->EnableAsynchronousTransfers();
        memory_optimizer_->InitializeMemoryPool(256); // 256MB pool
    }

    void TearDown() override {
        memory_optimizer_.reset();
        bandwidth_validator_.reset();
    }

    // Create test data with specific access patterns
    std::vector<std::pair<void*, void*>> CreateSequentialAccessData(size_t count, size_t size_mb) {
        std::vector<std::pair<void*, void*>> data;

        for (size_t i = 0; i < count; ++i) {
            void* host_ptr = malloc(size_mb * 1024 * 1024);
            void* device_ptr = nullptr;

            if (host_ptr && cudaMalloc(&device_ptr, size_mb * 1024 * 1024) == cudaSuccess) {
                // Initialize with sequential pattern for optimal coalescing
                uint8_t* host_bytes = static_cast<uint8_t*>(host_ptr);
                for (size_t j = 0; j < size_mb * 1024 * 1024; ++j) {
                    host_bytes[j] = static_cast<uint8_t>((i + j) % 256);
                }

                data.emplace_back(host_ptr, device_ptr);
                test_memory_.emplace_back(device_ptr, host_ptr);
            }
        }

        return data;
    }

    // Create test data with random access patterns (worst case)
    std::vector<std::pair<void*, void*>> CreateRandomAccessData(size_t count, size_t size_mb) {
        std::vector<std::pair<void*, void*>> data;
        std::random_device rd;
        std::mt19937 gen(rd());

        for (size_t i = 0; i < count; ++i) {
            void* host_ptr = malloc(size_mb * 1024 * 1024);
            void* device_ptr = nullptr;

            if (host_ptr && cudaMalloc(&device_ptr, size_mb * 1024 * 1024) == cudaSuccess) {
                // Initialize with random pattern for worst-case coalescing
                uint8_t* host_bytes = static_cast<uint8_t*>(host_ptr);
                std::uniform_int_distribution<uint8_t> dis(0, 255);
                for (size_t j = 0; j < size_mb * 1024 * 1024; ++j) {
                    host_bytes[j] = dis(gen);
                }

                data.emplace_back(host_ptr, device_ptr);
                test_memory_.emplace_back(device_ptr, host_ptr);
            }
        }

        return data;
    }

    void CleanupTestMemory() {
        for (const auto& [device_ptr, host_ptr] : test_memory_) {
            cudaFree(device_ptr);
            free(host_ptr);
        }
        test_memory_.clear();
    }

    // Measure coalescing efficiency through repeated access patterns
    double MeasureCoalescingEfficiency(const std::vector<std::pair<void*, void*>>& data,
                                      const std::vector<size_t>& sizes,
                                      bool is_sequential = true) {
        auto start_time = std::chrono::high_resolution_clock::now();

        std::vector<std::pair<void*, void*>> transfers = data;
        std::vector<size_t> transfer_sizes = sizes;

        // Execute transfers
        bool batch_success = memory_optimizer_->BatchMemoryTransfer(transfers, transfer_sizes, true);
        if (batch_success) {
            memory_optimizer_->ProcessTransferBatch();
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

        // Calculate total bytes transferred
        size_t total_bytes = 0;
        for (size_t size : sizes) {
            total_bytes += size;
        }

        // Calculate bandwidth in GB/s
        double bandwidth_gb_per_sec = (static_cast<double>(total_bytes) / duration.count()) * 1000000.0 /
                                     (1024.0 * 1024.0 * 1024.0);

        // Get theoretical peak for efficiency calculation
        double theoretical_peak = bandwidth_validator_->GetTheoreticalPeakBandwidth(device_id_);
        double efficiency = bandwidth_gb_per_sec / theoretical_peak;

        return efficiency;
    }

protected:
    int device_id_;
    std::unique_ptr<MemoryOptimizer> memory_optimizer_;
    std::unique_ptr<BandwidthValidator> bandwidth_validator_;
    std::vector<std::pair<void*, void*>> test_memory_;
};

TEST_F(MemoryAccessPatternTest, ValidateSequentialAccessCoalescing) {
    constexpr size_t TRANSFER_COUNT = 16;
    constexpr size_t TRANSFER_SIZE_MB = 8;
    constexpr double TARGET_COALESCING_EFFICIENCY = 0.90; // 90% target for coalesced access

    std::cout << "\n=== Sequential Access Coalescing Validation ===" << std::endl;

    // Create sequentially accessible data (optimally coalesced)
    auto sequential_data = CreateSequentialAccessData(TRANSFER_COUNT, TRANSFER_SIZE_MB);
    ASSERT_FALSE(sequential_data.empty()) << "Failed to create sequential test data";

    std::vector<size_t> sizes(TRANSFER_COUNT, TRANSFER_SIZE_MB * 1024 * 1024);

    // Measure coalescing efficiency
    double coalescing_efficiency = MeasureCoalescingEfficiency(sequential_data, sizes, true);

    EXPECT_GE(coalescing_efficiency, TARGET_COALESCING_EFFICIENCY)
        << "Sequential access coalescing efficiency below target: "
        << (coalescing_efficiency * 100) << "% < " << (TARGET_COALESCING_EFFICIENCY * 100) << "%";

    // Analyze memory access pattern
    MemoryAccessPattern pattern = bandwidth_validator_->AnalyzeMemoryAccessPattern(
        "sequential_test", TRANSFER_COUNT * TRANSFER_SIZE_MB * 1024 * 1024);

    EXPECT_TRUE(bandwidth_validator_->ValidateCoalescedAccessPattern(pattern))
        << "Memory access pattern validation failed";

    EXPECT_GE(pattern.coalescing_efficiency, TARGET_COALESCING_EFFICIENCY)
        << "Memory access pattern coalescing efficiency below target";

    std::cout << "Transfer Count: " << TRANSFER_COUNT << std::endl;
    std::cout << "Transfer Size: " << TRANSFER_SIZE_MB << " MB each" << std::endl;
    std::cout << "Total Data: " << (TRANSFER_COUNT * TRANSFER_SIZE_MB) << " MB" << std::endl;
    std::cout << "Coalescing Efficiency: " << (coalescing_efficiency * 100) << "%" << std::endl;
    std::cout << "Pattern Coalescing Efficiency: " << (pattern.coalescing_efficiency * 100) << "%" << std::endl;
    std::cout << "Target Efficiency: " << (TARGET_COALESCING_EFFICIENCY * 100) << "%" << std::endl;
    std::cout << "Status: " << (coalescing_efficiency >= TARGET_COALESCING_EFFICIENCY ? "PASS" : "FAIL") << std::endl;

    CleanupTestMemory();
}

TEST_F(MemoryAccessPatternTest, ValidateCacheHitRateTargets) {
    constexpr double TARGET_CACHE_HIT_RATE = 0.75; // FR-004a: ≥75% cache hit rate
    constexpr size_t WORKLOAD_SIZE_MB = 128;

    std::cout << "\n=== Cache Hit Rate Validation ===" << std::endl;

    // Create data for cache testing (repeated access patterns)
    auto cache_test_data = CreateSequentialAccessData(8, WORKLOAD_SIZE_MB / 8);
    std::vector<size_t> cache_sizes(8, (WORKLOAD_SIZE_MB / 8) * 1024 * 1024);

    // First pass - populate cache
    memory_optimizer_->BatchMemoryTransfer(cache_test_data, cache_sizes, true);
    memory_optimizer_->ProcessTransferBatch();

    // Second pass - should benefit from cache
    auto cache_start_time = std::chrono::high_resolution_clock::now();
    memory_optimizer_->BatchMemoryTransfer(cache_test_data, cache_sizes, true);
    memory_optimizer_->ProcessTransferBatch();
    auto cache_end_time = std::chrono::high_resolution_clock::now();

    // Measure cache performance
    CachePerformanceMetrics cache_metrics = bandwidth_validator_->MeasureCachePerformance("cache_test");

    EXPECT_GE(cache_metrics.l1_cache_hit_rate, TARGET_CACHE_HIT_RATE)
        << "L1 cache hit rate below target: "
        << (cache_metrics.l1_cache_hit_rate * 100) << "% < " << (TARGET_CACHE_HIT_RATE * 100) << "%";

    EXPECT_GE(cache_metrics.l2_cache_hit_rate, TARGET_CACHE_HIT_RATE)
        << "L2 cache hit rate below target: "
        << (cache_metrics.l2_cache_hit_rate * 100) << "% < " << (TARGET_CACHE_HIT_RATE * 100) << "%";

    auto cache_duration = std::chrono::duration_cast<std::chrono::microseconds>(
        cache_end_time - cache_start_time);

    std::cout << "Workload Size: " << WORKLOAD_SIZE_MB << " MB" << std::endl;
    std::cout << "L1 Cache Hit Rate: " << (cache_metrics.l1_cache_hit_rate * 100) << "%" << std::endl;
    std::cout << "L2 Cache Hit Rate: " << (cache_metrics.l2_cache_hit_rate * 100) << "%" << std::endl;
    std::cout << "Shared Memory Efficiency: " << (cache_metrics.shared_memory_efficiency * 100) << "%" << std::endl;
    std::cout << "Second Pass Duration: " << cache_duration.count() << " μs" << std::endl;
    std::cout << "Target Cache Hit Rate: " << (TARGET_CACHE_HIT_RATE * 100) << "%" << std::endl;
    std::cout << "Status: " << (cache_metrics.l1_cache_hit_rate >= TARGET_CACHE_HIT_RATE ? "PASS" : "FAIL") << std::endl;

    CleanupTestMemory();
}

TEST_F(MemoryAccessPatternTest, ValidateSharedMemoryBankConflicts) {
    constexpr double TARGET_MAX_BANK_CONFLICTS = 0.05; // FR-004b: <5% shared memory bank conflicts

    std::cout << "\n=== Shared Memory Bank Conflicts Validation ===" << std::endl;

    // Measure cache performance including shared memory metrics
    CachePerformanceMetrics shared_memory_metrics = bandwidth_validator_->MeasureCachePerformance("shared_memory_test");

    EXPECT_LT(shared_memory_metrics.shared_memory_bank_conflicts, TARGET_MAX_BANK_CONFLICTS)
        << "Shared memory bank conflicts above target: "
        << (shared_memory_metrics.shared_memory_bank_conflicts * 100) << "% > " << (TARGET_MAX_BANK_CONFLICTS * 100) << "%";

    std::cout << "Shared Memory Bank Conflicts: " << (shared_memory_metrics.shared_memory_bank_conflicts * 100) << "%" << std::endl;
    std::cout << "Shared Memory Efficiency: " << (shared_memory_metrics.shared_memory_efficiency * 100) << "%" << std::endl;
    std::cout << "Max Allowed Conflicts: " << (TARGET_MAX_BANK_CONFLICTS * 100) << "%" << std::endl;
    std::cout << "Status: " << (shared_memory_metrics.shared_memory_bank_conflicts < TARGET_MAX_BANK_CONFLICTS ? "PASS" : "FAIL") << std::endl;
}

TEST_F(MemoryAccessPatternTest, ValidatePrefetchAccuracy) {
    constexpr double TARGET_PREFETCH_ACCURACY = 0.90; // FR-004c: ≥90% prefetch accuracy

    std::cout << "\n=== Prefetch Accuracy Validation ===" << std::endl;

    // Create test data for prefetching
    auto prefetch_data = CreateSequentialAccessData(4, 32); // 4 x 32MB
    std::vector<size_t> prefetch_sizes(4, 32 * 1024 * 1024);

    // Test prefetching effectiveness
    for (size_t i = 0; i < prefetch_data.size(); ++i) {
        // Prefetch to device
        memory_optimizer_->PrefetchToDevice(prefetch_data[i].second, prefetch_sizes[i]);

        // Small delay to allow prefetch to complete
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        // Access the prefetched data (simulating kernel access)
        void* temp_buffer = malloc(1024);
        cudaMemcpy(temp_buffer, prefetch_data[i].second, 1024, cudaMemcpyDeviceToHost);
        free(temp_buffer);
    }

    // Get memory metrics to estimate prefetch effectiveness
    MemoryMetrics metrics = memory_optimizer_->GetCurrentMemoryMetrics();

    // Calculate prefetch accuracy based on hit/miss ratio
    double prefetch_accuracy = 0.0;
    if (metrics.prefetch_hits + metrics.prefetch_misses > 0) {
        prefetch_accuracy = static_cast<double>(metrics.prefetch_hits) /
                           (metrics.prefetch_hits + metrics.prefetch_misses);
    }

    EXPECT_GE(prefetch_accuracy, TARGET_PREFETCH_ACCURACY)
        << "Prefetch accuracy below target: "
        << (prefetch_accuracy * 100) << "% < " << (TARGET_PREFETCH_ACCURACY * 100) << "%";

    std::cout << "Prefetch Hits: " << metrics.prefetch_hits << std::endl;
    std::cout << "Prefetch Misses: " << metrics.prefetch_misses << std::endl;
    std::cout << "Prefetch Accuracy: " << (prefetch_accuracy * 100) << "%" << std::endl;
    std::cout << "Target Accuracy: " << (TARGET_PREFETCH_ACCURACY * 100) << "%" << std::endl;
    std::cout << "Status: " << (prefetch_accuracy >= TARGET_PREFETCH_ACCURACY ? "PASS" : "FAIL") << std::endl;

    CleanupTestMemory();
}

TEST_F(MemoryAccessPatternTest, ValidateMemoryOptimizationIntegration) {
    // Test that all memory optimization features work together effectively

    std::cout << "\n=== Memory Optimization Integration Test ===" << std::endl;

    // Enable all optimization features
    memory_optimizer_->EnableDoubleBuffering(64 * 1024 * 1024); // 64MB double buffers

    // Create comprehensive test workload
    std::vector<std::pair<void*, void*>> integration_data;
    std::vector<size_t> integration_sizes;

    // Mixed transfer sizes for realistic workload
    std::vector<size_t> transfer_sizes_mb = {4, 8, 16, 32, 16, 8, 4}; // MB

    for (size_t size_mb : transfer_sizes_mb) {
        auto data = CreateSequentialAccessData(1, size_mb);
        if (!data.empty()) {
            integration_data.insert(integration_data.end(), data.begin(), data.end());
            integration_sizes.push_back(size_mb * 1024 * 1024);
        }
    }

    // Execute with all optimizations enabled
    auto integration_start = std::chrono::high_resolution_clock::now();

    bool integration_success = memory_optimizer_->BatchMemoryTransfer(integration_data, integration_sizes, true);
    EXPECT_TRUE(integration_success) << "Integration test batching failed";

    if (integration_success) {
        memory_optimizer_->ProcessTransferBatch();
    }

    auto integration_end = std::chrono::high_resolution_clock::now();
    auto integration_duration = std::chrono::duration_cast<std::chrono::microseconds>(
        integration_end - integration_start);

    // Calculate overall performance
    size_t total_bytes = 0;
    for (size_t size : integration_sizes) {
        total_bytes += size;
    }

    double integration_bandwidth = (static_cast<double>(total_bytes) / integration_duration.count()) * 1000000.0 /
                                  (1024.0 * 1024.0 * 1024.0);

    double theoretical_peak = bandwidth_validator_->GetTheoreticalPeakBandwidth(device_id_);
    double overall_efficiency = integration_bandwidth / theoretical_peak;

    // Get final metrics
    MemoryMetrics final_metrics = memory_optimizer_->GetCurrentMemoryMetrics();
    CachePerformanceMetrics final_cache_metrics = bandwidth_validator_->MeasureCachePerformance("integration_test");

    std::cout << "Integration Test Results:" << std::endl;
    std::cout << "  Total Transfers: " << integration_data.size() << std::endl;
    std::cout << "  Total Data: " << (total_bytes / (1024.0 * 1024.0)) << " MB" << std::endl;
    std::cout << "  Integration Bandwidth: " << integration_bandwidth << " GB/s" << std::endl;
    std::cout << "  Overall Efficiency: " << (overall_efficiency * 100) << "%" << std::endl;
    std::cout << "  Memory Efficiency: " << final_metrics.memory_efficiency_percentage << "%" << std::endl;
    std::cout << "  L1 Cache Hit Rate: " << (final_cache_metrics.l1_cache_hit_rate * 100) << "%" << std::endl;
    std::cout << "  L2 Cache Hit Rate: " << (final_cache_metrics.l2_cache_hit_rate * 100) << "%" << std::endl;
    std::cout << "  Async Overlap Ratio: " << final_metrics.async_overlap_ratio << std::endl;
    std::cout << "Status: " << (integration_success && overall_efficiency >= 0.80 ? "PASS" : "FAIL") << std::endl;

    // Overall integration should achieve at least 80% efficiency
    EXPECT_GE(overall_efficiency, 0.80) << "Integration test efficiency below 80%";
    EXPECT_GT(final_metrics.memory_efficiency_percentage, 70.0) << "Memory efficiency too low";

    CleanupTestMemory();
}

} // namespace puzzle71::gpu::performance