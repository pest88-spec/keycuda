/**
 * @file health_monitor.cpp
 * @brief Health monitoring system implementation
 *
 * Implementation of the health monitoring system for the third-party
 * dependencies integration framework. Provides comprehensive health
 * monitoring, dependency conflict detection, and automated reporting.
 *
 * Created: 2025-10-22
 * Feature: Third-Party Dependencies Integration Optimization
 */

#include "health_monitor.h"
#include "../utils/digest_verifier.h"
#include "../utils/checkpoint_crypto.h"
#include "../utils/telemetry_logger.h"
#include "../core/uint256.h"
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <numeric>
#include <sstream>
#include <iomanip>
#include <random>

namespace integration {
namespace health {

// Implementation structure
struct StandardHealthMonitor::Impl {
    HealthMonitorConfig config;
    std::atomic<bool> is_monitoring{false};
    std::thread monitoring_thread;
    std::mutex mutex;
    std::condition_variable cv;

    // Health checks
    std::map<std::string, std::pair<HealthCategory, HealthCheckFunction>> registered_checks;
    std::vector<HealthCheckResult> health_history;
    std::vector<DependencyConflict> dependency_conflicts;

    // Status tracking
    SystemHealthSummary last_summary;
    std::function<void(const SystemHealthSummary&)> status_change_callback;

    // Statistics
    std::map<std::string, double> statistics;
    std::atomic<uint64_t> total_checks_performed{0};
    std::atomic<uint64_t> conflicts_detected{0};
    std::chrono::system_clock::time_point start_time;

    Impl() : start_time(std::chrono::system_clock::now()) {
        setup_default_health_checks();
    }

    ~Impl() {
        stop_monitoring();
    }

    void setup_default_health_checks() {
        // Dependency health checks
        register_health_check("extracted_libraries", HealthCategory::DEPENDENCIES,
            [this]() { return check_extracted_libraries(); });

        register_health_check("integration_manifests", HealthCategory::DEPENDENCIES,
            [this]() { return check_integration_manifests(); });

        register_health_check("dependency_conflicts", HealthCategory::DEPENDENCIES,
            [this]() { return check_dependency_conflicts_health(); });

        // Build system checks
        register_health_check("cmake_configuration", HealthCategory::BUILD_SYSTEM,
            [this]() { return check_cmake_configuration(); });

        register_health_check("build_directory", HealthCategory::BUILD_SYSTEM,
            [this]() { return check_build_directory(); });

        // Integrity checks
        register_health_check("source_integrity", HealthCategory::INTEGRITY,
            [this]() { return check_source_integrity(); });

        register_health_check("attribution_coverage", HealthCategory::INTEGRITY,
            [this]() { return check_attribution_coverage(); });

        // Performance checks
        register_health_check("response_time", HealthCategory::PERFORMANCE,
            [this]() { return check_response_time(); });

        register_health_check("memory_usage", HealthCategory::PERFORMANCE,
            [this]() { return check_memory_usage(); });

        // Resource checks
        register_health_check("disk_space", HealthCategory::RESOURCES,
            [this]() { return check_disk_space(); });

        register_health_check("cpu_usage", HealthCategory::RESOURCES,
            [this]() { return check_cpu_usage(); });

        // Configuration checks
        register_health_check("project_structure", HealthCategory::CONFIGURATION,
            [this]() { return check_project_structure(); });

        register_health_check("git_status", HealthCategory::CONFIGURATION,
            [this]() { return check_git_status(); });
    }

    HealthCheckResult check_extracted_libraries() {
        HealthCheckResult result;
        result.check_name = "extracted_libraries";
        result.category = HealthCategory::DEPENDENCIES;
        result.timestamp = std::chrono::system_clock::now();

        const std::string extracted_path = "src/extracted";

        try {
            if (!std::filesystem::exists(extracted_path)) {
                result.status = HealthStatus::CRITICAL;
                result.message = "Extracted libraries directory does not exist";
                result.metric_value = 0;
                result.metric_unit = "directories";
                return result;
            }

            std::vector<std::string> libraries;
            for (const auto& entry : std::filesystem::directory_iterator(extracted_path)) {
                if (entry.is_directory() && !entry.path().filename().string().starts_with(".")) {
                    libraries.push_back(entry.path().filename().string());
                }
            }

            result.metric_value = libraries.size();
            result.metric_unit = "libraries";
            result.details["libraries"] = std::accumulate(libraries.begin(), libraries.end(), std::string(),
                [](const std::string& acc, const std::string& lib) {
                    return acc.empty() ? lib : acc + "," + lib;
                });

            if (libraries.empty()) {
                result.status = HealthStatus::WARNING;
                result.message = "No extracted libraries found";
            } else {
                // Check if libraries have source files
                int libraries_with_sources = 0;
                for (const auto& lib : libraries) {
                    std::string lib_path = extracted_path + "/" + lib;
                    int source_files = 0;
                    for (const auto& entry : std::filesystem::recursive_directory_iterator(lib_path)) {
                        if (entry.is_regular_file()) {
                            std::string ext = entry.path().extension().string();
                            if (ext == ".c" || ext == ".cpp" || ext == ".h" || ext == ".hpp") {
                                source_files++;
                            }
                        }
                    }
                    if (source_files > 0) {
                        libraries_with_sources++;
                    }
                }

                if (libraries_with_sources == libraries.size()) {
                    result.status = HealthStatus::HEALTHY;
                    result.message = "All extracted libraries have source files";
                } else {
                    result.status = HealthStatus::WARNING;
                    result.message = "Some extracted libraries lack source files";
                    result.details["libraries_with_sources"] = std::to_string(libraries_with_sources);
                }
            }

        } catch (const std::exception& e) {
            result.status = HealthStatus::CRITICAL;
            result.message = "Error checking extracted libraries: " + std::string(e.what());
        }

        return result;
    }

