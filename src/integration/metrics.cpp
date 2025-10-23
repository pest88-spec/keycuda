/**
 * @file metrics.cpp
 * @brief Integration metrics collection and logging implementation
 *
 * T025: Integration logging for setup complexity reduction
 *
 * Implementation of comprehensive metrics collection system focused on
 * measuring and logging setup complexity reduction for third-party
 * dependencies integration.
 *
 * @author T025 Implementation Team
 * @date 2025-10-22
 */

#include "metrics.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <random>
#include <thread>

namespace integration {
namespace metrics {

// StandardMetricsCollector implementation
StandardMetricsCollector::StandardMetricsCollector() {
    logger_ = std::make_unique<JsonMetricsLogger>();
}

StandardMetricsCollector::~StandardMetricsCollector() = default;

void StandardMetricsCollector::record_metric(const std::string& name,
                                           const MetricValue& value,
                                           const std::map<std::string, std::string>& tags) {
    std::lock_guard<std::mutex> lock(mutex_);
    metrics_[name].push_back(value);

    if (logger_) {
        log_metric_to_json(name, value, tags);
    }
}

std::string StandardMetricsCollector::start_timer(const std::string& name,
                                                 const std::map<std::string, std::string>& tags) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string timer_id = generate_timer_id();
    TimerInfo info;
    info.name = name;
    info.start_time = std::chrono::system_clock::now();
    info.tags = tags;

    active_timers_[timer_id] = info;

    return timer_id;
}

void StandardMetricsCollector::end_timer(const std::string& timer_id) {
    auto end_time = std::chrono::system_clock::now();

    std::lock_guard<std::mutex> lock(mutex_);

    auto it = active_timers_.find(timer_id);
    if (it != active_timers_.end()) {
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - it->second.start_time);

        MetricValue value(MetricType::TIMER);
        value.timer_value = duration;
        value.unit = "ms";

        metrics_[it->second.name].push_back(value);

        if (logger_) {
            log_metric_to_json(it->second.name, value, it->second.tags);
        }

        active_timers_.erase(it);
    }
}

void StandardMetricsCollector::record_setup_metrics(const SetupComplexityMetrics& metrics) {
    if (logger_) {
        logger_->log_setup_metrics(metrics);
    }
}

void StandardMetricsCollector::record_operation_metrics(const IntegrationOperationMetrics& metrics) {
    if (logger_) {
        logger_->log_operation_metrics(metrics);
    }
}

void StandardMetricsCollector::record_library_metrics(const LibraryIntegrationMetrics& metrics) {
    // Create a JSON representation and log it
    if (logger_) {
        std::ostringstream json;
        json << "{"
              << "\"type\":\"library_metrics\","
              << "\"library_name\":\"" << metrics.library_name << "\","
              << "\"library_version\":\"" << metrics.library_version << "\","
              << "\"extraction_time_ms\":" << metrics.extraction_time.count() << ","
              << "\"attribution_time_ms\":" << metrics.attribution_time.count() << ","
              << "\"build_integration_time_ms\":" << metrics.build_integration_time.count() << ","
              << "\"source_files_count\":" << metrics.source_files_count << ","
              << "\"attribution_files_count\":" << metrics.attribution_files_count << ","
              << "\"attribution_coverage\":" << metrics.attribution_coverage_percentage << ","
              << "\"extraction_success\":" << (metrics.extraction_success ? "true" : "false") << ","
              << "\"build_success\":" << (metrics.build_success ? "true" : "false") << ","
              << "\"timestamp\":\"" << std::chrono::duration_cast<std::chrono::seconds>(
                  std::chrono::system_clock::now().time_since_epoch()).count() << "\""
              << "}";

        logger_->write_json_log(json.str());
    }
}

std::map<std::string, std::vector<MetricValue>> StandardMetricsCollector::get_all_metrics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return metrics_;
}

void StandardMetricsCollector::clear_metrics() {
    std::lock_guard<std::mutex> lock(mutex_);
    metrics_.clear();
    active_timers_.clear();
}

