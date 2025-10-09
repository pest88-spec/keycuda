/**
 * Puzzle71Solver - Integration Metrics Collection System Implementation
 *
 * Provides comprehensive metrics collection and analysis for third-party library
 * integration operations, including performance tracking, quality metrics,
 * and trend analysis.
 *
 * @author       Puzzle71Solver Team
 * @created      2025-10-09
 * @license      MIT
 */

#include "metrics.h"
#include "baseline_measurer.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <atomic>

namespace integration::metrics {

// Metric definitions
namespace metrics {
    const MetricDefinition INTEGRATION_TOTAL_TIME{
        "integration_total_time",
        "Total time taken for complete integration operation",
        MetricCategory::PERFORMANCE,
        MetricType::TIMER,
        "milliseconds"
    };

    const MetricDefinition EXTRACTION_TIME{
        "extraction_time",
        "Time taken to extract source files",
        MetricCategory::PERFORMANCE,
        MetricType::TIMER,
        "milliseconds"
    };

    const MetricDefinition ATTRIBUTION_TIME{
        "attribution_time",
        "Time taken to add attribution headers",
        MetricCategory::PERFORMANCE,
        MetricType::TIMER,
        "milliseconds"
    };

    const MetricDefinition BUILD_TIME{
        "build_time",
        "Time taken for build system integration",
        MetricCategory::PERFORMANCE,
        MetricType::TIMER,
        "milliseconds"
    };

    const MetricDefinition VERIFICATION_TIME{
        "verification_time",
        "Time taken for integration verification",
        MetricCategory::PERFORMANCE,
        MetricType::TIMER,
        "milliseconds"
    };

    const MetricDefinition ATTRIBUTION_COVERAGE{
        "attribution_coverage",
        "Percentage of files with proper attribution",
        MetricCategory::QUALITY,
        MetricType::RATIO,
        "percent"
    };

    const MetricDefinition SOURCE_INTEGRITY_SCORE{
        "source_integrity_score",
        "Integrity score for source files (0-1)",
        MetricCategory::QUALITY,
        MetricType::GAUGE,
        "score"
    };

    const MetricDefinition BUILD_SUCCESS_RATE{
        "build_success_rate",
        "Percentage of successful builds",
        MetricCategory::RELIABILITY,
        MetricType::RATIO,
        "percent"
    };

    const MetricDefinition OVERALL_SUCCESS_RATE{
        "overall_success_rate",
        "Overall integration success rate",
        MetricCategory::RELIABILITY,
        MetricType::RATIO,
        "percent"
    };

    const MetricDefinition FAILURE_COUNT{
        "failure_count",
        "Number of integration failures",
        MetricCategory::RELIABILITY,
        MetricType::COUNTER,
        "count"
    };

    const MetricDefinition RECOVERY_TIME{
        "recovery_time",
        "Time taken to recover from failures",
        MetricCategory::RELIABILITY,
        MetricType::TIMER,
        "milliseconds"
    };

    const MetricDefinition DISK_USAGE{
        "disk_usage",
        "Disk space usage in bytes",
        MetricCategory::OPERATIONAL,
        MetricType::GAUGE,
        "bytes"
    };

    const MetricDefinition MEMORY_USAGE{
        "memory_usage",
        "Memory usage in bytes",
        MetricCategory::OPERATIONAL,
        MetricType::GAUGE,
        "bytes"
    };

    const MetricDefinition CPU_UTILIZATION{
        "cpu_utilization",
        "CPU utilization percentage",
        MetricCategory::OPERATIONAL,
        MetricType::GAUGE,
        "percent"
    };

    const MetricDefinition LICENSE_COMPLIANCE{
        "license_compliance",
        "License compliance score",
        MetricCategory::COMPLIANCE,
        MetricType::GAUGE,
        "score"
    };

    const MetricDefinition ATTRIBUTION_COMPLIANCE{
        "attribution_compliance",
        "Attribution compliance score",
        MetricCategory::COMPLIANCE,
        MetricType::GAUGE,
        "score"
    };