    HealthCheckResult check_integration_manifests() {
        HealthCheckResult result;
        result.check_name = "integration_manifests";
        result.category = HealthCategory::DEPENDENCIES;
        result.timestamp = std::chrono::system_clock::now();

        const std::string manifests_path = "build/integration-manifests";

        try {
            if (!std::filesystem::exists(manifests_path)) {
                result.status = HealthStatus::WARNING;
                result.message = "Integration manifests directory does not exist";
                result.metric_value = 0;
                result.metric_unit = "manifests";
                return result;
            }

            std::vector<std::string> manifests;
            for (const auto& entry : std::filesystem::directory_iterator(manifests_path)) {
                if (entry.is_regular_file() && entry.path().extension() == ".json") {
                    manifests.push_back(entry.path().filename().string());
                }
            }

            result.metric_value = manifests.size();
            result.metric_unit = "manifests";

            if (manifests.empty()) {
                result.status = HealthStatus::WARNING;
                result.message = "No integration manifests found";
            } else {
                // Validate JSON format
                int valid_manifests = 0;
                for (const auto& manifest : manifests) {
                    std::string manifest_path = manifests_path + "/" + manifest;
                    std::ifstream file(manifest_path);
                    if (file.is_open()) {
                        // Simple JSON validation - check for basic structure
                        std::string content((std::istreambuf_iterator<char>(file)),
                                           std::istreambuf_iterator<char>());
                        if (content.find("\"manifest_id\"") != std::string::npos &&
                            content.find("\"schema_version\"") != std::string::npos) {
                            valid_manifests++;
                        }
                    }
                }

                if (valid_manifests == manifests.size()) {
                    result.status = HealthStatus::HEALTHY;
                    result.message = "All integration manifests are valid";
                } else {
                    result.status = HealthStatus::WARNING;
                    result.message = "Some integration manifests are invalid";
                    result.details["valid_manifests"] = std::to_string(valid_manifests);
                }
            }

        } catch (const std::exception& e) {
            result.status = HealthStatus::CRITICAL;
            result.message = "Error checking integration manifests: " + std::string(e.what());
        }

        return result;
    }

    HealthCheckResult check_dependency_conflicts_health() {
        HealthCheckResult result;
        result.check_name = "dependency_conflicts";
        result.category = HealthCategory::DEPENDENCIES;
        result.timestamp = std::chrono::system_clock::now();

        try {
            auto conflicts = check_dependency_conflicts();
            result.metric_value = conflicts.size();
            result.metric_unit = "conflicts";

            if (conflicts.empty()) {
                result.status = HealthStatus::HEALTHY;
                result.message = "No dependency conflicts detected";
            } else {
                // Count critical conflicts
                int critical_conflicts = 0;
                for (const auto& conflict : conflicts) {
                    if (conflict.severity == "critical" || conflict.severity == "high") {
                        critical_conflicts++;
                    }
                }

                if (critical_conflicts > 0) {
                    result.status = HealthStatus::CRITICAL;
                    result.message = "Critical dependency conflicts detected";
                    result.details["critical_conflicts"] = std::to_string(critical_conflicts);
                } else {
                    result.status = HealthStatus::WARNING;
                    result.message = "Dependency conflicts detected";
                }
            }

        } catch (const std::exception& e) {
            result.status = HealthStatus::CRITICAL;
            result.message = "Error checking dependency conflicts: " + std::string(e.what());
        }

        return result;
    }

