// T004: Baseline Integration Measurement Framework
// Measures and tracks baseline performance for integration operations

#include "baseline_measurer.h"
#include <fstream>
#include <algorithm>
#include <numeric>
#include <iostream>
#include <iomanip>
#include <sstream>

namespace integration {
namespace baseline {

BaselineMeasurer::BaselineMeasurer() {
    load_baseline_data();
}

void BaselineMeasurer::record_build_time(double duration_ms) {
    build_times_.push_back(duration_ms);
}

void BaselineMeasurer::record_setup_steps(const std::vector<std::string>& steps) {
    setup_steps_ = steps;
}

void BaselineMeasurer::record_dependency_count(int count) {
    dependency_counts_.push_back(count);
}

void BaselineMeasurer::record_disk_usage(long long bytes) {
    disk_usage_.push_back(bytes);
}

void BaselineMeasurer::save_baseline() const {
    nlohmann::json baseline;
    baseline["version"] = "1.0.0";
    baseline["generated"] = get_current_timestamp();
    baseline["measurements"] = calculate_statistics();

    std::string filename = output_dir_ + "/integration-baseline.json";
    std::ofstream file(filename);
    if (file.is_open()) {
        file << baseline.dump(2) << std::endl;
        file.close();
    }
}

nlohmann::json BaselineMeasurer::calculate_statistics() const {
    nlohmann::json stats;

    // Build time statistics
    if (!build_times_.empty()) {
        double sum = std::accumulate(build_times_.begin(), build_times_.end(), 0.0);
        stats["build_time_ms"] = {
            {"count", build_times_.size()},
            {"average", sum / build_times_.size()},
            {"min", *std::min_element(build_times_.begin(), build_times_.end())},
            {"max", *std::max_element(build_times_.begin(), build_times_.end())},
            {"target_ms", 300000.0} // 5 minutes
        };
    }

    // Setup steps
    stats["setup_steps"] = {
        {"current_count", setup_steps_.size()},
        {"steps", setup_steps_},
        {"target_reduction_percent", 80},
        {"target_max_steps", 2}
    };

    // Dependency count
    if (!dependency_counts_.empty()) {
        double avg_deps = std::accumulate(dependency_counts_.begin(),
                                         dependency_counts_.end(), 0.0) / dependency_counts_.size();
        stats["dependencies"] = {
            {"average_count", static_cast<int>(avg_deps)},
            {"current_count", dependency_counts_.back()},
            {"integrated_count", static_cast<int>(dependency_counts_.size())}
        };
    }

    // Disk usage
    if (!disk_usage_.empty()) {
        long long total_disk = std::accumulate(disk_usage_.begin(), disk_usage_.end(), 0LL);
        stats["disk_usage"] = {
            {"total_bytes", total_disk},
            {"average_bytes", total_disk / disk_usage_.size()},
            {"increase_limit_percent", 50}
        };
    }

    return stats;
}

bool BaselineMeasurer::meets_build_target(double build_time_ms) const {
    return build_time_ms <= 300000.0; // 5 minutes
}

bool BaselineMeasurer::meets_complexity_reduction(int current_steps) const {
    return current_steps <= 2; // Target: git clone + build
}

bool BaselineMeasurer::meets_disk_limit(long long current_bytes, long long baseline_bytes) const {
    double increase_percent = (static_cast<double>(current_bytes) / baseline_bytes - 1.0) * 100.0;
    return increase_percent <= 50.0;
}

void BaselineMeasurer::load_baseline_data() {
    std::string filename = output_dir_ + "/integration-baseline.json";
    std::ifstream file(filename);

    if (!file.is_open()) {
        return; // No baseline exists yet
    }

    try {
        nlohmann::json baseline;
        file >> baseline;

        if (baseline.contains("measurements")) {
            auto measurements = baseline["measurements"];

            if (measurements.contains("build_time_ms")) {
                auto build_data = measurements["build_time_ms"];
                // Load historical build times for comparison
            }

            if (measurements.contains("setup_steps")) {
                auto steps_data = measurements["setup_steps"];
                if (steps_data.contains("steps")) {
                    for (const auto& step : steps_data["steps"]) {
                        setup_steps_.push_back(step.get<std::string>());
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error loading baseline data: " << e.what() << std::endl;
    }
}

std::string BaselineMeasurer::get_current_timestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

// RAII timer for automatic measurement
BaselineTimer::BaselineTimer(BaselineMeasurer& measurer,
                           MeasurementType type,
                           const std::string& description)
    : measurer_(measurer)
    , type_(type)
    , description_(description)
    , start_(std::chrono::high_resolution_clock::now()) {
}

BaselineTimer::~BaselineTimer() {
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start_);

    switch (type_) {
        case MeasurementType::BUILD_TIME:
            measurer_.record_build_time(duration.count());
            break;
        default:
            break;
    }
}

} // namespace baseline
} // namespace integration