    const MetricDefinition STANDARDS_COMPLIANCE{
        "standards_compliance",
        "Coding standards compliance score",
        MetricCategory::COMPLIANCE,
        MetricType::GAUGE,
        "score"
    };
}

// Implementation of MetricsCollector
class InMemoryMetricsCollector : public MetricsCollector {
public:
    InMemoryMetricsCollector() = default;
    ~InMemoryMetricsCollector() override = default;

    void record_metric(const std::string& name, MetricValue value,
                      const std::map<std::string, std::string>& tags = {}) override {
        std::unique_lock lock(mutex_);

        MetricDataPoint point;
        point.timestamp = std::chrono::system_clock::now();
        point.value = value;
        point.tags = tags;
        point.description = name;

        metrics_[name].push_back(point);

        // Enforce retention policy
        auto retention_it = retention_periods_.find(name);
        if (retention_it != retention_periods_.end()) {
            enforce_retention_policy(name, retention_it->second);
        }
    }

    void start_timer(const std::string& name,
                    const std::map<std::string, std::string>& tags = {}) override {
        std::unique_lock lock(mutex_);
        active_timers_[name] = {std::chrono::high_resolution_clock::now(), tags};
    }

    void end_timer(const std::string& name) override {
        auto end_time = std::chrono::high_resolution_clock::now();
        std::map<std::string, std::string> tags;

        {
            std::unique_lock lock(mutex_);
            auto it = active_timers_.find(name);
            if (it == active_timers_.end()) {
                return; // Timer not started
            }

            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                end_time - it->second.first);
            tags = it->second.second;
            active_timers_.erase(it);
        }

        record_metric(name, static_cast<MetricValue>(duration.count()), tags);
    }

    void increment_counter(const std::string& name, MetricValue increment = 1.0,
                          const std::map<std::string, std::string>& tags = {}) override {
        std::shared_lock lock(mutex_);
        auto it = metrics_.find(name);
        MetricValue current_value = 0.0;

        if (it != metrics_.end() && !it->second.empty()) {
            current_value = it->second.back().value;
        }
        lock.unlock();

        record_metric(name, current_value + increment, tags);
    }

    void set_gauge(const std::string& name, MetricValue value,
                   const std::map<std::string, std::string>& tags = {}) override {
        record_metric(name, value, tags);
    }

    void record_batch(const std::vector<std::pair<std::string, MetricValue>>& metrics,
                     const std::map<std::string, std::string>& tags = {}) override {
        auto timestamp = std::chrono::system_clock::now();
        std::unique_lock lock(mutex_);

        for (const auto& [name, value] : metrics) {
            MetricDataPoint point;
            point.timestamp = timestamp;
            point.value = value;
            point.tags = tags;
            point.description = name;
            metrics_[name].push_back(point);
        }
    }

    std::vector<MetricDataPoint> get_metric_history(const std::string& name,
                                                   const MetricTimestamp& since,
                                                   const MetricTimestamp& until = {}) const override {
        std::shared_lock lock(mutex_);
        auto it = metrics_.find(name);
        if (it == metrics_.end()) {
            return {};
        }

        std::vector<MetricDataPoint> result;
        for (const auto& point : it->second) {
            if (point.timestamp >= since) {
                if (!until.has_value() || point.timestamp <= until) {
                    result.push_back(point);
                }
            }
        }

        return result;
    }

    MetricValue get_latest_value(const std::string& name) const override {
        std::shared_lock lock(mutex_);
        auto it = metrics_.find(name);
        if (it == metrics_.end() || it->second.empty()) {
            return 0.0;
        }
        return it->second.back().value;
    }

    std::map<std::string, MetricValue> get_all_latest_values() const override {
        std::shared_lock lock(mutex_);
        std::map<std::string, MetricValue> result;

        for (const auto& [name, points] : metrics_) {
            if (!points.empty()) {
                result[name] = points.back().value;
            }
        }

        return result;
    }

    void set_metric_definition(const MetricDefinition& definition) override {
        std::unique_lock lock(mutex_);
        metric_definitions_[definition.name] = definition;
    }