    HealthCheckResult check_cmake_configuration() {
        HealthCheckResult result;
        result.check_name = "cmake_configuration";
        result.category = HealthCategory::BUILD_SYSTEM;
        result.timestamp = std::chrono::system_clock::now();

        const std::string cmake_file = "CMakeLists.txt";

        try {
            if (!std::filesystem::exists(cmake_file)) {
                result.status = HealthStatus::CRITICAL;
                result.message = "CMakeLists.txt does not exist";
                result.metric_value = 0;
                result.metric_unit = "files";
                return result;
            }

            std::ifstream file(cmake_file);
            if (!file.is_open()) {
                result.status = HealthStatus::CRITICAL;
                result.message = "Cannot open CMakeLists.txt";
                return result;
            }

            std::string content((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());

            result.metric_value = 1;
            result.metric_unit = "files";

            // Check for essential CMake components
            std::vector<std::string> required_components = {
                "cmake_minimum_required",
                "project",
                "add_executable"
            };

            int missing_components = 0;
            for (const auto& component : required_components) {
                if (content.find(component) == std::string::npos) {
                    missing_components++;
                }
            }

            if (missing_components == 0) {
                result.status = HealthStatus::HEALTHY;
                result.message = "CMakeLists.txt has all required components";
            } else {
                result.status = HealthStatus::WARNING;
                result.message = "CMakeLists.txt missing required components";
                result.details["missing_components"] = std::to_string(missing_components);
            }

        } catch (const std::exception& e) {
            result.status = HealthStatus::CRITICAL;
            result.message = "Error checking CMake configuration: " + std::string(e.what());
        }

        return result;
    }

    HealthCheckResult check_build_directory() {
        HealthCheckResult result;
        result.check_name = "build_directory";
        result.category = HealthCategory::BUILD_SYSTEM;
        result.timestamp = std::chrono::system_clock::now();

        const std::string build_path = "build";

        try {
            if (!std::filesystem::exists(build_path)) {
                result.status = HealthStatus::WARNING;
                result.message = "Build directory does not exist";
                result.metric_value = 0;
                result.metric_unit = "directories";
                return result;
            }

            result.metric_value = 1;
            result.metric_unit = "directories";

            // Check for CMake cache
            const std::string cache_file = build_path + "/CMakeCache.txt";
            if (std::filesystem::exists(cache_file)) {
                result.status = HealthStatus::HEALTHY;
                result.message = "Build directory is configured";
            } else {
                result.status = HealthStatus::WARNING;
                result.message = "Build directory exists but not configured";
            }

        } catch (const std::exception& e) {
            result.status = HealthStatus::CRITICAL;
            result.message = "Error checking build directory: " + std::string(e.what());
        }

        return result;
    }

    HealthCheckResult check_source_integrity() {
        HealthCheckResult result;
        result.check_name = "source_integrity";
        result.category = HealthCategory::INTEGRITY;
        result.timestamp = std::chrono::system_clock::now();

        try {
            const std::string extracted_path = "src/extracted";
            if (!std::filesystem::exists(extracted_path)) {
                result.status = HealthStatus::WARNING;
                result.message = "No extracted libraries to check";
                result.metric_value = 0;
                result.metric_unit = "files";
                return result;
            }

            int total_files = 0;
            int checked_files = 0;

            for (const auto& entry : std::filesystem::recursive_directory_iterator(extracted_path)) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    if (ext == ".c" || ext == ".cpp" || ext == ".h" || ext == ".hpp") {
                        total_files++;
                        // Simple integrity check - file can be opened and read
                        std::ifstream file(entry.path());
                        if (file.is_open()) {
                            file.seekg(0, std::ios::end);
                            if (file.tellg() > 0) {
                                checked_files++;
                            }
                        }
                    }
                }
            }

            result.metric_value = total_files;
            result.metric_unit = "files";
            result.details["checked_files"] = std::to_string(checked_files);

            if (total_files == 0) {
                result.status = HealthStatus::WARNING;
                result.message = "No source files found in extracted libraries";
            } else if (checked_files == total_files) {
                result.status = HealthStatus::HEALTHY;
                result.message = "All source files passed integrity check";
            } else {
                result.status = HealthStatus::WARNING;
                result.message = "Some source files failed integrity check";
            }

        } catch (const std::exception& e) {
            result.status = HealthStatus::CRITICAL;
            result.message = "Error checking source integrity: " + std::string(e.what());
        }

        return result;
    }

