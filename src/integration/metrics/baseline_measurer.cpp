/**
 * Puzzle71Solver - Baseline Integration Measurement Framework Implementation
 *
 * Provides performance measurement and baseline establishment for integration
 * operations, tracking build times, resource usage, and integration efficiency.
 *
 * @author       Puzzle71Solver Team
 * @created      2025-10-09
 * @license      MIT
 */

#include "baseline_measurer.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <filesystem>
#include <random>
#include <thread>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#include <unistd.h>
#include <ctime>
#endif

namespace integration {
namespace metrics {

struct BaselineMeasurer::Impl {
    std::map<std::string, BaselineMetrics> baselines;
    std::map<std::string, std::chrono::steady_clock::time_point> active_measurements;
    std::map<std::string, std::string> measurement_types;
    std::map<std::string, std::chrono::steady_clock::time_point> build_measurements;
    std::map<std::string, std::string> build_types;
    std::map<std::string, std::chrono::steady_clock::time_point> integration_measurements;
    std::map<std::string, std::string> integration_libraries;
    std::map<std::string, std::map<std::string, std::chrono::milliseconds>> integration_steps;
    std::map<std::string, std::chrono::milliseconds> performance_targets;
    std::string baseline_file_path = "integration_baselines.json";
    bool auto_save_enabled = true;
    size_t sample_size = 10;
    std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<uint64_t> dist;

    std::string generate_id() {
        std::ostringstream oss;
        oss << std::hex << dist(rng);
        return oss.str();
    }

    size_t get_memory_usage_mb() const {
#ifdef _WIN32
        PROCESS_MEMORY_COUNTERS pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
            return pmc.WorkingSetSize / (1024 * 1024);
        }
#else
        struct rusage usage;
        if (getrusage(RUSAGE_SELF, &usage) == 0) {
            return usage.ru_maxrss / 1024; // On Linux, ru_maxrss is in KB
        }
#endif
        return 0;
    }

    double get_cpu_usage_percent() const {
        // Simplified CPU usage estimation
        // In a real implementation, you would use platform-specific APIs
        return 0.0;
    }

    std::chrono::milliseconds get_steady_clock_duration(
        std::chrono::steady_clock::time_point start,
        std::chrono::steady_clock::time_point end) const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    }
};

BaselineMeasurer::BaselineMeasurer() : p_impl(std::make_unique<Impl>()) {}

BaselineMeasurer::BaselineMeasurer(const std::string& baseline_file_path) : BaselineMeasurer() {
    set_baseline_storage_path(baseline_file_path);
    load_baselines();
}

BaselineMeasurer::~BaselineMeasurer() {
    if (p_impl->auto_save_enabled) {
        save_baselines();
    }
}

void BaselineMeasurer::set_baseline_storage_path(const std::string& file_path) {
    p_impl->baseline_file_path = file_path;
}

void BaselineMeasurer::enable_auto_save(bool enabled) {
    p_impl->auto_save_enabled = enabled;
}

void BaselineMeasurer::set_sample_size(size_t sample_size) {
    p_impl->sample_size = std::max(size_t(1), sample_size);
}

bool BaselineMeasurer::load_baselines() {
    std::ifstream file(p_impl->baseline_file_path);
    if (!file.is_open()) {
        return false;
    }

    // Simplified loading - in a real implementation, you would parse JSON
    // For now, just return true if file exists
    file.close();
    return true;
}

bool BaselineMeasurer::save_baselines() const {
    std::ofstream file(p_impl->baseline_file_path);
    if (!file.is_open()) {
        return false;
    }

    // Write JSON header
    file << "{\n";
    file << "  \"baselines\": {\n";

    bool first = true;
    for (const auto& pair : p_impl->baselines) {
        if (!first) {
            file << ",\n";
        }

        const auto& baseline = pair.second;
        file << "    \"" << pair.first << "\": {\n";
        file << "      \"operation_name\": \"" << baseline.operation_name << "\",\n";
        file << "      \"sample_count\": " << baseline.sample_count << ",\n";
        file << "      \"average_duration_ms\": " << get_average_duration(baseline).count() << ",\n";
        file << "      \"best_duration_ms\": " << baseline.best_case_metrics.duration.count() << ",\n";
        file << "      \"worst_duration_ms\": " << baseline.worst_case_metrics.duration.count() << "\n";
        file << "    }";
        first = false;
    }

    file << "\n  }\n";
    file << "}\n";

    file.close();
    return true;
}

