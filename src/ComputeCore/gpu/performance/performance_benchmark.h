#pragma once

#include <chrono>
#include <vector>
#include <memory>
#include <string>
#include <nlohmann/json.hpp>
#include "core/uint256.h"

namespace puzzle71::gpu::benchmarking {

using json = nlohmann::json;

struct BenchmarkConfig {
    std::chrono::seconds warmup_time{30};
    std::chrono::seconds measurement_time{300};
    int num_iterations = 10;
    std::vector<int> gpu_ids;
    bool enable_detailed_profiling = true;
};

struct BenchmarkResults {
    double mean_throughput_mkeys_per_sec = 0.0;
    double std_deviation_throughput = 0.0;
    double p95_throughput_mkeys_per_sec = 0.0;
    double p99_throughput_mkeys_per_sec = 0.0;
    double memory_bandwidth_utilization = 0.0;
    double gpu_utilization_percentage = 0.0;
    std::chrono::microseconds mean_execution_time{0};
    std::vector<double> throughput_samples;
};

class PerformanceBenchmark {
public:
    PerformanceBenchmark();
    virtual ~PerformanceBenchmark();

    virtual BenchmarkResults RunBenchmark(
        const core::UInt256& start_key,
        const core::UInt256& end_key,
        const BenchmarkConfig& config = {}
    ) = 0;

    virtual std::pair<BenchmarkResults, BenchmarkResults> CompareConfigurations(
        const core::UInt256& start_key,
        const core::UInt256& end_key,
        const BenchmarkConfig& baseline_config,
        const BenchmarkConfig& optimized_config
    ) = 0;

    virtual std::string GeneratePerformanceReport(const std::vector<BenchmarkResults>& results) const = 0;
};

} // namespace puzzle71::gpu::benchmarking