    HealthCheckResult check_attribution_coverage() {
        HealthCheckResult result;
        result.check_name = "attribution_coverage";
        result.category = HealthCategory::INTEGRITY;
        result.timestamp = std::chrono::system_clock::now();

        try {
            const std::string extracted_path = "src/extracted";
            if (!std::filesystem::exists(extracted_path)) {
                result.status = HealthStatus::WARNING;
                result.message = "No extracted libraries to check";
                result.metric_value = 0;
                result.metric_unit = "files";
                return result;
            }

            int total_files = 0;
            int files_with_attribution = 0;

            for (const auto& entry : std::filesystem::recursive_directory_iterator(extracted_path)) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    if (ext == ".c" || ext == ".cpp" || ext == ".h" || ext == ".hpp") {
                        total_files++;
                        std::ifstream file(entry.path());
                        if (file.is_open()) {
                            std::string content((std::istreambuf_iterator<char>(file)),
                                               std::istreambuf_iterator<char>());
                            // Check for attribution indicators
                            if (content.find("SPDX-License-Identifier:") != std::string::npos ||
                                content.find("Copyright") != std::string::npos ||
                                content.find("Original author") != std::string::npos) {
                                files_with_attribution++;
                            }
                        }
                    }
                }
            }

            result.metric_value = total_files;
            result.metric_unit = "files";

            if (total_files == 0) {
                result.status = HealthStatus::WARNING;
                result.message = "No source files found for attribution check";
            } else {
                double coverage = (double)files_with_attribution / total_files * 100;
                result.details["coverage_percentage"] = std::to_string(coverage);

                if (coverage >= 95) {
                    result.status = HealthStatus::HEALTHY;
                    result.message = "Excellent attribution coverage";
                } else if (coverage >= 80) {
                    result.status = HealthStatus::WARNING;
                    result.message = "Good attribution coverage with room for improvement";
                } else {
                    result.status = HealthStatus::DEGRADED;
                    result.message = "Poor attribution coverage";
                }
            }

        } catch (const std::exception& e) {
            result.status = HealthStatus::CRITICAL;
            result.message = "Error checking attribution coverage: " + std::string(e.what());
        }

