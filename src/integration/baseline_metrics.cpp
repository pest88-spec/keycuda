/**
 * Baseline Integration Measurement Framework Implementation
 *
 * @origin       https://github.com/Puzzle71Solver/Puzzle71Solver
 * @origin_path  src/integration/baseline_metrics.cpp
 * @origin_commit <current_commit>
 * @origin_license MIT
 * @extracted_date   2025-10-10
 * @extracted_by     Puzzle71Solver Team
 * @modifications    Created for third-party dependency integration optimization
 * @spdx_license_identifier MIT
 */

#include "baseline_metrics.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <random>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

BaselineMetrics::BaselineMetrics(const std::string& storage_path)
    : storage_path_(storage_path),
    current_session_id_() {

    // Create storage directory if it doesn't exist
    if (!storage_path_.empty() && !std::filesystem::exists(storage_path_)) {
        std::filesystem::create_directories(storage_path_);
    }

    // Load existing data
    load_metrics();
    load_baselines();
}

BaselineMetrics::~BaselineMetrics() {
    // End any active sessions
    for (auto& [session_id, session] : active_sessions_) {
        if (session.end_time.time_since_epoch().count() == 0) {
            end_session(session_id);
        }
    }

    // Save data
    save_metrics();
    save_baselines();
}

std::string BaselineMetrics::generate_session_id() const {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(100000, 999999);

    return "session_" + std::to_string(dis(gen));
}

std::string BaselineMetrics::start_session(
    const std::string& session_type,
    const std::map<std::string, std::string>& tags
) {
    std::string session_id = generate_session_id();

    CollectionSession session;
    session.session_id = session_id;
    session.session_type = session_type;
    session.session_tags = tags;
    session.start_time = std::chrono::system_clock::now();

    active_sessions_[session_id] = session;
    current_session_id_ = session_id;

    return session_id;
}

void BaselineMetrics::end_session(const std::string& session_id) {
    auto it = active_sessions_.find(session_id);
    if (it != active_sessions_.end()) {
        it->second.end_time = std::chrono::system_clock::now();
        current_session_id_.clear();
    }
}

void BaselineMetrics::record_metric(
    MetricCategory category,
    const std::string& metric_name,
    double value,
    const std::string& unit,
    const std::map<std::string, std::string>& tags,
    const std::string& session_id
) {
    MetricData metric;
    metric.timestamp = format_timestamp();
    metric.category = category_to_string(category);
    metric.metric_name = metric_name;
    metric.value = value;
    metric.unit = unit;
    metric.tags = tags;

    metrics_history_.push_back(metric);

    // Add to active session if provided or current session
    std::string target_session_id = session_id.empty() ? current_session_id_ : session_id;
    if (!target_session_id.empty()) {
        auto it = active_sessions_.find(target_session_id);
        if (it != active_sessions_.end()) {
            it->second.metrics.push_back(metric);
        }
    }
}

bool BaselineMetrics::establish_baseline(
    const std::string& metric_name,
    const std::string& description,
    size_t sample_count,
    const std::map<std::string, std::string>& tags
) {
    // Collect recent samples for the metric
    std::vector<double> recent_values;

    for (const auto& metric : metrics_history_) {
        if (metric.metric_name == metric_name) {
            recent_values.push_back(metric.value);
            if (recent_values.size() >= sample_count) {
                break;
            }
        }
    }

    if (recent_values.size() < sample_count) {
        return false; // Not enough samples
    }

    // Calculate baseline as median of recent values
    std::sort(recent_values.begin(), recent_values.end());
    double baseline_value = recent_values[recent_values.size() / 2];

    Baseline baseline;
    baseline.metric_name = metric_name;
    baseline.baseline_value = baseline_value;
    baseline.unit = "seconds"; // Default unit
    baseline.established_timestamp = format_timestamp();
    baseline.description = description;
    baseline.sample_count = recent_values.size();

    baselines_[metric_name] = baseline;
    save_baselines();

    return true;
}

