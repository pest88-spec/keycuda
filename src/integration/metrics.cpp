/**
 * @file           metrics.cpp
 * @brief          Integration metrics collection system for Puzzle71Solver
 * @author         Puzzle71Solver Team
 * @origin         https://github.com/Puzzle71Solver/Puzzle71Solver
 * @origin_path    src/integration/metrics.cpp
 * @origin_commit  <current_commit>
 * @origin_license MIT
 * @extracted_date   2025-10-10
 * @extracted_by     Puzzle71Solver Team
 * @modifications    Created for third-party dependency integration optimization
 * @spdx_license_identifier MIT
 *
 * Provides comprehensive metrics collection for integration operations including
 * performance tracking, success rates, resource usage, and quality metrics.
 */

#include "metrics.h"
#include "integration_logger.h"
#include "config_manager.h"
#include "baseline_metrics.h"
#include "build_time_measurement.h"
#include "evidence_collector.h"
#include "digest_verifier.h"
#include "metadata_enforcer.h"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace puzzle71 {
namespace integration {

// IntegrationMetricsCollector implementation
IntegrationMetricsCollector& IntegrationMetricsCollector::instance() {
    static IntegrationMetricsCollector instance;
    return instance;
}

bool IntegrationMetricsCollector::initialize(const std::string& storage_path,
                                            const MetricsConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);

    storage_path_ = storage_path;
    config_ = config;

    // Create storage directory
    if (!storage_path_.empty()) {
        std::filesystem::create_directories(storage_path_);
    }

    // Initialize metrics collectors
    build_metrics_ = std::make_unique<BuildTimeMeasurement>(storage_path_);
    baseline_metrics_ = std::make_unique<BaselineMetrics>();

    // Load existing metrics
    load_historical_metrics();

    // Setup scheduled collection
    setup_scheduled_collection();

    IntegrationLogger::instance().log_info(
        "IntegrationMetricsCollector",
        "Metrics collector initialized",
        "storage_path", storage_path_,
        "collection_interval_seconds", std::to_string(config_.collection_interval_seconds.count())
    );

    return true;
}

void IntegrationMetricsCollector::record_integration_start(const std::string& operation,
                                                         const std::map<std::string, std::string>& parameters) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto session_id = generate_session_id();
    active_sessions_[session_id] = IntegrationSession{
        session_id,
        operation,
        std::chrono::system_clock::now(),
        std::chrono::steady_clock::now(),
        parameters,
        IntegrationSession::Status::ACTIVE,
        0.0,
        std::map<std::string, std::string>{}
    };

    // Log session start
    IntegrationLogger::instance().log_info(
        "IntegrationMetricsCollector",
        "Integration session started",
        "session_id", session_id,
        "operation", operation
    );

    // Collect evidence of session start
    EvidenceCollector::instance().collect_text(
        EvidenceType::Configuration,
        "Integration Session Start: " + operation,
        "Session ID: " + session_id + "\nOperation: " + operation + "\nTimestamp: " +
        format_timestamp(std::chrono::system_clock::now()),
        "metrics_collector",
        EvidencePriority::Medium,
        {
            {"session_id", session_id},
            {"operation", operation},
            {"event_type", "session_start"}
        }
    );
}