        return result;
    }

    HealthCheckResult check_response_time() {
        HealthCheckResult result;
        result.check_name = "response_time";
        result.category = HealthCategory::PERFORMANCE;
        result.timestamp = std::chrono::system_clock::now();

        try {
            auto start_time = std::chrono::high_resolution_clock::now();

            // Perform a simple operation to measure response time
            std::vector<std::string> files;
            for (const auto& entry : std::filesystem::directory_iterator(".")) {
                if (entry.is_regular_file() && entry.path().extension() == ".cpp") {
                    files.push_back(entry.path().filename().string());
                    if (files.size() >= 100) break; // Limit to 100 files
                }
            }

            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

            result.metric_value = duration.count();
            result.metric_unit = "ms";

            if (duration.count() < 100) {
                result.status = HealthStatus::HEALTHY;
                result.message = "Excellent response time";
            } else if (duration.count() < 500) {
                result.status = HealthStatus::HEALTHY;
                result.message = "Good response time";
            } else if (duration.count() < 1000) {
                result.status = HealthStatus::WARNING;
                result.message = "Slow response time";
            } else {
                result.status = HealthStatus::DEGRADED;
                result.message = "Very slow response time";
            }

        } catch (const std::exception& e) {
            result.status = HealthStatus::CRITICAL;
            result.message = "Error measuring response time: " + std::string(e.what());
        }

        return result;
    }

    HealthCheckResult check_memory_usage() {
        HealthCheckResult result;
        result.check_name = "memory_usage";
        result.category = HealthCategory::PERFORMANCE;
        result.timestamp = std::chrono::system_clock::now();

        try {
            // Simple memory check using /proc/self/status on Linux
            std::ifstream status_file("/proc/self/status");
            long memory_kb = 0;

            if (status_file.is_open()) {
                std::string line;
                while (std::getline(status_file, line)) {
                    if (line.substr(0, 6) == "VmRSS:") {
                        std::istringstream iss(line);
                        std::string label, value, unit;
                        iss >> label >> value >> unit;
                        memory_kb = std::stol(value);
                        break;
                    }
                }
            }

            result.metric_value = memory_kb / 1024.0; // Convert to MB
            result.metric_unit = "MB";

            if (memory_kb == 0) {
                result.status = HealthStatus::WARNING;
                result.message = "Could not determine memory usage";
            } else if (memory_kb < 100 * 1024) { // Less than 100MB
                result.status = HealthStatus::HEALTHY;
                result.message = "Low memory usage";
            } else if (memory_kb < 500 * 1024) { // Less than 500MB
                result.status = HealthStatus::HEALTHY;
                result.message = "Normal memory usage";
            } else {
                result.status = HealthStatus::WARNING;
                result.message = "High memory usage";
            }

        } catch (const std::exception& e) {
            result.status = HealthStatus::WARNING;
            result.message = "Error checking memory usage: " + std::string(e.what());
        }

        return result;
    }

    HealthCheckResult check_disk_space() {
        HealthCheckResult result;
        result.check_name = "disk_space";
        result.category = HealthCategory::RESOURCES;
        result.timestamp = std::chrono::system_clock::now();

        try {
            std::filesystem::space_info space = std::filesystem::space(".");

            double available_gb = space.available / 1024.0 / 1024.0 / 1024.0;
            double total_gb = space.capacity / 1024.0 / 1024.0 / 1024.0;
            double used_percentage = (double)(space.capacity - space.available) / space.capacity * 100;

            result.metric_value = available_gb;
            result.metric_unit = "GB";
            result.details["total_gb"] = std::to_string(total_gb);
            result.details["used_percentage"] = std::to_string(used_percentage);

            if (available_gb > 10) {
                result.status = HealthStatus::HEALTHY;
                result.message = "Sufficient disk space available";
            } else if (available_gb > 1) {
                result.status = HealthStatus::WARNING;
                result.message = "Low disk space";
            } else {
                result.status = HealthStatus::CRITICAL;
                result.message = "Critical disk space shortage";
            }

        } catch (const std::exception& e) {
            result.status = HealthStatus::CRITICAL;
            result.message = "Error checking disk space: " + std::string(e.what());
        }

        return result;
    }

    HealthCheckResult check_cpu_usage() {
        HealthCheckResult result;
        result.check_name = "cpu_usage";
        result.category = HealthCategory::RESOURCES;
        result.timestamp = std::chrono::system_clock::now();

        try {
            // Simple CPU usage check using /proc/stat on Linux
            std::ifstream stat_file("/proc/stat");
            double cpu_usage = 0;

            if (stat_file.is_open()) {
                std::string line;
                if (std::getline(stat_file, line) && line.substr(0, 3) == "cpu") {
                    std::istringstream iss(line);
                    std::string cpu_label;
                    long user, nice, system, idle, iowait, irq, softirq;
                    iss >> cpu_label >> user >> nice >> system >> idle >> iowait >> irq >> softirq;

                    long total = user + nice + system + idle + iowait + irq + softirq;
                    long used = user + nice + system + irq + softirq;

                    if (total > 0) {
                        cpu_usage = (double)used / total * 100;
                    }
                }
            }

            result.metric_value = cpu_usage;
            result.metric_unit = "%";

            if (cpu_usage < 50) {
                result.status = HealthStatus::HEALTHY;
                result.message = "Low CPU usage";
            } else if (cpu_usage < 80) {
                result.status = HealthStatus::HEALTHY;
                result.message = "Normal CPU usage";
            } else {
                result.status = HealthStatus::WARNING;
                result.message = "High CPU usage";
            }

        } catch (const std::exception& e) {
            result.status = HealthStatus::WARNING;
            result.message = "Error checking CPU usage: " + std::string(e.what());
        }

        return result;
    }

    HealthCheckResult check_project_structure() {
        HealthCheckResult result;
        result.check_name = "project_structure";
        result.category = HealthCategory::CONFIGURATION;
        result.timestamp = std::chrono::system_clock::now();

        try {
            std::vector<std::string> required_dirs = {"src", "scripts", "tests", "specs"};
            std::vector<std::string> required_files = {"CMakeLists.txt"};

            int missing_dirs = 0;
            int missing_files = 0;

            for (const auto& dir : required_dirs) {
                if (!std::filesystem::exists(dir)) {
                    missing_dirs++;
                }
            }

            for (const auto& file : required_files) {
                if (!std::filesystem::exists(file)) {
                    missing_files++;
                }
            }

            result.metric_value = required_dirs.size() + required_files.size() - missing_dirs - missing_files;
            result.metric_unit = "items";
            result.details["missing_dirs"] = std::to_string(missing_dirs);
            result.details["missing_files"] = std::to_string(missing_files);

            if (missing_dirs == 0 && missing_files == 0) {
                result.status = HealthStatus::HEALTHY;
                result.message = "Project structure is complete";
            } else if (missing_dirs == 0 && missing_files <= 1) {
                result.status = HealthStatus::WARNING;
                result.message = "Project structure mostly complete";
            } else {
                result.status = HealthStatus::CRITICAL;
                result.message = "Project structure has significant issues";
            }

        } catch (const std::exception& e) {
            result.status = HealthStatus::CRITICAL;
            result.message = "Error checking project structure: " + std::string(e.what());
        }

        return result;
    }

    HealthCheckResult check_git_status() {
        HealthCheckResult result;
        result.check_name = "git_status";
        result.category = HealthCategory::CONFIGURATION;
        result.timestamp = std::chrono::system_clock::now();

        try {
            // Check if we're in a git repository
            std::string git_check = ".git";
            if (!std::filesystem::exists(git_check)) {
                result.status = HealthStatus::WARNING;
                result.message = "Not in a git repository";
                result.metric_value = 0;
                result.metric_unit = "repositories";
                return result;
            }

            result.metric_value = 1;
            result.metric_unit = "repositories";

            // Check for uncommitted changes
            std::ifstream head_file(".git/HEAD");
            if (head_file.is_open()) {
                std::string head_line;
                std::getline(head_file, head_line);
                if (head_line.find("ref:") == 0) {
                    result.status = HealthStatus::HEALTHY;
                    result.message = "Git repository is accessible";
                } else {
                    result.status = HealthStatus::WARNING;
                    result.message = "Git repository in detached HEAD state";
                }
            } else {
                result.status = HealthStatus::WARNING;
                result.message = "Cannot access git repository state";
            }

        } catch (const std::exception& e) {
            result.status = HealthStatus::WARNING;
            result.message = "Error checking git status: " + std::string(e.what());
        }

        return result;
    }

    void monitoring_loop() {
        while (is_monitoring.load()) {
            try {
                perform_all_health_checks();

                // Check for dependency conflicts periodically
                if (config.enable_dependency_conflict_detection) {
                    check_dependency_conflicts();
                }

                // Sleep for the configured interval
                std::unique_lock<std::mutex> lock(mutex);
                cv.wait_for(lock, config.check_interval, [this] { return !is_monitoring.load(); });

            } catch (const std::exception& e) {
                // Log error but continue monitoring
                // In a real implementation, would log to proper logging system
            }
        }
    }

    void perform_all_health_checks() {
        std::lock_guard<std::mutex> lock(mutex);

        std::vector<HealthCheckResult> results;

        for (const auto& [name, check_data] : registered_checks) {
            try {
                auto result = check_data.second();
                results.push_back(result);

                // Add to history
                health_history.push_back(result);
                total_checks_performed++;

                // Limit history size
                if (health_history.size() > config.max_history_size) {
                    health_history.erase(health_history.begin());
                }

            } catch (const std::exception& e) {
                HealthCheckResult failed_result;
                failed_result.check_name = name;
                failed_result.category = check_data.first;
                failed_result.status = HealthStatus::CRITICAL;
                failed_result.message = "Health check failed: " + std::string(e.what());
                failed_result.timestamp = std::chrono::system_clock::now();
                results.push_back(failed_result);
            }
        }

        // Update summary
        update_health_summary(results);
    }

    void update_health_summary(const std::vector<HealthCheckResult>& results) {
        SystemHealthSummary summary;
        summary.timestamp = std::chrono::system_clock::now();
        summary.check_results = results;
        summary.total_checks = results.size();
        summary.conflicts = dependency_conflicts;

        int passed = 0, warnings = 0, failed = 0;
        for (const auto& result : results) {
            switch (result.status) {
                case HealthStatus::HEALTHY:
                    passed++;
                    break;
                case HealthStatus::WARNING:
                    warnings++;
                    break;
                case HealthStatus::DEGRADED:
                case HealthStatus::CRITICAL:
                    failed++;
                    break;
                case HealthStatus::UNKNOWN:
                    warnings++;
                    break;
            }
        }

        summary.passed_checks = passed;
        summary.warning_checks = warnings;
        summary.failed_checks = failed;
        summary.overall_status = determine_overall_status(results);
        summary.health_score = calculate_health_score(results);

        // Check for status changes
        if (status_change_callback &&
            (summary.overall_status != last_summary.overall_status ||
             std::abs(summary.health_score - last_summary.health_score) > 5.0)) {
            status_change_callback(summary);
        }

        last_summary = summary;

        // Update statistics
        statistics["health_score"] = summary.health_score;
        statistics["uptime_seconds"] = std::chrono::duration<double>(
            std::chrono::system_clock::now() - start_time).count();
        statistics["total_checks"] = total_checks_performed.load();
        statistics["conflicts_detected"] = conflicts_detected.load();
    }
};