    void enable_metric(const std::string& name, bool enabled = true) override {
        std::unique_lock lock(mutex_);
        enabled_metrics_[name] = enabled;
    }

    void set_retention_period(const std::string& name, std::chrono::seconds period) override {
        std::unique_lock lock(mutex_);
        retention_periods_[name] = period;
        enforce_retention_policy(name, period);
    }

private:
    void enforce_retention_policy(const std::string& name, std::chrono::seconds period) {
        auto it = metrics_.find(name);
        if (it == metrics_.end()) {
            return;
        }

        auto cutoff_time = std::chrono::system_clock::now() - period;
        auto& points = it->second;

        // Remove old points
        points.erase(
            std::remove_if(points.begin(), points.end(),
                          [cutoff_time](const MetricDataPoint& point) {
                              return point.timestamp < cutoff_time;
                          }),
            points.end()
        );
    }

    mutable std::shared_mutex mutex_;
    std::map<std::string, std::vector<MetricDataPoint>> metrics_;
    std::map<std::string, std::pair<std::chrono::high_resolution_clock::time_point,
                                     std::map<std::string, std::string>>> active_timers_;
    std::map<std::string, MetricDefinition> metric_definitions_;
    std::map<std::string, bool> enabled_metrics_;
    std::map<std::string, std::chrono::seconds> retention_periods_;
};

// Implementation of MetricsAnalyzer
class StandardMetricsAnalyzer : public MetricsAnalyzer {
public:
    StandardMetricsAnalyzer(MetricsCollector& collector) : collector_(collector) {}
    ~StandardMetricsAnalyzer() override = default;

    IntegrationMetricsReport generate_report(const std::string& library_name,
                                             const MetricTimestamp& since,
                                             const MetricTimestamp& until = {}) const override {
        IntegrationMetricsReport report;
        report.library_name = library_name;
        report.report_timestamp = std::chrono::system_clock::now();
        report.reporting_period = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now() - since);

        // Generate performance metrics
        report.performance = generate_performance_metrics(library_name, since, until);

        // Generate quality metrics
        report.quality = generate_quality_metrics(library_name, since, until);

        // Generate reliability metrics
        report.reliability = generate_reliability_metrics(library_name, since, until);

        // Generate operational metrics
        report.operational = generate_operational_metrics(library_name, since, until);

        // Generate compliance metrics
        report.compliance = generate_compliance_metrics(library_name, since, until);

        // Calculate overall score
        report.overall_score = calculate_overall_score(report);
        report.category_scores = calculate_category_scores(report);

