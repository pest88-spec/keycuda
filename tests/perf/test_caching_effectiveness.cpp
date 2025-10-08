#include <gtest/gtest.h>
#include <chrono>
#include <vector>
#include <memory>
#include <random>
#include <algorithm>
#include <thread>
#include "ComputeCore/gpu/performance/memory_optimizer.h"
#include "ComputeCore/gpu/performance/bandwidth_validator.h"

namespace puzzle71::gpu::performance {

class CachingEffectivenessTest : public ::testing::Test {
protected:
    void SetUp() override {
        device_id_ = 0;
        memory_optimizer_ = std::make_unique<MemoryOptimizer>(device_id_);
        bandwidth_validator_ = std::make_unique<BandwidthValidator>(device_id_);

        // Initialize with caching-optimized settings
        memory_optimizer_->EnableAsynchronousTransfers();
        memory_optimizer_->InitializeMemoryPool(512); // 512MB pool for caching tests

        // Enable prefetching for caching effectiveness
        MemoryOptimizationConfig config;
        config.enable_prefetching = true;
        config.prefetch_distance = 3; // More aggressive prefetching for caching tests
        memory_optimizer_->UpdateConfiguration(config);
    }

    void TearDown() override {
        CleanupTestMemory();
        memory_optimizer_.reset();
        bandwidth_validator_.reset();
    }

    // Create test data with specific access patterns for caching tests
    struct CacheTestData {
        void* host_ptr;
        void* device_ptr;
        size_t size;
        std::string access_pattern;
        std::vector<size_t> access_indices; // For simulating specific access patterns
    };