void StandardMetricsCollector::set_logger(std::unique_ptr<MetricsLogger> logger) {
    logger_ = std::move(logger);
}

std::string StandardMetricsCollector::generate_timer_id() {
    static std::atomic<uint64_t> counter{0};
    return "timer_" + std::to_string(counter++);
}

void StandardMetricsCollector::log_metric_to_json(const std::string& name,
                                                  const MetricValue& value,
                                                  const std::map<std::string, std::string>& tags) {
    if (logger_) {
        logger_->log_metric(name, value, std::chrono::system_clock::now(), tags);
    }
}

// JsonMetricsLogger implementation
JsonMetricsLogger::JsonMetricsLogger() : enable_console_(true) {}

JsonMetricsLogger::~JsonMetricsLogger() {
    if (log_file_.is_open()) {
        log_file_.close();
    }
}

bool JsonMetricsLogger::initialize(const std::string& log_file, bool enable_console) {
    enable_console_ = enable_console;

    // Create directory if it doesn't exist
    size_t last_slash = log_file.find_last_of('/');
    if (last_slash != std::string::npos) {
        std::string dir = log_file.substr(0, last_slash);
        std::filesystem::create_directories(dir);
    }

    log_file_.open(log_file, std::ios::app);
    if (!log_file_.is_open()) {
        std::cerr << "Failed to open metrics log file: " << log_file << std::endl;
        return false;
    }

    // Write initial log entry
    std::ostringstream init;
    init << "{"
         << "\"type\":\"log_initialized\","
         << "\"timestamp\":\"" << time_to_string(std::chrono::system_clock::now()) << "\","
         << "\"file\":\"" << log_file << "\""
         << "}";
    write_json_log(init.str());

    return true;
}

void JsonMetricsLogger::log_setup_metrics(const SetupComplexityMetrics& metrics) {
    std::string report = generate_setup_report(metrics);
    write_json_log(report);
}

void JsonMetricsLogger::log_operation_metrics(const IntegrationOperationMetrics& metrics) {
    std::ostringstream json;
    json << "{"
         << "\"type\":\"operation_metrics\","
         << "\"operation_name\":\"" << metrics.operation_name << "\","
         << "\"start_time\":\"" << time_to_string(metrics.start_time) << "\","
         << "\"end_time\":\"" << time_to_string(metrics.end_time) << "\","
         << "\"duration_ms\":" << metrics.duration.count() << ","
         << "\"success\":" << (metrics.success ? "true" : "false") << ",";

    if (!metrics.error_message.empty()) {
        json << "\"error_message\":\"" << metrics.error_message << "\",";
    }

    json << "\"additional_data\":{";
    bool first = true;
    for (const auto& [key, value] : metrics.additional_data) {
        if (!first) json << ",";
        json << "\"" << key << "\":\"" << value << "\"";
        first = false;
    }
    json << "}}";

    write_json_log(json.str());
}

std::string JsonMetricsLogger::generate_setup_report(const SetupComplexityMetrics& metrics) {
    std::ostringstream json;
    json << "{"
         << "\"type\":\"setup_complexity_report\","
         << "\"timestamp\":\"" << time_to_string(std::chrono::system_clock::now()) << "\","
         << "\"baseline\":{"
         << "\"setup_time_ms\":" << metrics.original_setup_time.count() << ","
         << "\"steps_count\":" << metrics.original_steps_count << ","
         << "\"dependencies_count\":" << metrics.original_dependencies_count << ","
         << "\"required_internet\":" << (metrics.original_required_internet ? "true" : "false") << ","
         << "\"manual_operations\":" << metrics.original_manual_operations
         << "},"
         << "\"optimized\":{"
         << "\"setup_time_ms\":" << metrics.optimized_setup_time.count() << ","
         << "\"steps_count\":" << metrics.optimized_steps_count << ","
         << "\"dependencies_count\":" << metrics.optimized_dependencies_count << ","
         << "\"required_internet\":" << (metrics.optimized_required_internet ? "true" : "false") << ","
         << "\"manual_operations\":" << metrics.optimized_manual_operations << ","
         << "\"offline_build_enabled\":" << (metrics.offline_build_enabled ? "true" : "false")
         << "},"
         << "\"improvements\":{"
         << "\"time_reduction_percentage\":" << std::fixed << std::setprecision(2) << metrics.time_reduction_percentage << ","
         << "\"steps_reduction_percentage\":" << std::fixed << std::setprecision(2) << metrics.steps_reduction_percentage << ","
         << "\"complexity_reduction_percentage\":" << std::fixed << std::setprecision(2) << metrics.complexity_reduction_percentage
         << "},"
         << "\"quality\":{"
         << "\"setup_success_rate\":" << std::fixed << std::setprecision(2) << metrics.setup_success_rate << ","
         << "\"build_success_rate\":" << std::fixed << std::setprecision(2) << metrics.build_success_rate << ","
         << "\"setup_attempts\":" << metrics.setup_attempts << ","
         << "\"successful_setups\":" << metrics.successful_setups
         << "}"
         << "}";

    return json.str();
}