        return report;
    }

    std::vector<std::string> detect_anomalies(const std::string& library_name,
                                              const MetricTimestamp& since) const override {
        std::vector<std::string> anomalies;

        // Check for unusual patterns in performance metrics
        auto recent_times = collector_.get_metric_history("integration_total_time", since);
        if (recent_times.size() >= 10) {
            // Calculate average and standard deviation
            double sum = 0.0, sum_sq = 0.0;
            for (const auto& point : recent_times) {
                sum += point.value;
                sum_sq += point.value * point.value;
            }
            double mean = sum / recent_times.size();
            double variance = (sum_sq / recent_times.size()) - (mean * mean);
            double std_dev = std::sqrt(variance);

            // Check if latest value is outlier (> 2 std devs from mean)
            double latest = recent_times.back().value;
            if (std::abs(latest - mean) > 2 * std_dev) {
                anomalies.push_back("Performance anomaly detected: integration time significantly deviates from normal");
            }
        }

        // Check for success rate drops
        auto success_rate = collector_.get_metric_history("overall_success_rate", since);
        if (!success_rate.empty() && success_rate.back().value < 0.9) {
            anomalies.push_back("Success rate below 90%");
        }

        // Check for high failure counts
        auto failure_count = collector_.get_metric_history("failure_count", since);
        if (!failure_count.empty() && failure_count.back().value > 5) {
            anomalies.push_back("High failure count detected");
        }

        return anomalies;
    }

    std::map<std::string, double> calculate_trends(const std::string& library_name,
                                                   const MetricTimestamp& since) const override {
        std::map<std::string, double> trends;

        // Calculate trend for integration time
        auto integration_times = collector_.get_metric_history("integration_total_time", since);
        if (integration_times.size() >= 2) {
            // Simple linear regression to determine trend
            size_t n = integration_times.size();
            double sum_x = 0, sum_y = 0, sum_xy = 0, sum_x2 = 0;

            for (size_t i = 0; i < n; ++i) {
                double x = static_cast<double>(i);
                double y = integration_times[i].value;
                sum_x += x;
                sum_y += y;
                sum_xy += x * y;
                sum_x2 += x * x;
            }

            double slope = (n * sum_xy - sum_x * sum_y) / (n * sum_x2 - sum_x * sum_x);
            trends["integration_time_trend"] = slope; // Positive slope = increasing time
        }

        // Calculate trend for success rate
        auto success_rates = collector_.get_metric_history("overall_success_rate", since);
        if (success_rates.size() >= 2) {
            double first = success_rates.front().value;
            double last = success_rates.back().value;
            trends["success_rate_trend"] = last - first; // Positive trend = improving
        }

        return trends;
    }

    std::map<std::string, double> predict_performance(const std::string& library_name) const override {
        std::map<std::string, double> predictions;

        // Simple prediction based on recent averages
        auto since = std::chrono::system_clock::now() - std::chrono::hours(24);

        auto integration_times = collector_.get_metric_history("integration_total_time", since);
        if (!integration_times.empty()) {
            double sum = 0.0;
            for (const auto& point : integration_times) {
                sum += point.value;
            }
            predictions["predicted_integration_time"] = sum / integration_times.size();
        }

        auto success_rates = collector_.get_metric_history("overall_success_rate", since);
        if (!success_rates.empty()) {
            double sum = 0.0;
            for (const auto& point : success_rates) {
                sum += point.value;
            }
            predictions["predicted_success_rate"] = sum / success_rates.size();
        }

        return predictions;
    }

    std::vector<std::string> generate_recommendations(const std::string& library_name) const override {
        std::vector<std::string> recommendations;

        auto since = std::chrono::system_clock::now() - std::chrono::hours(24);
        auto anomalies = detect_anomalies(library_name, since);
        auto trends = calculate_trends(library_name, since);

        // Performance recommendations
        if (trends.find("integration_time_trend") != trends.end()) {
            if (trends.at("integration_time_trend") > 100) { // Increasing by >100ms per operation
                recommendations.push_back("Consider optimizing integration process - integration times are trending upward");
            }
        }

        // Reliability recommendations
        auto latest_success_rate = collector_.get_latest_value("overall_success_rate");
        if (latest_success_rate < 0.95) {
            recommendations.push_back("Success rate below 95% - investigate failure patterns and improve error handling");
        }

        // Quality recommendations
        auto attribution_coverage = collector_.get_latest_value("attribution_coverage");
        if (attribution_coverage < 1.0) {
            recommendations.push_back("Attribution coverage incomplete - ensure all files have proper attribution");
        }

        // Operational recommendations
        auto disk_usage = collector_.get_latest_value("disk_usage");
        if (disk_usage > 10ull * 1024 * 1024 * 1024) { // >10GB
            recommendations.push_back("High disk usage detected - consider cleanup or compression");
        }

        return recommendations;
    }

