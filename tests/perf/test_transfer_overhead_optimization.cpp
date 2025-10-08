#include <gtest/gtest.h>
#include <chrono>
#include <vector>
        <memory>
        <random>
#include <algorithm>
#include <thread>
#include "ComputeCore/gpu/performance/memory_optimizer.h"
#include "ComputeCore/gpu/performance/bandwidth_validator.h"

namespace puzzle71::gpu::performance {

class TransferOverheadOptimizationTest : public ::testing::Test {
protected:
    void SetUp() override {
        device_id_ = 0;
        memory_optimizer_ = std::make_unique<MemoryOptimizer>(device_id_);
        bandwidth_validator_ = std::make_unique<BandwidthValidator>(device_id_);

        // Initialize with transfer optimization settings
        memory_optimizer_->EnableAsynchronousTransfers();
        memory_optimizer_->InitializeMemoryPool(256); // 256MB pool

        // Configure for optimal transfer batching
        MemoryOptimizationConfig config;
        config.enable_transfer_batching = true;
        config.max_batch_size_mb = 64;
        config.max_concurrent_batches = 4;
        config.batching_efficiency_threshold = 0.75;
        memory_optimizer_->UpdateConfiguration(config);
    }

    void TearDown() override {
        CleanupTestMemory();
        memory_optimizer_.reset();
        bandwidth_validator_.reset();
    }

    struct TransferTestData {
        void* host_ptr;
        void* device_ptr;
        size_t size;
        std::chrono::high_resolution_clock::time_point create_time;
    };

    std::vector<TransferTestData> CreateTransferTestData(size_t count, size_t size_kb) {
        std::vector<TransferTestData> data;
        size_t size_bytes = size_kb * 1024;

        for (size_t i = 0; i < count; ++i) {
            TransferTestData test_data;
            test_data.size = size_bytes;
            test_data.create_time = std::chrono::high_resolution_clock::now();

            // Allocate host memory
            test_data.host_ptr = malloc(size_bytes);
            ASSERT_NE(test_data.host_ptr, nullptr) << "Failed to allocate host memory";

            // Allocate device memory
            cudaError_t err = cudaMalloc(&test_data.device_ptr, size_bytes);
            ASSERT_EQ(err, cudaSuccess) << "Failed to allocate device memory";

            // Initialize with pattern for validation
            uint8_t* host_bytes = static_cast<uint8_t*>(test_data.host_ptr);
            for (size_t j = 0; j < size_bytes; ++j) {
                host_bytes[j] = static_cast<uint8_t>((i * size_bytes + j) % 256);
            }

            data.push_back(test_data);
            test_memory_.push_back(test_data);
        }

        return data;
    }

    void CleanupTestMemory() {
        for (const auto& test_data : test_memory_) {
            if (test_data.device_ptr) {
                cudaFree(test_data.device_ptr);
            }
            if (test_data.host_ptr) {
                free(test_data.host_ptr);
            }
        }
        test_memory_.clear();
    }

    struct TransferOverheadResult {
        std::chrono::microseconds individual_transfer_time{0};
        std::chrono::microseconds batched_transfer_time{0};
        std::chrono::microseconds setup_overhead_time{0};
        std::chrono::microseconds completion_overhead_time{0};
        double overhead_reduction_percentage{0.0};
        double efficiency_improvement{0.0};
        size_t transfer_count{0};
        size_t total_bytes{0};
        bool batched_more_efficient{false};
    };

