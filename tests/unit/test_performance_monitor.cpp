/**
 * Unit Tests for PerformanceMonitor
 * 
 * Tests P1-007: Dynamic Performance Tuning
 * - Sliding window metrics collection
 * - Average calculation
 * - Tuning need detection
 * - Telemetry persistence
 * 
 * @origin       https://github.com/Puzzle71Solver/Puzzle71Solver
 * @origin_path  tests/unit/test_performance_monitor.cpp
 * @origin_commit <current_commit>
 * @origin_license MIT
 * @extracted_date   2025-10-13
 * @extracted_by     Puzzle71Solver Team
 * @modifications    Created for P1-007 testing
 * @spdx_license_identifier MIT
 */

#include <gtest/gtest.h>
#include "../../src/performance/performance_monitor.h"
#include "../../src/performance/telemetry_persistence.h"
#include <filesystem>
#include <chrono>

using namespace puzzle71::performance;

class PerformanceMonitorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create test telemetry directory
        std::filesystem::create_directories("test_telemetry");
    }
    
    void TearDown() override {
        // Clean up test files
        std::filesystem::remove_all("test_telemetry");
    }
};

// Test 1: Constructor and basic properties
TEST_F(PerformanceMonitorTest, ConstructorAndProperties) {
    PerformanceMonitor monitor(10, 0.90, 0.80);
    
    EXPECT_EQ(monitor.get_window_size(), 10);
    EXPECT_DOUBLE_EQ(monitor.get_target_gpu_utilization(), 0.90);
    EXPECT_DOUBLE_EQ(monitor.get_target_memory_bandwidth(), 0.80);
    EXPECT_EQ(monitor.get_sample_count(), 0);
}

// Test 2: Add sample and sliding window
TEST_F(PerformanceMonitorTest, AddSampleAndSlidingWindow) {
    PerformanceMonitor monitor(5, 0.90, 0.80);
    
    // Add 3 samples
    for (int i = 0; i < 3; ++i) {
        PerformanceMonitor::PerformanceMetrics metrics;
        metrics.keys_per_sec = 1000.0 + i * 100.0;
        metrics.gpu_utilization = 0.80 + i * 0.05;
        metrics.memory_bandwidth = 0.70 + i * 0.05;
        metrics.timestamp = std::chrono::steady_clock::now();
        
        monitor.add_sample(metrics);
    }
    
    EXPECT_EQ(monitor.get_sample_count(), 3);
    
    // Add 5 more samples (should overflow window)
    for (int i = 0; i < 5; ++i) {
        PerformanceMonitor::PerformanceMetrics metrics;
        metrics.keys_per_sec = 2000.0;
        metrics.gpu_utilization = 0.95;
        metrics.memory_bandwidth = 0.85;
        metrics.timestamp = std::chrono::steady_clock::now();
        
        monitor.add_sample(metrics);
    }
    
    // Window size should be capped at 5
    EXPECT_EQ(monitor.get_sample_count(), 5);
}

// Test 3: Average metrics calculation
TEST_F(PerformanceMonitorTest, AverageMetricsCalculation) {
    PerformanceMonitor monitor(10, 0.90, 0.80);
    
    // Add 5 samples with known values
    for (int i = 0; i < 5; ++i) {
        PerformanceMonitor::PerformanceMetrics metrics;
        metrics.keys_per_sec = 1000.0;  // All same
        metrics.gpu_utilization = 0.80;  // All same
        metrics.memory_bandwidth = 0.70;  // All same
        metrics.active_blocks = 100;
        metrics.active_warps = 50;
        metrics.timestamp = std::chrono::steady_clock::now();
        
        monitor.add_sample(metrics);
    }
    
    auto avg = monitor.get_average_metrics();
    
    EXPECT_DOUBLE_EQ(avg.keys_per_sec, 1000.0);
    EXPECT_DOUBLE_EQ(avg.gpu_utilization, 0.80);
    EXPECT_DOUBLE_EQ(avg.memory_bandwidth, 0.70);
    EXPECT_EQ(avg.active_blocks, 100);
    EXPECT_EQ(avg.active_warps, 50);
}