std::string BaselineMetrics::category_to_string(MetricCategory category) const {
    switch (category) {
        case MetricCategory::BUILD_PERFORMANCE:    return "BUILD_PERFORMANCE";
        case MetricCategory::INTEGRATION_OPERATIONS: return "INTEGRATION_OPERATIONS";
        case MetricCategory::RESOURCE_USAGE:       return "RESOURCE_USAGE";
        case MetricCategory::SUCCESS_RATES:        return "SUCCESS_RATES";
        case MetricCategory::COMPLIANCE_METRICS:   return "COMPLIANCE_METRICS";
        default:                                   return "UNKNOWN_CATEGORY";
    }
}

std::string BaselineMetrics::format_timestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);

    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

BaselineMetrics::Baseline BaselineMetrics::get_baseline(const std::string& metric_name) const {
    auto it = baselines_.find(metric_name);
    if (it != baselines_.end()) {
        return it->second;
    }
    return Baseline(); // Return empty baseline if not found
}

double BaselineMetrics::compare_to_baseline(const std::string& metric_name, double current_value) const {
    Baseline baseline = get_baseline(metric_name);
    if (baseline.metric_name.empty()) {
        return 0.0; // No baseline established
    }
    return current_value - baseline.baseline_value;
}

BaselineMetrics::Statistics BaselineMetrics::calculate_statistics(const std::vector<double>& values) const {
    Statistics stats;

    if (values.empty()) {
        return stats;
    }

    stats.count = values.size();
    stats.min_value = values[0];
    stats.max_value = values[0];
    stats.mean = 0.0;

    // Calculate min, max, and mean
    for (double value : values) {
        stats.min_value = std::min(stats.min_value, value);
        stats.max_value = std::max(stats.max_value, value);
        stats.mean += value;
    }
    stats.mean /= stats.count;

    // Calculate median
    std::vector<double> sorted_values = values;
    std::sort(sorted_values.begin(), sorted_values.end());

    if (sorted_values.size() % 2 == 0) {
        stats.median = (sorted_values[sorted_values.size()/2 - 1] + sorted_values[sorted_values.size()/2]) / 2.0;
    } else {
        stats.median = sorted_values[sorted_values.size()/2];
    }

    // Calculate standard deviation
    for (double value : values) {
        stats.std_deviation += (value - stats.mean) * (value - stats.mean);
    }
    stats.std_deviation = std::sqrt(stats.std_deviation / stats.count);

    return stats;
}

void BaselineMetrics::load_metrics() {
    if (storage_path_.empty()) {
        return;
    }

    std::ifstream file(storage_path_ + "/metrics.json");
    if (!file.is_open()) {
        return;
    }

    try {
        json data;
        file >> data;

        metrics_history_.clear();
        for (const auto& metric_json : data["metrics"]) {
            MetricData metric;
            metric.timestamp = metric_json["timestamp"];
            metric.category = metric_json["category"];
            metric.metric_name = metric_json["metric_name"];
            metric.value = metric_json["value"];
            metric.unit = metric_json["unit"];

            if (metric_json.contains("tags")) {
                for (auto& [key, value] : metric_json["tags"].items()) {
                    metric.tags[key] = value.get<std::string>();
                }
            }

            metrics_history_.push_back(metric);
        }
    } catch (const std::exception& e) {
        // Handle parsing errors silently
    }
}

void BaselineMetrics::save_metrics() const {
    if (storage_path_.empty()) {
        return;
    }

    std::ofstream file(storage_path_ + "/metrics.json");
    if (!file.is_open()) {
        return;
    }

    json data;
    data["metrics"] = json::array();

    for (const auto& metric : metrics_history_) {
        json metric_json;
        metric_json["timestamp"] = metric.timestamp;
        metric_json["category"] = metric.category;
        metric_json["metric_name"] = metric.metric_name;
        metric_json["value"] = metric.value;
        metric_json["unit"] = metric.unit;

        json tags_json;
        for (const auto& [key, value] : metric.tags) {
            tags_json[key] = value;
        }
        metric_json["tags"] = tags_json;

        data["metrics"].push_back(metric_json);
    }

    file << std::setw(4) << data << std::endl;
}