private:
    PerformanceMetrics generate_performance_metrics(const std::string& library_name,
                                                   const MetricTimestamp& since,
                                                   const MetricTimestamp& until) const {
        PerformanceMetrics perf;

        // Get timing metrics
        auto total_time_points = collector_.get_metric_history("integration_total_time", since, until);
        if (!total_time_points.empty()) {
            double sum = 0.0;
            for (const auto& point : total_time_points) {
                sum += point.value;
            }
            perf.total_integration_time = std::chrono::milliseconds(static_cast<long>(sum / total_time_points.size()));
        }

        auto extraction_points = collector_.get_metric_history("extraction_time", since, until);
        if (!extraction_points.empty()) {
            double sum = 0.0;
            for (const auto& point : extraction_points) {
                sum += point.value;
            }
            perf.extraction_time = std::chrono::milliseconds(static_cast<long>(sum / extraction_points.size()));
        }

        auto attribution_points = collector_.get_metric_history("attribution_time", since, until);
        if (!attribution_points.empty()) {
            double sum = 0.0;
            for (const auto& point : attribution_points) {
                sum += point.value;
            }
            perf.attribution_time = std::chrono::milliseconds(static_cast<long>(sum / attribution_points.size()));
        }

        auto build_points = collector_.get_metric_history("build_time", since, until);
        if (!build_points.empty()) {
            double sum = 0.0;
            for (const auto& point : build_points) {
                sum += point.value;
            }
            perf.build_time = std::chrono::milliseconds(static_cast<long>(sum / build_points.size()));
        }

        auto verification_points = collector_.get_metric_history("verification_time", since, until);
        if (!verification_points.empty()) {
            double sum = 0.0;
            for (const auto& point : verification_points) {
                sum += point.value;
            }
            perf.verification_time = std::chrono::milliseconds(static_cast<long>(sum / verification_points.size()));
        }

        // Get resource metrics
        perf.peak_memory_usage_bytes = static_cast<size_t>(collector_.get_latest_value("memory_usage"));
        perf.disk_space_used_bytes = static_cast<size_t>(collector_.get_latest_value("disk_usage"));
        perf.cpu_utilization_percent = collector_.get_latest_value("cpu_utilization");

        // Calculate efficiency metrics
        auto total_files = collector_.get_latest_value("total_files_processed");
        if (total_files > 0 && perf.total_integration_time.count() > 0) {
            perf.files_processed_per_second = total_files / (perf.total_integration_time.count() / 1000.0);
        }

        // Get success/failure counts
        perf.successful_operations = static_cast<size_t>(collector_.get_latest_value("successful_operations"));
        perf.failed_operations = static_cast<size_t>(collector_.get_latest_value("failed_operations"));

        if (perf.successful_operations + perf.failed_operations > 0) {
            perf.success_rate = static_cast<double>(perf.successful_operations) /
                            (perf.successful_operations + perf.failed_operations);
        }

        return perf;
    }

    QualityMetrics generate_quality_metrics(const std::string& library_name,
                                           const MetricTimestamp& since,
                                           const MetricTimestamp& until) const {
        QualityMetrics quality;

        quality.attribution_coverage_percent = collector_.get_latest_value("attribution_coverage") * 100.0;
        quality.source_integrity_score = collector_.get_latest_value("source_integrity_score");
        quality.documentation_coverage_percent = collector_.get_latest_value("documentation_coverage") * 100.0;

        quality.build_success_rate = collector_.get_latest_value("build_success_rate");
        quality.test_pass_rate = collector_.get_latest_value("test_pass_rate");
        quality.verification_success_rate = collector_.get_latest_value("verification_success_rate");

        quality.license_compliance_score = collector_.get_latest_value("license_compliance");
        quality.coding_standards_score = collector_.get_latest_value("coding_standards_score");
        quality.attribution_standards_score = collector_.get_latest_value("attribution_standards_score");

        return quality;
    }

    ReliabilityMetrics generate_reliability_metrics(const std::string& library_name,
                                                     const MetricTimestamp& since,
                                                     const MetricTimestamp& until) const {
        ReliabilityMetrics reliability;

        reliability.overall_success_rate = collector_.get_latest_value("overall_success_rate");
        reliability.extraction_success_rate = collector_.get_latest_value("extraction_success_rate");
        reliability.build_success_rate = collector_.get_latest_value("build_success_rate");
        reliability.attribution_success_rate = collector_.get_latest_value("attribution_success_rate");

        reliability.system_availability_percent = collector_.get_latest_value("system_availability") * 100.0;
        reliability.mean_time_between_failures_hours = collector_.get_latest_value("mtbf_hours");

        return reliability;
    }

    OperationalMetrics generate_operational_metrics(const std::string& library_name,
                                                     const MetricTimestamp& since,
                                                     const MetricTimestamp& until) const {
        OperationalMetrics operational;

        operational.total_integrations = static_cast<size_t>(collector_.get_latest_value("total_integrations"));
        operational.active_libraries = static_cast<size_t>(collector_.get_latest_value("active_libraries"));
        operational.concurrent_operations = static_cast<size_t>(collector_.get_latest_value("concurrent_operations"));

        operational.disk_space_utilization_percent = collector_.get_latest_value("disk_space_utilization") * 100.0;
        operational.memory_utilization_percent = collector_.get_latest_value("memory_utilization") * 100.0;
        operational.cpu_utilization_percent = collector_.get_latest_value("cpu_utilization");

        operational.warning_count = static_cast<size_t>(collector_.get_latest_value("warning_count"));
        operational.error_count = static_cast<size_t>(collector_.get_latest_value("error_count"));
        operational.critical_issues_count = static_cast<size_t>(collector_.get_latest_value("critical_issues_count"));

        return operational;
    }

    ComplianceMetrics generate_compliance_metrics(const std::string& library_name,
                                                 const MetricTimestamp& since,
                                                 const MetricTimestamp& until) const {
        ComplianceMetrics compliance;

        compliance.attribution_header_coverage = collector_.get_latest_value("attribution_header_coverage");
        compliance.license_file_coverage = collector_.get_latest_value("license_file_coverage");
        compliance.source_origin_tracking = collector_.get_latest_value("source_origin_tracking");

        compliance.license_compatibility_score = collector_.get_latest_value("license_compatibility");
        compliance.license_documentation_score = collector_.get_latest_value("license_documentation");

        compliance.coding_standards_adherence = collector_.get_latest_value("coding_standards_adherence");
        compliance.documentation_completeness = collector_.get_latest_value("documentation_completeness");
        compliance.integration_process_compliance = collector_.get_latest_value("integration_process_compliance");

        return compliance;
    }

    double calculate_overall_score(const IntegrationMetricsReport& report) const {
        double scores[] = {
            calculate_performance_score(report.performance),
            calculate_quality_score(report.quality),
            calculate_reliability_score(report.reliability),
            calculate_operational_score(report.operational),
            calculate_compliance_score(report.compliance)
        };

        return std::accumulate(std::begin(scores), std::end(scores), 0.0) / std::size(scores);
    }

    std::map<std::string, double> calculate_category_scores(const IntegrationMetricsReport& report) const {
        return {
            {"performance", calculate_performance_score(report.performance)},
            {"quality", calculate_quality_score(report.quality)},
            {"reliability", calculate_reliability_score(report.reliability)},
            {"operational", calculate_operational_score(report.operational)},
            {"compliance", calculate_compliance_score(report.compliance)}
        };
    }

    double calculate_performance_score(const PerformanceMetrics& perf) const {
        // Score based on efficiency and speed
        double time_score = 1.0; // Normalize against expected performance
        double resource_score = 1.0; // Normalize against resource usage
        double efficiency_score = perf.success_rate;

        return (time_score + resource_score + efficiency_score) / 3.0;
    }

    double calculate_quality_score(const QualityMetrics& quality) const {
        return (quality.attribution_coverage_percent / 100.0 +
                quality.source_integrity_score +
                quality.build_success_rate +
                quality.verification_success_rate) / 4.0;
    }

    double calculate_reliability_score(const ReliabilityMetrics& reliability) const {
        return reliability.overall_success_rate;
    }

    double calculate_operational_score(const OperationalMetrics& operational) const {
        // Score based on low error rates and healthy resource usage
        double error_score = 1.0; // Inverse of error rates
        double resource_score = std::max(0.0, 1.0 - operational.disk_space_utilization_percent / 100.0);

        return (error_score + resource_score) / 2.0;
    }

    double calculate_compliance_score(const ComplianceMetrics& compliance) const {
        return (compliance.attribution_header_coverage +
                compliance.license_compatibility_score +
                compliance.coding_standards_adherence) / 3.0;
    }

    MetricsCollector& collector_;
};

