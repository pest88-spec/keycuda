// T020-T021: Integration Orchestrator
// Orchestrates the complete secp256k1-zkp extraction and attribution process

#include "integration_orchestrator.h"
#include "integration_manager.h"
#include "../metrics/metrics.h"
#include <iomanip>
#include <sstream>
#include <random>

namespace integration {
namespace manager {

IntegrationOrchestrator::IntegrationOrchestrator() {
    extractor_ = &extraction::get_secp256k1_extractor();
    attribution_generator_ = &attribution::get_attribution_generator();
}

bool IntegrationOrchestrator::integrate_secp256k1_zkp(const std::string& source_path,
                                                      const std::string& target_path,
                                                      const std::string& version,
                                                      const std::string& source_url) {
    INTEGRATION_TIMER_SCOPE("secp256k1_zkp_integration");

    LOG_INTEGRATION_OP("secp256k1_zkp_integration_start", {
        {"source_path", source_path},
        {"target_path", target_path},
        {"version", version},
        {"source_url", source_url}
    });

    // Create integration task
    std::map<std::string, std::string> parameters = {
        {"source_path", source_path},
        {"target_path", target_path},
        {"version", version},
        {"source_url", source_url}
    };

    std::string task_id = create_integration_task("secp256k1-zkp", parameters);

    bool success = execute_task(task_id);

    if (success) {
        LOG_INTEGRATION_OP("secp256k1_zkp_integration_success", {
            {"task_id", task_id},
            {"target_path", target_path}
        });
    } else {
        LOG_INTEGRATION_OP("secp256k1_zkp_integration_failed", {
            {"task_id", task_id},
            {"error", get_task_status(task_id).error_message}
        });
    }

    return success;
}

std::string IntegrationOrchestrator::create_integration_task(const std::string& library_name,
                                                            const std::map<std::string, std::string>& parameters) {
    std::lock_guard<std::mutex> lock(tasks_mutex_);

    IntegrationTask task;
    task.task_id = generate_task_id();
    task.library_name = library_name;
    task.parameters = parameters;
    task.created_at = std::chrono::system_clock::now();
    task.completed = false;
    task.success = false;

    // Extract parameters
    auto it = parameters.find("source_path");
    if (it != parameters.end()) task.source_path = it->second;

    it = parameters.find("target_path");
    if (it != parameters.end()) task.target_path = it->second;

    it = parameters.find("version");
    if (it != parameters.end()) task.version = it->second;

    it = parameters.find("source_url");
    if (it != parameters.end()) task.source_url = it->second;

    tasks_[task.task_id] = task;

    log_task_event(task.task_id, "created", {
        {"library_name", library_name},
        {"parameters", parameters}
    });

    return task.task_id;
}

bool IntegrationOrchestrator::execute_task(const std::string& task_id) {
    auto task = get_task_status(task_id);
    if (task.task_id.empty()) {
        return false;
    }

    task.started_at = std::chrono::system_clock::now();
    log_task_event(task_id, "started", {});

    try {
        // Step 1: Prepare environment
        if (!prepare_integration_environment(task)) {
            update_task_status(task_id, false, "Environment preparation failed");
            return false;
        }

        // Step 2: Execute extraction
        if (!execute_extraction(task)) {
            update_task_status(task_id, false, "Extraction failed");
            return false;
        }

        // Step 3: Apply attribution
        if (!apply_attribution(task)) {
            update_task_status(task_id, false, "Attribution application failed");
            return false;
        }

        // Step 4: Verify integrity
        if (!verify_integrity(task)) {
            update_task_status(task_id, false, "Integrity verification failed");
            return false;
        }

        // Step 5: Update build system
        if (!update_build_system(task)) {
            update_task_status(task_id, false, "Build system update failed");
            return false;
        }

        // Step 6: Cleanup
        if (!cleanup_integration(task)) {
            update_task_status(task_id, false, "Cleanup failed");
            return false;
        }

        update_task_status(task_id, true);
        return true;

    } catch (const std::exception& e) {
        update_task_status(task_id, false, std::string("Exception: ") + e.what());
        return false;
    }
}

IntegrationTask IntegrationOrchestrator::get_task_status(const std::string& task_id) const {
    std::lock_guard<std::mutex> lock(tasks_mutex_);

    auto it = tasks_.find(task_id);
    if (it != tasks_.end()) {
        return it->second;
    }

    return IntegrationTask{}; // Return empty task if not found
}

std::vector<IntegrationTask> IntegrationOrchestrator::get_all_tasks() const {
    std::lock_guard<std::mutex> lock(tasks_mutex_);

    std::vector<IntegrationTask> result;
    for (const auto& pair : tasks_) {
        result.push_back(pair.second);
    }

    return result;
}

bool IntegrationOrchestrator::integrate_multiple_libraries(const nlohmann::json& config) {
    if (!config.contains("libraries") || !config["libraries"].is_array()) {
        return false;
    }

    bool overall_success = true;

    for (const auto& lib_config : config["libraries"]) {
        if (!lib_config.contains("name") || !lib_config.contains("source_path") ||
            !lib_config.contains("target_path")) {
            overall_success = false;
            continue;
        }

        std::string name = lib_config["name"];
        std::string source_path = lib_config["source_path"];
        std::string target_path = lib_config["target_path"];
        std::string version = lib_config.value("version", "master");
        std::string source_url = lib_config.value("source_url", "");

        if (name == "secp256k1-zkp") {
            bool success = integrate_secp256k1_zkp(source_path, target_path, version, source_url);
            if (!success) {
                overall_success = false;
            }
        } else {
            // Handle other libraries as needed
            overall_success = false;
        }
    }

    return overall_success;
}

bool IntegrationOrchestrator::validate_integration(const std::string& target_path) {
    if (!std::filesystem::exists(target_path)) {
        return false;
    }

    // Check for integrity manifest
    std::string manifest_path = target_path + "/MANIFEST.json";
    if (!std::filesystem::exists(manifest_path)) {
        return false;
    }

    // Verify manifest integrity
    auto& integrity = integration::manager::get_integration_manager().get_integrity_verifier();
    return integrity->verify_directory_integrity(target_path);
}

bool IntegrationOrchestrator::prepare_integration_environment(const IntegrationTask& task) {
    LOG_INTEGRATION_OP("integration_prepare_environment", {
        {"task_id", task.task_id},
        {"target_path", task.target_path}
    });

    // Create target directory if it doesn't exist
    if (!std::filesystem::exists(task.target_path)) {
        if (!std::filesystem::create_directories(task.target_path)) {
            return false;
        }
    }

    // Validate source exists
    if (!std::filesystem::exists(task.source_path)) {
        return false;
    }

    return true;
}

bool IntegrationOrchestrator::execute_extraction(const IntegrationTask& task) {
    LOG_INTEGRATION_OP("integration_extraction_start", {
        {"task_id", task.task_id},
        {"source_path", task.source_path},
        {"target_path", task.target_path}
    });

    extraction::ExtractionConfig config;
    config.source_path = task.source_path;
    config.target_path = task.target_path;
    config.source_url = task.source_url;
    config.version = task.version;
    config.include_attribution = false; // We'll handle attribution separately
    config.verify_integrity = true;
    config.verify_extraction = true;

    return extractor_->extract(config);
}

bool IntegrationOrchestrator::apply_attribution(const IntegrationTask& task) {
    LOG_INTEGRATION_OP("integration_attribution_start", {
        {"task_id", task.task_id},
        {"target_path", task.target_path}
    });

    attribution::AttributionInfo info;
    info.library_name = task.library_name;
    info.library_version = task.version;
    info.source_url = task.source_url;
    info.license_type = "MIT";
    info.spdx_license_id = "MIT";
    info.integration_date = get_current_timestamp();
    info.copyright_holder = "The Bitcoin Core Developers";
    info.modification_summary = "Namespace adaptation for integration";

    return attribution_generator_->apply_attribution_to_directory(task.target_path, info, true);
}

bool IntegrationOrchestrator::verify_integrity(const IntegrationTask& task) {
    LOG_INTEGRATION_OP("integration_integrity_verification", {
        {"task_id", task.task_id},
        {"target_path", task.target_path}
    });

    return validate_integration(task.target_path);
}

bool IntegrationOrchestrator::update_build_system(const IntegrationTask& task) {
    LOG_INTEGRATION_OP("integration_build_system_update", {
        {"task_id", task.task_id}
    });

    // Update CMakeLists.txt to include the extracted library
    // This is a placeholder - actual CMakeLists.txt update would go here

    return true;
}

bool IntegrationOrchestrator::cleanup_integration(const IntegrationTask& task) {
    LOG_INTEGRATION_OP("integration_cleanup", {
        {"task_id", task.task_id}
    });

    // Perform any necessary cleanup
    return true;
}

std::string IntegrationOrchestrator::generate_task_id() const {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(100000, 999999);

    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);