// StandardHealthMonitor implementation
StandardHealthMonitor::StandardHealthMonitor() : p_impl(std::make_unique<Impl>()) {}

StandardHealthMonitor::~StandardHealthMonitor() = default;

bool StandardHealthMonitor::initialize(const HealthMonitorConfig& config) {
    std::lock_guard<std::mutex> lock(p_impl->mutex);
    p_impl->config = config;
    return true;
}

bool StandardHealthMonitor::start_monitoring() {
    if (p_impl->is_monitoring.load()) {
        return true; // Already monitoring
    }

    p_impl->is_monitoring = true;
    p_impl->monitoring_thread = std::thread(&Impl::monitoring_loop, p_impl.get());

    return true;
}

void StandardHealthMonitor::stop_monitoring() {
    if (!p_impl->is_monitoring.load()) {
        return;
    }

    p_impl->is_monitoring = false;
    p_impl->cv.notify_all();

    if (p_impl->monitoring_thread.joinable()) {
        p_impl->monitoring_thread.join();
    }
}

HealthCheckResult StandardHealthMonitor::perform_health_check() {
    p_impl->perform_all_health_checks();
    std::lock_guard<std::mutex> lock(p_impl->mutex);
    return p_impl->last_summary.check_results.empty() ?
        HealthCheckResult{} : p_impl->last_summary.check_results.back();
}