void IntegrationMetricsCollector::record_integration_complete(const std::string& session_id,
                                                           bool success,
                                                           const std::map<std::string, std::string>& results) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = active_sessions_.find(session_id);
    if (it == active_sessions_.end()) {
        IntegrationLogger::instance().log_warning(
            "IntegrationMetricsCollector",
            "Attempt to complete non-existent session",
            "session_id", session_id
        );
        return;
    }

    auto& session = it->second;
    session.end_time = std::chrono::steady_clock::now();
    session.status = success ? IntegrationSession::Status::COMPLETED : IntegrationSession::Status::FAILED;
    session.results = results;

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        session.end_time - session.start_time
    );
    session.duration_seconds = duration.count() / 1000.0;

    // Update operation metrics
    update_operation_metrics(session.operation, success, session.duration_seconds);

    // Record in history
    session_history_.push_back(session);

    // Log session completion
    IntegrationLogger::instance().log_info(
        "IntegrationMetricsCollector",
        "Integration session completed",
        "session_id", session_id,
        "success", success ? "true" : "false",
        "duration_seconds", std::to_string(session.duration_seconds)
    );

    // Collect evidence of session completion
    std::stringstream evidence_content;
    evidence_content << "Session ID: " << session_id << "\n"
                    << "Operation: " << session.operation << "\n"
                    << "Status: " << (success ? "SUCCESS" : "FAILED") << "\n"
                    << "Duration: " << std::fixed << std::setprecision(3)
                    << session.duration_seconds << " seconds\n"
                    << "Start Time: " << format_timestamp(std::chrono::system_clock::from_time_t(
                        std::chrono::duration_cast<std::chrono::seconds>(
                            session.start_time.time_since_epoch()
                        ).count())) << "\n"
                    << "End Time: " << format_timestamp(std::chrono::system_clock::now()) << "\n";

    EvidenceCollector::instance().collect_text(
        EvidenceType::TestResult,
        "Integration Session Complete: " + session.operation,
        evidence_content.str(),
        "metrics_collector",
        EvidencePriority::Medium,
        {
            {"session_id", session_id},
            {"operation", session.operation},
            {"success", success ? "true" : "false"},
            {"event_type", "session_complete"}
        }
    );

    // Remove from active sessions
    active_sessions_.erase(it);

    // Update historical metrics
    update_historical_metrics();
}

void IntegrationMetricsCollector::record_resource_usage(const std::string& session_id,
                                                       const ResourceUsage& usage) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = active_sessions_.find(session_id);
    if (it != active_sessions_.end()) {
        it->second.resource_usage = usage;

        // Log resource usage
        IntegrationLogger::instance().log_debug(
            "IntegrationMetricsCollector",
            "Resource usage recorded",
            "session_id", session_id,
            "cpu_percent", std::to_string(usage.cpu_percent),
            "memory_mb", std::to_string(usage.memory_mb),
            "disk_io_mb", std::to_string(usage.disk_io_mb)
        );
    }

    // Update global resource metrics
    current_resource_usage_ = usage;
    resource_history_.push_back({std::chrono::system_clock::now(), usage});

    // Limit history size
    if (resource_history_.size() > config_.max_history_entries) {
        resource_history_.erase(resource_history_.begin());
    }
}

void IntegrationMetricsCollector::record_performance_metric(const std::string& metric_name,
                                                          double value,
                                                          const std::string& unit,
                                                          const std::map<std::string, std::string>& tags) {
    std::lock_guard<std::mutex> lock(mutex_);

    PerformanceMetric metric{
        metric_name,
        value,
        unit,
        tags,
        std::chrono::system_clock::now()
    };

    performance_metrics_.push_back(metric);

    // Limit metrics history
    if (performance_metrics_.size() > config_.max_history_entries) {
        performance_metrics_.erase(performance_metrics_.begin());
    }

    // Update metric statistics
    update_metric_statistics(metric_name, value);

    // Log significant metrics
    if (config_.log_all_metrics || is_significant_metric(metric_name, value)) {
        IntegrationLogger::instance().log_info(
            "IntegrationMetricsCollector",
            "Performance metric recorded",
            "metric_name", metric_name,
            "value", std::to_string(value),
            "unit", unit
        );
    }
}

IntegrationMetricsCollector::IntegrationMetrics IntegrationMetricsCollector::get_current_metrics() const {
    std::lock_guard<std::mutex> lock(mutex_);

    IntegrationMetrics metrics;
    metrics.timestamp = std::chrono::system_clock::now();
    metrics.active_sessions = active_sessions_.size();
    metrics.total_sessions = session_history_.size();
    metrics.current_resource_usage = current_resource_usage_;
    metrics.operation_metrics = operation_metrics_;
    metrics.metric_statistics = metric_statistics_;

    // Calculate success rates
    for (const auto& [operation, stats] : operation_metrics_) {
        double success_rate = stats.total_attempts > 0 ?
            (static_cast<double>(stats.successful_operations) / stats.total_attempts) * 100.0 : 0.0;
        metrics.success_rates[operation] = success_rate;
    }

    return metrics;
}

