// T004: Baseline Integration Measurement Framework
// Measures and tracks baseline performance for integration operations

#pragma once

#include <vector>
#include <string>
#include <chrono>
#include <nlohmann/json.hpp>

namespace integration {
namespace baseline {

enum class MeasurementType {
    BUILD_TIME,
    SETUP_TIME,
    INTEGRATION_TIME,
    EXTRACTION_TIME
};

class BaselineMeasurer {
public:
    BaselineMeasurer();

    // Recording methods
    void record_build_time(double duration_ms);
    void record_setup_steps(const std::vector<std::string>& steps);
    void record_dependency_count(int count);
    void record_disk_usage(long long bytes);

    // Analysis methods
    bool meets_build_target(double build_time_ms) const;
    bool meets_complexity_reduction(int current_steps) const;
    bool meets_disk_limit(long long current_bytes, long long baseline_bytes) const;

    // Persistence
    void save_baseline() const;

    // Configuration
    void set_output_directory(const std::string& dir) { output_dir_ = dir; }
    const std::vector<std::string>& get_setup_steps() const { return setup_steps_; }

private:
    nlohmann::json calculate_statistics() const;
    void load_baseline_data();
    std::string get_current_timestamp() const;

    std::string output_dir_ = "build/integration-metrics";
    std::vector<double> build_times_;
    std::vector<std::string> setup_steps_;
    std::vector<int> dependency_counts_;
    std::vector<long long> disk_usage_;
};

// RAII timer class for automatic measurement
class BaselineTimer {
public:
    BaselineTimer(BaselineMeasurer& measurer,
                 MeasurementType type,
                 const std::string& description = "");
    ~BaselineTimer();

private:
    BaselineMeasurer& measurer_;
    MeasurementType type_;
    std::string description_;
    std::chrono::high_resolution_clock::time_point start_;
};

} // namespace baseline
} // namespace integration

// Convenience macros
#define MEASURE_BUILD_TIME(measurer, desc) \
    integration::baseline::BaselineTimer _timer(measurer, \
        integration::baseline::MeasurementType::BUILD_TIME, #desc);