SystemHealthSummary StandardHealthMonitor::get_health_summary() {
    std::lock_guard<std::mutex> lock(p_impl->mutex);
    return p_impl->last_summary;
}

void StandardHealthMonitor::register_health_check(
    const std::string& name,
    HealthCategory category,
    HealthCheckFunction check_function) {
    std::lock_guard<std::mutex> lock(p_impl->mutex);
    p_impl->registered_checks[name] = {category, check_function};
}

std::vector<DependencyConflict> StandardHealthMonitor::get_dependency_conflicts() {
    std::lock_guard<std::mutex> lock(p_impl->mutex);
    return p_impl->dependency_conflicts;
}

std::vector<DependencyConflict> StandardHealthMonitor::check_dependency_conflicts() {
    std::lock_guard<std::mutex> lock(p_impl->mutex);

    std::vector<DependencyConflict> conflicts;

    try {
        // Check for version conflicts between extracted libraries
        const std::string extracted_path = "src/extracted";
        if (std::filesystem::exists(extracted_path)) {
            std::map<std::string, std::vector<std::string>> library_versions;

            for (const auto& entry : std::filesystem::directory_iterator(extracted_path)) {
                if (entry.is_directory()) {
                    std::string lib_name = entry.path().filename().string();
                    // Simple version detection - could be enhanced
                    std::string version = "unknown";

                    // Look for version information in common files
                    std::vector<std::string> version_files = {
                        "VERSION", "version.txt", "CMakeLists.txt", "configure.ac"
                    };

                    for (const auto& version_file : version_files) {
                        std::string version_path = entry.path() / version_file;
                        if (std::filesystem::exists(version_path)) {
                            std::ifstream file(version_path);
                            std::string line;
                            while (std::getline(file, line)) {
                                if (line.find("version") != std::string::npos ||
                                    line.find("VERSION") != std::string::npos) {
                                    version = line;
                                    break;
                                }
                            }
                            break;
                        }
                    }

                    library_versions[lib_name].push_back(version);
                }
            }

            // Detect conflicts
            for (const auto& [lib_name, versions] : library_versions) {
                if (versions.size() > 1) {
                    DependencyConflict conflict;
                    conflict.library_name = lib_name;
                    conflict.conflict_type = "version";
                    conflict.conflict_description = "Multiple versions detected";
                    conflict.conflicting_libraries = versions;
                    conflict.severity = "medium";
                    conflict.resolution_hint = "Resolve version conflicts by using a single version";
                    conflicts.push_back(conflict);
                }
            }
        }

        p_impl->dependency_conflicts = conflicts;
        if (!conflicts.empty()) {
            p_impl->conflicts_detected += conflicts.size();
        }

    } catch (const std::exception& e) {
        // Could add a conflict about inability to check
    }

    return conflicts;
}

std::vector<HealthCheckResult> StandardHealthMonitor::get_health_history(
    std::optional<HealthCategory> category, size_t limit) {
    std::lock_guard<std::mutex> lock(p_impl->mutex);

    std::vector<HealthCheckResult> filtered_history;

    for (const auto& result : p_impl->health_history) {
        if (!category || result.category == *category) {
            filtered_history.push_back(result);
        }
    }

    // Return most recent results up to limit
    if (filtered_history.size() > limit) {
        filtered_history.erase(filtered_history.begin(),
                             filtered_history.end() - limit);
    }

    return filtered_history;
}

void StandardHealthMonitor::set_status_change_callback(
    std::function<void(const SystemHealthSummary&)> callback) {
    std::lock_guard<std::mutex> lock(p_impl->mutex);
    p_impl->status_change_callback = callback;
}

std::map<std::string, double> StandardHealthMonitor::get_statistics() {
    std::lock_guard<std::mutex> lock(p_impl->mutex);
    return p_impl->statistics;
}

// Utility function implementations
std::unique_ptr<HealthMonitor> create_health_monitor() {
    return std::make_unique<StandardHealthMonitor>();
}

