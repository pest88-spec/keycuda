/**
 * Library-Type-Specific Performance Baseline Measurements Implementation
 *
 * @origin       https://github.com/Puzzle71Solver/Puzzle71Solver
 * @origin_path  src/integration/performance_baselines.cpp
 * @origin_commit <current_commit>
 * @origin_license MIT
 * @extracted_date   2025-10-10
 * @extracted_by     Puzzle71Solver Team
 * @modifications    Created for third-party dependency integration optimization
 * @spdx_license_identifier MIT
 */

#include "performance_baselines.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <filesystem>
#include <random>
#include <numeric>
#include <cmath>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

PerformanceBaselines::PerformanceBaselines(
    const std::string& storage_path,
    double regression_threshold
) : storage_path_(storage_path),
    regression_threshold_(std::chrono::milliseconds(static_cast<long long>(regression_threshold * 1000))) {

    // Create storage directory if it doesn't exist
    if (!storage_path_.empty()) {
        std::filesystem::create_directories(storage_path_);
    }

    // Load existing baselines
    load_baselines();

    // Initialize default baselines if none exist
    if (baselines_.empty()) {
        initialize_default_baselines();
    }
}

PerformanceBaselines::~PerformanceBaselines() {
    std::lock_guard<std::mutex> lock(baselines_mutex_);
    save_baselines();
}

void PerformanceBaselines::initialize_default_baselines() {
    // Crypto libraries: 8 minutes target
    PerformanceBaseline crypto_baseline;
    crypto_baseline.library_type = LibraryType::CRYPTO;
    crypto_baseline.target_build_time = std::chrono::minutes(8);
    crypto_baseline.acceptable_variance = std::chrono::minutes(1);
    crypto_baseline.baseline_established = std::chrono::system_clock::now();
    crypto_baseline.sample_count = 0;
    crypto_baseline.is_healthy = true;

    // Utility libraries: 3 minutes target
    PerformanceBaseline utility_baseline;
    utility_baseline.library_type = LibraryType::UTILITY;
    utility_baseline.target_build_time = std::chrono::minutes(3);
    utility_baseline.acceptable_variance = std::chrono::seconds(30);
    utility_baseline.baseline_established = std::chrono::system_clock::now();
    utility_baseline.sample_count = 0;
    utility_baseline.is_healthy = true;

    // General libraries: 5 minutes target
    PerformanceBaseline general_baseline;
    general_baseline.library_type = LibraryType::GENERAL;
    general_baseline.target_build_time = std::chrono::minutes(5);
    general_baseline.acceptable_variance = std::chrono::seconds(45);
    general_baseline.baseline_established = std::chrono::system_clock::now();
    general_baseline.sample_count = 0;
    general_baseline.is_healthy = true;

    baselines_[LibraryType::CRYPTO] = crypto_baseline;
    baselines_[LibraryType::UTILITY] = utility_baseline;
    baselines_[LibraryType::GENERAL] = general_baseline;

    save_baselines();
}

std::string PerformanceBaselines::library_type_to_string(LibraryType type) const {
    switch (type) {
        case LibraryType::CRYPTO:  return "CRYPTO";
        case LibraryType::UTILITY: return "UTILITY";
        case LibraryType::GENERAL: return "GENERAL";
        default:                   return "UNKNOWN";
    }
}

PerformanceBaselines::LibraryType PerformanceBaselines::string_to_library_type(const std::string& type) const {
    if (type == "CRYPTO") return LibraryType::CRYPTO;
    if (type == "UTILITY") return LibraryType::UTILITY;
    if (type == "GENERAL") return LibraryType::GENERAL;
    return LibraryType::UNKNOWN;
}

