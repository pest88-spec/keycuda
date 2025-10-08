#include "ComputeCore/gpu/performance/configuration_logger.h"
#include <iomanip>
#include <algorithm>
#include <filesystem>
#include <random>

namespace keycuda {
namespace gpu {
namespace performance {

ConfigurationLogger::ConfigurationLogger()
    : log_file_path_("configuration_decisions.log")
    , file_logging_enabled_(true)
    , current_log_level_(LogLevel::INFO)
    , max_log_entries_(10000)
    , real_time_analysis_enabled_(true)
    , total_decisions_(0)
    , last_analysis_(std::chrono::system_clock::now())
    , cumulative_confidence_(0.0)
    , confidence_samples_(0) {
}

ConfigurationLogger::ConfigurationLogger(const std::string& log_file_path)
    : log_file_path_(log_file_path)
    , file_logging_enabled_(true)
    , current_log_level_(LogLevel::INFO)
    , max_log_entries_(10000)
    , real_time_analysis_enabled_(true)
    , total_decisions_(0)
    , last_analysis_(std::chrono::system_clock::now())
    , cumulative_confidence_(0.0)
    , confidence_samples_(0) {
}

ConfigurationLogger::~ConfigurationLogger() {
    // Final analysis before shutdown
    if (real_time_analysis_enabled_) {
        auto analysis = GenerateAnalysis(std::chrono::hours(1));
        if (analysis.total_decisions > 0) {
            std::cout << "[CONFIG_LOGGER] Final analysis: " << analysis.total_decisions
                      << " decisions, avg confidence: " << std::fixed << std::setprecision(3)
                      << analysis.average_confidence_score * 100 << "%" << std::endl;
        }
    }
}

void ConfigurationLogger::LogConfigurationDecision(const ConfigurationDecision& decision) {
    if (decision.log_level < current_log_level_) {
        return;
    }

    std::lock_guard<std::mutex> lock(decisions_mutex_);

    // Add to memory buffer
    decisions_.push_back(decision);

    // Maintain buffer size
    if (decisions_.size() > max_log_entries_) {
        decisions_.erase(decisions_.begin());
    }

    // Update statistics
    UpdateStatistics(decision);

    // Format and output
    std::string console_output = FormatDecisionForConsole(decision);
    std::cout << "[CONFIG_DECISION] " << console_output << std::endl;

    // Write to file if enabled
    if (file_logging_enabled_) {
        nlohmann::json decision_json = DecisionToJson(decision);
        WriteToFile(decision_json.dump(2));
    }

    // Real-time analysis
    if (real_time_analysis_enabled_) {
        PerformRealTimeAnalysis(decision);
    }
}

void ConfigurationLogger::LogInitialConfiguration(const ParallelismConfiguration& config,
                                                    const GpuCapabilities& gpu_caps,
                                                    size_t workload_size,
                                                    double confidence_score,
                                                    const std::string& rationale) {
    ConfigurationDecision decision;
    decision.decision_id = GenerateDecisionId();
    decision.timestamp = std::chrono::system_clock::now();
    decision.decision_type = DecisionType::INITIAL_CONFIGURATION;
    decision.log_level = LogLevel::INFO;

    // GPU context
    decision.gpu_name = gpu_caps.device_name;
    decision.compute_capability = gpu_caps.compute_capability;
    decision.total_memory_mb = gpu_caps.total_memory_mb;
    decision.free_memory_mb = gpu_caps.free_memory_mb;
    decision.memory_bandwidth_gb_per_sec = gpu_caps.memory_bandwidth_gb_per_sec;
    decision.sm_count = gpu_caps.sm_count;

    // Configuration
    decision.input_config = config;
    decision.output_config = config;
    decision.workload_size = workload_size;
    decision.operation_type = "initial_setup";

    // Decision details
    decision.decision_logic = "initial_gpu_capability_analysis";
    decision.rationale = rationale;
    decision.confidence_score = confidence_score;
    decision.estimated_throughput_mkeys_per_sec = 1000.0; // Default estimate
    decision.memory_utilization_estimate = config.memory_utilization_estimate;
    decision.expected_occupancy = config.expected_occupancy;

    LogConfigurationDecision(decision);
}

void ConfigurationLogger::LogMemoryConstraintAdjustment(const ParallelismConfiguration& input_config,
                                                          const ParallelismConfiguration& output_config,
                                                          const GpuCapabilities& gpu_caps,
                                                          double memory_pressure,
                                                          const std::string& constraint_type) {
    ConfigurationDecision decision;
    decision.decision_id = GenerateDecisionId();
    decision.timestamp = std::chrono::system_clock::now();
    decision.decision_type = DecisionType::MEMORY_CONSTRAINT_ADJUSTMENT;
    decision.log_level = memory_pressure > 0.9 ? LogLevel::WARNING : LogLevel::INFO;

    // GPU context
    decision.gpu_name = gpu_caps.device_name;
    decision.compute_capability = gpu_caps.compute_capability;
    decision.total_memory_mb = gpu_caps.total_memory_mb;
    decision.free_memory_mb = gpu_caps.free_memory_mb;
    decision.memory_bandwidth_gb_per_sec = gpu_caps.memory_bandwidth_gb_per_sec;
    decision.sm_count = gpu_caps.sm_count;

    // Configuration
    decision.input_config = input_config;
    decision.output_config = output_config;
    decision.operation_type = "memory_constraint_handling";

    // Decision details
    decision.decision_logic = "memory_pressure_analysis";
    decision.rationale = "Memory utilization (" + std::to_string(memory_pressure * 100) +
                        "%) exceeded threshold, applying " + constraint_type;
    decision.confidence_score = 0.8;
    decision.constraints_applied.push_back(constraint_type);
    decision.metadata["memory_pressure"] = std::to_string(memory_pressure);
    decision.metadata["constraint_type"] = constraint_type;

    // Performance impact estimation
    double scaling_factor = static_cast<double>(output_config.points_per_thread * output_config.block_size) /
                          static_cast<double>(input_config.points_per_thread * input_config.block_size);
    decision.estimated_throughput_mkeys_per_sec = 1000.0 * scaling_factor; // Rough estimate

    LogConfigurationDecision(decision);
}

void ConfigurationLogger::LogPerformanceBasedScaling(const ParallelismConfiguration& input_config,
                                                     const ParallelismConfiguration& output_config,
                                                     double current_throughput,
                                                     double target_throughput,
                                                     const std::string& scaling_reason) {
    ConfigurationDecision decision;
    decision.decision_id = GenerateDecisionId();
    decision.timestamp = std::chrono::system_clock::now();
    decision.decision_type = DecisionType::PERFORMANCE_BASED_SCALING;
    decision.log_level = LogLevel::INFO;

    // Configuration
    decision.input_config = input_config;
    decision.output_config = output_config;
    decision.operation_type = "performance_optimization";

    // Decision details
    decision.decision_logic = "performance_gap_analysis";
    double performance_ratio = current_throughput / target_throughput;
    decision.rationale = "Performance gap detected (" + std::to_string(performance_ratio * 100) +
                        "% of target): " + scaling_reason;
    decision.confidence_score = 0.9;
    decision.metadata["current_throughput"] = std::to_string(current_throughput);
    decision.metadata["target_throughput"] = std::to_string(target_throughput);
    decision.metadata["performance_ratio"] = std::to_string(performance_ratio);

    // Performance impact estimation
    double estimated_improvement = static_cast<double>(output_config.points_per_thread * output_config.block_size) /
                                     static_cast<double>(input_config.points_per_thread * input_config.block_size);
    decision.estimated_throughput_mkeys_per_sec = current_throughput * estimated_improvement;

    LogConfigurationDecision(decision);
}

void ConfigurationLogger::LogDynamicRuntimeScaling(const ParallelismConfiguration& old_config,
                                                    const ParallelismConfiguration& new_config,
                                                    double performance_ratio,
                                                    const std::string& trigger_event) {
    ConfigurationDecision decision;
    decision.decision_id = GenerateDecisionId();
    decision.timestamp = std::chrono::system_clock::now();
    decision.decision_type = DecisionType::DYNAMIC_RUNTIME_SCALING;
    decision.log_level = performance_ratio < 0.5 ? LogLevel::WARNING : LogLevel::INFO;

    // Configuration
    decision.input_config = old_config;
    decision.output_config = new_config;
    decision.operation_type = "runtime_adaptation";

    // Decision details
    decision.decision_logic = "runtime_performance_monitoring";
    decision.rationale = "Dynamic scaling triggered: " + trigger_event +
                        " (performance ratio: " + std::to_string(performance_ratio * 100) + "%)";
    decision.confidence_score = 0.7;
    decision.metadata["performance_ratio"] = std::to_string(performance_ratio);
    decision.metadata["trigger_event"] = trigger_event;

    // Performance impact
    double scaling_factor = static_cast<double>(new_config.points_per_thread * new_config.block_size) /
                          static_cast<double>(old_config.points_per_thread * old_config.block_size);
    decision.estimated_throughput_mkeys_per_sec = 1000.0 * scaling_factor;

    LogConfigurationDecision(decision);
}

void ConfigurationLogger::LogFallbackActivation(const std::string& reason,
                                                const ParallelismConfiguration& safe_config,
                                                const std::vector<std::string>& attempted_configs) {
    ConfigurationDecision decision;
    decision.decision_id = GenerateDecisionId();
    decision.timestamp = std::chrono::system_clock::now();
    decision.decision_type = DecisionType::FALLBACK_ACTIVATION;
    decision.log_level = LogLevel::WARNING;

    // Configuration
    decision.input_config = safe_config;  // Use safe as input for logging purposes
    decision.output_config = safe_config;
    decision.operation_type = "error_recovery";

    // Decision details
    decision.decision_logic = "error_recovery_mechanism";
    decision.rationale = "Fallback activated: " + reason;
    decision.confidence_score = 0.6;
    decision.error_message = reason;

    // Add attempted configurations as metadata
    for (size_t i = 0; i < attempted_configs.size(); ++i) {
        decision.metadata["attempted_config_" + std::to_string(i)] = attempted_configs[i];
    }
    decision.metadata["fallback_reason"] = reason;

    LogConfigurationDecision(decision);
}

void ConfigurationLogger::LogError(const std::string& error_message,
                                    const ParallelismConfiguration& failed_config,
                                    const std::string& error_type) {
    ConfigurationDecision decision;
    decision.decision_id = GenerateDecisionId();
    decision.timestamp = std::chrono::system_clock::now();
    decision.decision_type = DecisionType::ERROR_RECOVERY;
    decision.log_level = LogLevel::ERROR;

    // Configuration
    decision.input_config = failed_config;
    decision.output_config = failed_config;
    decision.operation_type = "error_handling";

    // Decision details
    decision.decision_logic = "error_handling";
    decision.rationale = "Error encountered during configuration: " + error_type;
    decision.confidence_score = 0.0;
    decision.error_message = error_message;
    decision.metadata["error_type"] = error_type;

    LogConfigurationDecision(decision);
}

DecisionAnalysis ConfigurationLogger::GenerateAnalysis(std::chrono::hours time_window) const {
    std::lock_guard<std::mutex> lock(decisions_mutex_);

    DecisionAnalysis analysis;
    analysis.analysis_timestamp = std::chrono::system_clock::now();

    auto cutoff_time = std::chrono::system_clock::now() - time_window;

    // Filter decisions within time window
    std::vector<ConfigurationDecision> recent_decisions;
    for (const auto& decision : decisions_) {
        if (decision.timestamp >= cutoff_time) {
            recent_decisions.push_back(decision);
        }
    }

    analysis.total_decisions = recent_decisions.size();

    // Count by decision type
    for (const auto& decision : recent_decisions) {
        analysis.decision_type_counts[decision.decision_type]++;

        if (decision.confidence_score > 0.0) {
            analysis.average_confidence_score += decision.confidence_score;
        }

        // Track successful vs failed configurations
        if (decision.decision_type == DecisionType::ERROR_RECOVERY ||
            decision.decision_type == DecisionType::FALLBACK_ACTIVATION) {
            analysis.failed_configurations++;
        } else {
            analysis.successful_configurations++;
        }
    }

    // Calculate average confidence
    if (analysis.total_decisions > 0) {
        auto it = std::find_if(recent_decisions.begin(), recent_decisions.end(),
                              [](const ConfigurationDecision& d) { return d.confidence_score > 0.0; });
        if (it != recent_decisions.end()) {
            size_t confidence_count = 0;
            for (const auto& decision : recent_decisions) {
                if (decision.confidence_score > 0.0) {
                    analysis.average_confidence_score /= ++confidence_count;
                }
            }
        }
    }

    // Extract most common rationales
    analysis.most_common_rationales = ExtractTopRationales(5);

    // Analyze constraint frequency
    analysis.constraint_frequency = AnalyzeConstraintFrequency();

    // Identify performance trends
    analysis.performance_trends = IdentifyPerformanceTrends(time_window);
    analysis.average_throughput_improvement = CalculateThroughputTrend(time_window);

    return analysis;
}

std::vector<ConfigurationDecision> ConfigurationLogger::GetRecentDecisions(size_t count) const {
    std::lock_guard<std::mutex> lock(decisions_mutex_);

    std::vector<ConfigurationDecision> recent;
    size_t start_index = decisions_.size() > count ? decisions_.size() - count : 0;

    for (size_t i = start_index; i < decisions_.size(); ++i) {
        recent.push_back(decisions_[i]);
    }

    return recent;
}

std::vector<ConfigurationDecision> ConfigurationLogger::GetDecisionsByType(DecisionType type,
                                                                         std::chrono::hours time_window) const {
    std::lock_guard<std::mutex> lock(decisions_mutex_);

    std::vector<ConfigurationDecision> filtered;
    auto cutoff_time = std::chrono::system_clock::now() - time_window;

    for (const auto& decision : decisions_) {
        if (decision.decision_type == type && decision.timestamp >= cutoff_time) {
            filtered.push_back(decision);
        }
    }

    return filtered;
}

std::string ConfigurationLogger::ExportToJson(std::chrono::hours time_window) const {
    auto analysis = GenerateAnalysis(time_window);
    auto recent_decisions = GetRecentDecisions(1000);

    nlohmann::json export_data;
    export_data["analysis"] = {
        {"timestamp", std::chrono::duration_cast<std::chrono::seconds>(
            analysis.analysis_timestamp.time_since_epoch()).count()},
        {"total_decisions", analysis.total_decisions},
        {"successful_configurations", analysis.successful_configurations},
        {"failed_configurations", analysis.failed_configurations},
        {"average_confidence_score", analysis.average_confidence_score},
        {"average_throughput_improvement", analysis.average_throughput_improvement}
    };

    // Convert decisions
    nlohmann::json decisions_json = nlohmann::json::array();
    for (const auto& decision : recent_decisions) {
        decisions_json.push_back(DecisionToJson(decision));
    }
    export_data["decisions"] = decisions_json;

    return export_data.dump(2);
}

void ConfigurationLogger::SetLogLevel(LogLevel level) {
    current_log_level_ = level;
}

void ConfigurationLogger::EnableFileLogging(bool enabled) {
    file_logging_enabled_ = enabled;
}

void ConfigurationLogger::SetLogFilePath(const std::string& file_path) {
    log_file_path_ = file_path;
}

void ConfigurationLogger::SetMaxLogEntries(size_t max_entries) {
    max_log_entries_ = max_entries;
}

void ConfigurationLogger::EnableRealTimeAnalysis(bool enabled) {
    real_time_analysis_enabled_ = enabled;
}

size_t ConfigurationLogger::GetTotalDecisions() const {
    std::lock_guard<std::mutex> lock(decisions_mutex_);
    return decisions_.size();
}

size_t ConfigurationLogger::GetDecisionsInLastHour() const {
    return GetDecisionsByType(DecisionType::INITIAL_CONFIGURATION, std::chrono::hours(1)).size() +
           GetDecisionsByType(DecisionType::MEMORY_CONSTRAINT_ADJUSTMENT, std::chrono::hours(1)).size() +
           GetDecisionsByType(DecisionType::PERFORMANCE_BASED_SCALING, std::chrono::hours(1)).size() +
           GetDecisionsByType(DecisionType::DYNAMIC_RUNTIME_SCALING, std::chrono::hours(1)).size();
}

// Private methods implementation
std::string ConfigurationLogger::GenerateDecisionId() const {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(100000, 999999);

    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    return "DEC_" + std::to_string(timestamp) + "_" + std::to_string(dis(gen));
}

std::string ConfigurationLogger::DecisionTypeToString(DecisionType type) const {
    switch (type) {
        case DecisionType::INITIAL_CONFIGURATION: return "INITIAL_CONFIGURATION";
        case DecisionType::MEMORY_CONSTRAINT_ADJUSTMENT: return "MEMORY_CONSTRAINT_ADJUSTMENT";
        case DecisionType::PERFORMANCE_BASED_SCALING: return "PERFORMANCE_BASED_SCALING";
        case DecisionType::OCCUPANCY_OPTIMIZATION: return "OCCUPANCY_OPTIMIZATION";
        case DecisionType::DYNAMIC_RUNTIME_SCALING: return "DYNAMIC_RUNTIME_SCALING";
        case DecisionType::FALLBACK_ACTIVATION: return "FALLBACK_ACTIVATION";
        case DecisionType::ARCHITECTURE_SPECIFIC_TWEAK: return "ARCHITECTURE_SPECIFIC_TWEAK";
        case DecisionType::ERROR_RECOVERY: return "ERROR_RECOVERY";
        case DecisionType::CONSTRAINT_VIOLATION: return "CONSTRAINT_VIOLATION";
        case DecisionType::MANUAL_OVERRIDE: return "MANUAL_OVERRIDE";
        default: return "UNKNOWN";
    }
}

std::string ConfigurationLogger::FormatDecisionForConsole(const ConfigurationDecision& decision) const {
    std::stringstream ss;
    ss << "[" << DecisionTypeToString(decision.decision_type) << "] "
       << "ID:" << decision.decision_id.substr(0, 20) << "... "
       << "Config: " << decision.output_config.block_size << "x" << decision.output_config.grid_size
       << " PPT:" << decision.output_config.points_per_thread
       << " Confidence:" << std::fixed << std::setprecision(2) << decision.confidence_score * 100 << "%"
       << " Rationale: " << decision.rationale.substr(0, 80);

    if (decision.rationale.length() > 80) {
        ss << "...";
    }

    return ss.str();
}

nlohmann::json ConfigurationLogger::DecisionToJson(const ConfigurationDecision& decision) const {
    nlohmann::json j;
    j["decision_id"] = decision.decision_id;
    j["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        decision.timestamp.time_since_epoch()).count();
    j["decision_type"] = DecisionTypeToString(decision.decision_type);
    j["log_level"] = static_cast<int>(decision.log_level);

    // GPU context
    j["gpu_context"] = {
        {"name", decision.gpu_name},
        {"compute_capability", decision.compute_capability},
        {"total_memory_mb", decision.total_memory_mb},
        {"free_memory_mb", decision.free_memory_mb},
        {"memory_bandwidth_gb_per_sec", decision.memory_bandwidth_gb_per_sec},
        {"sm_count", decision.sm_count}
    };

    // Configuration
    j["input_config"] = {
        {"block_size", decision.input_config.block_size},
        {"grid_size", decision.input_config.grid_size},
        {"points_per_thread", decision.input_config.points_per_thread}
    };

    j["output_config"] = {
        {"block_size", decision.output_config.block_size},
        {"grid_size", decision.output_config.grid_size},
        {"points_per_thread", decision.output_config.points_per_thread}
    };

    j["workload_size"] = decision.workload_size;
    j["operation_type"] = decision.operation_type;

    // Decision details
    j["decision_logic"] = decision.decision_logic;
    j["rationale"] = decision.rationale;
    j["confidence_score"] = decision.confidence_score;
    j["constraints_applied"] = decision.constraints_applied;
    j["optimization_metrics"] = decision.optimization_metrics;

    // Performance impact
    j["performance_impact"] = {
        {"estimated_throughput_mkeys_per_sec", decision.estimated_throughput_mkeys_per_sec},
        {"estimated_execution_time_ms", decision.estimated_execution_time_ms},
        {"memory_utilization_estimate", decision.memory_utilization_estimate},
        {"expected_occupancy", decision.expected_occupancy}
    };

    j["metadata"] = decision.metadata;

    if (!decision.error_message.empty()) {
        j["error_message"] = decision.error_message;
    }

    return j;
}

void ConfigurationLogger::WriteToFile(const std::string& message) {
    try {
        std::ofstream log_file(log_file_path_, std::ios::app);
        if (log_file.is_open()) {
            auto now = std::chrono::system_clock::now();
            auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count();

            log_file << "[" << timestamp << "] " << message << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "[CONFIG_LOGGER] Failed to write to log file: " << e.what() << std::endl;
    }
}

void ConfigurationLogger::UpdateStatistics(const ConfigurationDecision& decision) {
    std::lock_guard<std::mutex> lock(stats_mutex_);

    total_decisions_++;
    decision_type_counts_[decision.decision_type]++;
    rationale_counts_[decision.rationale]++;

    if (decision.confidence_score > 0.0) {
        cumulative_confidence_ += decision.confidence_score;
        confidence_samples_++;
    }
}

void ConfigurationLogger::PerformRealTimeAnalysis(const ConfigurationDecision& decision) {
    auto now = std::chrono::system_clock::now();
    if (now - last_analysis_ > std::chrono::minutes(5)) { // Analyze every 5 minutes
        last_analysis_ = now;

        // Quick analysis for real-time feedback
        if (total_decisions_ > 0 && total_decisions_ % 10 == 0) {
            double avg_confidence = confidence_samples_ > 0 ? cumulative_confidence_ / confidence_samples_ : 0.0;

            if (avg_confidence < 0.5) {
                std::cout << "[CONFIG_LOGGER] Warning: Low average confidence score ("
                          << std::fixed << std::setprecision(2) << avg_confidence * 100
                          << "%) over " << total_decisions_ << " decisions" << std::endl;
            }
        }
    }
}

std::vector<std::string> ConfigurationLogger::ExtractTopRationales(size_t count) const {
    std::lock_guard<std::mutex> lock(stats_mutex_);

    std::vector<std::pair<std::string, size_t>> rationale_pairs;
    for (const auto& [rationale, count] : rationale_counts_) {
        rationale_pairs.emplace_back(rationale, count);
    }

    std::sort(rationale_pairs.begin(), rationale_pairs.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    std::vector<std::string> top_rationales;
    for (size_t i = 0; i < std::min(count, rationale_pairs.size()); ++i) {
        top_rationales.push_back(rationale_pairs[i].first);
    }

    return top_rationales;
}

std::map<std::string, size_t> ConfigurationLogger::AnalyzeConstraintFrequency() const {
    std::lock_guard<std::mutex> lock(decisions_mutex_);

    std::map<std::string, size_t> constraint_counts;

    for (const auto& decision : decisions_) {
        for (const auto& constraint : decision.constraints_applied) {
            constraint_counts[constraint]++;
        }
    }

    return constraint_counts;
}

std::vector<std::string> ConfigurationLogger::IdentifyPerformanceTrends(std::chrono::hours time_window) const {
    std::vector<std::string> trends;

    auto analysis = GenerateAnalysis(time_window);

    if (analysis.average_throughput_improvement > 0.1) {
        trends.push_back("Positive throughput improvement trend detected");
    } else if (analysis.average_throughput_improvement < -0.1) {
        trends.push_back("Negative throughput trend detected - review configurations");
    }

    if (analysis.average_confidence_score < 0.6) {
        trends.push_back("Low confidence scores - consider tuning decision logic");
    }

    if (analysis.failed_configurations > analysis.successful_configurations * 0.2) {
        trends.push_back("High failure rate - investigate fallback triggers");
    }

    return trends;
}

double ConfigurationLogger::CalculateThroughputTrend(std::chrono::hours time_window) const {
    std::lock_guard<std::mutex> lock(decisions_mutex_);

    auto cutoff_time = std::chrono::system_clock::now() - time_window;

    std::vector<double> throughput_values;
    for (const auto& decision : decisions_) {
        if (decision.timestamp >= cutoff_time && decision.estimated_throughput_mkeys_per_sec > 0) {
            throughput_values.push_back(decision.estimated_throughput_mkeys_per_sec);
        }
    }

    if (throughput_values.size() < 2) {
        return 0.0;
    }

    // Simple linear trend calculation
    double sum_x = 0, sum_y = 0, sum_xy = 0, sum_x2 = 0;
    for (size_t i = 0; i < throughput_values.size(); ++i) {
        sum_x += i;
        sum_y += throughput_values[i];
        sum_xy += i * throughput_values[i];
        sum_x2 += i * i;
    }

    double n = static_cast<double>(throughput_values.size());
    double slope = (n * sum_xy - sum_x * sum_y) / (n * sum_x2 - sum_x * sum_x);

    return slope; // Mkeys/sec per decision
}

// Factory function
std::unique_ptr<ConfigurationLogger> CreateConfigurationLogger(const std::string& log_file_path) {
    return std::make_unique<ConfigurationLogger>(log_file_path);
}

} // namespace performance
} // namespace gpu
} // namespace keycuda