std::string health_status_to_string(HealthStatus status) {
    switch (status) {
        case HealthStatus::HEALTHY: return "HEALTHY";
        case HealthStatus::WARNING: return "WARNING";
        case HealthStatus::DEGRADED: return "DEGRADED";
        case HealthStatus::CRITICAL: return "CRITICAL";
        case HealthStatus::UNKNOWN: return "UNKNOWN";
        default: return "UNKNOWN";
    }
}

std::string health_category_to_string(HealthCategory category) {
    switch (category) {
        case HealthCategory::DEPENDENCIES: return "DEPENDENCIES";
        case HealthCategory::BUILD_SYSTEM: return "BUILD_SYSTEM";
        case HealthCategory::INTEGRITY: return "INTEGRITY";
        case HealthCategory::PERFORMANCE: return "PERFORMANCE";
        case HealthCategory::RESOURCES: return "RESOURCES";
        case HealthCategory::CONFIGURATION: return "CONFIGURATION";
        default: return "UNKNOWN";
    }
}

HealthStatus string_to_health_status(const std::string& status_str) {
    if (status_str == "HEALTHY") return HealthStatus::HEALTHY;
    if (status_str == "WARNING") return HealthStatus::WARNING;
    if (status_str == "DEGRADED") return HealthStatus::DEGRADED;
    if (status_str == "CRITICAL") return HealthStatus::CRITICAL;
    if (status_str == "UNKNOWN") return HealthStatus::UNKNOWN;
    return HealthStatus::UNKNOWN;
}

HealthCategory string_to_health_category(const std::string& category_str) {
    if (category_str == "DEPENDENCIES") return HealthCategory::DEPENDENCIES;
    if (category_str == "BUILD_SYSTEM") return HealthCategory::BUILD_SYSTEM;
    if (category_str == "INTEGRITY") return HealthCategory::INTEGRITY;
    if (category_str == "PERFORMANCE") return HealthCategory::PERFORMANCE;
    if (category_str == "RESOURCES") return HealthCategory::RESOURCES;
    if (category_str == "CONFIGURATION") return HealthCategory::CONFIGURATION;
    return HealthCategory::DEPENDENCIES; // Default
}

double calculate_health_score(const std::vector<HealthCheckResult>& results) {
    if (results.empty()) return 100.0;

    double total_score = 0.0;

    for (const auto& result : results) {
        switch (result.status) {
            case HealthStatus::HEALTHY:
                total_score += 100.0;
                break;
            case HealthStatus::WARNING:
                total_score += 75.0;
                break;
            case HealthStatus::DEGRADED:
                total_score += 50.0;
                break;
            case HealthStatus::CRITICAL:
                total_score += 25.0;
                break;
            case HealthStatus::UNKNOWN:
                total_score += 50.0; // Treat unknown as medium health
                break;
        }
    }

    return total_score / results.size();
}

HealthStatus determine_overall_status(const std::vector<HealthCheckResult>& results) {
    if (results.empty()) return HealthStatus::UNKNOWN;

    bool has_critical = false;
    bool has_degraded = false;
    bool has_warning = false;
    bool has_healthy = false;

    for (const auto& result : results) {
        switch (result.status) {
            case HealthStatus::CRITICAL:
                has_critical = true;
                break;
            case HealthStatus::DEGRADED:
                has_degraded = true;
                break;
            case HealthStatus::WARNING:
                has_warning = true;
                break;
            case HealthStatus::HEALTHY:
                has_healthy = true;
                break;
            case HealthStatus::UNKNOWN:
                has_warning = true; // Treat unknown as warning
                break;
        }
    }

    if (has_critical) return HealthStatus::CRITICAL;
    if (has_degraded) return HealthStatus::DEGRADED;
    if (has_warning) return HealthStatus::WARNING;
    return HealthStatus::HEALTHY;
}

std::string format_health_result(const HealthCheckResult& result) {
    std::ostringstream oss;
    oss << "[" << health_category_to_string(result.category) << "] "
        << result.check_name << ": " << health_status_to_string(result.status)
        << " - " << result.message;

    if (result.metric_value > 0) {
        oss << " (" << std::fixed << std::setprecision(2)
            << result.metric_value << " " << result.metric_unit << ")";
    }

    return oss.str();
}

std::string format_dependency_conflict(const DependencyConflict& conflict) {
    std::ostringstream oss;
    oss << "[" << conflict.severity << "] " << conflict.library_name
        << " - " << conflict.conflict_type << " conflict: "
        << conflict.conflict_description;

    if (!conflict.conflicting_libraries.empty()) {
        oss << " [";
        for (size_t i = 0; i < conflict.conflicting_libraries.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << conflict.conflicting_libraries[i];
        }
        oss << "]";
    }

    return oss.str();
}

} // namespace health
} // namespace integration