std::string BaselineMeasurer::start_build_measurement(const std::string& build_type,
                                                     const std::map<std::string, std::string>& environment) {
    auto measurement_id = p_impl->generate_id();
    auto start_time = std::chrono::steady_clock::now();

    p_impl->build_measurements[measurement_id] = start_time;
    p_impl->build_types[measurement_id] = build_type;

    return measurement_id;
}

bool BaselineMeasurer::end_build_measurement(const std::string& measurement_id, BuildMetrics& metrics) {
    auto it = p_impl->build_measurements.find(measurement_id);
    if (it == p_impl->build_measurements.end()) {
        return false;
    }

    auto end_time = std::chrono::steady_clock::now();
    auto start_time = it->second;
    auto duration = p_impl->get_steady_clock_duration(start_time, end_time);

    metrics.total_build_time = duration;
    metrics.build_type = p_impl->build_types[measurement_id];
    metrics.incremental_build = false; // Simplified

    // Record as generic performance metrics
    PerformanceMetrics perf_metrics;
    perf_metrics.duration = duration;
    perf_metrics.success = true;

    add_measurement_sample("build_" + metrics.build_type, perf_metrics);

    p_impl->build_measurements.erase(it);
    p_impl->build_types.erase(measurement_id);

    return true;
}

BuildMetrics BaselineMeasurer::get_build_baseline(const std::string& build_type) const {
    BuildMetrics baseline;
    auto it = p_impl->baselines.find("build_" + build_type);
    if (it != p_impl->baselines.end()) {
        baseline.total_build_time = get_average_duration(it->second);
    }
    return baseline;
}

std::string BaselineMeasurer::start_integration_measurement(const std::string& library_name) {
    auto measurement_id = p_impl->generate_id();
    auto start_time = std::chrono::steady_clock::now();

    p_impl->integration_measurements[measurement_id] = start_time;
    p_impl->integration_libraries[measurement_id] = library_name;

    return measurement_id;
}

bool BaselineMeasurer::end_integration_measurement(const std::string& measurement_id, IntegrationMetrics& metrics) {
    auto it = p_impl->integration_measurements.find(measurement_id);
    if (it == p_impl->integration_measurements.end()) {
        return false;
    }

    auto end_time = std::chrono::steady_clock::now();
    auto start_time = it->second;
    auto total_duration = p_impl->get_steady_clock_duration(start_time, end_time);

    metrics.total_integration_time = total_duration;

    // Get step breakdown if available
    auto steps_it = p_impl->integration_steps.find(measurement_id);
    if (steps_it != p_impl->integration_steps.end()) {
        const auto& steps = steps_it->second;
        auto extraction_it = steps.find("extraction");
        if (extraction_it != steps.end()) {
            metrics.extraction_time = extraction_it->second;
        }
        auto attribution_it = steps.find("attribution");
        if (attribution_it != steps.end()) {
            metrics.attribution_time = attribution_it->second;
        }
        auto verification_it = steps.find("verification");
        if (verification_it != steps.end()) {
            metrics.verification_time = verification_it->second;
        }
    }

    // Record as generic performance metrics
    PerformanceMetrics perf_metrics;
    perf_metrics.duration = total_duration;
    perf_metrics.success = true;
    perf_metrics.files_processed = metrics.files_extracted;

    add_measurement_sample("integration", perf_metrics);

    p_impl->integration_measurements.erase(it);
    p_impl->integration_libraries.erase(measurement_id);
    if (steps_it != p_impl->integration_steps.end()) {
        p_impl->integration_steps.erase(steps_it);
    }

    return true;
}

IntegrationMetrics BaselineMeasurer::get_integration_baseline() const {
    IntegrationMetrics baseline;
    auto it = p_impl->baselines.find("integration");
    if (it != p_impl->baselines.end()) {
        baseline.total_integration_time = get_average_duration(it->second);
    }
    return baseline;
}

std::string BaselineMeasurer::start_measurement(const std::string& operation_name) {
    auto measurement_id = p_impl->generate_id();
    auto start_time = std::chrono::steady_clock::now();

    p_impl->active_measurements[measurement_id] = start_time;
    p_impl->measurement_types[measurement_id] = operation_name;

    return measurement_id;
}

bool BaselineMeasurer::end_measurement(const std::string& measurement_id, PerformanceMetrics& metrics) {
    auto it = p_impl->active_measurements.find(measurement_id);
    if (it == p_impl->active_measurements.end()) {
        return false;
    }

    auto end_time = std::chrono::steady_clock::now();
    auto start_time = it->second;
    auto duration = p_impl->get_steady_clock_duration(start_time, end_time);

    metrics.duration = duration;
    metrics.memory_usage_mb = p_impl->get_memory_usage_mb();
    metrics.cpu_usage_percent = p_impl->get_cpu_usage_percent();

    std::string operation_name = p_impl->measurement_types[measurement_id];
    add_measurement_sample(operation_name, metrics);

    p_impl->active_measurements.erase(it);
    p_impl->measurement_types.erase(measurement_id);

    return true;
}

