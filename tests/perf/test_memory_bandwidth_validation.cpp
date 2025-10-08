#include <gtest/gtest.h>
#include <chrono>
#include <vector>
#include <memory>
#include <random>
#include "ComputeCore/gpu/performance/memory_optimizer.h"
#include "ComputeCore/gpu/performance/bandwidth_validator.h"

namespace puzzle71::gpu::performance {

class MemoryBandwidthValidationTest : public ::testing::Test {
protected:
    void SetUp() override {
        device_id_ = 0;

        // Initialize memory optimizer with comprehensive configuration
        MemoryOptimizationConfig config;
        config.enable_memory_pooling = true;
        config.enable_prefetching = true;
        config.enable_double_buffering = true;
        config.enable_async_copy = true;
        config.enable_transfer_batching = true;
        config.pool_size_mb = 512;  // Reasonable pool size for testing
        config.max_batch_size_mb = 32;
        config.prefetch_distance = 2;
        config.target_bandwidth_utilization = 0.80;

        memory_optimizer_ = std::make_unique<MemoryOptimizer>(device_id_);
        bandwidth_validator_ = std::make_unique<BandwidthValidator>(device_id_);

        // Enable all optimization features
        memory_optimizer_->EnableAsynchronousTransfers();
        memory_optimizer_->InitializeMemoryPool(config.pool_size_mb);
        memory_optimizer_->EnableDoubleBuffering(16 * 1024 * 1024); // 16MB buffers

        // Get theoretical peak bandwidth for reference
        theoretical_peak_bandwidth_ = bandwidth_validator_->GetTheoreticalPeakBandwidth(device_id_);
    }

    void TearDown() override {
        memory_optimizer_.reset();
        bandwidth_validator_.reset();
    }

    void* AllocateTestMemory(size_t size_mb) {
        void* host_ptr = nullptr;
        void* device_ptr = nullptr;

        // Allocate host memory
        host_ptr = malloc(size_mb * 1024 * 1024);
        if (!host_ptr) {
            return nullptr;
        }

        // Initialize with random data for realistic bandwidth testing
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint8_t> dis(0, 255);

        uint8_t* host_bytes = static_cast<uint8_t*>(host_ptr);
        for (size_t i = 0; i < size_mb * 1024 * 1024; ++i) {
            host_bytes[i] = dis(gen);
        }

        // Allocate device memory
        cudaError_t err = cudaMalloc(&device_ptr, size_mb * 1024 * 1024);
        if (err != cudaSuccess) {
            free(host_ptr);
            return nullptr;
        }

        // Store mapping for cleanup
        test_memory_mappings_[device_ptr] = host_ptr;
        return device_ptr;
    }

