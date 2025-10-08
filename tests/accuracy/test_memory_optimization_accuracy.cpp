#include <gtest/gtest.h>
#include <chrono>
#include <vector>
#include <memory>
#include <random>
#include <algorithm>
#include "ComputeCore/gpu/performance/memory_optimizer.h"
#include "ComputeCore/gpu/performance/bandwidth_validator.h"
#include "ComputeCore/gpu/gpu_executor.h"
#include "core/uint256.h"

namespace puzzle71::gpu::performance {

class MemoryOptimizationAccuracyTest : public ::testing::Test {
protected:
    void SetUp() override {
        device_id_ = 0;
        memory_optimizer_ = std::make_unique<MemoryOptimizer>(device_id_);
        bandwidth_validator_ = std::make_unique<BandwidthValidator>(device_id_);

        // Initialize with all memory optimizations enabled
        memory_optimizer_->EnableAsynchronousTransfers();
        memory_optimizer_->InitializeMemoryPool(512); // 512MB pool

        // Configure for aggressive memory optimization
        MemoryOptimizationConfig config;
        config.enable_memory_pooling = true;
        config.enable_prefetching = true;
        config.enable_double_buffering = true;
        config.enable_async_copy = true;
        config.enable_transfer_batching = true;
        config.prefetch_distance = 3;
        config.max_batch_size_mb = 64;
        memory_optimizer_->UpdateConfiguration(config);

        // Enable double buffering
        memory_optimizer_->EnableDoubleBuffering(32 * 1024 * 1024); // 32MB buffers

        // Initialize test data with known values for accuracy validation
        InitializeAccuracyTestData();
    }

    void TearDown() override {
        CleanupTestMemory();
        memory_optimizer_.reset();
        bandwidth_validator_.reset();
    }

    void InitializeAccuracyTestData() {
        // Create deterministic test data with known checksums
        std::mt19937 gen(12345); // Fixed seed for reproducible results
        std::uniform_int_distribution<uint8_t> dis(0, 255);

        constexpr size_t TEST_DATA_SIZE = 64 * 1024 * 1024; // 64MB test data
        test_data_size_ = TEST_DATA_SIZE;

        // Allocate and initialize test data
        test_data_host_ = malloc(TEST_DATA_SIZE);
        ASSERT_NE(test_data_host_, nullptr) << "Failed to allocate test data";

        // Initialize with known pattern
        uint8_t* data = static_cast<uint8_t*>(test_data_host_);
        for (size_t i = 0; i < TEST_DATA_SIZE; ++i) {
            data[i] = dis(gen);
        }

        // Calculate reference checksums
        CalculateReferenceChecksums();

        // Allocate device memory
        cudaError_t err = cudaMalloc(&test_data_device_, TEST_DATA_SIZE);
        ASSERT_EQ(err, cudaSuccess) << "Failed to allocate device test data";

        test_memory_.push_back({test_data_host_, test_data_device_, TEST_DATA_SIZE});
    }

    void CalculateReferenceChecksums() {
        // Calculate multiple checksum types for comprehensive validation
        uint8_t* data = static_cast<uint8_t*>(test_data_host_);

        // Simple byte sum
        reference_byte_sum_ = 0;
        for (size_t i = 0; i < test_data_size_; ++i) {
            reference_byte_sum_ += data[i];
        }

        // XOR checksum
        reference_xor_checksum_ = 0;
        for (size_t i = 0; i < test_data_size_; ++i) {
            reference_xor_checksum_ ^= data[i];
        }

        // CRC32-like checksum (simplified)
        reference_crc32_ = 0xFFFFFFFF;
        for (size_t i = 0; i < test_data_size_; ++i) {
            reference_crc32_ ^= data[i];
            for (int j = 0; j < 8; ++j) {
                if (reference_crc32_ & 1) {
                    reference_crc32_ = (reference_crc32_ >> 1) ^ 0xEDB88320;
                } else {
                    reference_crc32_ >>= 1;
                }
            }
        }
        reference_crc32_ ^= 0xFFFFFFFF;

        // Sample values for spot checking
        reference_sample_values_.clear();
        std::mt19937 sample_gen(54321); // Different seed for sampling
        std::uniform_int_distribution<size_t> sample_dist(0, test_data_size_ - 1);

        for (int i = 0; i < 1000; ++i) {
            size_t index = sample_dist(sample_gen);
            reference_sample_values_.push_back({index, data[index]});
        }
    }