// Implementation of TimerScope
TimerScope::TimerScope(const std::string& name,
                       const std::map<std::string, std::string>& tags)
    : timer_name_(name) {
    get_metrics_manager().collector().start_timer(name, tags);
    start_time_ = std::chrono::high_resolution_clock::now();
}

TimerScope::~TimerScope() {
    get_metrics_manager().collector().end_timer(timer_name_);
}

// Implementation of MetricsManager
struct MetricsManager::Impl {
    std::unique_ptr<MetricsCollector> collector;
    std::unique_ptr<MetricsAnalyzer> analyzer;
    std::filesystem::path data_directory;
    std::map<std::string, std::string> default_tags;
    std::atomic<bool> auto_export_enabled{false};
    std::chrono::seconds auto_export_interval{300}; // 5 minutes
    std::thread auto_export_thread;
    std::atomic<bool> shutdown_requested{false};

    Impl() {
        collector = std::make_unique<InMemoryMetricsCollector>();
        analyzer = std::make_unique<StandardMetricsAnalyzer>(*collector);
    }

    ~Impl() {
        if (auto_export_thread.joinable()) {
            shutdown_requested = true;
            auto_export_thread.join();
        }
    }

    void start_auto_export() {
        if (auto_export_enabled.load() && !auto_export_thread.joinable()) {
            auto_export_thread = std::thread([this]() {
                while (!shutdown_requested.load()) {
                    std::this_thread::sleep_for(auto_export_interval);
                    if (auto_export_enabled.load()) {
                        export_metrics();
                    }
                }
            });
        }
    }