void BaselineMetrics::load_baselines() {
    if (storage_path_.empty()) {
        return;
    }

    std::ifstream file(storage_path_ + "/baselines.json");
    if (!file.is_open()) {
        return;
    }

    try {
        json data;
        file >> data;

        baselines_.clear();
        for (auto& [metric_name, baseline_json] : data.items()) {
            Baseline baseline;
            baseline.metric_name = baseline_json["metric_name"];
            baseline.baseline_value = baseline_json["baseline_value"];
            baseline.unit = baseline_json["unit"];
            baseline.established_timestamp = baseline_json["established_timestamp"];
            baseline.description = baseline_json["description"];
            baseline.sample_count = baseline_json["sample_count"];

            baselines_[metric_name] = baseline;
        }
    } catch (const std::exception& e) {
        // Handle parsing errors silently
    }
}

void BaselineMetrics::save_baselines() const {
    if (storage_path_.empty()) {
        return;
    }

    std::ofstream file(storage_path_ + "/baselines.json");
    if (!file.is_open()) {
        return;
    }

    json data;

    for (const auto& [metric_name, baseline] : baselines_) {
        json baseline_json;
        baseline_json["metric_name"] = baseline.metric_name;
        baseline_json["baseline_value"] = baseline.baseline_value;
        baseline_json["unit"] = baseline.unit;
        baseline_json["established_timestamp"] = baseline.established_timestamp;
        baseline_json["description"] = baseline.description;
        baseline_json["sample_count"] = baseline.sample_count;

        data[metric_name] = baseline_json;
    }

    file << std::setw(4) << data << std::endl;
}

BaselineMetrics::Statistics BaselineMetrics::get_statistics(
    const std::string& metric_name,
    const std::map<std::string, std::string>& tags
) const {
    std::vector<double> values;

    for (const auto& metric : metrics_history_) {
        if (metric.metric_name == metric_name) {
            // Check if tags match (if specified)
            bool tags_match = true;
            for (const auto& [key, value] : tags) {
                auto it = metric.tags.find(key);
                if (it == metric.tags.end() || it->second != value) {
                    tags_match = false;
                    break;
                }
            }

            if (tags_match) {
                values.push_back(metric.value);
            }
        }
    }

    return calculate_statistics(values);
}

std::vector<BaselineMetrics::MetricData> BaselineMetrics::get_metrics(
    const std::chrono::system_clock::time_point* start_time,
    const std::chrono::system_clock::time_point* end_time,
    const std::map<std::string, std::string>& tags
) const {
    std::vector<MetricData> filtered_metrics;

    for (const auto& metric : metrics_history_) {
        // Time filtering
        if (start_time || end_time) {
            // Parse timestamp to time_point for comparison
            std::tm tm = {};
            std::istringstream ss(metric.timestamp);
            ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
            auto metric_time = std::chrono::system_clock::from_time_t(std::mktime(&tm));

            if (start_time && metric_time < *start_time) {
                continue;
            }
            if (end_time && metric_time > *end_time) {
                continue;
            }
        }

        // Tag filtering
        bool tags_match = true;
        for (const auto& [key, value] : tags) {
            auto it = metric.tags.find(key);
            if (it == metric.tags.end() || it->second != value) {
                tags_match = false;
                break;
            }
        }

        if (tags_match) {
            filtered_metrics.push_back(metric);
        }
    }

    return filtered_metrics;
}

std::map<std::string, BaselineMetrics::Baseline> BaselineMetrics::get_all_baselines() const {
    return baselines_;
}

std::string BaselineMetrics::generate_report(
    const std::chrono::system_clock::time_point& start_time,
    const std::chrono::system_clock::time_point& end_time,
    const std::string& format
) const {
    json report;
    report["report_generated"] = format_timestamp();
    report["period_start"] = format_timestamp(); // Would need conversion from time_point
    report["period_end"] = format_timestamp();   // Would need conversion from time_point

    report["baselines"] = json::object();
    for (const auto& [metric_name, baseline] : baselines_) {
        json baseline_json;
        baseline_json["baseline_value"] = baseline.baseline_value;
        baseline_json["unit"] = baseline.unit;
        baseline_json["established_timestamp"] = baseline.established_timestamp;
        baseline_json["description"] = baseline.description;
        baseline_json["sample_count"] = baseline.sample_count;

        report["baselines"][metric_name] = baseline_json;
    }

    report["summary"] = json::object();
    report["summary"]["total_baselines"] = baselines_.size();
    report["summary"]["total_metrics"] = metrics_history_.size();

    if (format == "json") {
        return report.dump(4);
    } else {
        // Text format
        std::stringstream ss;
        ss << "Baseline Metrics Report\n";
        ss << "Generated: " << report["report_generated"] << "\n\n";

        ss << "Established Baselines:\n";
        for (const auto& [metric_name, baseline] : baselines_) {
            ss << "  " << metric_name << ": " << baseline.baseline_value
               << " " << baseline.unit << " (" << baseline.sample_count << " samples)\n";
        }

        return ss.str();
    }
}