    std::stringstream ss;
    ss << "task_" << std::put_time(std::gmtime(&time_t), "%Y%m%d_%H%M%S")
       << "_" << dis(gen);

    return ss.str();
}

void IntegrationOrchestrator::log_task_event(const std::string& task_id, const std::string& event, const nlohmann::json& details) {
    LOG_INTEGRATION_OP("integration_task_event", {
        {"task_id", task_id},
        {"event", event},
        {"details", details}
    });
}

void IntegrationOrchestrator::update_task_status(const std::string& task_id, bool success, const std::string& error) {
    std::lock_guard<std::mutex> lock(tasks_mutex_);

    auto it = tasks_.find(task_id);
    if (it != tasks_.end()) {
        it->second.completed = true;
        it->second.success = success;
        it->second.completed_at = std::chrono::system_clock::now();
        it->second.error_message = error;

        log_task_event(task_id, success ? "completed" : "failed", {
            {"success", success},
            {"error", error}
        });
    }
}

bool IntegrationOrchestrator::validate_parameters(const std::map<std::string, std::string>& parameters) {
    // Check required parameters
    std::vector<std::string> required = {"source_path", "target_path"};

    for (const auto& param : required) {
        if (parameters.find(param) == parameters.end()) {
            return false;
        }
    }

    return true;
}

nlohmann::json IntegrationOrchestrator::create_integration_report(const std::string& task_id) const {
    IntegrationTask task = get_task_status(task_id);

    nlohmann::json report;
    report["task_id"] = task.task_id;
    report["library_name"] = task.library_name;
    report["source_path"] = task.source_path;
    report["target_path"] = task.target_path;
    report["version"] = task.version;
    report["completed"] = task.completed;
    report["success"] = task.success;
    report["error_message"] = task.error_message;

    // Add timestamps
    auto format_time = [](const std::chrono::system_clock::time_point& tp) -> std::string {
        auto time_t = std::chrono::system_clock::to_time_t(tp);
        std::stringstream ss;
        ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");
        return ss.str();
    };

    report["created_at"] = format_time(task.created_at);
    report["started_at"] = format_time(task.started_at);
    report["completed_at"] = format_time(task.completed_at);

    // Add parameters
    report["parameters"] = task.parameters;

    return report;
}

// Global instance
static std::unique_ptr<IntegrationOrchestrator> g_integration_orchestrator;

IntegrationOrchestrator& get_integration_orchestrator() {
    if (!g_integration_orchestrator) {
        g_integration_orchestrator = std::make_unique<IntegrationOrchestrator>();
    }
    return *g_integration_orchestrator;
}

} // namespace manager
} // namespace integration