    void CleanupTestMemory() {
        for (const auto& [host_ptr, device_ptr, size] : test_memory_) {
            if (device_ptr) {
                cudaFree(device_ptr);
            }
            if (host_ptr) {
                free(host_ptr);
            }
        }
        test_memory_.clear();

        if (validation_buffer_) {
            free(validation_buffer_);
            validation_buffer_ = nullptr;
        }

        if (validation_device_buffer_) {
            cudaFree(validation_device_buffer_);
            validation_device_buffer_ = nullptr;
        }
    }

    struct AccuracyValidationResult {
        bool byte_sum_match{false};
        bool xor_checksum_match{false};
        bool crc32_match{false};
        bool all_samples_match{false};
        double byte_sum_error_rate{0.0};
        size_t mismatched_samples{0};
        double sample_error_rate{0.0};
        std::chrono::microseconds transfer_time{0};
        bool accuracy_maintained{false};
        std::vector<std::string> error_details;
    };

    AccuracyValidationResult ValidateMemoryTransferAccuracy(bool use_optimizations) {
        AccuracyValidationResult result;

        // Allocate validation buffer
        if (!validation_buffer_) {
            validation_buffer_ = malloc(test_data_size_);
            ASSERT_NE(validation_buffer_, nullptr) << "Failed to allocate validation buffer";

            cudaError_t err = cudaMalloc(&validation_device_buffer_, test_data_size_);
            ASSERT_EQ(err, cudaSuccess) << "Failed to allocate validation device buffer";
        }

        auto transfer_start = std::chrono::high_resolution_clock::now();

        if (use_optimizations) {
            // Use optimized memory transfer path
            std::vector<std::pair<void*, void*>> transfers = {{test_data_host_, test_data_device_}};
            std::vector<size_t> sizes = {test_data_size_};

            // Use batching
            bool batch_success = memory_optimizer_->BatchMemoryTransfer(transfers, sizes, true);
            if (batch_success) {
                memory_optimizer_->ProcessTransferBatch();
            }

            // Prefetch for validation
            memory_optimizer_->PrefetchToHost(test_data_device_, test_data_size_);

            // Transfer back for validation using optimized path
            std::vector<std::pair<void*, void*>> return_transfers = {{test_data_device_, validation_device_buffer_}};
            std::vector<size_t> return_sizes = {test_data_size_};

            bool return_batch_success = memory_optimizer_->BatchMemoryTransfer(return_transfers, return_sizes, false);
            if (return_batch_success) {
                memory_optimizer_->ProcessTransferBatch();
            }

            // Copy to host validation buffer
            cudaMemcpy(validation_buffer_, validation_device_buffer_, test_data_size_, cudaMemcpyDeviceToHost);

        } else {
            // Use direct memory transfer path (baseline)
            cudaMemcpy(test_data_device_, test_data_host_, test_data_size_, cudaMemcpyHostToDevice);
            cudaMemcpy(validation_buffer_, test_data_device_, test_data_size_, cudaMemcpyDeviceToHost);
        }

        auto transfer_end = std::chrono::high_resolution_clock::now();
        result.transfer_time = std::chrono::duration_cast<std::chrono::microseconds>(transfer_end - transfer_start);

        // Validate accuracy
        uint8_t* validation_data = static_cast<uint8_t*>(validation_buffer_);

        // Calculate byte sum
        size_t validation_byte_sum = 0;
        for (size_t i = 0; i < test_data_size_; ++i) {
            validation_byte_sum += validation_data[i];
        }
        result.byte_sum_match = (validation_byte_sum == reference_byte_sum_);
        if (!result.byte_sum_match) {
            double diff = std::abs(static_cast<double>(validation_byte_sum - reference_byte_sum_));
            result.byte_sum_error_rate = diff / static_cast<double>(reference_byte_sum_) * 100.0;
            result.error_details.push_back("Byte sum mismatch: " + std::to_string(result.byte_sum_error_rate) + "% error");
        }

        // Calculate XOR checksum
        uint8_t validation_xor_checksum = 0;
        for (size_t i = 0; i < test_data_size_; ++i) {
            validation_xor_checksum ^= validation_data[i];
        }
        result.xor_checksum_match = (validation_xor_checksum == reference_xor_checksum_);
        if (!result.xor_checksum_match) {
            result.error_details.push_back("XOR checksum mismatch");
        }

        // Calculate CRC32
        uint32_t validation_crc32 = 0xFFFFFFFF;
        for (size_t i = 0; i < test_data_size_; ++i) {
            validation_crc32 ^= validation_data[i];
            for (int j = 0; j < 8; ++j) {
                if (validation_crc32 & 1) {
                    validation_crc32 = (validation_crc32 >> 1) ^ 0xEDB88320;
                } else {
                    validation_crc32 >>= 1;
                }
            }
        }
        validation_crc32 ^= 0xFFFFFFFF;
        result.crc32_match = (validation_crc32 == reference_crc32_);
        if (!result.crc32_match) {
            result.error_details.push_back("CRC32 mismatch");
        }

        // Validate sample values
        size_t mismatched_count = 0;
        for (const auto& [index, expected_value] : reference_sample_values_) {
            if (validation_data[index] != expected_value) {
                mismatched_count++;
            }
        }
        result.mismatched_samples = mismatched_count;
        result.sample_error_rate = static_cast<double>(mismatched_count) / reference_sample_values_.size() * 100.0;
        result.all_samples_match = (mismatched_count == 0);

        if (!result.all_samples_match) {
            result.error_details.push_back("Sample mismatch: " + std::to_string(result.sample_error_rate) + "% samples incorrect");
        }

        // Overall accuracy assessment
        result.accuracy_maintained = result.byte_sum_match && result.xor_checksum_match &&
                                   result.crc32_match && result.all_samples_match;

        return result;
    }