    void export_metrics() {
        // Export metrics to JSON file
        std::filesystem::path export_file = data_directory / "metrics_export.json";
        // Implementation would use MetricsExporter
    }
};

MetricsManager::MetricsManager() : p_impl(std::make_unique<Impl>()) {}

MetricsManager::~MetricsManager() = default;

bool MetricsManager::initialize(const std::filesystem::path& data_dir) {
    p_impl->data_directory = data_dir;
    std::filesystem::create_directories(data_dir);

    // Register default metric definitions
    p_impl->collector->set_metric_definition(metrics::INTEGRATION_TOTAL_TIME);
    p_impl->collector->set_metric_definition(metrics::EXTRACTION_TIME);
    p_impl->collector->set_metric_definition(metrics::ATTRIBUTION_TIME);
    p_impl->collector->set_metric_definition(metrics::BUILD_TIME);
    p_impl->collector->set_metric_definition(metrics::VERIFICATION_TIME);
    p_impl->collector->set_metric_definition(metrics::ATTRIBUTION_COVERAGE);
    p_impl->collector->set_metric_definition(metrics::SOURCE_INTEGRITY_SCORE);
    p_impl->collector->set_metric_definition(metrics::BUILD_SUCCESS_RATE);
    p_impl->collector->set_metric_definition(metrics::OVERALL_SUCCESS_RATE);
    p_impl->collector->set_metric_definition(metrics::FAILURE_COUNT);
    p_impl->collector->set_metric_definition(metrics::RECOVERY_TIME);
    p_impl->collector->set_metric_definition(metrics::DISK_USAGE);
    p_impl->collector->set_metric_definition(metrics::MEMORY_USAGE);
    p_impl->collector->set_metric_definition(metrics::CPU_UTILIZATION);
    p_impl->collector->set_metric_definition(metrics::LICENSE_COMPLIANCE);
    p_impl->collector->set_metric_definition(metrics::ATTRIBUTION_COMPLIANCE);
    p_impl->collector->set_metric_definition(metrics::STANDARDS_COMPLIANCE);

    // Set default retention periods
    p_impl->collector->set_retention_period("integration_total_time", std::chrono::hours(24 * 7)); // 1 week
    p_impl->collector->set_retention_period("failure_count", std::chrono::hours(24 * 30)); // 30 days

    return true;
}

bool MetricsManager::shutdown() {
    p_impl->shutdown_requested = true;
    if (p_impl->auto_export_thread.joinable()) {
        p_impl->auto_export_thread.join();
    }
    return true;
}

MetricsCollector& MetricsManager::collector() {
    return *p_impl->collector;
}

MetricsAnalyzer& MetricsManager::analyzer() {
    return *p_impl->analyzer;
}