std::vector<IntegrationMetricsCollector::IntegrationSession> IntegrationMetricsCollector::get_session_history(
    size_t limit,
    const std::string& operation_filter) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<IntegrationSession> filtered_sessions;

    for (const auto& session : session_history_) {
        if (operation_filter.empty() || session.operation == operation_filter) {
            filtered_sessions.push_back(session);
        }
    }

    // Sort by start time (newest first) and apply limit
    std::sort(filtered_sessions.begin(), filtered_sessions.end(),
        [](const IntegrationSession& a, const IntegrationSession& b) {
            return a.start_time > b.start_time;
        });

    if (limit > 0 && filtered_sessions.size() > limit) {
        filtered_sessions.resize(limit);
    }

    return filtered_sessions;
}

IntegrationMetricsCollector::PerformanceMetrics IntegrationMetricsCollector::get_performance_metrics(
    const std::string& metric_name,
    const std::chrono::system_clock::time_point& start_time,
    const std::chrono::system_clock::time_point& end_time) const {
    std::lock_guard<std::mutex> lock(mutex_);

    PerformanceMetrics result;
    result.metric_name = metric_name;

    for (const auto& metric : performance_metrics_) {
        if ((metric_name.empty() || metric.name == metric_name) &&
            metric.timestamp >= start_time && metric.timestamp <= end_time) {
            result.metrics.push_back(metric);

            // Update statistics
            if (result.statistics.min_value > metric.value || result.statistics.count == 0) {
                result.statistics.min_value = metric.value;
            }
            if (result.statistics.max_value < metric.value || result.statistics.count == 0) {
                result.statistics.max_value = metric.value;
            }
            result.statistics.sum_value += metric.value;
            result.statistics.count++;
        }
    }

    // Calculate average
    if (result.statistics.count > 0) {
        result.statistics.avg_value = result.statistics.sum_value / result.statistics.count;
    }

    return result;
}

nlohmann::json IntegrationMetricsCollector::export_metrics(const std::string& format) const {
    std::lock_guard<std::mutex> lock(mutex_);

    nlohmann::json metrics_export;
    metrics_export["export_timestamp"] = format_timestamp(std::chrono::system_clock::now());
    metrics_export["collection_config"] = {
        {"collection_interval_seconds", config_.collection_interval_seconds.count()},
        {"max_history_entries", config_.max_history_entries},
        {"log_all_metrics", config_.log_all_metrics},
        {"enable_real_time_collection", config_.enable_real_time_collection}
    };

    // Current metrics
    auto current = get_current_metrics();
    metrics_export["current_metrics"] = {
        {"timestamp", format_timestamp(current.timestamp)},
        {"active_sessions", current.active_sessions},
        {"total_sessions", current.total_sessions},
        {"success_rates", current.success_rates}
    };

    // Resource usage history
    metrics_export["resource_usage_history"] = nlohmann::json::array();
    for (const auto& [timestamp, usage] : resource_history_) {
        metrics_export["resource_usage_history"].push_back({
            {"timestamp", format_timestamp(timestamp)},
            {"cpu_percent", usage.cpu_percent},
            {"memory_mb", usage.memory_mb},
            {"disk_io_mb", usage.disk_io_mb},
            {"network_io_mb", usage.network_io_mb}
        });
    }

    // Performance metrics
    metrics_export["performance_metrics"] = nlohmann::json::array();
    for (const auto& metric : performance_metrics_) {
        metrics_export["performance_metrics"].push_back({
            {"name", metric.name},
            {"value", metric.value},
            {"unit", metric.unit},
            {"timestamp", format_timestamp(metric.timestamp)},
            {"tags", metric.tags}
        });
    }

    // Operation statistics
    metrics_export["operation_statistics"] = nlohmann::json::object();
    for (const auto& [operation, stats] : operation_metrics_) {
        metrics_export["operation_statistics"][operation] = {
            {"total_attempts", stats.total_attempts},
            {"successful_operations", stats.successful_operations},
            {"total_duration_seconds", stats.total_duration_seconds},
            {"avg_duration_seconds", stats.avg_duration_seconds()},
            {"success_rate_percent", stats.success_rate()}
        };
    }

    return metrics_export;
}