bool BaselineMeasurer::add_measurement_sample(const std::string& operation_name, const PerformanceMetrics& metrics) {
    auto& baseline = p_impl->baselines[operation_name];
    if (baseline.operation_name.empty()) {
        baseline.operation_name = operation_name;
        baseline.best_case_metrics = metrics;
        baseline.worst_case_metrics = metrics;
    } else {
        // Update best and worst cases
        if (metrics.duration < baseline.best_case_metrics.duration) {
            baseline.best_case_metrics = metrics;
        }
        if (metrics.duration > baseline.worst_case_metrics.duration) {
            baseline.worst_case_metrics = metrics;
        }
    }

    baseline.measurements.push_back(metrics);

    // Keep only the most recent samples
    if (baseline.measurements.size() > p_impl->sample_size) {
        baseline.measurements.erase(baseline.measurements.begin());
    }

    baseline.sample_count = baseline.measurements.size();
    baseline.last_updated = std::chrono::system_clock::now();

    update_baseline_statistics(baseline, metrics);

    if (p_impl->auto_save_enabled) {
        save_baselines();
    }

    return true;
}

BaselineMetrics BaselineMeasurer::get_baseline(const std::string& operation_name) const {
    auto it = p_impl->baselines.find(operation_name);
    if (it != p_impl->baselines.end()) {
        return it->second;
    }
    return BaselineMetrics{};
}

bool BaselineMeasurer::update_baseline(const std::string& operation_name, const PerformanceMetrics& metrics) {
    return add_measurement_sample(operation_name, metrics);
}

bool BaselineMeasurer::remove_baseline(const std::string& operation_name) {
    return p_impl->baselines.erase(operation_name) > 0;
}

std::vector<std::string> BaselineMeasurer::get_available_baselines() const {
    std::vector<std::string> names;
    for (const auto& pair : p_impl->baselines) {
        names.push_back(pair.first);
    }
    return names;
}

double BaselineMeasurer::compare_to_baseline(const std::string& operation_name, const PerformanceMetrics& metrics) const {
    auto baseline_it = p_impl->baselines.find(operation_name);
    if (baseline_it == p_impl->baselines.end()) {
        return 0.0; // No baseline available
    }

    auto baseline_duration = get_average_duration(baseline_it->second).count();
    if (baseline_duration == 0) {
        return 0.0;
    }

    double current_duration = static_cast<double>(metrics.duration.count());
    return ((current_duration - baseline_duration) / baseline_duration) * 100.0;
}

bool BaselineMeasurer::is_performance_acceptable(const std::string& operation_name, const PerformanceMetrics& metrics,
                                                 double tolerance_percentage) const {
    double performance_change = compare_to_baseline(operation_name, metrics);
    return std::abs(performance_change) <= tolerance_percentage;
}

std::vector<std::string> BaselineMeasurer::identify_performance_regressions(double threshold_percentage) const {
    std::vector<std::string> regressions;

    for (const auto& pair : p_impl->baselines) {
        const auto& baseline = pair.second;
        if (!baseline.measurements.empty()) {
            const auto& latest = baseline.measurements.back();
            double change = compare_to_baseline(pair.first, latest);
            if (change > threshold_percentage) {
                regressions.push_back(pair.first + " (+" + std::to_string(static_cast<int>(change)) + "%)");
            }
        }
    }

    return regressions;
}

std::map<std::string, BaselineMetrics> BaselineMeasurer::get_all_baselines() const {
    return p_impl->baselines;
}

size_t BaselineMeasurer::get_baseline_count() const {
    return p_impl->baselines.size();
}

std::string BaselineMeasurer::generate_performance_report() const {
    std::ostringstream report;
    report << "Integration Performance Baseline Report\n";
    report << "=====================================\n\n";

    for (const auto& pair : p_impl->baselines) {
        const auto& baseline = pair.second;
        report << "Operation: " << baseline.operation_name << "\n";
        report << "Sample Count: " << baseline.sample_count << "\n";
        report << "Average Duration: " << get_average_duration(baseline).count() << "ms\n";
        report << "Best Case: " << baseline.best_case_metrics.duration.count() << "ms\n";
        report << "Worst Case: " << baseline.worst_case_metrics.duration.count() << "ms\n";
        report << "Last Updated: " << std::chrono::duration_cast<std::chrono::seconds>(
            baseline.last_updated.time_since_epoch()).count() << "\n\n";
    }

    auto regressions = identify_performance_regressions();
    if (!regressions.empty()) {
        report << "Performance Regressions Detected:\n";
        for (const auto& regression : regressions) {
            report << "- " << regression << "\n";
        }
    }

    return report.str();
}

