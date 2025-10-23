// T020-T021: Integration Orchestrator
// Orchestrates the complete secp256k1-zkp extraction and attribution process

#pragma once

#include "secp256k1_extractor.h"
#include "../attribution/attribution_generator.h"
#include <nlohmann/json.hpp>

namespace integration {
namespace manager {

struct IntegrationTask {
    std::string task_id;
    std::string library_name;
    std::string source_path;
    std::string target_path;
    std::string source_url;
    std::string version;
    std::map<std::string, std::string> parameters;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point started_at;
    std::chrono::system_clock::time_point completed_at;
    bool completed = false;
    bool success = false;
    std::string error_message;
};

class IntegrationOrchestrator {
public:
    IntegrationOrchestrator();
    ~IntegrationOrchestrator() = default;

    // Main orchestration methods
    bool integrate_secp256k1_zkp(const std::string& source_path,
                                const std::string& target_path,
                                const std::string& version = "master",
                                const std::string& source_url = "https://github.com/BlockstreamResearch/secp256k1-zkp");

    // Task management
    std::string create_integration_task(const std::string& library_name,
                                       const std::map<std::string, std::string>& parameters);
    bool execute_task(const std::string& task_id);
    IntegrationTask get_task_status(const std::string& task_id) const;
    std::vector<IntegrationTask> get_all_tasks() const;

    // Batch operations
    bool integrate_multiple_libraries(const nlohmann::json& config);
    bool validate_integration(const std::string& target_path);

private:
    // Orchestration steps
    bool prepare_integration_environment(const IntegrationTask& task);
    bool execute_extraction(const IntegrationTask& task);
    bool apply_attribution(const IntegrationTask& task);
    bool verify_integrity(const IntegrationTask& task);
    bool update_build_system(const IntegrationTask& task);
    bool cleanup_integration(const IntegrationTask& task);

    // Helper methods
    std::string generate_task_id() const;
    void log_task_event(const std::string& task_id, const std::string& event, const nlohmann::json& details);
    void update_task_status(const std::string& task_id, bool success, const std::string& error = "");
    bool validate_parameters(const std::map<std::string, std::string>& parameters);
    nlohmann::json create_integration_report(const std::string& task_id) const;

    // Member variables
    std::map<std::string, IntegrationTask> tasks_;
    std::mutex tasks_mutex_;
    std::atomic<uint64_t> task_counter_{0};

    // Component references
    extraction::Secp256k1Extractor* extractor_;
    attribution::AttributionGenerator* attribution_generator_;
};

// Global accessor
IntegrationOrchestrator& get_integration_orchestrator();

} // namespace manager
} // namespace integration