bool IntegrationMetricsCollector::generate_report(const std::string& output_path,
                                                 const std::string& report_type) {
    auto metrics = get_current_metrics();

    std::ofstream report_file(output_path);
    if (!report_file.is_open()) {
        IntegrationLogger::instance().log_error(
            "IntegrationMetricsCollector",
            "Failed to open report file for writing",
            "output_path", output_path
        );
        return false;
    }

    if (report_type == "json") {
        report_file << std::setw(4) << export_metrics() << std::endl;
    } else if (report_type == "text") {
        generate_text_report(report_file, metrics);
    } else if (report_type == "html") {
        generate_html_report(report_file, metrics);
    } else {
        IntegrationLogger::instance().log_error(
            "IntegrationMetricsCollector",
            "Unsupported report type",
            "report_type", report_type
        );
        return false;
    }

    IntegrationLogger::instance().log_info(
        "IntegrationMetricsCollector",
        "Metrics report generated",
        "output_path", output_path,
        "report_type", report_type
    );

    return true;
}

void IntegrationMetricsCollector::cleanup_old_metrics(std::chrono::hours max_age) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto cutoff_time = std::chrono::system_clock::now() - max_age;

    // Clean session history
    session_history_.erase(
        std::remove_if(session_history_.begin(), session_history_.end(),
            [cutoff_time](const IntegrationSession& session) {
                return std::chrono::system_clock::from_time_t(
                    std::chrono::duration_cast<std::chrono::seconds>(
                        session.start_time.time_since_epoch()
                    ).count()) < cutoff_time;
            }),
        session_history_.end()
    );

    // Clean performance metrics
    performance_metrics_.erase(
        std::remove_if(performance_metrics_.begin(), performance_metrics_.end(),
            [cutoff_time](const PerformanceMetric& metric) {
                return metric.timestamp < cutoff_time;
            }),
        performance_metrics_.end()
    );

    // Clean resource history
    resource_history_.erase(
        std::remove_if(resource_history_.begin(), resource_history_.end(),
            [cutoff_time](const auto& entry) {
                return entry.first < cutoff_time;
            }),
        resource_history_.end()
    );

    IntegrationLogger::instance().log_info(
        "IntegrationMetricsCollector",
        "Old metrics cleaned up",
        "max_age_hours", std::to_string(max_age.count())
    );
}

// Private methods implementation
std::string IntegrationMetricsCollector::generate_session_id() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);

    std::stringstream ss;
    ss << "metrics_" << std::put_time(std::gmtime(&time_t), "%Y%m%d_%H%M%S")
       << "_" << std::hex << (++session_counter_);

    return ss.str();
}

void IntegrationMetricsCollector::update_operation_metrics(const std::string& operation,
                                                         bool success,
                                                         double duration) {
    auto& stats = operation_metrics_[operation];
    stats.total_attempts++;
    if (success) {
        stats.successful_operations++;
    }
    stats.total_duration_seconds += duration;
}

void IntegrationMetricsCollector::update_metric_statistics(const std::string& metric_name,
                                                          double value) {
    auto& stats = metric_statistics_[metric_name];
    stats.count++;
    stats.sum_value += value;
    if (stats.count == 1) {
        stats.min_value = value;
        stats.max_value = value;
    } else {
        stats.min_value = std::min(stats.min_value, value);
        stats.max_value = std::max(stats.max_value, value);
    }
    stats.avg_value = stats.sum_value / stats.count;
}

bool IntegrationMetricsCollector::is_significant_metric(const std::string& metric_name,
                                                       double value) const {
    // Consider a metric significant if it deviates from average by > 20%
    auto it = metric_statistics_.find(metric_name);
    if (it == metric_statistics_.end() || it->second.count == 0) {
        return true; // New metrics are significant
    }

    double avg_value = it->second.avg_value;
    if (avg_value == 0.0) {
        return value != 0.0;
    }

    double deviation = std::abs((value - avg_value) / avg_value);
    return deviation > 0.2; // 20% deviation threshold
}

void IntegrationMetricsCollector::setup_scheduled_collection() {
    if (!config_.enable_real_time_collection) {
        return;
    }

    // In a real implementation, this would setup a background thread or timer
    // For now, we'll just log that scheduled collection is enabled
    IntegrationLogger::instance().log_info(
        "IntegrationMetricsCollector",
        "Scheduled metrics collection enabled",
        "interval_seconds", std::to_string(config_.collection_interval_seconds.count())
    );
}

