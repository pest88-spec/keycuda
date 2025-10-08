#pragma once

#include <chrono>
#include <vector>
#include <memory>
#include <string>
#include <nlohmann/json.hpp>
#include "core/uint256.h"

namespace puzzle71::gpu::performance {

using json = nlohmann::json;

struct OptimizationConfig {
    int target_points_per_thread = 128;
    bool enable_dynamic_block_sizing = true;
    bool enable_asynchronous_transfers = true;
    bool enable_memory_pooling = true;
    double target_memory_bandwidth_utilization = 0.85;
    int max_sync_overhead_reduction_percent = 90;
};

struct PerformanceMetrics {
    double baseline_throughput_mkeys_per_sec = 0.0;
    double optimized_throughput_mkeys_per_sec = 0.0;
    double performance_improvement_factor = 0.0;
    double sync_overhead_reduction_percent = 0.0;
    double memory_bandwidth_utilization = 0.0;
    double gpu_utilization_percentage = 0.0;
    std::chrono::microseconds total_execution_time{0};
    bool accuracy_verification_passed = false;
};

class GpuPerformanceManager {
public:
    GpuPerformanceManager();
    virtual ~GpuPerformanceManager();

    virtual PerformanceMetrics OptimizeKeySearch(
        const core::UInt256& start_key,
        const core::UInt256& end_key,
        const OptimizationConfig& config = {}
    ) = 0;

    virtual void ApplyOptimalConfiguration(int gpu_id) = 0;
    virtual OptimizationConfig GetCurrentConfiguration() const = 0;
    virtual void ResetToBaselineConfiguration() = 0;

    virtual PerformanceMetrics GetLastPerformanceMetrics() const = 0;
    virtual std::vector<PerformanceMetrics> GetHistoricalMetrics() const = 0;
    virtual bool ValidatePerformanceImprovement(double min_improvement_factor = 8.0) const = 0;
};

} // namespace puzzle71::gpu::performance