void JsonMetricsLogger::log_metric(const std::string& name,
                                   const MetricValue& value,
                                   std::chrono::system_clock::time_point timestamp,
                                   const std::map<std::string, std::string>& tags) {
    std::ostringstream json;
    json << "{"
         << "\"type\":\"metric\","
         << "\"name\":\"" << name << "\","
         << "\"value\":" << metric_value_to_json(value) << ","
         << "\"unit\":\"" << value.unit << "\","
         << "\"timestamp\":\"" << time_to_string(timestamp) << "\","
         << "\"tags\":" << tags_to_json(tags)
         << "}";

    write_json_log(json.str());
}

std::string JsonMetricsLogger::metric_value_to_json(const MetricValue& value) const {
    std::ostringstream oss;
    switch (value.type) {
        case MetricType::COUNTER:
            oss << value.counter_value;
            break;
        case MetricType::GAUGE:
        case MetricType::PERCENTAGE:
            oss << std::fixed << std::setprecision(2) << value.gauge_value;
            break;
        case MetricType::TIMER:
            oss << value.timer_value.count();
            break;
    }
    return oss.str();
}

std::string JsonMetricsLogger::tags_to_json(const std::map<std::string, std::string>& tags) const {
    if (tags.empty()) {
        return "{}";
    }

    std::ostringstream oss;
    oss << "{";
    bool first = true;
    for (const auto& [key, value] : tags) {
        if (!first) oss << ",";
        oss << "\"" << key << "\":\"" << value << "\"";
        first = false;
    }
    oss << "}";
    return oss.str();
}

std::string JsonMetricsLogger::time_to_string(std::chrono::system_clock::time_point tp) const {
    auto time_t = std::chrono::system_clock::to_time_t(tp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        tp.time_since_epoch()) % 1000;

    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
    return oss.str();
}

void JsonMetricsLogger::write_json_log(const std::string& json_line) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Write to file
    if (log_file_.is_open()) {
        log_file_ << json_line << std::endl;
        log_file_.flush();
    }

    // Write to console if enabled
    if (enable_console_) {
        std::cout << "[METRICS] " << json_line << std::endl;
    }
}

// StandardSetupComplexityAnalyzer implementation
double StandardSetupComplexityAnalyzer::calculate_complexity_reduction(
    const SetupComplexityMetrics& original,
    const SetupComplexityMetrics& optimized) {

    double original_complexity = calculate_weighted_complexity(original);
    double optimized_complexity = calculate_weighted_complexity(optimized);

    if (original_complexity == 0.0) {
        return 0.0;
    }

    double reduction = ((original_complexity - optimized_complexity) / original_complexity) * 100.0;
    return std::max(0.0, std::min(100.0, reduction));
}