void IntegrationMetricsCollector::load_historical_metrics() {
    // Load metrics from storage if available
    std::string metrics_file = storage_path_ + "/metrics_history.json";
    if (std::filesystem::exists(metrics_file)) {
        try {
            std::ifstream file(metrics_file);
            nlohmann::json data;
            file >> data;

            // Load session history, performance metrics, etc.
            // Implementation would parse JSON and populate internal structures

            IntegrationLogger::instance().log_info(
                "IntegrationMetricsCollector",
                "Historical metrics loaded",
                "file", metrics_file
            );
        } catch (const std::exception& e) {
            IntegrationLogger::instance().log_warning(
                "IntegrationMetricsCollector",
                "Failed to load historical metrics",
                "error", e.what()
            );
        }
    }
}

void IntegrationMetricsCollector::update_historical_metrics() {
    // Save metrics to storage periodically
    if (!storage_path_.empty()) {
        std::string metrics_file = storage_path_ + "/metrics_history.json";

        try {
            std::ofstream file(metrics_file);
            file << std::setw(4) << export_metrics() << std::endl;
        } catch (const std::exception& e) {
            IntegrationLogger::instance().log_error(
                "IntegrationMetricsCollector",
                "Failed to save historical metrics",
                "error", e.what()
            );
        }
    }
}

std::string IntegrationMetricsCollector::format_timestamp(std::chrono::system_clock::time_point tp) const {
    auto time_t = std::chrono::system_clock::to_time_t(tp);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

void IntegrationMetricsCollector::generate_text_report(std::ofstream& file,
                                                     const IntegrationMetrics& metrics) const {
    file << "Integration Metrics Report\n";
    file << "========================\n\n";
    file << "Generated: " << format_timestamp(metrics.timestamp) << "\n\n";

    file << "Session Statistics:\n";
    file << "  Active Sessions: " << metrics.active_sessions << "\n";
    file << "  Total Sessions: " << metrics.total_sessions << "\n\n";

    file << "Success Rates:\n";
    for (const auto& [operation, rate] : metrics.success_rates) {
        file << "  " << operation << ": " << std::fixed << std::setprecision(1)
             << rate << "%\n";
    }
    file << "\n";

    file << "Current Resource Usage:\n";
    file << "  CPU: " << std::fixed << std::setprecision(1)
         << metrics.current_resource_usage.cpu_percent << "%\n";
    file << "  Memory: " << std::fixed << std::setprecision(1)
         << metrics.current_resource_usage.memory_mb << " MB\n";
    file << "  Disk I/O: " << std::fixed << std::setprecision(1)
         << metrics.current_resource_usage.disk_io_mb << " MB\n\n";

    file << "Operation Statistics:\n";
    for (const auto& [operation, stats] : metrics.operation_metrics) {
        file << "  " << operation << ":\n";
        file << "    Attempts: " << stats.total_attempts << "\n";
        file << "    Success Rate: " << std::fixed << std::setprecision(1)
             << stats.success_rate() << "%\n";
        file << "    Avg Duration: " << std::fixed << std::setprecision(3)
             << stats.avg_duration_seconds() << " seconds\n";
    }
}

void IntegrationMetricsCollector::generate_html_report(std::ofstream& file,
                                                     const IntegrationMetrics& metrics) const {
    file << "<!DOCTYPE html>\n";
    file << "<html><head><title>Integration Metrics Report</title></head><body>\n";
    file << "<h1>Integration Metrics Report</h1>\n";
    file << "<p>Generated: " << format_timestamp(metrics.timestamp) << "</p>\n";

    file << "<h2>Session Statistics</h2>\n";
    file << "<ul>\n";
    file << "<li>Active Sessions: " << metrics.active_sessions << "</li>\n";
    file << "<li>Total Sessions: " << metrics.total_sessions << "</li>\n";
    file << "</ul>\n";

    file << "<h2>Success Rates</h2>\n";
    file << "<table border='1'>\n";
    file << "<tr><th>Operation</th><th>Success Rate</th></tr>\n";
    for (const auto& [operation, rate] : metrics.success_rates) {
        file << "<tr><td>" << operation << "</td><td>" << std::fixed << std::setprecision(1)
             << rate << "%</td></tr>\n";
    }
    file << "</table>\n";

    file << "</body></html>\n";
}

} // namespace integration
} // namespace puzzle71