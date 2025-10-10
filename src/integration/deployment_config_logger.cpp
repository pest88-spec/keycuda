/**
 * Puzzle71Solver - Deployment Configuration Logger
 *
 * Provides comprehensive logging of deployment configuration decisions,
 * build choices, and deployment parameters for visibility and debugging.
 *
 * @author       Puzzle71Solver Team
 * @created      2025-10-10
 * @license      MIT
 */

#include "integration/deployment_config_logger.h"
#include "integration/logging/integration_logger.h"
#include <chrono>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <algorithm>

namespace integration {
namespace deployment {

// Initialize static members
std::unique_ptr<DeploymentConfigLogger> DeploymentConfigLogger::s_instance = nullptr;
std::mutex DeploymentConfigLogger::s_instance_mutex;

DeploymentConfigLogger& DeploymentConfigLogger::get_instance() {
    std::lock_guard<std::mutex> lock(s_instance_mutex);
    if (!s_instance) {
        s_instance = std::unique_ptr<DeploymentConfigLogger>(new DeploymentConfigLogger());
    }
    return *s_instance;
}

DeploymentConfigLogger::DeploymentConfigLogger()
    : m_enabled(true)
    , m_log_level(LogLevel::INFO)
    , m_start_time(std::chrono::system_clock::now()) {
}

DeploymentConfigLogger::~DeploymentConfigLogger() = default;

void DeploymentConfigLogger::initialize(const std::string& log_file_path, LogLevel level) {
    std::lock_guard<std::mutex> lock(m_mutex);

    m_log_level = level;
    m_log_file_path = log_file_path;

    if (!m_log_file_path.empty()) {
        // Create log file directory if it doesn't exist
        std::filesystem::path log_path(log_file_path);
        std::filesystem::create_directories(log_path.parent_path());

        log_decision("CONFIG_LOGGER_INIT", "Deployment configuration logger initialized", {
            {"log_file", log_file_path},
            {"log_level", log_level_to_string(level)},
            {"timestamp", get_current_timestamp()}
        });
    }
}

void DeploymentConfigLogger::set_enabled(bool enabled) {
    std::lock_guard<std::mutex> lock(m_mutex);
    bool was_enabled = m_enabled;
    m_enabled = enabled;

    if (enabled && !was_enabled) {
        log_decision("CONFIG_LOGGER_ENABLED", "Deployment configuration logging enabled", {
            {"timestamp", get_current_timestamp()}
        });
    } else if (!enabled && was_enabled) {
        log_decision("CONFIG_LOGGER_DISABLED", "Deployment configuration logging disabled", {
            {"timestamp", get_current_timestamp()}
        });
    }
}

void DeploymentConfigLogger::log_decision(const std::string& decision_type,
                                        const std::string& description,
                                        const std::map<std::string, std::string>& context) {
    if (!m_enabled) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    // Create decision entry
    ConfigDecision decision;
    decision.decision_type = decision_type;
    decision.description = description;
    decision.context = context;
    decision.timestamp = timestamp;
    decision.iso_timestamp = format_iso_timestamp(now);

    m_decision_history.push_back(decision);

    // Log to integration logger
    std::stringstream ss;
    ss << "[CONFIG_DECISION] " << decision_type << ": " << description;

    if (!context.empty()) {
        ss << " | Context: ";
        bool first = true;
        for (const auto& [key, value] : context) {
            if (!first) ss << ", ";
            ss << key << "=" << value;
            first = false;
        }
    }

    auto& logger = integration::logging::IntegrationLogger::get_instance();
    logger.log_info("DeploymentConfig", ss.str());

    // Write to log file if configured
    if (!m_log_file_path.empty()) {
        write_decision_to_file(decision);
    }
}

void DeploymentConfigLogger::log_cmake_option(const std::string& option_name,
                                            const std::string& option_value,
                                            const std::string& source) {
    log_decision("CMAKE_OPTION", "CMake configuration option set", {
        {"option", option_name},
        {"value", option_value},
        {"source", source},
        {"type", "configuration"}
    });
}

void DeploymentConfigLogger::log_feature_flag(const std::string& feature_name,
                                            bool enabled,
                                            const std::string& reason) {
    log_decision("FEATURE_FLAG", "Feature flag configuration", {
        {"feature", feature_name},
        {"enabled", enabled ? "true" : "false"},
        {"reason", reason},
        {"type", "feature"}
    });
}

void DeploymentConfigLogger::log_dependency_resolution(const std::string& dependency_name,
                                                     const std::string& resolution_type,
                                                     const std::string& version,
                                                     const std::string& source_path) {
    log_decision("DEPENDENCY_RESOLUTION", "Dependency resolution completed", {
        {"dependency", dependency_name},
        {"resolution_type", resolution_type},
        {"version", version},
        {"source_path", source_path},
        {"type", "dependency"}
    });
}

void DeploymentConfigLogger::log_build_configuration(const std::string& config_type,
                                                    const std::map<std::string, std::string>& config) {
    std::map<std::string, std::string> context = config;
    context["config_type"] = config_type;
    context["type"] = "build_config";

    log_decision("BUILD_CONFIGURATION", "Build configuration determined", context);
}

void DeploymentConfigLogger::log_deployment_target(const std::string& target_name,
                                                 const std::vector<std::string>& dependencies,
                                                 const std::map<std::string, std::string>& properties) {
    std::map<std::string, std::string> context = properties;
    context["target_name"] = target_name;
    context["dependency_count"] = std::to_string(dependencies.size());
    context["dependencies"] = join_strings(dependencies, ",");
    context["type"] = "deployment_target";

    log_decision("DEPLOYMENT_TARGET", "Deployment target configured", context);
}

void DeploymentConfigLogger::log_package_configuration(const std::string& package_format,
                                                     const std::map<std::string, std::string>& package_config) {
    std::map<std::string, std::string> context = package_config;
    context["package_format"] = package_format;
    context["type"] = "package_config";

    log_decision("PACKAGE_CONFIGURATION", "Package configuration determined", context);
}

void DeploymentConfigLogger::log_environment_detection(const std::map<std::string, std::string>& env_info) {
    std::map<std::string, std::string> context = env_info;
    context["type"] = "environment";

    log_decision("ENVIRONMENT_DETECTION", "Deployment environment detected", context);
}

std::vector<ConfigDecision> DeploymentConfigLogger::get_decision_history() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_decision_history;
}

std::vector<ConfigDecision> DeploymentConfigLogger::get_decisions_by_type(const std::string& decision_type) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::vector<ConfigDecision> filtered;
    std::copy_if(m_decision_history.begin(), m_decision_history.end(),
                 std::back_inserter(filtered),
                 [&decision_type](const ConfigDecision& decision) {
                     return decision.decision_type == decision_type;
                 });