double StandardSetupComplexityAnalyzer::analyze_steps_complexity(const std::vector<std::string>& steps) {
    double complexity = 0.0;

    for (const auto& step : steps) {
        // Base complexity for each step
        complexity += 1.0;

        // Add complexity for manual operations
        if (step.find("manual") != std::string::npos ||
            step.find("Manual") != std::string::npos) {
            complexity += 3.0;
        }

        // Add complexity for network operations
        if (step.find("download") != std::string::npos ||
            step.find("clone") != std::string::npos ||
            step.find("fetch") != std::string::npos) {
            complexity += 2.0;
        }

        // Add complexity for configuration
        if (step.find("configure") != std::string::npos ||
            step.find("setup") != std::string::npos) {
            complexity += 1.5;
        }

        // Add complexity for building
        if (step.find("build") != std::string::npos ||
            step.find("compile") != std::string::npos) {
            complexity += 2.5;
        }
    }

    return complexity;
}

std::vector<std::string> StandardSetupComplexityAnalyzer::generate_improvements(
    const SetupComplexityMetrics& metrics) {

    std::vector<std::string> improvements;

    if (metrics.original_required_internet && !metrics.optimized_required_internet) {
        improvements.push_back("✓ Eliminated internet dependency for offline builds");
    }

    if (metrics.time_reduction_percentage < 50.0) {
        improvements.push_back("⚠ Build time still above optimal target (50% reduction achieved)");
    }

    if (metrics.optimized_manual_operations > 0) {
        improvements.push_back("→ Consider automating remaining manual operations");
    }

    if (metrics.setup_success_rate < 95.0) {
        improvements.push_back("⚠ Setup success rate below 95% - investigate failures");
    }

    if (metrics.build_success_rate < 99.0) {
        improvements.push_back("⚠ Build success rate below 99% - fix build issues");
    }

    if (metrics.complexity_reduction_percentage >= 80.0) {
        improvements.push_back("✓ Excellent complexity reduction achieved");
    }

    return improvements;
}

double StandardSetupComplexityAnalyzer::calculate_weighted_complexity(
    const SetupComplexityMetrics& metrics) {

    double complexity = 0.0;

    // Time component (weighted by minutes)
    complexity += std::chrono::duration<double>(metrics.optimized_setup_time).count() / 60.0 * 10.0;

    // Steps component
    complexity += metrics.optimized_steps_count * 5.0;

    // Manual operations component (high weight)
    complexity += metrics.optimized_manual_operations * 15.0;

    // Internet dependency penalty
    if (metrics.optimized_required_internet) {
        complexity += 20.0;
    }

    // Dependencies component
    complexity += metrics.optimized_dependencies_count * 2.0;

    return complexity;
}

// IntegrationMetricsManager implementation
bool IntegrationMetricsManager::initialize(const std::string& log_file, bool enable_console) {
    collector_ = std::make_unique<StandardMetricsCollector>();
    logger_ = std::make_unique<JsonMetricsLogger>();
    analyzer_ = std::make_unique<StandardSetupComplexityAnalyzer>();

    if (!logger_->initialize(log_file, enable_console)) {
        return false;
    }

    collector_->set_logger(std::move(logger_));

    initialized_ = true;
    return true;
}

MetricsCollector& IntegrationMetricsManager::collector() {
    return *collector_;
}

SetupComplexityAnalyzer& IntegrationMetricsManager::analyzer() {
    return *analyzer_;
}

std::string IntegrationMetricsManager::setup_auto_tracking(MetricsCollector& collector) {
    // Create a unique tracker ID
    static std::atomic<uint64_t> counter{0};
    std::string tracker_id = "setup_tracker_" + std::to_string(counter++);

    // Record initial setup metrics
    collector.record_metric("setup_tracker_active",
                          MetricValue(MetricType::COUNTER),
                          {{"tracker_id", tracker_id}});

    return tracker_id;
}

void IntegrationMetricsManager::log_complexity_reduction_report(
    const SetupComplexityMetrics& baseline,
    const SetupComplexityMetrics& current) {

    // Calculate improvements
    SetupComplexityMetrics report = current;
    report.time_reduction_percentage = analyzer_->calculate_complexity_reduction(baseline, current);

    // Generate and log report
    if (collector_) {
        collector_->record_setup_metrics(report);
    }
}

std::unique_ptr<MetricsCollector> IntegrationMetricsManager::create_collector() {
    return std::make_unique<StandardMetricsCollector>();
}