std::string BaselineMeasurer::export_baselines_json() const {
    std::ostringstream json;
    json << "{\n";
    json << "  \"export_timestamp\": " << std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() << ",\n";
    json << "  \"baselines\": {\n";

    bool first = true;
    for (const auto& pair : p_impl->baselines) {
        if (!first) {
            json << ",\n";
        }
        json << "    \"" << pair.first << "\": {\n";
        json << "      \"average_duration_ms\": " << get_average_duration(pair.second).count() << ",\n";
        json << "      \"sample_count\": " << pair.second.sample_count << "\n";
        json << "    }";
        first = false;
    }

    json << "\n  }\n";
    json << "}\n";

    return json.str();
}

void BaselineMeasurer::set_performance_target(const std::string& operation_name, std::chrono::milliseconds target_time) {
    p_impl->performance_targets[operation_name] = target_time;
}

bool BaselineMeasurer::meets_performance_target(const std::string& operation_name, const PerformanceMetrics& metrics) const {
    auto it = p_impl->performance_targets.find(operation_name);
    if (it == p_impl->performance_targets.end()) {
        return true; // No target set
    }
    return metrics.duration <= it->second;
}

std::map<std::string, std::chrono::milliseconds> BaselineMeasurer::get_performance_targets() const {
    return p_impl->performance_targets;
}

bool BaselineMeasurer::exceeds_time_budget(const std::string& operation_name, const PerformanceMetrics& metrics,
                                           std::chrono::milliseconds budget) const {
    return metrics.duration > budget;
}

std::map<std::string, std::chrono::milliseconds> BaselineMeasurer::get_default_time_budgets() const {
    std::map<std::string, std::chrono::milliseconds> budgets;
    budgets["integration"] = std::chrono::minutes(10); // 10 minutes per library
    budgets["build"] = std::chrono::minutes(5); // 5 minutes for fresh build
    budgets["verification"] = std::chrono::minutes(2); // 2 minutes for verification
    budgets["attribution"] = std::chrono::minutes(1); // 1 minute for attribution
    return budgets;
}

void BaselineMeasurer::record_integration_step(const std::string& operation_id, const std::string& step_name,
                                               std::chrono::milliseconds duration) {
    p_impl->integration_steps[operation_id][step_name] = duration;
}

std::map<std::string, std::chrono::milliseconds> BaselineMeasurer::get_integration_breakdown(const std::string& operation_id) const {
    auto it = p_impl->integration_steps.find(operation_id);
    if (it != p_impl->integration_steps.end()) {
        return it->second;
    }
    return {};
}

std::chrono::milliseconds BaselineMeasurer::get_average_duration(const BaselineMetrics& baseline) const {
    if (baseline.measurements.empty()) {
        return std::chrono::milliseconds{0};
    }

    auto total = std::accumulate(baseline.measurements.begin(), baseline.measurements.end(),
                                 std::chrono::milliseconds{0},
                                 [](const auto& sum, const auto& metric) {
                                     return sum + metric.duration;
                                 });

    return std::chrono::duration_cast<std::chrono::milliseconds>(total / baseline.measurements.size());
}

void BaselineMeasurer::update_baseline_statistics(BaselineMetrics& baseline, const PerformanceMetrics& metrics) {
    if (baseline.measurements.empty()) {
        baseline.average_metrics = metrics;
        return;
    }

    // Update average metrics
    auto total_duration = baseline.average_metrics.duration.count() * (baseline.measurements.size() - 1) + metrics.duration.count();
    baseline.average_metrics.duration = std::chrono::milliseconds{total_duration / baseline.measurements.size()};

    // Update other average metrics
    baseline.average_metrics.memory_usage_mb =
        (baseline.average_metrics.memory_usage_mb * (baseline.measurements.size() - 1) + metrics.memory_usage_mb) / baseline.measurements.size();
}

std::string BaselineMeasurer::generate_measurement_id() const {
    return p_impl->generate_id();
}

PerformanceMetrics BaselineMeasurer::measure_current_performance() const {
    PerformanceMetrics metrics;
    metrics.memory_usage_mb = p_impl->get_memory_usage_mb();
    metrics.cpu_usage_percent = p_impl->get_cpu_usage_percent();
    return metrics;
}

// Global measurer instance
BaselineMeasurer& get_baseline_measurer() {
    static BaselineMeasurer instance;
    return instance;
}

} // namespace metrics
} // namespace integration