    std::vector<CacheTestData> CreateCachingTestData(size_t count, size_t size_mb,
                                                    const std::string& pattern = "sequential") {
        std::vector<CacheTestData> data;

        for (size_t i = 0; i < count; ++i) {
            CacheTestData test_data;
            test_data.size = size_mb * 1024 * 1024;
            test_data.access_pattern = pattern;

            // Allocate host memory
            test_data.host_ptr = malloc(test_data.size);
            ASSERT_NE(test_data.host_ptr, nullptr) << "Failed to allocate host memory";

            // Allocate device memory
            cudaError_t err = cudaMalloc(&test_data.device_ptr, test_data.size);
            ASSERT_EQ(err, cudaSuccess) << "Failed to allocate device memory";

            // Initialize data based on access pattern
            uint8_t* host_bytes = static_cast<uint8_t*>(test_data.host_ptr);

            if (pattern == "sequential") {
                // Sequential pattern - good for spatial locality
                for (size_t j = 0; j < test_data.size; ++j) {
                    host_bytes[j] = static_cast<uint8_t>((i * test_data.size + j) % 256);
                }
                // Sequential access indices
                for (size_t j = 0; j < test_data.size; j += 4096) { // Access in 4KB chunks
                    test_data.access_indices.push_back(j);
                }
            } else if (pattern == "strided") {
                // Strided pattern - moderate spatial locality
                const size_t stride = 64 * 1024; // 64KB stride
                for (size_t j = 0; j < test_data.size; ++j) {
                    host_bytes[j] = static_cast<uint8_t>((j / stride) % 256);
                }
                // Strided access indices
                for (size_t j = 0; j < test_data.size; j += stride) {
                    test_data.access_indices.push_back(j);
                }
            } else if (pattern == "random_locality") {
                // Random with temporal locality - repeat some accesses
                std::random_device rd;
                std::mt19937 gen(rd() + i); // Different seed per block
                std::uniform_int_distribution<uint8_t> dis(0, 255);

                for (size_t j = 0; j < test_data.size; ++j) {
                    host_bytes[j] = dis(gen);
                }

                // Create access pattern with temporal locality
                const size_t hot_spot_size = test_data.size / 10; // 10% hot spots
                const size_t hot_spot_start = (i * hot_spot_size) % (test_data.size - hot_spot_size);

                // 70% of accesses to hot spots, 30% random
                for (size_t j = 0; j < 1000; ++j) {
                    if (j < 700) {
                        // Hot spot access
                        size_t offset = (j * 4096) % hot_spot_size;
                        test_data.access_indices.push_back(hot_spot_start + offset);
                    } else {
                        // Random access
                        test_data.access_indices.push_back((gen() % test_data.size) & ~0xFF); // 256-byte aligned
                    }
                }
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

    // Simulate memory access patterns to test caching effectiveness
    struct CachingResult {
        std::chrono::microseconds first_pass_time{0};
        std::chrono::microseconds second_pass_time{0};
        std::chrono::microseconds third_pass_time{0};
        double cache_speedup_ratio{0.0};
        double cache_effectiveness_percentage{0.0};
        CachePerformanceMetrics cache_metrics;
        bool caching_effective{false};
    };

    CachingResult MeasureCachingEffectiveness(const std::vector<CacheTestData>& test_data) {
        CachingResult result;

        // First pass - cold cache
        auto first_start = std::chrono::high_resolution_clock::now();

        for (const auto& data : test_data) {
            // Transfer to device (cold cache)
            cudaMemcpy(data.device_ptr, data.host_ptr, data.size, cudaMemcpyHostToDevice);

            // Simulate access pattern
            if (!data.access_indices.empty()) {
                void* temp_buffer = malloc(1024);
                for (size_t index : data.access_indices) {
                    if (index < data.size - 1024) {
                        cudaMemcpy(temp_buffer,
                                 static_cast<char*>(data.device_ptr) + index,
                                 1024, cudaMemcpyDeviceToHost);
                    }
                }
                free(temp_buffer);
            }
        }

        auto first_end = std::chrono::high_resolution_clock::now();
        result.first_pass_time = std::chrono::duration_cast<std::chrono::microseconds>(
            first_end - first_start);

        // Prefetch data back to device for second pass
        for (const auto& data : test_data) {
            memory_optimizer_->PrefetchToDevice(data.device_ptr, data.size);
        }

        // Small delay to allow prefetching
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // Second pass - warm cache
        auto second_start = std::chrono::high_resolution_clock::now();

        for (const auto& data : test_data) {
            // Access same data again (should benefit from cache)
            if (!data.access_indices.empty()) {
                void* temp_buffer = malloc(1024);
                for (size_t index : data.access_indices) {
                    if (index < data.size - 1024) {
                        cudaMemcpy(temp_buffer,
                                 static_cast<char*>(data.device_ptr) + index,
                                 1024, cudaMemcpyDeviceToHost);
                    }
                }
                free(temp_buffer);
            }
        }

        auto second_end = std::chrono::high_resolution_clock::now();
        result.second_pass_time = std::chrono::duration_cast<std::chrono::microseconds>(
            second_end - second_start);

        // Third pass - verify cache persistence
        auto third_start = std::chrono::high_resolution_clock::now();

        for (const auto& data : test_data) {
            // Access data with different pattern
            if (!data.access_indices.empty()) {
                void* temp_buffer = malloc(1024);
                // Reverse access pattern to test cache lines
                for (auto it = data.access_indices.rbegin(); it != data.access_indices.rend(); ++it) {
                    size_t index = *it;
                    if (index < data.size - 1024) {
                        cudaMemcpy(temp_buffer,
                                 static_cast<char*>(data.device_ptr) + index,
                                 1024, cudaMemcpyDeviceToHost);
                    }
                }
                free(temp_buffer);
            }
        }

        auto third_end = std::chrono::high_resolution_clock::now();
        result.third_pass_time = std::chrono::duration_cast<std::chrono::microseconds>(
            third_end - third_start);

        // Calculate cache effectiveness metrics
        if (result.first_pass_time.count() > 0) {
            result.cache_speedup_ratio = static_cast<double>(result.first_pass_time.count()) /
                                       result.second_pass_time.count();
        }

        // Get cache performance metrics
        result.cache_metrics = bandwidth_validator_->MeasureCachePerformance("caching_test");

        // Determine caching effectiveness (speedup >= 2.0x indicates good caching)
        result.caching_effective = result.cache_speedup_ratio >= 2.0;

        // Calculate effectiveness percentage
        if (result.cache_speedup_ratio > 1.0) {
            result.cache_effectiveness_percentage = ((result.cache_speedup_ratio - 1.0) / result.cache_speedup_ratio) * 100.0;
        }

        return result;
    }

protected:
    int device_id_;
    std::unique_ptr<MemoryOptimizer> memory_optimizer_;
    std::unique_ptr<BandwidthValidator> bandwidth_validator_;
    std::vector<CacheTestData> test_memory_;
};

TEST_F(CachingEffectivenessTest, ValidateSequentialAccessCaching) {
    constexpr double TARGET_CACHE_EFFECTIVENESS = 50.0; // 50% improvement target
    constexpr double TARGET_CACHE_SPEEDUP = 2.0; // 2x speedup target

    std::cout << "\n=== Sequential Access Caching Effectiveness Test ===" << std::endl;

    // Create sequential access data (optimal for caching)
    auto sequential_data = CreateCachingTestData(8, 32, "sequential");
    ASSERT_FALSE(sequential_data.empty()) << "Failed to create sequential test data";

    // Measure caching effectiveness
    CachingResult result = MeasureCachingEffectiveness(sequential_data);

    EXPECT_GT(result.cache_speedup_ratio, TARGET_CACHE_SPEEDUP)
        << "Sequential access cache speedup below target: "
        << result.cache_speedup_ratio << "x < " << TARGET_CACHE_SPEEDUP << "x";

    EXPECT_GT(result.cache_effectiveness_percentage, TARGET_CACHE_EFFECTIVENESS)
        << "Sequential access cache effectiveness below target: "
        << result.cache_effectiveness_percentage << "% < " << TARGET_CACHE_EFFECTIVENESS << "%";

    EXPECT_TRUE(result.caching_effective) << "Sequential access caching should be effective";

    // Cache hit rates should be high for sequential access
    EXPECT_GT(result.cache_metrics.l1_cache_hit_rate, 0.80)
        << "L1 cache hit rate should be >80% for sequential access";

    EXPECT_GT(result.cache_metrics.l2_cache_hit_rate, 0.80)
        << "L2 cache hit rate should be >80% for sequential access";

    std::cout << "Sequential Access Results:" << std::endl;
    std::cout << "  Data Blocks: " << sequential_data.size() << std::endl;
    std::cout << "  Block Size: 32 MB each" << std::endl;
    std::cout << "  First Pass Time: " << result.first_pass_time.count() << " μs" << std::endl;
    std::cout << "  Second Pass Time: " << result.second_pass_time.count() << " μs" << std::endl;
    std::cout << "  Third Pass Time: " << result.third_pass_time.count() << " μs" << std::endl;
    std::cout << "  Cache Speedup: " << result.cache_speedup_ratio << "x" << std::endl;
    std::cout << "  Cache Effectiveness: " << result.cache_effectiveness_percentage << "%" << std::endl;
    std::cout << "  L1 Cache Hit Rate: " << (result.cache_metrics.l1_cache_hit_rate * 100) << "%" << std::endl;
    std::cout << "  L2 Cache Hit Rate: " << (result.cache_metrics.l2_cache_hit_rate * 100) << "%" << std::endl;
    std::cout << "  Shared Memory Efficiency: " << (result.cache_metrics.shared_memory_efficiency * 100) << "%" << std::endl;
    std::cout << "  Target Speedup: " << TARGET_CACHE_SPEEDUP << "x" << std::endl;
    std::cout << "  Target Effectiveness: " << TARGET_CACHE_EFFECTIVENESS << "%" << std::endl;
    std::cout << "  Status: " << (result.caching_effective ? "PASS" : "FAIL") << std::endl;

    CleanupTestMemory();
}

TEST_F(CachingEffectivenessTest, ValidateTemporalLocalityCaching) {
    constexpr double TARGET_TEMPORAL_LOCALITY_IMPROVEMENT = 30.0; // 30% improvement for temporal locality

    std::cout << "\n=== Temporal Locality Caching Effectiveness Test ===" << std::endl;

    // Create data with temporal locality (repeated access to same regions)
    auto temporal_data = CreateCachingTestData(6, 24, "random_locality");
    ASSERT_FALSE(temporal_data.empty()) << "Failed to create temporal locality test data";

    // Measure caching effectiveness for temporal locality patterns
    CachingResult result = MeasureCachingEffectiveness(temporal_data);

    EXPECT_GT(result.cache_effectiveness_percentage, TARGET_TEMPORAL_LOCALITY_IMPROVEMENT)
        << "Temporal locality cache effectiveness below target: "
        << result.cache_effectiveness_percentage << "% < " << TARGET_TEMPORAL_LOCALITY_IMPROVEMENT << "%";

    // Temporal locality should show good L1 cache hit rates
    EXPECT_GT(result.cache_metrics.l1_cache_hit_rate, 0.70)
        << "L1 cache hit rate should be >70% for temporal locality";

    std::cout << "Temporal Locality Results:" << std::endl;
    std::cout << "  Data Blocks: " << temporal_data.size() << std::endl;
    std::cout << "  Block Size: 24 MB each" << std::endl;
    std::cout << "  Cache Speedup: " << result.cache_speedup_ratio << "x" << std::endl;
    std::cout << "  Cache Effectiveness: " << result.cache_effectiveness_percentage << "%" << std::endl;
    std::cout << "  L1 Cache Hit Rate: " << (result.cache_metrics.l1_cache_hit_rate * 100) << "%" << std::endl;
    std::cout << "  L2 Cache Hit Rate: " << (result.cache_metrics.l2_cache_hit_rate * 100) << "%" << std::endl;
    std::cout << "  Target Improvement: " << TARGET_TEMPORAL_LOCALITY_IMPROVEMENT << "%" << std::endl;
    std::cout << "  Status: " << (result.cache_effectiveness_percentage >= TARGET_TEMPORAL_LOCALITY_IMPROVEMENT ? "PASS" : "FAIL") << std::endl;

    CleanupTestMemory();
}

TEST_F(CachingEffectivenessTest, ValidateSpatialLocalityCaching) {
    std::cout << "\n=== Spatial Locality Caching Effectiveness Test ===" << std::endl;

    // Create strided access data (tests spatial locality)
    auto strided_data = CreateCachingTestData(10, 16, "strided");
    ASSERT_FALSE(strided_data.empty()) << "Failed to create strided test data";

    // Measure caching effectiveness for spatial locality
    CachingResult result = MeasureCachingEffectiveness(strided_data);

    // Spatial locality should still show reasonable improvement
    EXPECT_GT(result.cache_speedup_ratio, 1.5)
        << "Strided access should show at least 1.5x cache speedup";

    // L2 cache should handle strided access well
    EXPECT_GT(result.cache_metrics.l2_cache_hit_rate, 0.60)
        << "L2 cache hit rate should be >60% for strided access";

    std::cout << "Spatial Locality Results:" << std::endl;
    std::cout << "  Data Blocks: " << strided_data.size() << std::endl;
    std::cout << "  Block Size: 16 MB each" << std::endl;
    std::cout << "  Cache Speedup: " << result.cache_speedup_ratio << "x" << std::endl;
    std::cout << "  Cache Effectiveness: " << result.cache_effectiveness_percentage << "%" << std::endl;
    std::cout << "  L1 Cache Hit Rate: " << (result.cache_metrics.l1_cache_hit_rate * 100) << "%" << std::endl;
    std::cout << "  L2 Cache Hit Rate: " << (result.cache_metrics.l2_cache_hit_rate * 100) << "%" << std::endl;
    std::cout << "  Stride Pattern: 64KB stride" << std::endl;
    std::cout << "  Status: " << (result.cache_speedup_ratio >= 1.5 ? "PASS" : "FAIL") << std::endl;

    CleanupTestMemory();
}

TEST_F(CachingEffectivenessTest, ValidateCacheHierarchyEffectiveness) {
    std::cout << "\n=== Cache Hierarchy Effectiveness Test ===" << std::endl;

    // Test different data sizes to stress different cache levels
    std::vector<std::pair<size_t, std::string>> test_configs = {
        {2, "L1_Optimized"},    // 2MB - fits in L1
        {8, "L2_Optimized"},    // 8MB - fits in L2
        {32, "L2_Extended"},    // 32MB - stresses L2
        {64, "Memory_Bound"}     // 64MB - memory bound
    };

    for (const auto& [size_mb, config_name] : test_configs) {
        std::cout << "\nTesting " << config_name << " (" << size_mb << "MB):" << std::endl;

        auto config_data = CreateCachingTestData(4, size_mb, "sequential");
        ASSERT_FALSE(config_data.empty()) << "Failed to create test data for " << config_name;

        CachingResult result = MeasureCachingEffectiveness(config_data);

        std::cout << "  Cache Speedup: " << result.cache_speedup_ratio << "x" << std::endl;
        std::cout << "  L1 Hit Rate: " << (result.cache_metrics.l1_cache_hit_rate * 100) << "%" << std::endl;
        std::cout << "  L2 Hit Rate: " << (result.cache_metrics.l2_cache_hit_rate * 100) << "%" << std::endl;
        std::cout << "  Shared Memory: " << (result.cache_metrics.shared_memory_efficiency * 100) << "%" << std::endl;

        // Expected behavior based on data size
        if (config_name == "L1_Optimized") {
            EXPECT_GT(result.cache_metrics.l1_cache_hit_rate, 0.90)
                << "L1 optimized data should have >90% L1 hit rate";
            EXPECT_GT(result.cache_speedup_ratio, 3.0)
                << "L1 optimized data should show >3x speedup";
        } else if (config_name == "L2_Optimized") {
            EXPECT_GT(result.cache_metrics.l2_cache_hit_rate, 0.85)
                << "L2 optimized data should have >85% L2 hit rate";
            EXPECT_GT(result.cache_speedup_ratio, 2.5)
                << "L2 optimized data should show >2.5x speedup";
        } else if (config_name == "L2_Extended") {
            EXPECT_GT(result.cache_metrics.l2_cache_hit_rate, 0.60)
                << "L2 extended data should have >60% L2 hit rate";
            EXPECT_GT(result.cache_speedup_ratio, 1.8)
                << "L2 extended data should show >1.8x speedup";
        } else { // Memory_Bound
            EXPECT_GT(result.cache_speedup_ratio, 1.2)
                << "Even memory bound data should show some caching benefit";
        }

        CleanupTestMemory();
    }
}

TEST_F(CachingEffectivenessTest, ValidateCacheConsistency) {
    std::cout << "\n=== Cache Consistency Test ===" << std::endl;

    // Test cache consistency across multiple passes
    auto consistency_data = CreateCachingTestData(5, 20, "sequential");
    ASSERT_FALSE(consistency_data.empty()) << "Failed to create consistency test data";

    // Perform multiple passes and verify consistent cache performance
    std::vector<std::chrono::microseconds> pass_times;
    std::vector<CachePerformanceMetrics> pass_metrics;

    for (int pass = 0; pass < 5; ++pass) {
        auto pass_start = std::chrono::high_resolution_clock::now();

        for (const auto& data : consistency_data) {
            cudaMemcpy(data.device_ptr, data.host_ptr, data.size, cudaMemcpyHostToDevice);

            // Access pattern
            void* temp_buffer = malloc(1024);
            for (size_t i = 0; i < data.size; i += 64 * 1024) { // 64KB stride
                if (i + 1024 < data.size) {
                    cudaMemcpy(temp_buffer,
                             static_cast<char*>(data.device_ptr) + i,
                             1024, cudaMemcpyDeviceToHost);
                }
            }
            free(temp_buffer);
        }

        auto pass_end = std::chrono::high_resolution_clock::now();
        pass_times.push_back(std::chrono::duration_cast<std::chrono::microseconds>(pass_end - pass_start));
        pass_metrics.push_back(bandwidth_validator_->MeasureCachePerformance("consistency_test"));

        // Small delay between passes
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // Analyze consistency
    double avg_time = 0.0;
    double variance = 0.0;
    for (const auto& time : pass_times) {
        avg_time += time.count();
    }
    avg_time /= pass_times.size();

    for (const auto& time : pass_times) {
        variance += std::pow(time.count() - avg_time, 2);
    }
    variance /= pass_times.size();
    double std_deviation = std::sqrt(variance);

    double coefficient_of_variation = std_deviation / avg_time;

    // Cache performance should be consistent (low variation)
    EXPECT_LT(coefficient_of_variation, 0.15)  // Less than 15% variation
        << "Cache performance should be consistent across multiple passes";

    // Cache hit rates should be stable
    double avg_l1_hit_rate = 0.0;
    for (const auto& metrics : pass_metrics) {
        avg_l1_hit_rate += metrics.l1_cache_hit_rate;
    }
    avg_l1_hit_rate /= pass_metrics.size();

    EXPECT_GT(avg_l1_hit_rate, 0.75) << "Average L1 hit rate should be >75%";

    std::cout << "Cache Consistency Results:" << std::endl;
    std::cout << "  Passes: " << pass_times.size() << std::endl;
    std::cout << "  Average Pass Time: " << avg_time << " μs" << std::endl;
    std::cout << "  Standard Deviation: " << std_deviation << " μs" << std::endl;
    std::cout << "  Coefficient of Variation: " << (coefficient_of_variation * 100) << "%" << std::endl;
    std::cout << "  Average L1 Hit Rate: " << (avg_l1_hit_rate * 100) << "%" << std::endl;
    std::cout << "  Consistency Threshold: 15%" << std::endl;
    std::cout << "  Status: " << (coefficient_of_variation < 0.15 ? "PASS" : "FAIL") << std::endl;

    CleanupTestMemory();
}

} // namespace puzzle71::gpu::performance