std::unique_ptr<MetricsLogger> IntegrationMetricsManager::create_logger() {
    return std::make_unique<JsonMetricsLogger>();
}

std::unique_ptr<SetupComplexityAnalyzer> IntegrationMetricsManager::create_analyzer() {
    return std::make_unique<StandardSetupComplexityAnalyzer>();
}

// ScopedTimer implementation
ScopedTimer::ScopedTimer(MetricsCollector& collector,
                         const std::string& name,
                         const std::map<std::string, std::string>& tags)
    : collector_(collector) {
    timer_id_ = collector_.start_timer(name, tags);
}

ScopedTimer::~ScopedTimer() {
    collector_.end_timer(timer_id_);
}

// SetupMetricsBuilder implementation
SetupMetricsBuilder& SetupMetricsBuilder::set_original_setup_time(std::chrono::milliseconds time) {
    metrics_.original_setup_time = time;
    return *this;
}

SetupMetricsBuilder& SetupMetricsBuilder::set_original_steps(size_t count) {
    metrics_.original_steps_count = count;
    return *this;
}

SetupMetricsBuilder& SetupMetricsBuilder::set_original_dependencies(size_t count) {
    metrics_.original_dependencies_count = count;
    return *this;
}

SetupMetricsBuilder& SetupMetricsBuilder::set_original_requires_internet(bool requires) {
    metrics_.original_required_internet = requires;
    return *this;
}

SetupMetricsBuilder& SetupMetricsBuilder::set_original_manual_operations(size_t count) {
    metrics_.original_manual_operations = count;
    return *this;
}

SetupMetricsBuilder& SetupMetricsBuilder::set_optimized_setup_time(std::chrono::milliseconds time) {
    metrics_.optimized_setup_time = time;
    return *this;
}

SetupMetricsBuilder& SetupMetricsBuilder::set_optimized_steps(size_t count) {
    metrics_.optimized_steps_count = count;
    return *this;
}

SetupMetricsBuilder& SetupMetricsBuilder::set_optimized_dependencies(size_t count) {
    metrics_.optimized_dependencies_count = count;
    return *this;
}

SetupMetricsBuilder& SetupMetricsBuilder::set_optimized_requires_internet(bool requires) {
    metrics_.optimized_required_internet = requires;
    return *this;
}

SetupMetricsBuilder& SetupMetricsBuilder::set_optimized_manual_operations(size_t count) {
    metrics_.optimized_manual_operations = count;
    return *this;
}

SetupMetricsBuilder& SetupMetricsBuilder::set_success_rates(double setup_rate, double build_rate) {
    metrics_.setup_success_rate = setup_rate;
    metrics_.build_success_rate = build_rate;
    return *this;
}

SetupMetricsBuilder& SetupMetricsBuilder::set_attempts(size_t setup_attempts, size_t successful_setups) {
    metrics_.setup_attempts = setup_attempts;
    metrics_.successful_setups = successful_setups;
    return *this;
}

SetupComplexityMetrics SetupMetricsBuilder::build() const {
    return metrics_;
}

// Utility functions
std::string format_time_duration(std::chrono::milliseconds duration) {
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
    auto minutes = std::chrono::duration_cast<std::chrono::minutes>(duration);
    auto hours = std::chrono::duration_cast<std::chrono::hours>(duration);

    if (hours.count() > 0) {
        return std::to_string(hours.count()) + "h " +
               std::to_string(minutes.count() % 60) + "m " +
               std::to_string(seconds.count() % 60) + "s";
    } else if (minutes.count() > 0) {
        return std::to_string(minutes.count()) + "m " +
               std::to_string(seconds.count() % 60) + "s";
    } else {
        return std::to_string(duration.count()) + "ms";
    }
}

std::string format_percentage(double percentage) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << percentage << "%";
    return oss.str();
}

std::string create_setup_metric_name(const std::string& operation) {
    return "setup_" + operation;
}

std::map<std::string, std::string> create_default_tags(const std::string& component) {
    return {
        {"component", component},
        {"integration", "third_party"},
        {"timestamp", std::to_string(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count())}
    };
}

} // namespace metrics
} // namespace integration