PerformanceBaselines::LibraryType PerformanceBaselines::determine_library_type(
    const std::string& library_name,
    const std::vector<std::string>& source_files) const {

    std::string lower_name = library_name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);

    // Crypto libraries - look for cryptographic patterns
    std::vector<std::string> crypto_keywords = {
        "crypto", "secp256k1", "sha", "aes", "rsa", "ecdsa", "hash", "signature",
        "elliptic", "curve", "bitcoin", "ethereum", "blockchain", "key", "cipher"
    };

    for (const auto& keyword : crypto_keywords) {
        if (lower_name.find(keyword) != std::string::npos) {
            return LibraryType::CRYPTO;
        }
    }

    // Check source files for crypto patterns
    for (const auto& file : source_files) {
        std::string lower_file = file;
        std::transform(lower_file.begin(), lower_file.end(), lower_file.begin(), ::tolower);

        for (const auto& keyword : crypto_keywords) {
            if (lower_file.find(keyword) != std::string::npos) {
                return LibraryType::CRYPTO;
            }
        }
    }

    // Utility libraries - look for utility patterns
    std::vector<std::string> utility_keywords = {
        "util", "helper", "tool", "common", "base", "core", "infrastructure"
    };

    for (const auto& keyword : utility_keywords) {
        if (lower_name.find(keyword) != std::string::npos) {
            return LibraryType::UTILITY;
        }
    }

    // Default to general
    return LibraryType::GENERAL;
}

bool PerformanceBaselines::record_measurement(const std::string& library_name,
                                             const std::vector<std::string>& source_files,
                                             const PerformanceMetrics& metrics) {
    std::lock_guard<std::mutex> lock(baselines_mutex_);

    LibraryType library_type = determine_library_type(library_name, source_files);

    // Create a copy of metrics with correct library type
    PerformanceMetrics recorded_metrics = metrics;
    recorded_metrics.library_type = library_type;
    recorded_metrics.library_name = library_name;
    recorded_metrics.measured_at = std::chrono::system_clock::now();

    // Add to recent measurements
    recent_measurements_.push_back(recorded_metrics);

    // Update baseline
    update_baseline(library_type, recorded_metrics);

    // Check for regressions
    auto regressions = detect_regressions(recorded_metrics);

    // Save data
    return save_baselines();
}

PerformanceBaselines::PerformanceMetrics PerformanceBaselines::measure_library_performance(
    const std::string& library_name,
    const std::string& library_path,
    const std::string& build_command) {

    PerformanceMetrics metrics;
    metrics.library_name = library_name;
    metrics.library_type = determine_library_type(library_name, {});

    // Measure build time
    auto build_start = std::chrono::high_resolution_clock::now();

    // Execute build command (simplified - in real implementation would use proper process execution)
    int result = std::system(build_command.c_str());

    auto build_end = std::chrono::high_resolution_clock::now();
    metrics.build_time = std::chrono::duration_cast<std::chrono::milliseconds>(build_end - build_start);

    // Get memory usage (simplified)
    std::ifstream memory_file("/proc/self/status");
    if (memory_file.is_open()) {
        std::string line;
        while (std::getline(memory_file, line)) {
            if (line.find("VmRSS:") == 0) {
                std::istringstream iss(line);
                std::string label, value, unit;
                iss >> label >> value >> unit;
                metrics.memory_usage_mb = std::stoul(value) / 1024; // Convert KB to MB
                break;
            }
        }
    }

    // Get disk usage (simplified)
    if (std::filesystem::exists(library_path)) {
        size_t total_size = 0;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(library_path)) {
            if (entry.is_regular_file()) {
                total_size += entry.file_size();
            }
        }
        metrics.disk_usage_mb = total_size / (1024 * 1024); // Convert bytes to MB
    }

    metrics.measured_at = std::chrono::system_clock::now();
    metrics.build_configuration = "Release";

    return metrics;
}