// Test 4: Tuning need detection - GPU underutilized
TEST_F(PerformanceMonitorTest, TuningNeedDetection_GPUUnderutilized) {
    PerformanceMonitor monitor(10, 0.90, 0.80);
    
    // Add samples with low GPU utilization
    for (int i = 0; i < 6; ++i) {
        PerformanceMonitor::PerformanceMetrics metrics;
        metrics.keys_per_sec = 1000.0;
        metrics.gpu_utilization = 0.70;  // Below target (0.90)
        metrics.memory_bandwidth = 0.85;  // Above target (0.80)
        metrics.timestamp = std::chrono::steady_clock::now();
        
        monitor.add_sample(metrics);
    }
    
    EXPECT_TRUE(monitor.needs_tuning());
}

// Test 5: Tuning need detection - Memory underutilized
TEST_F(PerformanceMonitorTest, TuningNeedDetection_MemoryUnderutilized) {
    PerformanceMonitor monitor(10, 0.90, 0.80);
    
    // Add samples with low memory bandwidth
    for (int i = 0; i < 6; ++i) {
        PerformanceMonitor::PerformanceMetrics metrics;
        metrics.keys_per_sec = 1000.0;
        metrics.gpu_utilization = 0.95;  // Above target (0.90)
        metrics.memory_bandwidth = 0.70;  // Below target (0.80)
        metrics.timestamp = std::chrono::steady_clock::now();
        
        monitor.add_sample(metrics);
    }
    
    EXPECT_TRUE(monitor.needs_tuning());
}

// Test 6: Tuning need detection - Optimal performance
TEST_F(PerformanceMonitorTest, TuningNeedDetection_Optimal) {
    PerformanceMonitor monitor(10, 0.90, 0.80);
    
    // Add samples with optimal metrics
    for (int i = 0; i < 6; ++i) {
        PerformanceMonitor::PerformanceMetrics metrics;
        metrics.keys_per_sec = 1000.0;
        metrics.gpu_utilization = 0.95;  // Above target
        metrics.memory_bandwidth = 0.85;  // Above target
        metrics.timestamp = std::chrono::steady_clock::now();
        
        monitor.add_sample(metrics);
    }
    
    EXPECT_FALSE(monitor.needs_tuning());
}

// Test 7: Telemetry save and load
TEST_F(PerformanceMonitorTest, TelemetrySaveAndLoad) {
    PerformanceMonitor monitor(10, 0.90, 0.80);
    monitor.set_telemetry_file("test_telemetry/test.jsonl");
    
    // Create and save telemetry packet
    PerformanceMonitor::TelemetryPacket packet;
    packet.gpu_model = "RTX 2080 Ti";
    packet.gpu_id = 0;
    packet.metrics.keys_per_sec = 1000.0;
    packet.metrics.gpu_utilization = 0.85;
    packet.metrics.memory_bandwidth = 0.75;
    packet.metrics.active_blocks = 100;
    packet.metrics.active_warps = 50;
    packet.metrics.timestamp = std::chrono::steady_clock::now();
    packet.config["block_size"] = 256;
    packet.config["grid_size"] = 1024;
    packet.digest = TelemetryPersistence::calculate_digest(packet);
    
    EXPECT_TRUE(monitor.save_telemetry(packet));
    
    // Load telemetry history
    EXPECT_TRUE(monitor.load_telemetry_history());
    
    auto history = monitor.get_telemetry_history();
    EXPECT_EQ(history.size(), 1);
    EXPECT_EQ(history[0].gpu_model, "RTX 2080 Ti");
    EXPECT_EQ(history[0].gpu_id, 0);
    EXPECT_DOUBLE_EQ(history[0].metrics.keys_per_sec, 1000.0);
}

// Test 8: Clear window
TEST_F(PerformanceMonitorTest, ClearWindow) {
    PerformanceMonitor monitor(10, 0.90, 0.80);
    
    // Add samples
    for (int i = 0; i < 5; ++i) {
        PerformanceMonitor::PerformanceMetrics metrics;
        metrics.keys_per_sec = 1000.0;
        metrics.gpu_utilization = 0.85;
        metrics.memory_bandwidth = 0.75;
        metrics.timestamp = std::chrono::steady_clock::now();
        
        monitor.add_sample(metrics);
    }
    
    EXPECT_EQ(monitor.get_sample_count(), 5);
    
    // Clear window
    monitor.clear_window();
    
    EXPECT_EQ(monitor.get_sample_count(), 0);
}

// Main function
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

