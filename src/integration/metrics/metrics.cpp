// T003: Integration Logging Infrastructure
// Provides JSON-capable logging for integration operations

#include "metrics.h"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>

namespace integration {
namespace metrics {

MetricsCollector::MetricsCollector() : start_time_(std::chrono::high_resolution_clock::now()) {
    log_entry_["session_id"] = generate_session_id();
    log_entry_["start_time"] = get_timestamp();
}

void MetricsCollector::log_operation(const std::string& operation,
                                   const nlohmann::json& details) {
    nlohmann::json entry = log_entry_;
    entry["operation"] = operation;
    entry["timestamp"] = get_timestamp();
    entry["details"] = details;

    // Store in memory for later export
    operations_log_.push_back(entry);

    // Also output to console if verbose
    if (verbose_) {
        std::cout << "[INTEGRATION] " << operation << ": " << details.dump(2) << std::endl;
    }
}

void MetricsCollector::log_timing(const std::string& operation,
                                double duration_ms) {
    nlohmann::json details;
    details["duration_ms"] = duration_ms;
    details["performance_ok"] = duration_ms < 600000.0; // 10 minutes budget

    log_operation("timing_" + operation, details);
}

void MetricsCollector::log_integrity_check(const std::string& path,
                                          const std::string& expected_sha256,
                                          const std::string& actual_sha256,
                                          bool passed) {
    nlohmann::json details;
    details["path"] = path;
    details["expected_sha256"] = expected_sha256;
    details["actual_sha256"] = actual_sha256;
    details["integrity_check_passed"] = passed;

    log_operation("integrity_check", details);
}

void MetricsCollector::log_attribution_check(const std::string& file_path,
                                            bool has_spdx,
                                            bool has_copyright,
                                            bool has_origin) {
    nlohmann::json details;
    details["file"] = file_path;
    details["has_spdx"] = has_spdx;
    details["has_copyright"] = has_copyright;
    details["has_origin"] = has_origin;
    details["attribution_complete"] = has_spdx && has_copyright && has_origin;

    log_operation("attribution_check", details);
}

void MetricsCollector::export_to_json(const std::string& filename) const {
    nlohmann::json report;
    report["session"] = log_entry_;
    report["operations"] = operations_log_;
    report["summary"] = generate_summary();

    std::ofstream file(filename);
    if (file.is_open()) {
        file << report.dump(2) << std::endl;
        file.close();
    }
}

nlohmann::json MetricsCollector::generate_summary() const {
    nlohmann::json summary;

    int total_operations = operations_log_.size();
    int integrity_checks = 0;
    int attribution_checks = 0;
    int passed_integrity = 0;
    int passed_attribution = 0;
    double total_time = 0.0;

    for (const auto& op : operations_log_) {
        if (op["operation"].get<std::string>().find("integrity") != std::string::npos) {
            integrity_checks++;
            if (op["details"]["integrity_check_passed"].get<bool>()) {
                passed_integrity++;
            }
        }

        if (op["operation"].get<std::string>().find("attribution") != std::string::npos) {
            attribution_checks++;
            if (op["details"]["attribution_complete"].get<bool>()) {
                passed_attribution++;
            }
        }

        if (op["details"].contains("duration_ms")) {
            total_time += op["details"]["duration_ms"].get<double>();
        }
    }

    summary["total_operations"] = total_operations;
    summary["integrity_checks"] = integrity_checks;
    summary["integrity_passed"] = passed_integrity;
    summary["integrity_success_rate"] = integrity_checks > 0 ?
        static_cast<double>(passed_integrity) / integrity_checks * 100.0 : 0.0;
    summary["attribution_checks"] = attribution_checks;
    summary["attribution_passed"] = passed_attribution;
    summary["attribution_success_rate"] = attribution_checks > 0 ?
        static_cast<double>(passed_attribution) / attribution_checks * 100.0 : 0.0;
    summary["total_time_ms"] = total_time;
    summary["average_operation_time_ms"] = total_operations > 0 ? total_time / total_operations : 0.0;

    return summary;
}

std::string MetricsCollector::get_timestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
    return ss.str();
}

std::string MetricsCollector::generate_session_id() const {
    // Generate a simple session ID based on timestamp and random value
    auto now = std::chrono::high_resolution_clock::now();
    auto timestamp = now.time_since_epoch().count();
    return "integration_" + std::to_string(timestamp);
}

// ScopedTimer implementation
ScopedTimer::ScopedTimer(const std::string& operation)
    : operation_(operation)
    , start_(std::chrono::high_resolution_clock::now()) {
}

ScopedTimer::~ScopedTimer() {
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start_);
    get_metrics_collector().log_timing(operation_, duration.count());
}

// Global instance
static std::unique_ptr<MetricsCollector> g_metrics_collector;

MetricsCollector& get_metrics_collector() {
    if (!g_metrics_collector) {
        g_metrics_collector = std::make_unique<MetricsCollector>();
    }
    return *g_metrics_collector;
}

} // namespace metrics
} // namespace integration