    struct TestMemoryChunk {
        void* host_ptr;
        void* device_ptr;
        size_t size;
    };

protected:
    int device_id_;
    std::unique_ptr<MemoryOptimizer> memory_optimizer_;
    std::unique_ptr<BandwidthValidator> bandwidth_validator_;
    std::vector<TestMemoryChunk> test_memory_;

    void* test_data_host_{nullptr};
    void* test_data_device_{nullptr};
    size_t test_data_size_{0};

    // Reference checksums for accuracy validation
    size_t reference_byte_sum_{0};
    uint8_t reference_xor_checksum_{0};
    uint32_t reference_crc32_{0};
    std::vector<std::pair<size_t, uint8_t>> reference_sample_values_;

    void* validation_buffer_{nullptr};
    void* validation_device_buffer_{nullptr};
};

TEST_F(MemoryOptimizationAccuracyTest, ValidateMemoryOptimizerAccuracy) {
    constexpr double ACCEPTABLE_ERROR_RATE = 0.0; // 0% error tolerance - perfect accuracy required

    std::cout << "\n=== Memory Optimizer Accuracy Validation ===" << std::endl;

    // Test baseline accuracy (without optimizations)
    std::cout << "Testing baseline accuracy (no optimizations)..." << std::endl;
    AccuracyValidationResult baseline_result = ValidateMemoryTransferAccuracy(false);

    EXPECT_TRUE(baseline_result.accuracy_maintained)
        << "Baseline memory transfer should maintain perfect accuracy";

    std::cout << "Baseline Results:" << std::endl;
    std::cout << "  Transfer Time: " << baseline_result.transfer_time.count() << " μs" << std::endl;
    std::cout << "  Byte Sum Match: " << (baseline_result.byte_sum_match ? "YES" : "NO") << std::endl;
    std::cout << "  XOR Checksum Match: " << (baseline_result.xor_checksum_match ? "YES" : "NO") << std::endl;
    std::cout << "  CRC32 Match: " << (baseline_result.crc32_match ? "YES" : "NO") << std::endl;
    std::cout << "  All Samples Match: " << (baseline_result.all_samples_match ? "YES" : "NO") << std::endl;
    std::cout << "  Baseline Accuracy: " << (baseline_result.accuracy_maintained ? "PASS" : "FAIL") << std::endl;

    // Test optimized accuracy (with all optimizations enabled)
    std::cout << "\nTesting optimized accuracy (all optimizations enabled)..." << std::endl;
    AccuracyValidationResult optimized_result = ValidateMemoryTransferAccuracy(true);

    EXPECT_TRUE(optimized_result.accuracy_maintained)
        << "Optimized memory transfer must maintain perfect accuracy";

    EXPECT_LE(optimized_result.sample_error_rate, ACCEPTABLE_ERROR_RATE)
        << "Sample error rate must be <= " << ACCEPTABLE_ERROR_RATE << "%";

    EXPECT_LE(optimized_result.byte_sum_error_rate, ACCEPTABLE_ERROR_RATE)
        << "Byte sum error rate must be <= " << ACCEPTABLE_ERROR_RATE << "%";

    // Performance should not be significantly worse
    double performance_ratio = static_cast<double>(optimized_result.transfer_time.count()) /
                             baseline_result.transfer_time.count();
    EXPECT_LT(performance_ratio, 2.0) << "Optimized transfer should not be more than 2x slower than baseline";

    std::cout << "Optimized Results:" << std::endl;
    std::cout << "  Transfer Time: " << optimized_result.transfer_time.count() << " μs" << std::endl;
    std::cout << "  Byte Sum Match: " << (optimized_result.byte_sum_match ? "YES" : "NO") << std::endl;
    std::cout << "  XOR Checksum Match: " << (optimized_result.xor_checksum_match ? "YES" : "NO") << std::endl;
    std::cout << "  CRC32 Match: " << (optimized_result.crc32_match ? "YES" : "NO") << std::endl;
    std::cout << "  All Samples Match: " << (optimized_result.all_samples_match ? "YES" : "NO") << std::endl;
    std::cout << "  Sample Error Rate: " << optimized_result.sample_error_rate << "%" << std::endl;
    std::cout << "  Performance Ratio: " << performance_ratio << "x" << std::endl;
    std::cout << "  Optimized Accuracy: " << (optimized_result.accuracy_maintained ? "PASS" : "FAIL") << std::endl;

    if (!optimized_result.accuracy_maintained) {
        std::cout << "Error Details:" << std::endl;
        for (const auto& error : optimized_result.error_details) {
            std::cout << "  - " << error << std::endl;
        }
    }

    // Final assertion - both baseline and optimized must maintain perfect accuracy
    EXPECT_TRUE(baseline_result.accuracy_maintained && optimized_result.accuracy_maintained)
        << "Both baseline and optimized memory transfers must maintain perfect accuracy";
}

TEST_F(MemoryOptimizationAccuracyTest, ValidateRepeatedTransferAccuracy) {
    constexpr int REPEAT_COUNT = 10;
    constexpr double ACCEPTABLE_ERROR_RATE = 0.0; // Perfect accuracy required for all transfers

    std::cout << "\n=== Repeated Transfer Accuracy Validation ===" << std::endl;
    std::cout << "Running " << REPEAT_COUNT << " repeated transfers..." << std::endl;

    std::vector<AccuracyValidationResult> repeat_results;
    bool all_transfers_accurate = true;

    for (int i = 0; i < REPEAT_COUNT; ++i) {
        std::cout << "Transfer " << (i + 1) << "/" << REPEAT_COUNT << "...";

        AccuracyValidationResult result = ValidateMemoryTransferAccuracy(true);
        repeat_results.push_back(result);

        if (!result.accuracy_maintained) {
            all_transfers_accurate = false;
            std::cout << " FAIL" << std::endl;
        } else {
            std::cout << " PASS" << std::endl;
        }

        // Small delay between transfers
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Analyze consistency across repeated transfers
    std::vector<double> transfer_times;
    for (const auto& result : repeat_results) {
        transfer_times.push_back(static_cast<double>(result.transfer_time.count()));
    }

    double avg_time = 0.0;
    for (double time : transfer_times) {
        avg_time += time;
    }
    avg_time /= transfer_times.size();

    double variance = 0.0;
    for (double time : transfer_times) {
        variance += std::pow(time - avg_time, 2);
    }
    variance /= transfer_times.size();
    double std_deviation = std::sqrt(variance);

    double coefficient_of_variation = std_deviation / avg_time;

    std::cout << "\nRepeated Transfer Analysis:" << std::endl;
    std::cout << "  Total Transfers: " << REPEAT_COUNT << std::endl;
    std::cout << "  All Transfers Accurate: " << (all_transfers_accurate ? "YES" : "NO") << std::endl;
    std::cout << "  Average Transfer Time: " << avg_time << " μs" << std::endl;
    std::cout << "  Standard Deviation: " << std_deviation << " μs" << std::endl;
    std::cout << "  Coefficient of Variation: " << (coefficient_of_variation * 100) << "%" << std::endl;

    // All repeated transfers must maintain perfect accuracy
    EXPECT_TRUE(all_transfers_accurate)
        << "All repeated transfers must maintain perfect accuracy";

    // Performance should be reasonably consistent
    EXPECT_LT(coefficient_of_variation, 0.20)  // Less than 20% variation
        << "Transfer performance should be reasonably consistent";

    std::cout << "  Repeated Accuracy Status: " << (all_transfers_accurate ? "PASS" : "FAIL") << std::endl;
}

TEST_F(MemoryOptimizationAccuracyTest, ValidateStressTestAccuracy) {
    constexpr int STRESS_ITERATIONS = 50;
    constexpr double ACCEPTABLE_ERROR_RATE = 0.0; // Perfect accuracy even under stress

    std::cout << "\n=== Stress Test Accuracy Validation ===" << std::endl;
    std::cout << "Running " << STRESS_ITERATIONS << " stress iterations..." << std::endl;

    int accurate_iterations = 0;
    std::vector<std::chrono::microseconds> iteration_times;

    for (int i = 0; i < STRESS_ITERATIONS; ++i) {
        if ((i + 1) % 10 == 0) {
            std::cout << "Completed " << (i + 1) << "/" << STRESS_ITERATIONS << " iterations" << std::endl;
        }

        auto iteration_start = std::chrono::high_resolution_clock::now();

        AccuracyValidationResult result = ValidateMemoryTransferAccuracy(true);
        iteration_times.push_back(result.transfer_time);

        if (result.accuracy_maintained) {
            accurate_iterations++;
        }

        // Add some memory pressure by allocating and freeing temporary buffers
        void* temp_host = malloc(1024 * 1024); // 1MB
        void* temp_device = nullptr;
        cudaMalloc(&temp_device, 1024 * 1024);

        // Quick transfer to add memory pressure
        cudaMemcpy(temp_device, temp_host, 1024 * 1024, cudaMemcpyHostToDevice);

        // Cleanup
        cudaFree(temp_device);
        free(temp_host);
    }

    double accuracy_rate = static_cast<double>(accurate_iterations) / STRESS_ITERATIONS * 100.0;

    // Calculate performance statistics
    std::vector<double> times;
    for (const auto& time : iteration_times) {
        times.push_back(static_cast<double>(time.count()));
    }

    double avg_time = 0.0;
    for (double time : times) {
        avg_time += time;
    }
    avg_time /= times.size();

    double min_time = *std::min_element(times.begin(), times.end());
    double max_time = *std::max_element(times.begin(), times.end());

    std::cout << "\nStress Test Results:" << std::endl;
    std::cout << "  Total Iterations: " << STRESS_ITERATIONS << std::endl;
    std::cout << "  Accurate Iterations: " << accurate_iterations << std::endl;
    std::cout << "  Accuracy Rate: " << accuracy_rate << "%" << std::endl;
    std::cout << "  Average Time: " << avg_time << " μs" << std::endl;
    std::cout << "  Min Time: " << min_time << " μs" << std::endl;
    std::cout << "  Max Time: " << max_time << " μs" << std::endl;
    std::cout << "  Time Range: " << (max_time - min_time) << " μs" << std::endl;

    // Even under stress, accuracy must be perfect
    EXPECT_EQ(accuracy_rate, 100.0)
        << "Accuracy rate must be 100% even under stress conditions";

    std::cout << "  Stress Test Status: " << (accuracy_rate == 100.0 ? "PASS" : "FAIL") << std::endl;
}

TEST_F(MemoryOptimizationAccuracyTest, ValidateMemoryIntegrityUnderOptimizations) {
    std::cout << "\n=== Memory Integrity Under Optimizations Test ===" << std::endl;

    // Test each optimization individually to ensure they don't compromise accuracy
    std::vector<std::pair<std::string, std::function<void()>>> optimizations = {
        {"Double Buffering", [this]() {
            memory_optimizer_->EnableDoubleBuffering(16 * 1024 * 1024);
        }},
        {"Prefetching", [this]() {
            memory_optimizer_->PrefetchToDevice(test_data_device_, test_data_size_);
        }},
        {"Memory Pooling", [this]() {
            void* pooled_ptr = memory_optimizer_->AllocateFromPool(1024 * 1024);
            if (pooled_ptr) {
                memory_optimizer_->DeallocateFromPool(pooled_ptr);
            }
        }}
    };

    for (const auto& [name, optimization] : optimizations) {
        std::cout << "Testing " << name << " optimization..." << std::endl;

        // Apply optimization
        optimization();

        // Test accuracy with optimization enabled
        AccuracyValidationResult result = ValidateMemoryTransferAccuracy(true);

        EXPECT_TRUE(result.accuracy_maintained)
            << name << " optimization must maintain perfect accuracy";

        std::cout << "  " << name << " Accuracy: " << (result.accuracy_maintained ? "PASS" : "FAIL") << std::endl;

        if (!result.accuracy_maintained) {
            std::cout << "  Error Details: ";
            for (const auto& error : result.error_details) {
                std::cout << error << "; ";
            }
            std::cout << std::endl;
        }
    }

    // Test all optimizations combined
    std::cout << "Testing all optimizations combined..." << std::endl;
    AccuracyValidationResult combined_result = ValidateMemoryTransferAccuracy(true);

    EXPECT_TRUE(combined_result.accuracy_maintained)
        << "All optimizations combined must maintain perfect accuracy";

    std::cout << "  Combined Optimizations Accuracy: "
              << (combined_result.accuracy_maintained ? "PASS" : "FAIL") << std::endl;
}

} // namespace puzzle71::gpu::performance