std::unique_ptr<MetricsExporter> MetricsManager::create_exporter(const std::string& type) {
    // Implementation would create appropriate exporter based on type
    return nullptr; // Placeholder
}

void MetricsManager::record_integration_start(const std::string& library_name) {
    std::map<std::string, std::string> tags = {
        {"library", library_name},
        {"operation", "integration"}
    };
    tags.insert(p_impl->default_tags.begin(), p_impl->default_tags.end());

    collector().start_timer("integration_total_time", tags);
    collector().increment_counter("total_integrations", 1.0, tags);
}

void MetricsManager::record_integration_complete(const std::string& library_name,
                                                const PerformanceMetrics& metrics) {
    std::map<std::string, std::string> tags = {
        {"library", library_name},
        {"operation", "integration"}
    };
    tags.insert(p_impl->default_tags.begin(), p_impl->default_tags.end());

    // End the integration timer
    collector().end_timer("integration_total_time");

    // Record performance metrics
    collector().record_metric("extraction_time",
                             static_cast<MetricValue>(metrics.extraction_time.count()), tags);
    collector().record_metric("attribution_time",
                             static_cast<MetricValue>(metrics.attribution_time.count()), tags);
    collector().record_metric("build_time",
                             static_cast<MetricValue>(metrics.build_time.count()), tags);
    collector().record_metric("verification_time",
                             static_cast<MetricValue>(metrics.verification_time.count()), tags);

    // Record resource usage
    collector().set_gauge("memory_usage", static_cast<MetricValue>(metrics.peak_memory_usage_bytes), tags);
    collector().set_gauge("disk_usage", static_cast<MetricValue>(metrics.disk_space_used_bytes), tags);
    collector().set_gauge("cpu_utilization", metrics.cpu_utilization_percent, tags);

    // Record success metrics
    collector().increment_counter("successful_operations", 1.0, tags);
    collector().set_gauge("total_files_processed", static_cast<MetricValue>(metrics.total_files_processed), tags);
}

void MetricsManager::record_integration_failure(const std::string& library_name,
                                               const std::string& error_type) {
    std::map<std::string, std::string> tags = {
        {"library", library_name},
        {"operation", "integration"},
        {"error_type", error_type}
    };
    tags.insert(p_impl->default_tags.begin(), p_impl->default_tags.end());

    collector().increment_counter("failure_count", 1.0, tags);
    collector().increment_counter("failed_operations", 1.0, tags);
}

void MetricsManager::record_quality_metrics(const std::string& library_name,
                                           const QualityMetrics& metrics) {
    std::map<std::string, std::string> tags = {
        {"library", library_name}
    };
    tags.insert(p_impl->default_tags.begin(), p_impl->default_tags.end());

    collector().set_gauge("attribution_coverage", metrics.attribution_coverage_percent / 100.0, tags);
    collector().set_gauge("source_integrity_score", metrics.source_integrity_score, tags);
    collector().set_gauge("build_success_rate", metrics.build_success_rate, tags);
    collector().set_gauge("verification_success_rate", metrics.verification_success_rate, tags);
    collector().set_gauge("license_compliance", metrics.license_compliance_score, tags);
}

IntegrationMetricsReport MetricsManager::generate_comprehensive_report(const std::string& library_name,
                                                                      const MetricTimestamp& since,
                                                                      const MetricTimestamp& until) const {
    return analyzer().generate_report(library_name, since, until);
}

void MetricsManager::set_default_tags(const std::map<std::string, std::string>& tags) {
    p_impl->default_tags = tags;
}

void MetricsManager::enable_auto_export(bool enabled, std::chrono::seconds interval) {
    p_impl->auto_export_enabled = enabled;
    p_impl->auto_export_interval = interval;

    if (enabled) {
        p_impl->start_auto_export();
    }
}

void MetricsManager::set_retention_policy(std::chrono::seconds default_retention) {
    // Apply to all metrics that don't have specific retention
}

// Global instance
MetricsManager& get_metrics_manager() {
    static MetricsManager instance;
    return instance;
}

} // namespace integration::metrics