    return filtered;
}

std::string DeploymentConfigLogger::generate_configuration_report() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::stringstream report;

    // Header
    report << "# Deployment Configuration Report\n\n";
    report << "**Generated**: " << format_iso_timestamp(std::chrono::system_clock::now()) << "\n";
    report << "**Total Decisions**: " << m_decision_history.size() << "\n";
    report << "**Logger Duration**: "
           << std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now() - m_start_time).count()
           << " seconds\n\n";

    // Summary by type
    std::map<std::string, size_t> type_counts;
    for (const auto& decision : m_decision_history) {
        type_counts[decision.decision_type]++;
    }

    report << "## Decision Summary by Type\n\n";
    for (const auto& [type, count] : type_counts) {
        report << "- **" << type << "**: " << count << " decisions\n";
    }
    report << "\n";

    // Detailed decisions
    report << "## Detailed Decision History\n\n";

    for (const auto& decision : m_decision_history) {
        report << "### " << decision.decision_type << "\n";
        report << "**Timestamp**: " << decision.iso_timestamp << "\n";
        report << "**Description**: " << decision.description << "\n";

        if (!decision.context.empty()) {
            report << "**Context**:\n";
            for (const auto& [key, value] : decision.context) {
                report << "- " << key << ": " << value << "\n";
            }
        }
        report << "\n";
    }

    return report.str();
}

bool DeploymentConfigLogger::save_configuration_report(const std::string& output_path) const {
    try {
        std::string report = generate_configuration_report();

        std::filesystem::path path(output_path);
        std::filesystem::create_directories(path.parent_path());

        std::ofstream file(output_path);
        if (!file.is_open()) {
            return false;
        }

        file << report;
        file.close();

        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

void DeploymentConfigLogger::clear_history() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_decision_history.clear();
    m_start_time = std::chrono::system_clock::now();

    log_decision("HISTORY_CLEARED", "Configuration decision history cleared", {
        {"timestamp", get_current_timestamp()},
        {"type", "maintenance"}
    });
}

size_t DeploymentConfigLogger::get_decision_count() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_decision_history.size();
}

std::string DeploymentConfigLogger::get_current_timestamp() const {
    return format_iso_timestamp(std::chrono::system_clock::now());
}

std::string DeploymentConfigLogger::format_iso_timestamp(const std::chrono::system_clock::time_point& time_point) const {
    auto time_t = std::chrono::system_clock::to_time_t(time_point);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        time_point.time_since_epoch()) % 1000;

    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%d %H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count() << " UTC";

    return ss.str();
}

std::string DeploymentConfigLogger::log_level_to_string(LogLevel level) const {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO: return "INFO";
        case LogLevel::WARN: return "WARN";
        case LogLevel::ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

std::string DeploymentConfigLogger::join_strings(const std::vector<std::string>& strings, const std::string& delimiter) const {
    if (strings.empty()) {
        return "";
    }

    std::stringstream ss;
    for (size_t i = 0; i < strings.size(); ++i) {
        if (i > 0) ss << delimiter;
        ss << strings[i];
    }

    return ss.str();
}

void DeploymentConfigLogger::write_decision_to_file(const ConfigDecision& decision) {
    try {
        std::ofstream file(m_log_file_path, std::ios::app);
        if (!file.is_open()) {
            return;
        }

        // JSON format for structured parsing
        file << "{\n";
        file << "  \"timestamp\": " << decision.timestamp << ",\n";
        file << "  \"iso_timestamp\": \"" << decision.iso_timestamp << "\",\n";
        file << "  \"type\": \"" << decision.decision_type << "\",\n";
        file << "  \"description\": \"" << decision.description << "\",\n";
        file << "  \"context\": {\n";

        bool first = true;
        for (const auto& [key, value] : decision.context) {
            if (!first) file << ",\n";
            file << "    \"" << key << "\": \"" << value << "\"";
            first = false;
        }

        file << "\n  }\n";
        file << "}\n";
        file.close();

    } catch (const std::exception& e) {
        // Ignore file write errors to avoid infinite recursion
    }
}

} // namespace deployment
} // namespace integration