bool PerformanceBaselines::establish_baseline(LibraryType library_type,
                                            const std::vector<PerformanceMetrics>& measurements) {
    if (measurements.empty()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(baselines_mutex_);

    auto& baseline = baselines_[library_type];
    baseline.library_type = library_type;
    baseline.historical_measurements = measurements;
    baseline.sample_count = measurements.size();
    baseline.baseline_established = std::chrono::system_clock::now();

    // Calculate average build time as baseline
    auto total_time = std::accumulate(measurements.begin(), measurements.end(),
                                     std::chrono::milliseconds(0),
                                     [](const auto& acc, const auto& metric) {
                                         return acc + metric.build_time;
                                     });
    baseline.baseline_build_time = total_time / measurements.size();

    // Calculate performance trend
    baseline.performance_trend = calculate_performance_trend(measurements);

    return save_baselines();
}

PerformanceBaselines::PerformanceBaseline PerformanceBaselines::get_baseline(LibraryType library_type) const {
    std::lock_guard<std::mutex> lock(baselines_mutex_);

    auto it = baselines_.find(library_type);
    if (it != baselines_.end()) {
        return it->second;
    }
    return PerformanceBaseline();
}

bool PerformanceBaselines::update_baseline(LibraryType library_type, const PerformanceMetrics& measurement) {
    std::lock_guard<std::mutex> lock(baselines_mutex_);

    auto& baseline = baselines_[library_type];
    baseline.historical_measurements.push_back(measurement);
    baseline.sample_count++;

    // Keep only last 100 measurements to prevent memory bloat
    if (baseline.historical_measurements.size() > 100) {
        baseline.historical_measurements.erase(baseline.historical_measurements.begin(),
                                              baseline.historical_measurements.begin() + 1);
    }

    // Update baseline if this is the first measurement or if we have enough samples
    if (baseline.sample_count == 1 || baseline.sample_count % 10 == 0) {
        auto total_time = std::accumulate(baseline.historical_measurements.begin(),
                                         baseline.historical_measurements.end(),
                                         std::chrono::milliseconds(0),
                                         [](const auto& acc, const auto& metric) {
                                             return acc + metric.build_time;
                                         });
        baseline.baseline_build_time = total_time / baseline.historical_measurements.size();
        baseline.performance_trend = calculate_performance_trend(baseline.historical_measurements);
    }

    return true;
}

double PerformanceBaselines::compare_to_baseline(const PerformanceMetrics& measurement) const {
    auto baseline = get_baseline(measurement.library_type);
    if (baseline.baseline_build_time.count() == 0) {
        return 0.0; // No baseline established
    }

    double baseline_ms = baseline.baseline_build_time.count();
    double current_ms = measurement.build_time.count();

    // Positive percentage = improvement (lower time)
    return ((baseline_ms - current_ms) / baseline_ms) * 100.0;
}

PerformanceBaselines::PerformanceAnalysis PerformanceBaselines::analyze_performance() const {
    std::lock_guard<std::mutex> lock(baselines_mutex_);

    PerformanceAnalysis analysis;
    analysis.analysis_time = std::chrono::system_clock::now();
    analysis.baselines = baselines_;

    // Check each library type for regressions
    for (const auto& [library_type, baseline] : baselines_) {
        if (!baseline.historical_measurements.empty()) {
            auto recent_regressions = check_regressions(library_type);
            analysis.regressions.insert(analysis.regressions.end(),
                                       recent_regressions.begin(),
                                       recent_regressions.end());
        }

        // Calculate performance improvements
        if (baseline.sample_count > 10) {
            double improvement = get_performance_improvement(library_type, 30);
            if (improvement > 0) {
                analysis.performance_improvements[library_type_to_string(library_type)] = improvement;
            }
        }
    }

    // Determine overall health
    analysis.overall_healthy = analysis.regressions.empty();

    // Generate summary
    std::stringstream ss;
    ss << "Performance Analysis completed. ";
    ss << "Analyzed " << baselines_.size() << " library types. ";
    ss << "Found " << analysis.regressions.size() << " regressions. ";
    ss << "Overall system health: " << (analysis.overall_healthy ? "HEALTHY" : "NEEDS ATTENTION");
    analysis.summary = ss.str();

    return analysis;
}

std::vector<PerformanceBaselines::RegressionAlert> PerformanceBaselines::check_regressions(
    LibraryType library_type) const {

    std::vector<RegressionAlert> alerts;

    if (library_type == LibraryType::UNKNOWN) {
        // Check all library types
        for (const auto& [type, baseline] : baselines_) {
            auto type_alerts = check_regressions(type);
            alerts.insert(alerts.end(), type_alerts.begin(), type_alerts.end());
        }
        return alerts;
    }

    auto baseline = get_baseline(library_type);
    if (baseline.historical_measurements.empty()) {
        return alerts;
    }

    // Get recent measurement
    const auto& recent = baseline.historical_measurements.back();
    double baseline_ms = baseline.baseline_build_time.count();
    double current_ms = recent.build_time.count();

    // Calculate regression percentage
    double regression_percentage = ((current_ms - baseline_ms) / baseline_ms) * 100.0;

    // Check if regression exceeds threshold
    if (regression_percentage > regression_threshold_.count() / 1000.0) {
        RegressionAlert alert;
        alert.alert_id = generate_alert_id();
        alert.library_type = library_type;
        alert.library_name = recent.library_name;
        alert.regression_percentage = regression_percentage;
        alert.current_time = recent.build_time;
        alert.baseline_time = baseline.baseline_build_time;
        alert.detected_at = std::chrono::system_clock::now();
        alert.description = "Performance regression detected for " + library_type_to_string(library_type) + " library";

        // Determine severity
        if (regression_percentage > 50.0) {
            alert.severity = "CRITICAL";
        } else if (regression_percentage > 25.0) {
            alert.severity = "HIGH";
        } else {
            alert.severity = "WARNING";
        }

        alerts.push_back(alert);
    }

    return alerts;
}

std::map<PerformanceBaselines::LibraryType, PerformanceBaselines::LibraryStats> PerformanceBaselines::get_statistics() const {
    std::lock_guard<std::mutex> lock(baselines_mutex_);

    std::map<LibraryType, LibraryStats> stats;

    for (const auto& [library_type, baseline] : baselines_) {
        LibraryStats type_stats;
        type_stats.library_type = library_type;
        type_stats.target_build_time = baseline.target_build_time;
        type_stats.sample_count = baseline.sample_count;

        if (!baseline.historical_measurements.empty()) {
            // Calculate statistics
            auto total_time = std::accumulate(baseline.historical_measurements.begin(),
                                             baseline.historical_measurements.end(),
                                             std::chrono::milliseconds(0),
                                             [](const auto& acc, const auto& metric) {
                                                 return acc + metric.build_time;
                                             });

            type_stats.average_build_time = total_time / baseline.historical_measurements.size();

            // Find min and max
            auto min_it = std::min_element(baseline.historical_measurements.begin(),
                                          baseline.historical_measurements.end(),
                                          [](const auto& a, const auto& b) {
                                              return a.build_time < b.build_time;
                                          });
            type_stats.min_build_time = min_it->build_time;

            auto max_it = std::max_element(baseline.historical_measurements.begin(),
                                          baseline.historical_measurements.end(),
                                          [](const auto& a, const auto& b) {
                                              return a.build_time < b.build_time;
                                          });
            type_stats.max_build_time = max_it->build_time;

            // Calculate performance score (0-100)
            double performance_ratio = (double)type_stats.target_build_time.count() / type_stats.average_build_time.count();
            type_stats.performance_score = std::min(100.0, std::max(0.0, performance_ratio * 100));

            // Check if target is met
            type_stats.meets_target = type_stats.average_build_time <= type_stats.target_build_time;
        }

        stats[library_type] = type_stats;
    }

    return stats;
}

std::string PerformanceBaselines::generate_report(const std::string& format, bool include_trends) const {
    auto stats = get_statistics();
    auto analysis = analyze_performance();

    if (format == "json") {
        json report;
        report["report_generated"] = "2025-10-10T00:00:00Z"; // Current timestamp
        report["include_trends"] = include_trends;
        report["overall_healthy"] = analysis.overall_healthy;
        report["summary"] = analysis.summary;

        report["library_statistics"] = json::object();
        for (const auto& [library_type, type_stats] : stats) {
            json stats_json;
            stats_json["library_type"] = library_type_to_string(library_type);
            stats_json["target_build_time_ms"] = type_stats.target_build_time.count();
            stats_json["average_build_time_ms"] = type_stats.average_build_time.count();
            stats_json["min_build_time_ms"] = type_stats.min_build_time.count();
            stats_json["max_build_time_ms"] = type_stats.max_build_time.count();
            stats_json["performance_score"] = type_stats.performance_score;
            stats_json["sample_count"] = type_stats.sample_count;
            stats_json["meets_target"] = type_stats.meets_target;

            report["library_statistics"][library_type_to_string(library_type)] = stats_json;
        }

        if (include_trends) {
            report["performance_improvements"] = json::object();
            for (const auto& [type, improvement] : analysis.performance_improvements) {
                report["performance_improvements"][type] = improvement;
            }
        }

        report["regressions"] = json::array();
        for (const auto& alert : analysis.regressions) {
            json alert_json;
            alert_json["alert_id"] = alert.alert_id;
            alert_json["library_type"] = library_type_to_string(alert.library_type);
            alert_json["library_name"] = alert.library_name;
            alert_json["regression_percentage"] = alert.regression_percentage;
            alert_json["current_time_ms"] = alert.current_time.count();
            alert_json["baseline_time_ms"] = alert.baseline_time.count();
            alert_json["severity"] = alert.severity;
            alert_json["description"] = alert.description;

            report["regressions"].push_back(alert_json);
        }

        return report.dump(4);
    } else {
        // Text format
        std::stringstream ss;
        ss << "Performance Baselines Report\n";
        ss << "===========================\n\n";
        ss << "Generated: 2025-10-10T00:00:00Z\n";
        ss << "Overall Health: " << (analysis.overall_healthy ? "HEALTHY" : "NEEDS ATTENTION") << "\n";
        ss << "Summary: " << analysis.summary << "\n\n";

        ss << "Library Statistics:\n";
        for (const auto& [library_type, type_stats] : stats) {
            ss << "  " << library_type_to_string(library_type) << ":\n";
            ss << "    Target: " << type_stats.target_build_time.count() / 1000.0 << " seconds\n";
            ss << "    Average: " << type_stats.average_build_time.count() / 1000.0 << " seconds\n";
            ss << "    Performance Score: " << std::fixed << std::setprecision(1) << type_stats.performance_score << "/100\n";
            ss << "    Meets Target: " << (type_stats.meets_target ? "YES" : "NO") << "\n";
            ss << "    Sample Count: " << type_stats.sample_count << "\n\n";
        }

        if (!analysis.regressions.empty()) {
            ss << "Regressions Detected:\n";
            for (const auto& alert : analysis.regressions) {
                ss << "  " << alert.alert_id << " - " << alert.severity << "\n";
                ss << "    Library: " << library_type_to_string(alert.library_type) << " (" << alert.library_name << ")\n";
                ss << "    Regression: " << std::fixed << std::setprecision(1) << alert.regression_percentage << "%\n";
                ss << "    Description: " << alert.description << "\n\n";
            }
        }

        return ss.str();
    }
}

// Private methods implementation
double PerformanceBaselines::calculate_performance_trend(const std::vector<PerformanceMetrics>& measurements) const {
    if (measurements.size() < 2) {
        return 0.0;
    }

    // Simple linear regression to calculate trend
    double n = measurements.size();
    double sum_x = 0, sum_y = 0, sum_xy = 0, sum_x2 = 0;

    for (size_t i = 0; i < measurements.size(); ++i) {
        double x = static_cast<double>(i);
        double y = static_cast<double>(measurements[i].build_time.count());

        sum_x += x;
        sum_y += y;
        sum_xy += x * y;
        sum_x2 += x * x;
    }

    double slope = (n * sum_xy - sum_x * sum_y) / (n * sum_x2 - sum_x * sum_x);

    // Convert slope to percentage change per measurement
    if (!measurements.empty()) {
        double avg_time = sum_y / n;
        return -(slope / avg_time) * 100.0; // Negative slope = improvement
    }

    return 0.0;
}

std::vector<PerformanceBaselines::RegressionAlert> PerformanceBaselines::detect_regressions(
    const PerformanceMetrics& current) const {

    std::vector<RegressionAlert> alerts;
    auto baseline = get_baseline(current.library_type);

    if (baseline.baseline_build_time.count() == 0) {
        return alerts; // No baseline established
    }

    double baseline_ms = baseline.baseline_build_time.count();
    double current_ms = current.build_time.count();
    double regression_percentage = ((current_ms - baseline_ms) / baseline_ms) * 100.0;

    if (regression_percentage > regression_threshold_.count() / 1000.0) {
        RegressionAlert alert;
        alert.alert_id = generate_alert_id();
        alert.library_type = current.library_type;
        alert.library_name = current.library_name;
        alert.regression_percentage = regression_percentage;
        alert.current_time = current.build_time;
        alert.baseline_time = baseline.baseline_build_time;
        alert.detected_at = std::chrono::system_clock::now();
        alert.description = "Performance regression detected";

        if (regression_percentage > 50.0) {
            alert.severity = "CRITICAL";
        } else if (regression_percentage > 25.0) {
            alert.severity = "HIGH";
        } else {
            alert.severity = "WARNING";
        }

        alerts.push_back(alert);
    }

    return alerts;
}

bool PerformanceBaselines::save_baselines() const {
    std::ofstream file(storage_path_ + "/performance_baselines.json");
    if (!file.is_open()) {
        return false;
    }

    json baselines_data;
    baselines_data["regression_threshold_percentage"] = regression_threshold_.count() / 1000.0;
    baselines_data["baselines"] = json::object();

    for (const auto& [library_type, baseline] : baselines_) {
        json baseline_json;
        baseline_json["library_type"] = library_type_to_string(library_type);
        baseline_json["target_build_time_ms"] = baseline.target_build_time.count();
        baseline_json["acceptable_variance_ms"] = baseline.acceptable_variance.count();
        baseline_json["baseline_build_time_ms"] = baseline.baseline_build_time.count();
        baseline_json["baseline_established"] = "2025-10-10T00:00:00Z"; // Current timestamp
        baseline_json["sample_count"] = baseline.sample_count;
        baseline_json["performance_trend"] = baseline.performance_trend;
        baseline_json["is_healthy"] = baseline.is_healthy;

        baseline_json["historical_measurements"] = json::array();
        for (const auto& measurement : baseline.historical_measurements) {
            json measurement_json;
            measurement_json["library_name"] = measurement.library_name;
            measurement_json["build_time_ms"] = measurement.build_time.count();
            measurement_json["integration_time_ms"] = measurement.integration_time.count();
            measurement_json["test_time_ms"] = measurement.test_time.count();
            measurement_json["memory_usage_mb"] = measurement.memory_usage_mb;
            measurement_json["disk_usage_mb"] = measurement.disk_usage_mb;
            measurement_json["cpu_utilization_percent"] = measurement.cpu_utilization_percent;
            measurement_json["measured_at"] = "2025-10-10T00:00:00Z"; // Current timestamp
            measurement_json["build_configuration"] = measurement.build_configuration;
            measurement_json["custom_metrics"] = measurement.custom_metrics;

            baseline_json["historical_measurements"].push_back(measurement_json);
        }

        baselines_data["baselines"][library_type_to_string(library_type)] = baseline_json;
    }

    file << std::setw(4) << baselines_data << std::endl;
    return true;
}

bool PerformanceBaselines::load_baselines() {
    std::ifstream file(storage_path_ + "/performance_baselines.json");
    if (!file.is_open()) {
        return false;
    }

    try {
        json baselines_data;
        file >> baselines_data;

        regression_threshold_ = std::chrono::milliseconds(
            static_cast<long long>(baselines_data.value("regression_threshold_percentage", 15.0) * 1000));

        for (auto& [type_str, baseline_json] : baselines_data["baselines"].items()) {
            LibraryType library_type = string_to_library_type(type_str);

            PerformanceBaseline baseline;
            baseline.library_type = library_type;
            baseline.target_build_time = std::chrono::milliseconds(
                baseline_json.value("target_build_time_ms", 300000));
            baseline.acceptable_variance = std::chrono::milliseconds(
                baseline_json.value("acceptable_variance_ms", 60000));
            baseline.baseline_build_time = std::chrono::milliseconds(
                baseline_json.value("baseline_build_time_ms", 0));
            baseline.baseline_established = std::chrono::system_clock::now(); // Parse timestamp in real implementation
            baseline.sample_count = baseline_json.value("sample_count", 0);
            baseline.performance_trend = baseline_json.value("performance_trend", 0.0);
            baseline.is_healthy = baseline_json.value("is_healthy", true);

            for (auto& measurement_json : baseline_json["historical_measurements"]) {
                PerformanceMetrics measurement;
                measurement.library_type = library_type;
                measurement.library_name = measurement_json.value("library_name", "");
                measurement.build_time = std::chrono::milliseconds(
                    measurement_json.value("build_time_ms", 0));
                measurement.integration_time = std::chrono::milliseconds(
                    measurement_json.value("integration_time_ms", 0));
                measurement.test_time = std::chrono::milliseconds(
                    measurement_json.value("test_time_ms", 0));
                measurement.memory_usage_mb = measurement_json.value("memory_usage_mb", 0);
                measurement.disk_usage_mb = measurement_json.value("disk_usage_mb", 0);
                measurement.cpu_utilization_percent = measurement_json.value("cpu_utilization_percent", 0.0);
                measurement.measured_at = std::chrono::system_clock::now(); // Parse timestamp in real implementation
                measurement.build_configuration = measurement_json.value("build_configuration", "");
                measurement.custom_metrics = measurement_json.value("custom_metrics", std::map<std::string, double>{});

                baseline.historical_measurements.push_back(measurement);
            }

            baselines_[library_type] = baseline;
        }

        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

std::string PerformanceBaselines::generate_alert_id() const {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(100000, 999999);

    return "alert_" + std::to_string(dis(gen));
}

double PerformanceBaselines::get_performance_improvement(LibraryType library_type, size_t period_days) const {
    auto baseline = get_baseline(library_type);
    if (baseline.historical_measurements.size() < 2) {
        return 0.0;
    }

    // Get current time
    auto now = std::chrono::system_clock::now();
    auto period_start = now - std::chrono::hours(24 * period_days);

    // Find measurements in the period
    std::vector<PerformanceMetrics> period_measurements;
    for (const auto& measurement : baseline.historical_measurements) {
        if (measurement.measured_at >= period_start) {
            period_measurements.push_back(measurement);
        }
    }

    if (period_measurements.empty()) {
        return 0.0;
    }

    // Calculate average for recent period
    auto recent_total = std::accumulate(period_measurements.begin(), period_measurements.end(),
                                       std::chrono::milliseconds(0),
                                       [](const auto& acc, const auto& metric) {
                                           return acc + metric.build_time;
                                       });
    auto recent_average = recent_total / period_measurements.size();

    // Calculate improvement percentage
    if (baseline.baseline_build_time.count() > 0) {
        double baseline_ms = baseline.baseline_build_time.count();
        double recent_ms = recent_average.count();
        return ((baseline_ms - recent_ms) / baseline_ms) * 100.0;
    }

    return 0.0;
}

// PerformanceMeasurementSession implementation
PerformanceMeasurementSession::PerformanceMeasurementSession(
    PerformanceBaselines& baselines,
    const std::string& library_name,
    const std::vector<std::string>& source_files)
    : baselines_(baselines), library_name_(library_name), source_files_(source_files),
      session_active_(true) {
    start_time_ = std::chrono::steady_clock::now();
}

PerformanceMeasurementSession::~PerformanceMeasurementSession() {
    if (session_active_) {
        // Auto-complete with estimated times
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time_);
        complete(duration, duration / 2); // Estimate integration time as half of total
    }
}

bool PerformanceMeasurementSession::complete(std::chrono::milliseconds build_time,
                                            std::chrono::milliseconds integration_time,
                                            const std::map<std::string, double>& additional_metrics) {
    if (!session_active_) {
        return false;
    }

    PerformanceBaselines::PerformanceMetrics metrics;
    metrics.library_name = library_name_;
    metrics.build_time = build_time;
    metrics.integration_time = integration_time;
    metrics.measured_at = std::chrono::system_clock::now();
    metrics.custom_metrics = additional_metrics;

    bool success = baselines_.record_measurement(library_name_, source_files_, metrics);
    session_active_ = false;

    return success;
}