    TransferOverheadResult MeasureTransferOverhead(const std::vector<TransferTestData>& test_data,
                                                   bool use_batching = true) {
        TransferOverheadResult result;
        result.transfer_count = test_data.size();

        // Calculate total bytes
        for (const auto& data : test_data) {
            result.total_bytes += data.size;
        }

        if (!use_batching) {
            // Measure individual transfer overhead
            auto individual_start = std::chrono::high_resolution_clock::now();

            for (const auto& data : test_data) {
                cudaMemcpy(data.device_ptr, data.host_ptr, data.size, cudaMemcpyHostToDevice);
            }

            auto individual_end = std::chrono::high_resolution_clock::now();
            result.individual_transfer_time = std::chrono::duration_cast<std::chrono::microseconds>(
                individual_end - individual_start);

        } else {
            // Measure batched transfer overhead
            // Setup phase overhead
            auto setup_start = std::chrono::high_resolution_clock::now();

            // Prepare batch data
            std::vector<std::pair<void*, void*>> transfers;
            std::vector<size_t> sizes;

            for (const auto& data : test_data) {
                transfers.emplace_back(data.host_ptr, data.device_ptr);
                sizes.push_back(data.size);
            }

            auto setup_end = std::chrono::high_resolution_clock::now();
            result.setup_overhead_time = std::chrono::duration_cast<std::chrono::microseconds>(
                setup_end - setup_start);

            // Transfer phase
            auto transfer_start = std::chrono::high_resolution_clock::now();

            bool batch_success = memory_optimizer_->BatchMemoryTransfer(transfers, sizes, true);
            EXPECT_TRUE(batch_success) << "Batch memory transfer failed";

            if (batch_success) {
                memory_optimizer_->ProcessTransferBatch();
            }

            auto transfer_end = std::chrono::high_resolution_clock::now();
            result.batched_transfer_time = std::chrono::duration_cast<std::chrono::microseconds>(
                transfer_end - transfer_start);

            // Completion phase overhead
            auto completion_start = std::chrono::high_resolution_clock::now();

            // Ensure all transfers are complete
            cudaDeviceSynchronize();

            auto completion_end = std::chrono::high_resolution_clock::now();
            result.completion_overhead_time = std::chrono::duration_cast<std::chrono::microseconds>(
                completion_end - completion_start);
        }

        return result;
    }