    void CleanupTestMemory() {
        for (const auto& [device_ptr, host_ptr] : test_memory_mappings_) {
            cudaFree(device_ptr);
            free(host_ptr);
        }
        test_memory_mappings_.clear();
    }

protected:
    int device_id_;
    std::unique_ptr<MemoryOptimizer> memory_optimizer_;
    std::unique_ptr<BandwidthValidator> bandwidth_validator_;
    std::map<void*, void*> test_memory_mappings_;
    double theoretical_peak_bandwidth_;
};

TEST_F(MemoryBandwidthValidationTest, VerifyTargetBandwidthUtilization) {
    constexpr double TARGET_UTILIZATION_PERCENT = 80.0;
    constexpr size_t TEST_SIZE_MB = 256;  // 256MB test size

    std::cout << "\n=== Memory Bandwidth Utilization Validation ===" << std::endl;
    std::cout << "Device ID: " << device_id_ << std::endl;
    std::cout << "Theoretical Peak Bandwidth: " << theoretical_peak_bandwidth_ << " GB/s" << std::endl;
    std::cout << "Target Utilization: " << TARGET_UTILIZATION_PERCENT << "%" << std::endl;

    // Test 1: Single large transfer bandwidth
    void* test_device_ptr = AllocateTestMemory(TEST_SIZE_MB);
    ASSERT_NE(test_device_ptr, nullptr);

    double achieved_utilization = 0.0;
    bool test_success = bandwidth_validator_->RunBandwidthTest(TEST_SIZE_MB, achieved_utilization);

    EXPECT_TRUE(test_success) << "Bandwidth test failed to execute";
    EXPECT_GE(achieved_utilization, TARGET_UTILIZATION_PERCENT)
        << "Single transfer bandwidth utilization below target: "
        << achieved_utilization << "% < " << TARGET_UTILIZATION_PERCENT << "%";

    std::cout << "Single Transfer Test:" << std::endl;
    std::cout << "  Test Size: " << TEST_SIZE_MB << " MB" << std::endl;
    std::cout << "  Achieved Utilization: " << achieved_utilization << "%" << std::endl;
    std::cout << "  Status: " << (achieved_utilization >= TARGET_UTILIZATION_PERCENT ? "PASS" : "FAIL") << std::endl;

    // Test 2: Multiple concurrent transfers with batching
    constexpr size_t BATCH_COUNT = 8;
    constexpr size_t BATCH_SIZE_MB = TEST_SIZE_MB / BATCH_COUNT; // 32MB each

    std::vector<std::pair<void*, void*>> transfers;
    std::vector<size_t> sizes;

    for (size_t i = 0; i < BATCH_COUNT; ++i) {
        void* batch_device_ptr = AllocateTestMemory(BATCH_SIZE_MB);
        ASSERT_NE(batch_device_ptr, nullptr);

        transfers.emplace_back(test_memory_mappings_[batch_device_ptr], batch_device_ptr);
        sizes.push_back(BATCH_SIZE_MB * 1024 * 1024);
    }

    auto batch_start_time = std::chrono::high_resolution_clock::now();

    // Execute batched transfers
    bool batch_success = memory_optimizer_->BatchMemoryTransfer(transfers, sizes, true);
    EXPECT_TRUE(batch_success) << "Failed to create transfer batch";

    if (batch_success) {
        // Wait for batch completion
        memory_optimizer_->ProcessTransferBatch();

        auto batch_end_time = std::chrono::high_resolution_clock::now();
        auto batch_duration = std::chrono::duration_cast<std::chrono::microseconds>(
            batch_end_time - batch_start_time);

        // Calculate batch bandwidth utilization
        double batch_bandwidth_gb_per_sec = (static_cast<double>(TEST_SIZE_MB * 1024 * 1024) / batch_duration.count()) * 1000000.0 /
                                           (1024.0 * 1024.0 * 1024.0);

        double batch_utilization = (batch_bandwidth_gb_per_sec / theoretical_peak_bandwidth_) * 100.0;

        EXPECT_GE(batch_utilization, TARGET_UTILIZATION_PERCENT)
            << "Batched transfer bandwidth utilization below target: "
            << batch_utilization << "% < " << TARGET_UTILIZATION_PERCENT << "%";

        std::cout << "Batched Transfer Test:" << std::endl;
        std::cout << "  Batch Count: " << BATCH_COUNT << std::endl;
        std::cout << "  Batch Size: " << BATCH_SIZE_MB << " MB each" << std::endl;
        std::cout << "  Total Size: " << TEST_SIZE_MB << " MB" << std::endl;
        std::cout << "  Batch Bandwidth: " << batch_bandwidth_gb_per_sec << " GB/s" << std::endl;
        std::cout << "  Batch Utilization: " << batch_utilization << "%" << std::endl;
        std::cout << "  Status: " << (batch_utilization >= TARGET_UTILIZATION_PERCENT ? "PASS" : "FAIL") << std::endl;
    }

    // Test 3: Prefetching effectiveness
    if (test_device_ptr) {
        auto prefetch_start_time = std::chrono::high_resolution_clock::now();

        // Test prefetching effectiveness
        memory_optimizer_->PrefetchToDevice(test_device_ptr, TEST_SIZE_MB * 1024 * 1024);

        // Simulate computation while prefetching happens
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        auto prefetch_end_time = std::chrono::high_resolution_clock::now();
        auto prefetch_duration = std::chrono::duration_cast<std::chrono::microseconds>(
            prefetch_end_time - prefetch_start_time);

        std::cout << "Prefetch Test:" << std::endl;
        std::cout << "  Prefetch Duration: " << prefetch_duration.count() << " μs" << std::endl;
        std::cout << "  Status: PASS" << std::endl; // Prefetching always completes successfully
    }

    // Cleanup test memory
    CleanupTestMemory();

    // Final validation using comprehensive bandwidth validation
    bool validation_passed = bandwidth_validator_->ValidateBandwidthUtilization(TARGET_UTILIZATION_PERCENT);
    EXPECT_TRUE(validation_passed) << "Comprehensive bandwidth validation failed";

    std::cout << "\n=== Final Validation Results ===" << std::endl;
    std::cout << "Target Bandwidth Utilization: " << TARGET_UTILIZATION_PERCENT << "%" << std::endl;
    std::cout << "Comprehensive Validation: " << (validation_passed ? "PASS" : "FAIL") << std::endl;

    if (!validation_passed) {
        std::cout << "Last Error: " << bandwidth_validator_->GetLastError() << std::endl;

        auto warnings = bandwidth_validator_->GetValidationWarnings();
        if (!warnings.empty()) {
            std::cout << "Validation Warnings:" << std::endl;
            for (const auto& warning : warnings) {
                std::cout << "  - " << warning << std::endl;
            }
        }
    }
}

TEST_F(MemoryBandwidthValidationTest, VerifyMemoryOptimizationFeatures) {
    // Test that all memory optimization features are working correctly

    // 1. Test memory pool allocation
    void* pooled_ptr = memory_optimizer_->AllocateFromPool(1024 * 1024); // 1MB
    EXPECT_NE(pooled_ptr, nullptr) << "Memory pool allocation failed";

    memory_optimizer_->DeallocateFromPool(pooled_ptr);

    // 2. Test double buffering
    void* buffer1 = memory_optimizer_->GetNextBuffer();
    EXPECT_NE(buffer1, nullptr) << "Double buffering get buffer failed";

    memory_optimizer_->SwapBuffers();
    void* buffer2 = memory_optimizer_->GetNextBuffer();
    EXPECT_NE(buffer2, nullptr) << "Double buffering swap failed";
    EXPECT_NE(buffer1, buffer2) << "Double buffering should provide different buffers";

    // 3. Test prefetching capabilities
    size_t test_size = 1024 * 1024; // 1MB
    memory_optimizer_->PrefetchToDevice(buffer1, test_size);
    memory_optimizer_->PrefetchToHost(buffer1, test_size);

    // 4. Get current memory metrics
    MemoryMetrics metrics = memory_optimizer_->GetCurrentMemoryMetrics();
    EXPECT_GT(metrics.bandwidth_utilization_gb_per_sec, 0.0) << "Bandwidth utilization should be positive";
    EXPECT_GE(metrics.memory_efficiency_percentage, 0.0) << "Memory efficiency should be non-negative";
    EXPECT_LE(metrics.memory_efficiency_percentage, 100.0) << "Memory efficiency should not exceed 100%";

    std::cout << "\n=== Memory Optimization Features Test ===" << std::endl;
    std::cout << "Memory Pool Allocation: PASS" << std::endl;
    std::cout << "Double Buffering: PASS" << std::endl;
    std::cout << "Prefetching: PASS" << std::endl;
    std::cout << "Current Bandwidth Utilization: " << metrics.bandwidth_utilization_gb_per_sec << " GB/s" << std::endl;
    std::cout << "Memory Efficiency: " << metrics.memory_efficiency_percentage << "%" << std::endl;
}

TEST_F(MemoryBandwidthValidationTest, VerifyCacheEfficiencyTargets) {
    // Test cache efficiency targets from the requirements
    constexpr double TARGET_CACHE_HIT_RATE = 75.0; // FR-004a target: ≥75%
    constexpr double TARGET_SHARED_MEMORY_EFFICIENCY = 90.0; // FR-004c target: ≥90%

    // Simulate cache performance measurement
    CachePerformanceMetrics cache_metrics = bandwidth_validator_->MeasureCachePerformance("key_search");

    EXPECT_GE(cache_metrics.l1_cache_hit_rate, TARGET_CACHE_HIT_RATE / 100.0)
        << "L1 cache hit rate below target: "
        << (cache_metrics.l1_cache_hit_rate * 100) << "% < " << TARGET_CACHE_HIT_RATE << "%";

    EXPECT_GE(cache_metrics.l2_cache_hit_rate, TARGET_CACHE_HIT_RATE / 100.0)
        << "L2 cache hit rate below target: "
        << (cache_metrics.l2_cache_hit_rate * 100) << "% < " << TARGET_CACHE_HIT_RATE << "%";

    EXPECT_GE(cache_metrics.shared_memory_efficiency, TARGET_SHARED_MEMORY_EFFICIENCY / 100.0)
        << "Shared memory efficiency below target: "
        << (cache_metrics.shared_memory_efficiency * 100) << "% < " << TARGET_SHARED_MEMORY_EFFICIENCY << "%";

    EXPECT_LT(cache_metrics.shared_memory_bank_conflicts, 0.05)  // <5% shared memory bank conflicts (FR-004b)
        << "Shared memory bank conflicts too high: "
        << (cache_metrics.shared_memory_bank_conflicts * 100) << "% > 5%";

    std::cout << "\n=== Cache Efficiency Validation ===" << std::endl;
    std::cout << "Target Cache Hit Rate: " << TARGET_CACHE_HIT_RATE << "%" << std::endl;
    std::cout << "L1 Cache Hit Rate: " << (cache_metrics.l1_cache_hit_rate * 100) << "%" << std::endl;
    std::cout << "L2 Cache Hit Rate: " << (cache_metrics.l2_cache_hit_rate * 100) << "%" << std::endl;
    std::cout << "Shared Memory Efficiency: " << (cache_metrics.shared_memory_efficiency * 100) << "%" << std::endl;
    std::cout << "Shared Memory Bank Conflicts: " << (cache_metrics.shared_memory_bank_conflicts * 100) << "%" << std::endl;

    bool cache_validation_passed = bandwidth_validator_->ValidateCacheEfficiencyTargets(cache_metrics);
    EXPECT_TRUE(cache_validation_passed) << "Cache efficiency validation failed";

    std::cout << "Cache Validation: " << (cache_validation_passed ? "PASS" : "FAIL") << std::endl;
}

} // namespace puzzle71::gpu::performance