bool BaselineMetrics::export_to_csv(
    const std::string& file_path,
    const std::chrono::system_clock::time_point& start_time,
    const std::chrono::system_clock::time_point& end_time
) const {
    std::ofstream file(file_path);
    if (!file.is_open()) {
        return false;
    }

    // CSV header
    file << "timestamp,category,metric_name,value,unit,tags\n";

    auto metrics = get_metrics(&start_time, &end_time);
    for (const auto& metric : metrics) {
        file << metric.timestamp << ","
             << metric.category << ","
             << metric.metric_name << ","
             << metric.value << ","
             << metric.unit << ",";

        // Convert tags to JSON string
        json tags_json;
        for (const auto& [key, value] : metric.tags) {
            tags_json[key] = value;
        }
        file << tags_json.dump() << "\n";
    }

    return true;
}

void BaselineMetrics::clear_all() {
    metrics_history_.clear();
    baselines_.clear();
    active_sessions_.clear();
    current_session_id_.clear();
}

void BaselineMetrics::clear_metrics_older_than(const std::chrono::system_clock::time_point& older_than) {
    auto it = std::remove_if(metrics_history_.begin(), metrics_history_.end(),
        [older_than](const MetricData& metric) {
            // Parse timestamp to time_point for comparison
            std::tm tm = {};
            std::istringstream ss(metric.timestamp);
            ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
            auto metric_time = std::chrono::system_clock::from_time_t(std::mktime(&tm));

            return metric_time < older_than;
        });

    metrics_history_.erase(it, metrics_history_.end());
}

std::map<std::string, BaselineMetrics::CollectionSession> BaselineMetrics::get_active_sessions() const {
    return active_sessions_;
}

double BaselineMetrics::get_performance_trend(const std::string& metric_name, size_t period_days) const {
    auto now = std::chrono::system_clock::now();
    auto period_start = now - std::chrono::hours(24 * period_days);

    std::vector<double> recent_values;
    std::vector<double> older_values;

    for (const auto& metric : metrics_history_) {
        if (metric.metric_name == metric_name) {
            // Parse timestamp to time_point for comparison
            std::tm tm = {};
            std::istringstream ss(metric.timestamp);
            ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
            auto metric_time = std::chrono::system_clock::from_time_t(std::mktime(&tm));

            if (metric_time >= period_start) {
                recent_values.push_back(metric.value);
            } else {
                older_values.push_back(metric.value);
            }
        }
    }

    if (recent_values.empty() || older_values.empty()) {
        return 0.0; // Insufficient data
    }

    double recent_avg = calculate_statistics(recent_values).mean;
    double older_avg = calculate_statistics(older_values).mean;

    if (older_avg == 0.0) {
        return 0.0;
    }

    // Positive trend = improvement (lower values for timing metrics)
    return (older_avg - recent_avg) / older_avg;
}

// MetricTimer implementation
MetricTimer::MetricTimer(
    BaselineMetrics& metrics,
    BaselineMetrics::MetricCategory category,
    const std::string& metric_name,
    const std::string& unit,
    const std::map<std::string, std::string>& tags,
    const std::string& session_id
) : metrics_(metrics),
    category_(category),
    metric_name_(metric_name),
    unit_(unit),
    tags_(tags),
    session_id_(session_id),
    start_time_(std::chrono::steady_clock::now()),
    completed_(false) {
}

MetricTimer::~MetricTimer() {
    if (!completed_) {
        complete();
    }
}

void MetricTimer::complete(const std::map<std::string, std::string>& additional_tags) {
    if (completed_) {
        return;
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::duration<double>>(end_time - start_time_);

    std::map<std::string, std::string> final_tags = tags_;
    for (const auto& [key, value] : additional_tags) {
        final_tags[key] = value;
    }

    metrics_.record_metric(category_, metric_name_, duration.count(), unit_, final_tags, session_id_);
    completed_ = true;
}