    TransferOverheadResult CompareOverheadReduction(const std::vector<TransferTestData>& test_data) {
        TransferOverheadResult individual_result = MeasureTransferOverhead(test_data, false);
        TransferOverheadResult batched_result = MeasureTransferOverhead(test_data, true);

        // Calculate overhead reduction
        if (individual_result.individual_transfer_time.count() > 0) {
            double reduction = (static_cast<double>(individual_result.individual_transfer_time.count()) -
                             static_cast<double>(batched_result.batched_transfer_time.count())) /
                            static_cast<double>(individual_result.individual_transfer_time.count());
            batched_result.overhead_reduction_percentage = reduction * 100.0;
        }

        // Calculate efficiency improvement
        double individual_bandwidth = (static_cast<double>(batched_result.total_bytes) /
                                    individual_result.individual_transfer_time.count()) * 1000000.0 /
                                   (1024.0 * 1024.0); // MB/s

        double batched_bandwidth = (static_cast<double>(batched_result.total_bytes) /
                                  batched_result.batched_transfer_time.count()) * 1000000.0 /
                                 (1024.0 * 1024.0); // MB/s

        if (individual_bandwidth > 0) {
            batched_result.efficiency_improvement = ((batched_bandwidth - individual_bandwidth) / individual_bandwidth) * 100.0;
        }

        batched_result.individual_transfer_time = individual_result.individual_transfer_time;
        batched_result.batched_more_efficient = batched_result.efficiency_improvement > 0;

        return batched_result;
    }

protected:
    int device_id_;
    std::unique_ptr<MemoryOptimizer> memory_optimizer_;
    std::unique_ptr<BandwidthValidator> bandwidth_validator_;
    std::vector<TransferTestData> test_memory_;
};

TEST_F(TransferOverheadOptimizationTest, ValidateSmallTransferBatching) {
    constexpr double TARGET_OVERHEAD_REDUCTION = 30.0; // 30% overhead reduction target
    constexpr double TARGET_EFFICIENCY_IMPROVEMENT = 25.0; // 25% efficiency improvement target

    std::cout << "\n=== Small Transfer Batching Overhead Test ===" << std::endl;

    // Create many small transfers (worst case for overhead)
    auto small_transfer_data = CreateTransferTestData(64, 4); // 64 transfers of 4KB each
    ASSERT_FALSE(small_transfer_data.empty()) << "Failed to create small transfer test data";

    // Compare individual vs batched transfer overhead
    TransferOverheadResult result = CompareOverheadReduction(small_transfer_data);

    EXPECT_GT(result.overhead_reduction_percentage, TARGET_OVERHEAD_REDUCTION)
        << "Small transfer batching overhead reduction below target: "
        << result.overhead_reduction_percentage << "% < " << TARGET_OVERHEAD_REDUCTION << "%";

    EXPECT_GT(result.efficiency_improvement, TARGET_EFFICIENCY_IMPROVEMENT)
        << "Small transfer batching efficiency improvement below target: "
        << result.efficiency_improvement << "% < " << TARGET_EFFICIENCY_IMPROVEMENT << "%";

    EXPECT_TRUE(result.batched_more_efficient) << "Batched transfers should be more efficient than individual transfers";

    // Analyze overhead components
    std::chrono::microseconds total_batched_overhead = result.setup_overhead_time + result.completion_overhead_time;
    double overhead_ratio = static_cast<double>(total_batched_overhead.count()) / result.batched_transfer_time.count();

    EXPECT_LT(overhead_ratio, 0.10)  // Overhead should be <10% of total transfer time
        << "Batching overhead should be minimal relative to transfer time";

    std::cout << "Small Transfer Batching Results:" << std::endl;
    std::cout << "  Transfer Count: " << result.transfer_count << std::endl;
    std::cout << "  Transfer Size: 4KB each" << std::endl;
    std::cout << "  Total Data: " << (result.total_bytes / 1024.0) << " KB" << std::endl;
    std::cout << "  Individual Transfer Time: " << result.individual_transfer_time.count() << " μs" << std::endl;
    std::cout << "  Batched Transfer Time: " << result.batched_transfer_time.count() << " μs" << std::endl;
    std::cout << "  Setup Overhead: " << result.setup_overhead_time.count() << " μs" << std::endl;
    std::cout << "  Completion Overhead: " << result.completion_overhead_time.count() << " μs" << std::endl;
    std::cout << "  Total Batching Overhead: " << total_batched_overhead.count() << " μs" << std::endl;
    std::cout << "  Overhead Reduction: " << result.overhead_reduction_percentage << "%" << std::endl;
    std::cout << "  Efficiency Improvement: " << result.efficiency_improvement << "%" << std::endl;
    std::cout << "  Overhead Ratio: " << (overhead_ratio * 100) << "%" << std::endl;
    std::cout << "  Target Overhead Reduction: " << TARGET_OVERHEAD_REDUCTION << "%" << std::endl;
    std::cout << "  Target Efficiency Improvement: " << TARGET_EFFICIENCY_IMPROVEMENT << "%" << std::endl;
    std::cout << "  Status: " << (result.batched_more_efficient ? "PASS" : "FAIL") << std::endl;

    CleanupTestMemory();
}

TEST_F(TransferOverheadOptimizationTest, ValidateMediumTransferBatching) {
    constexpr double TARGET_MEDIUM_OVERHEAD_REDUCTION = 20.0; // 20% for medium transfers
    constexpr double TARGET_MEDIUM_EFFICIENCY_IMPROVEMENT = 15.0; // 15% for medium transfers

    std::cout << "\n=== Medium Transfer Batching Overhead Test ===" << std::endl;

    // Create medium-sized transfers
    auto medium_transfer_data = CreateTransferTestData(16, 256); // 16 transfers of 256KB each
    ASSERT_FALSE(medium_transfer_data.empty()) << "Failed to create medium transfer test data";

    // Compare overhead reduction
    TransferOverheadResult result = CompareOverheadReduction(medium_transfer_data);

    EXPECT_GT(result.overhead_reduction_percentage, TARGET_MEDIUM_OVERHEAD_REDUCTION)
        << "Medium transfer batching overhead reduction below target: "
        << result.overhead_reduction_percentage << "% < " << TARGET_MEDIUM_OVERHEAD_REDUCTION << "%";

    EXPECT_GT(result.efficiency_improvement, TARGET_MEDIUM_EFFICIENCY_IMPROVEMENT)
        << "Medium transfer batching efficiency improvement below target: "
        << result.efficiency_improvement << "% < " << TARGET_MEDIUM_EFFICIENCY_IMPROVEMENT << "%";

    std::cout << "Medium Transfer Batching Results:" << std::endl;
    std::cout << "  Transfer Count: " << result.transfer_count << std::endl;
    std::cout << "  Transfer Size: 256KB each" << std::endl;
    std::cout << "  Total Data: " << (result.total_bytes / (1024.0 * 1024.0)) << " MB" << std::endl;
    std::cout << "  Individual Transfer Time: " << result.individual_transfer_time.count() << " μs" << std::endl;
    std::cout << "  Batched Transfer Time: " << result.batched_transfer_time.count() << " μs" << std::endl;
    std::cout << "  Setup Overhead: " << result.setup_overhead_time.count() << " μs" << std::endl;
    std::cout << "  Completion Overhead: " << result.completion_overhead_time.count() << " μs" << std::endl;
    std::cout << "  Overhead Reduction: " << result.overhead_reduction_percentage << "%" << std::endl;
    std::cout << "  Efficiency Improvement: " << result.efficiency_improvement << "%" << std::endl;
    std::cout << "  Target Overhead Reduction: " << TARGET_MEDIUM_OVERHEAD_REDUCTION << "%" << std::endl;
    std::cout << "  Status: " << (result.efficiency_improvement >= TARGET_MEDIUM_EFFICIENCY_IMPROVEMENT ? "PASS" : "FAIL") << std::endl;

    CleanupTestMemory();
}

TEST_F(TransferOverheadOptimizationTest, ValidateOptimalBatchSizeSelection) {
    std::cout << "\n=== Optimal Batch Size Selection Test ===" << std::endl;

    // Test different transfer sizes to find optimal batching points
    std::vector<size_t> transfer_sizes_kb = {1, 4, 16, 64, 256, 1024, 4096}; // 1KB to 4MB

    for (size_t transfer_size_kb : transfer_sizes_kb) {
        std::cout << "\nTesting " << transfer_size_kb << "KB transfers:" << std::endl;

        auto test_data = CreateTransferTestData(32, transfer_size_kb);
        ASSERT_FALSE(test_data.empty()) << "Failed to create test data for " << transfer_size_kb << "KB";

        TransferOverheadResult result = CompareOverheadReduction(test_data);

        // Get optimal batch size recommendation
        size_t optimal_batch_size = memory_optimizer_->GetOptimalBatchSize(test_data[0].size);

        std::cout << "  Transfer Count: " << result.transfer_count << std::endl;
        std::cout << "  Transfer Size: " << transfer_size_kb << "KB each" << std::endl;
        std::cout << "  Total Data: " << (result.total_bytes / 1024.0) << " KB" << std::endl;
        std::cout << "  Overhead Reduction: " << result.overhead_reduction_percentage << "%" << std::endl;
        std::cout << "  Efficiency Improvement: " << result.efficiency_improvement << "%" << std::endl;
        std::cout << "  Optimal Batch Size: " << (optimal_batch_size / 1024) << "KB" << std::endl;
        std::cout << "  Batching Beneficial: " << (result.batched_more_efficient ? "YES" : "NO") << std::endl;

        // Verify optimal batch size selection logic
        if (transfer_size_kb < 64) { // Small transfers - should benefit significantly from batching
            EXPECT_GT(result.efficiency_improvement, 10.0)
                << "Small transfers should show >10% improvement from batching";
            EXPECT_GT(optimal_batch_size, test_data[0].size)
                << "Optimal batch size should be larger than individual transfer size for small transfers";
        } else if (transfer_size_kb < 1024) { // Medium transfers - moderate benefit
            EXPECT_GT(result.efficiency_improvement, 5.0)
                << "Medium transfers should show >5% improvement from batching";
        } else { // Large transfers - minimal or no benefit
            // Large transfers might not benefit from batching, and that's okay
            if (!result.batched_more_efficient) {
                std::cout << "  Note: Large transfers don't benefit from batching (expected)" << std::endl;
            }
        }

        CleanupTestMemory();
    }
}

TEST_F(TransferOverheadOptimizationTest, ValidateConcurrentBatchProcessing) {
    std::cout << "\n=== Concurrent Batch Processing Test ===" << std::endl;

    // Test multiple concurrent batches
    constexpr size_t CONCURRENT_BATCHES = 3;
    constexpr size_t TRANSFERS_PER_BATCH = 12;
    constexpr size_t TRANSFER_SIZE_KB = 128;

    std::vector<std::vector<TransferTestData>> concurrent_batches;
    std::vector<std::future<TransferOverheadResult>> batch_futures;

    // Create multiple batches
    for (size_t batch = 0; batch < CONCURRENT_BATCHES; ++batch) {
        auto batch_data = CreateTransferTestData(TRANSFERS_PER_BATCH, TRANSFER_SIZE_KB);
        concurrent_batches.push_back(batch_data);
    }

    // Process batches concurrently
    auto concurrent_start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> batch_threads;
    std::vector<TransferOverheadResult> batch_results(CONCURRENT_BATCHES);

    for (size_t batch = 0; batch < CONCURRENT_BATCHES; ++batch) {
        batch_threads.emplace_back([this, &concurrent_batches, &batch_results, batch]() {
            batch_results[batch] = MeasureTransferOverhead(concurrent_batches[batch], true);
        });
    }

    // Wait for all batches to complete
    for (auto& thread : batch_threads) {
        thread.join();
    }

    auto concurrent_end = std::chrono::high_resolution_clock::now();
    auto total_concurrent_time = std::chrono::duration_cast<std::chrono::microseconds>(
        concurrent_end - concurrent_start);

    // Compare with sequential processing
    auto sequential_start = std::chrono::high_resolution_clock::now();

    TransferOverheadResult sequential_result;
    for (size_t batch = 0; batch < CONCURRENT_BATCHES; ++batch) {
        TransferOverheadResult batch_result = MeasureTransferOverhead(concurrent_batches[batch], true);
        sequential_result.batched_transfer_time += batch_result.batched_transfer_time;
        sequential_result.total_bytes += batch_result.total_bytes;
    }

    auto sequential_end = std::chrono::high_resolution_clock::now();
    auto total_sequential_time = std::chrono::duration_cast<std::chrono::microseconds>(
        sequential_end - sequential_start);

    // Calculate concurrent processing improvement
    double concurrent_improvement = 0.0;
    if (total_sequential_time.count() > 0) {
        concurrent_improvement = ((static_cast<double>(total_sequential_time.count()) -
                                 static_cast<double>(total_concurrent_time.count())) /
                                static_cast<double>(total_sequential_time.count())) * 100.0;
    }

    // Concurrent processing should be more efficient
    EXPECT_LT(total_concurrent_time.count(), total_sequential_time.count())
        << "Concurrent batch processing should be faster than sequential";

    EXPECT_GT(concurrent_improvement, 10.0)  // At least 10% improvement
        << "Concurrent processing should show significant improvement";

    std::cout << "Concurrent Batch Processing Results:" << std::endl;
    std::cout << "  Concurrent Batches: " << CONCURRENT_BATCHES << std::endl;
    std::cout << "  Transfers per Batch: " << TRANSFERS_PER_BATCH << std::endl;
    std::cout << "  Transfer Size: " << TRANSFER_SIZE_KB << "KB each" << std::endl;
    std::cout << "  Total Transfers: " << (CONCURRENT_BATCHES * TRANSFERS_PER_BATCH) << std::endl;
    std::cout << "  Concurrent Processing Time: " << total_concurrent_time.count() << " μs" << std::endl;
    std::cout << "  Sequential Processing Time: " << total_sequential_time.count() << " μs" << std::endl;
    std::cout << "  Concurrent Improvement: " << concurrent_improvement << "%" << std::endl;
    std::cout << "  Status: " << (concurrent_improvement > 10.0 ? "PASS" : "FAIL") << std::endl;

    for (size_t i = 0; i < concurrent_batches.size(); ++i) {
        CleanupTestMemory();
    }
}

TEST_F(TransferOverheadOptimizationTest, ValidateBatchingDecisionLogic) {
    std::cout << "\n=== Batching Decision Logic Test ===" << std::endl;

    // Test the logic that determines when to use batching
    std::vector<std::pair<size_t, size_t>> test_scenarios = {
        {32, 1},      // Many small transfers - should batch
        {16, 4},      // Small transfers - should batch
        {8, 16},      // Medium transfers - should batch
        {4, 64},      // Medium transfers - should batch
        {2, 256},     // Large transfers - might not batch
        {1, 1024}     // Very large transfers - probably not batch
    };

    for (const auto& [count, size_kb] : test_scenarios) {
        std::cout << "\nScenario: " << count << " transfers of " << size_kb << "KB each:" << std::endl;

        auto test_data = CreateTransferTestData(count, size_kb);
        ASSERT_FALSE(test_data.empty()) << "Failed to create test data";

        // Check if transfers can be batched
        std::vector<std::pair<void*, void*>> transfers;
        std::vector<size_t> sizes;

        for (const auto& data : test_data) {
            transfers.emplace_back(data.host_ptr, data.device_ptr);
            sizes.push_back(data.size);
        }

        bool can_batch = memory_optimizer_->CanBatchTransfers(transfers, sizes);
        TransferOverheadResult result = CompareOverheadReduction(test_data);

        std::cout << "  Transfer Count: " << count << std::endl;
        std::cout << "  Transfer Size: " << size_kb << "KB each" << std::endl;
        std::cout << "  Total Data: " << (result.total_bytes / 1024.0) << " KB" << std::endl;
        std::cout << "  Can Batch: " << (can_batch ? "YES" : "NO") << std::endl;
        std::cout << "  Overhead Reduction: " << result.overhead_reduction_percentage << "%" << std::endl;
        std::cout << "  Efficiency Improvement: " << result.efficiency_improvement << "%" << std::endl;
        std::cout << "  Batching Decision Correct: " << ((can_batch == result.batched_more_efficient) ? "YES" : "NO") << std::endl;

        // The decision logic should be accurate
        EXPECT_EQ(can_batch, result.batched_more_efficient)
            << "Batching decision logic should match actual efficiency results";

        // For small transfers, batching should be beneficial
        if (size_kb < 64) {
            EXPECT_TRUE(can_batch) << "Small transfers should be batchable";
            EXPECT_TRUE(result.batched_more_efficient) << "Small transfers should benefit from batching";
        }

        CleanupTestMemory();
    }
}

} // namespace puzzle71::gpu::performance