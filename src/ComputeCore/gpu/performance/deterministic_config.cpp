#include "ComputeCore/gpu/performance/deterministic_config.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <filesystem>
#include <random>
#include <regex>
#include <thread>
#include <openssl/sha.h>

namespace puzzle71::gpu::performance {

std::unique_ptr<DeterministicConfigManager> DeterministicConfigManager::Create() {
    return std::make_unique<ReferenceDeterministicConfigManager>();
}

// ReferenceDeterministicConfigManager implementation
ReferenceDeterministicConfigManager::ReferenceDeterministicConfigManager() {
    // Create configuration storage directory
    std::filesystem::create_directories(config_storage_path_);

    // Initialize default templates
    InitializeDefaultTemplates();

    // Create a default configuration if none exists
    if (configurations_.empty()) {
        core::UInt256 default_seed = core::UInt256::One();
        CreateConfiguration(default_seed, "default", json{}, "Default deterministic configuration");
    }

    PERF_LOG_INFO("deterministic_config", "Deterministic configuration manager initialized",
                 json{{"config_storage_path", config_storage_path_},
                      {"initial_configs", configurations_.size()},
                      {"templates", templates_.size()}});
}

std::string ReferenceDeterministicConfigManager::CreateConfiguration(
    const core::UInt256& replay_seed,
    const std::string& config_name,
    const json& configuration_data,
    const std::string& description) {

    std::lock_guard<std::mutex> lock(config_mutex_);

    if (!config_utils::IsValidConfigurationName(config_name)) {
        PERF_LOG_ERROR("deterministic_config", "Invalid configuration name",
                      json{{"config_name", config_name}});
        return "";
    }

    if (!ValidateReplaySeed(replay_seed)) {
        PERF_LOG_ERROR("deterministic_config", "Invalid replay seed",
                      json{{"replay_seed", replay_seed.ToHex()}});
        return "";
    }

    if (!ValidateConfigurationData(configuration_data)) {
        PERF_LOG_ERROR("deterministic_config", "Invalid configuration data",
                      json{{"config_name", config_name}});
        return "";
    }

    DeterministicConfig new_config(replay_seed, config_name, description);
    new_config.configuration_data = configuration_data;
    new_config.config_id = GenerateDeterministicConfigId(replay_seed, config_name);

    // Check for duplicates
    if (configurations_.find(new_config.config_id) != configurations_.end()) {
        PERF_LOG_WARNING("deterministic_config", "Configuration already exists",
                        json{{"config_id", new_config.config_id}});
        return new_config.config_id;
    }

    configurations_[new_config.config_id] = new_config;
    current_config_ = new_config;
    current_config_id_ = new_config.config_id;

    // Create initial snapshot
    CreateSnapshot("Initial configuration creation");

    PERF_LOG_INFO("deterministic_config", "Configuration created",
                 json{{"config_id", new_config.config_id},
                      {"config_name", config_name},
                      {"replay_seed", replay_seed.ToHex()}});

    return new_config.config_id;
}

bool ReferenceDeterministicConfigManager::LoadConfiguration(const std::string& config_id) {
    std::lock_guard<std::mutex> lock(config_mutex_);

    // First try to load from memory
    auto it = configurations_.find(config_id);
    if (it != configurations_.end()) {
        current_config_ = it->second;
        current_config_id_ = config_id;

        PERF_LOG_INFO("deterministic_config", "Configuration loaded from memory",
                     json{{"config_id", config_id},
                          {"config_name", current_config_.config_name}});
        return true;
    }

    // Try to load from file
    std::string file_path = GetConfigurationFilePath(config_id);
    DeterministicConfig loaded_config;
    if (LoadConfigurationFromFile(file_path, loaded_config)) {
        configurations_[config_id] = loaded_config;
        current_config_ = loaded_config;
        current_config_id_ = config_id;

        PERF_LOG_INFO("deterministic_config", "Configuration loaded from file",
                     json{{"config_id", config_id},
                          {"config_name", loaded_config.config_name},
                          {"file_path", file_path}});
        return true;
    }

    PERF_LOG_ERROR("deterministic_config", "Configuration not found",
                  json{{"config_id", config_id}});
    return false;
}

bool ReferenceDeterministicConfigManager::SaveConfiguration(const std::string& config_id) {
    std::lock_guard<std::mutex> lock(config_mutex_);

    auto it = configurations_.find(config_id);
    if (it == configurations_.end()) {
        PERF_LOG_ERROR("deterministic_config", "Configuration not found for saving",
                      json{{"config_id", config_id}});
        return false;
    }

    std::string file_path = GetConfigurationFilePath(config_id);
    bool success = SaveConfigurationToFile(it->second, file_path);

    if (success) {
        PERF_LOG_INFO("deterministic_config", "Configuration saved",
                     json{{"config_id", config_id},
                          {"file_path", file_path}});
    } else {
        PERF_LOG_ERROR("deterministic_config", "Failed to save configuration",
                      json{{"config_id", config_id},
                           {"file_path", file_path}});
    }

    return success;
}

bool ReferenceDeterministicConfigManager::DeleteConfiguration(const std::string& config_id) {
    std::lock_guard<std::mutex> lock(config_mutex_);

    auto it = configurations_.find(config_id);
    if (it == configurations_.end()) {
        PERF_LOG_WARNING("deterministic_config", "Configuration not found for deletion",
                        json{{"config_id", config_id}});
        return false;
    }

    // Don't allow deletion of currently loaded configuration
    if (current_config_id_ == config_id) {
        PERF_LOG_ERROR("deterministic_config", "Cannot delete currently loaded configuration",
                      json{{"config_id", config_id}});
        return false;
    }

    configurations_.erase(it);

    // Delete file if it exists
    std::string file_path = GetConfigurationFilePath(config_id);
    std::filesystem::remove(file_path);

    PERF_LOG_INFO("deterministic_config", "Configuration deleted",
                 json{{"config_id", config_id}});

    return true;
}

DeterministicConfig ReferenceDeterministicConfigManager::GetCurrentConfiguration() const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    return current_config_;
}

bool ReferenceDeterministicConfigManager::UpdateConfigurationParameter(const std::string& parameter_path, const json& value) {
    std::lock_guard<std::mutex> lock(config_mutex_);

    if (!IsValidParameterPath(parameter_path)) {
        PERF_LOG_ERROR("deterministic_config", "Invalid parameter path",
                      json{{"parameter_path", parameter_path}});
        return false;
    }

    // Create snapshot before modification
    CreateSnapshot("Before parameter update: " + parameter_path);

    json old_value = GetParameterAtPath(current_config_.configuration_data, parameter_path);
    bool success = SetParameterAtPath(current_config_.configuration_data, parameter_path, value);

    if (success) {
        // Update the configuration in storage
        configurations_[current_config_id_] = current_config_;

        // Log the change
        LogConfigurationChange(current_config_id_, "Parameter update: " + parameter_path, old_value, value);

        PERF_LOG_INFO("deterministic_config", "Configuration parameter updated",
                     json{{"parameter_path", parameter_path},
                          {"old_value", old_value},
                          {"new_value", value}});
    } else {
        PERF_LOG_ERROR("deterministic_config", "Failed to update parameter",
                      json{{"parameter_path", parameter_path},
                           {"value", value}});
    }

    return success;
}

json ReferenceDeterministicConfigManager::GetConfigurationParameter(const std::string& parameter_path) const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    return GetParameterAtPath(current_config_.configuration_data, parameter_path);
}

bool ReferenceDeterministicConfigManager::ResetConfigurationToDefault() {
    std::lock_guard<std::mutex> lock(config_mutex_);

    auto default_it = configurations_.find("default");
    if (default_it == configurations_.end()) {
        PERF_LOG_ERROR("deterministic_config", "Default configuration not found");
        return false;
    }

    CreateSnapshot("Before reset to default");
    current_config_ = default_it->second;
    current_config_id_ = "default";

    PERF_LOG_INFO("deterministic_config", "Configuration reset to default");
    return true;
}

bool ReferenceDeterministicConfigManager::SetReplaySeed(const core::UInt256& seed) {
    std::lock_guard<std::mutex> lock(config_mutex_);

    if (!ValidateReplaySeed(seed)) {
        PERF_LOG_ERROR("deterministic_config", "Invalid replay seed",
                      json{{"replay_seed", seed.ToHex()}});
        return false;
    }

    CreateSnapshot("Before replay seed update");

    core::UInt256 old_seed = current_config_.replay_seed;
    current_config_.replay_seed = seed;

    // Generate new config ID based on new seed
    std::string new_config_id = GenerateDeterministicConfigId(seed, current_config_.config_name);
    current_config_.config_id = new_config_id;

    // Update configuration in storage
    configurations_[current_config_id_] = current_config_;

    LogConfigurationChange(current_config_id_, "Replay seed update", json(old_seed.ToHex()), json(seed.ToHex()));

    PERF_LOG_INFO("deterministic_config", "Replay seed updated",
                 json{{"old_seed", old_seed.ToHex()},
                      {"new_seed", seed.ToHex()},
                      {"new_config_id", new_config_id}});

    return true;
}

core::UInt256 ReferenceDeterministicConfigManager::GetReplaySeed() const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    return current_config_.replay_seed;
}

std::string ReferenceDeterministicConfigManager::GenerateDeterministicConfigId(const core::UInt256& seed, const std::string& name) const {
    std::string combined = seed.ToHex() + name + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count());

    return config_utils::HashConfiguration(json{{"combined", combined}});
}

bool ReferenceDeterministicConfigManager::CompareConfigurations(const std::string& config_id1, const std::string& config_id2) const {
    std::lock_guard<std::mutex> lock(config_mutex_);

    auto it1 = configurations_.find(config_id1);
    auto it2 = configurations_.find(config_id2);

    if (it1 == configurations_.end() || it2 == configurations_.end()) {
        return false;
    }

    // Compare configuration data (excluding metadata like timestamps and IDs)
    return it1->second.configuration_data == it2->second.configuration_data &&
           it1->second.replay_seed == it2->second.replay_seed;
}

std::vector<std::string> ReferenceDeterministicConfigManager::GetConfigurationDifferences(const std::string& config_id1, const std::string& config_id2) const {
    std::lock_guard<std::mutex> lock(config_mutex_);

    auto it1 = configurations_.find(config_id1);
    auto it2 = configurations_.find(config_id2);

    if (it1 == configurations_.end() || it2 == configurations_.end()) {
        return {};
    }

    std::vector<std::string> differences;

    // Compare replay seeds
    if (it1->second.replay_seed != it2->second.replay_seed) {
        differences.push_back("Replay seed differs: " + it1->second.replay_seed.ToHex() + " vs " + it2->second.replay_seed.ToHex());
    }

    // Compare configuration data
    if (it1->second.configuration_data != it2->second.configuration_data) {
        differences.push_back("Configuration data differs");
    }

    // Compare metadata
    if (it1->second.config_name != it2->second.config_name) {
        differences.push_back("Configuration name differs: " + it1->second.config_name + " vs " + it2->second.config_name);
    }

    return differences;
}

bool ReferenceDeterministicConfigManager::ValidateConfiguration(const DeterministicConfig& config) const {
    return ValidateReplaySeed(config.replay_seed) &&
           ValidateConfigurationData(config.configuration_data) &&
           config_utils::IsValidConfigurationName(config.config_name);
}

std::string ReferenceDeterministicConfigManager::CreateSnapshot(const std::string& reason) {
    std::lock_guard<std::mutex> lock(config_mutex_);

    ConfigurationSnapshot snapshot;
    snapshot.config_id = current_config_id_;
    snapshot.state_snapshot = GetFullConfigurationState();
    snapshot.snapshot_reason = reason;
    snapshot.is_rollback_point = true;

    std::string snapshot_id = GenerateUniqueId();
    snapshots_[snapshot_id] = snapshot;

    PERF_LOG_INFO("deterministic_config", "Configuration snapshot created",
                 json{{"snapshot_id", snapshot_id},
                      {"config_id", current_config_id_},
                      {"reason", reason}});

    return snapshot_id;
}

bool ReferenceDeterministicConfigManager::RestoreSnapshot(const std::string& snapshot_id) {
    std::lock_guard<std::mutex> lock(config_mutex_);

    auto it = snapshots_.find(snapshot_id);
    if (it == snapshots_.end()) {
        PERF_LOG_ERROR("deterministic_config", "Snapshot not found",
                      json{{"snapshot_id", snapshot_id}});
        return false;
    }

    if (!it->second.is_rollback_point) {
        PERF_LOG_ERROR("deterministic_config", "Snapshot is not a rollback point",
                      json{{"snapshot_id", snapshot_id}});
        return false;
    }

    // Restore configuration from snapshot
    // This is a simplified restoration - in practice, you'd need to carefully reconstruct the configuration
    current_config_id_ = it->second.config_id;
    auto config_it = configurations_.find(current_config_id_);
    if (config_it != configurations_.end()) {
        current_config_ = config_it->second;
    }

    PERF_LOG_INFO("deterministic_config", "Configuration snapshot restored",
                 json{{"snapshot_id", snapshot_id},
                      {"config_id", current_config_id_},
                      {"reason", it->second.snapshot_reason}});

    return true;
}

std::vector<ConfigurationSnapshot> ReferenceDeterministicConfigManager::GetSnapshots() const {
    std::lock_guard<std::mutex> lock(config_mutex_);

    std::vector<ConfigurationSnapshot> snapshot_list;
    for (const auto& [id, snapshot] : snapshots_) {
        snapshot_list.push_back(snapshot);
    }

    // Sort by timestamp (most recent first)
    std::sort(snapshot_list.begin(), snapshot_list.end(),
              [](const ConfigurationSnapshot& a, const ConfigurationSnapshot& b) {
                  return a.timestamp > b.timestamp;
              });

    return snapshot_list;
}

bool ReferenceDeterministicConfigManager::DeleteSnapshot(const std::string& snapshot_id) {
    std::lock_guard<std::mutex> lock(config_mutex_);

    auto it = snapshots_.find(snapshot_id);
    if (it == snapshots_.end()) {
        PERF_LOG_WARNING("deterministic_config", "Snapshot not found for deletion",
                        json{{"snapshot_id", snapshot_id}});
        return false;
    }

    snapshots_.erase(it);

    PERF_LOG_INFO("deterministic_config", "Snapshot deleted",
                 json{{"snapshot_id", snapshot_id}});

    return true;
}

std::string ReferenceDeterministicConfigManager::DeriveConfiguration(
    const std::string& parent_config_id,
    const core::UInt256& new_seed,
    const std::unordered_map<std::string, json>& parameter_overrides) {

    std::lock_guard<std::mutex> lock(config_mutex_);

    auto parent_it = configurations_.find(parent_config_id);
    if (parent_it == configurations_.end()) {
        PERF_LOG_ERROR("deterministic_config", "Parent configuration not found",
                      json{{"parent_config_id", parent_config_id}});
        return "";
    }

    // Create derived configuration
    DeterministicConfig derived_config = parent_it->second;
    derived_config.replay_seed = new_seed;
    derived_config.parent_config_id = parent_config_id;
    derived_config.config_name += "_derived";
    derived_config.is_baseline_config = false;
    derived_config.creation_timestamp = std::chrono::high_resolution_clock::now();

    // Apply parameter overrides
    for (const auto& [param_path, value] : parameter_overrides) {
        if (!SetParameterAtPath(derived_config.configuration_data, param_path, value)) {
            PERF_LOG_WARNING("deterministic_config", "Failed to apply parameter override",
                            json{{"param_path", param_path}});
        }
    }

    derived_config.config_id = GenerateDeterministicConfigId(new_seed, derived_config.config_name);
    configurations_[derived_config.config_id] = derived_config;

    PERF_LOG_INFO("deterministic_config", "Configuration derived",
                 json{{"derived_config_id", derived_config.config_id},
                      {"parent_config_id", parent_config_id},
                      {"new_seed", new_seed.ToHex()},
                      {"overrides_count", parameter_overrides.size()}});

    return derived_config.config_id;
}

bool ReferenceDeterministicConfigManager::ExportConfiguration(const std::string& config_id, const std::string& file_path) const {
    std::lock_guard<std::mutex> lock(config_mutex_);

    auto it = configurations_.find(config_id);
    if (it == configurations_.end()) {
        PERF_LOG_ERROR("deterministic_config", "Configuration not found for export",
                      json{{"config_id", config_id}});
        return false;
    }

    json export_data = config_utils::DeterministicConfigToJson(it->second);
    export_data["export_timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    export_data["export_version"] = "1.0";

    try {
        std::ofstream file(file_path);
        file << export_data.dump(4);
        bool success = file.good();

        if (success) {
            PERF_LOG_INFO("deterministic_config", "Configuration exported",
                         json{{"config_id", config_id},
                              {"file_path", file_path}});
        } else {
            PERF_LOG_ERROR("deterministic_config", "Failed to write export file",
                          json{{"config_id", config_id},
                               {"file_path", file_path}});
        }

        return success;
    } catch (const std::exception& e) {
        PERF_LOG_ERROR("deterministic_config", "Export failed with exception",
                      json{{"config_id", config_id},
                           {"file_path", file_path},
                           {"error", e.what()}});
        return false;
    }
}

std::string ReferenceDeterministicConfigManager::ImportConfiguration(const std::string& file_path) {
    std::lock_guard<std::mutex> lock(config_mutex_);

    try {
        std::ifstream file(file_path);
        if (!file.is_open()) {
            PERF_LOG_ERROR("deterministic_config", "Cannot open import file",
                          json{{"file_path", file_path}});
            return "";
        }

        json import_data;
        file >> import_data;

        DeterministicConfig imported_config = config_utils::JsonToDeterministicConfig(import_data);

        if (!ValidateConfiguration(imported_config)) {
            PERF_LOG_ERROR("deterministic_config", "Imported configuration validation failed",
                          json{{"file_path", file_path}});
            return "";
        }

        // Generate new ID to avoid conflicts
        imported_config.config_id = GenerateDeterministicConfigId(imported_config.replay_seed, imported_config.config_name + "_imported");

        configurations_[imported_config.config_id] = imported_config;

        PERF_LOG_INFO("deterministic_config", "Configuration imported",
                     json{{"imported_config_id", imported_config.config_id},
                          {"file_path", file_path}});

        return imported_config.config_id;
    } catch (const std::exception& e) {
        PERF_LOG_ERROR("deterministic_config", "Import failed with exception",
                      json{{"file_path", file_path},
                           {"error", e.what()}});
        return "";
    }
}

std::vector<std::string> ReferenceDeterministicConfigManager::ListConfigurations() const {
    std::lock_guard<std::mutex> lock(config_mutex_);

    std::vector<std::string> config_ids;
    for (const auto& [id, config] : configurations_) {
        config_ids.push_back(id);
    }

    return config_ids;
}

bool ReferenceDeterministicConfigManager::ApplyConfigurationBatch(const std::unordered_map<std::string, json>& parameters) {
    std::lock_guard<std::mutex> lock(config_mutex_);

    CreateSnapshot("Before batch configuration update");

    bool all_success = true;
    for (const auto& [param_path, value] : parameters) {
        if (!UpdateConfigurationParameter(param_path, value)) {
            all_success = false;
            PERF_LOG_WARNING("deterministic_config", "Batch parameter update failed",
                            json{{"param_path", param_path}});
        }
    }

    PERF_LOG_INFO("deterministic_config", "Batch configuration update completed",
                 json{{"parameters_count", parameters.size()},
                      {"success", all_success}});

    return all_success;
}

std::vector<std::string> ReferenceDeterministicConfigManager::ValidateConfigurationBatch(const std::unordered_map<std::string, json>& parameters) const {
    std::lock_guard<std::mutex> lock(config_mutex_);

    std::vector<std::string> invalid_parameters;

    for (const auto& [param_path, value] : parameters) {
        if (!IsValidParameterPath(param_path)) {
            invalid_parameters.push_back(param_path + " (invalid path)");
        }
    }

    return invalid_parameters;
}

json ReferenceDeterministicConfigManager::GetConfigurationHistory(const std::string& config_id) const {
    std::lock_guard<std::mutex> lock(config_mutex_);

    json history = json::array();
    for (const auto& change : change_history_) {
        if (change.contains("config_id") && change["config_id"] == config_id) {
            history.push_back(change);
        }
    }

    return history;
}

bool ReferenceDeterministicConfigManager::LogConfigurationChange(const std::string& config_id, const std::string& change_description, const json& old_value, const json& new_value) {
    json change_entry;
    change_entry["config_id"] = config_id;
    change_entry["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    change_entry["description"] = change_description;
    change_entry["old_value"] = old_value;
    change_entry["new_value"] = new_value;

    change_history_.push_back(change_entry);

    // Keep history manageable
    if (change_history_.size() > 10000) {
        change_history_.erase(change_history_.begin(),
                             change_history_.begin() + (change_history_.size() - 10000));
    }

    return true;
}

std::string ReferenceDeterministicConfigManager::CreateFromTemplate(const std::string& template_name, const core::UInt256& seed, const std::unordered_map<std::string, json>& overrides) {
    std::lock_guard<std::mutex> lock(config_mutex_);

    auto template_it = templates_.find(template_name);
    if (template_it == templates_.end()) {
        PERF_LOG_ERROR("deterministic_config", "Template not found",
                      json{{"template_name", template_name}});
        return "";
    }

    DeterministicConfig new_config = template_it->second;
    new_config.replay_seed = seed;
    new_config.config_name = template_name + "_from_template";
    new_config.is_baseline_config = false;

    // Apply overrides
    for (const auto& [param_path, value] : overrides) {
        SetParameterAtPath(new_config.configuration_data, param_path, value);
    }

    new_config.config_id = GenerateDeterministicConfigId(seed, new_config.config_name);
    configurations_[new_config.config_id] = new_config;

    PERF_LOG_INFO("deterministic_config", "Configuration created from template",
                 json{{"template_name", template_name},
                      {"new_config_id", new_config.config_id},
                      {"overrides_count", overrides.size()}});

    return new_config.config_id;
}

std::vector<std::string> ReferenceDeterministicConfigManager::GetAvailableTemplates() const {
    std::lock_guard<std::mutex> lock(config_mutex_);

    std::vector<std::string> template_names;
    for (const auto& [name, template_config] : templates_) {
        template_names.push_back(name);
    }

    return template_names;
}

// Private helper methods implementation
std::string ReferenceDeterministicConfigManager::GenerateUniqueId() const {
    auto now = std::chrono::high_resolution_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();

    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;

    return "config_" + std::to_string(timestamp) + "_" + std::to_string(dis(gen));
}

json ReferenceDeterministicConfigManager::GetFullConfigurationState() const {
    json state;
    state["configuration"] = config_utils::DeterministicConfigToJson(current_config_);
    state["snapshot_timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    return state;
}

bool ReferenceDeterministicConfigManager::IsValidParameterPath(const std::string& parameter_path) const {
    return config_utils::IsValidParameterPath(parameter_path);
}

json ReferenceDeterministicConfigManager::GetParameterAtPath(const json& config, const std::string& path) const {
    std::vector<std::string> path_parts = SplitParameterPath(path);
    json current = config;

    try {
        for (const auto& part : path_parts) {
            if (current.contains(part)) {
                current = current[part];
            } else {
                return json{};
            }
        }
        return current;
    } catch (...) {
        return json{};
    }
}

bool ReferenceDeterministicConfigManager::SetParameterAtPath(json& config, const std::string& path, const json& value) const {
    std::vector<std::string> path_parts = SplitParameterPath(path);
    json* current = &config;

    try {
        // Navigate to the parent of the target
        for (size_t i = 0; i < path_parts.size() - 1; ++i) {
            if (!current->contains(path_parts[i])) {
                (*current)[path_parts[i]] = json{};
            }
            current = &(*current)[path_parts[i]];
        }

        // Set the final value
        (*current)[path_parts.back()] = value;
        return true;
    } catch (...) {
        return false;
    }
}

std::vector<std::string> ReferenceDeterministicConfigManager::SplitParameterPath(const std::string& path) const {
    std::vector<std::string> parts;
    std::stringstream ss(path);
    std::string part;

    while (std::getline(ss, part, '.')) {
        if (!part.empty()) {
            parts.push_back(part);
        }
    }

    return parts;
}

bool ReferenceDeterministicConfigManager::ValidateReplaySeed(const core::UInt256& seed) const {
    // Simple validation - ensure seed is not zero
    return seed != core::UInt256::Zero();
}

bool ReferenceDeterministicConfigManager::ValidateConfigurationData(const json& config_data) const {
    // Basic validation - ensure config_data is a JSON object
    return config_data.is_object() || config_data.is_null();
}

std::string ReferenceDeterministicConfigManager::GetConfigurationFilePath(const std::string& config_id) const {
    return config_storage_path_ + config_id + ".json";
}

bool ReferenceDeterministicConfigManager::SaveConfigurationToFile(const DeterministicConfig& config, const std::string& file_path) const {
    try {
        json config_json = config_utils::DeterministicConfigToJson(config);
        std::ofstream file(file_path);
        file << config_json.dump(4);
        return file.good();
    } catch (...) {
        return false;
    }
}

bool ReferenceDeterministicConfigManager::LoadConfigurationFromFile(const std::string& file_path, DeterministicConfig& config) const {
    try {
        std::ifstream file(file_path);
        if (!file.is_open()) {
            return false;
        }

        json config_json;
        file >> config_json;
        config = config_utils::JsonToDeterministicConfig(config_json);
        return true;
    } catch (...) {
        return false;
    }
}

void ReferenceDeterministicConfigManager::InitializeDefaultTemplates() {
    // Create a basic template for GPU configurations
    DeterministicConfig gpu_template;
    gpu_template.config_name = "gpu_performance";
    gpu_template.description = "Template for GPU performance optimization configurations";
    gpu_template.configuration_data = json{
        {"gpu", {
            {"block_size", 384}, // Updated to reflect flexible block sizing
            {"grid_size", 4096},
            {"points_per_thread", 1},
            {"shared_memory_size", 0}
        }},
        {"optimization", {
            {"enable_sync_optimization", true},
            {"enable_memory_optimization", true},
            {"target_throughput_mkeys_per_sec", 10000.0}
        }}
    };

    templates_["gpu_performance"] = gpu_template;

    PERF_LOG_INFO("deterministic_config", "Default templates initialized",
                 json{{"template_count", templates_.size()}});
}

// DeterministicConfigSession implementation
DeterministicConfigSession::DeterministicConfigSession(DeterministicConfigManager* manager, const core::UInt256& seed)
    : manager_(manager), session_seed_(seed), session_active_(false) {
}

DeterministicConfigSession::~DeterministicConfigSession() {
    if (session_active_) {
        EndSession();
    }
}

bool DeterministicConfigSession::BeginSession() {
    if (!manager_) {
        return false;
    }

    // Create snapshot before session
    snapshot_id_ = manager_->CreateSnapshot("Before deterministic session");

    // Set replay seed for session
    bool success = manager_->SetReplaySeed(session_seed_);
    if (success) {
        session_config_id_ = manager_->GetCurrentConfiguration().config_id;
        session_active_ = true;

        PERF_LOG_INFO("deterministic_config", "Deterministic session started",
                     json{{"session_seed", session_seed_.ToHex()},
                          {"session_config_id", session_config_id_}});
    }

    return success;
}

bool DeterministicConfigSession::EndSession() {
    if (!session_active_ || !manager_) {
        return false;
    }

    // Create snapshot after session
    manager_->CreateSnapshot("After deterministic session");

    session_active_ = false;

    PERF_LOG_INFO("deterministic_config", "Deterministic session ended",
                 json{{"session_config_id", session_config_id_}});

    return true;
}

std::string DeterministicConfigSession::GetSessionConfigId() const {
    return session_config_id_;
}

bool DeterministicConfigSession::IsSessionActive() const {
    return session_active_;
}

// Utility functions implementation
namespace config_utils {

core::UInt256 GenerateDeterministicSeed(const std::string& input_string) {
    // Simple hash-based seed generation
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(input_string.c_str()), input_string.length(), hash);

    std::string hex_string;
    hex_string.reserve(SHA256_DIGEST_LENGTH * 2);
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        hex_string += sprintf("%02x", hash[i]);
    }

    return core::UInt256(hex_string);
}

core::UInt256 CombineSeeds(const core::UInt256& seed1, const core::UInt256& seed2) {
    // Simple combination using XOR
    return core::UInt256(seed1.ToHex() ^ seed2.ToHex());
}

std::string HashConfiguration(const json& config) {
    std::string config_str = config.dump();
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(config_str.c_str()), config_str.length(), hash);

    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        ss << std::setw(2) << static_cast<int>(hash[i]);
    }

    return ss.str();
}

bool IsValidConfigurationName(const std::string& name) {
    if (name.empty() || name.length() > 256) {
        return false;
    }

    // Allow alphanumeric characters, underscores, and hyphens
    std::regex valid_name_regex("^[a-zA-Z0-9_-]+$");
    return std::regex_match(name, valid_name_regex);
}

std::string SanitizeConfigurationName(const std::string& name) {
    std::string sanitized = name;

    // Replace invalid characters with underscores
    std::regex invalid_chars_regex("[^a-zA-Z0-9_-]");
    sanitized = std::regex_replace(sanitized, invalid_chars_regex, "_");

    // Limit length
    if (sanitized.length() > 256) {
        sanitized = sanitized.substr(0, 256);
    }

    return sanitized;
}

json DeterministicConfigToJson(const DeterministicConfig& config) {
    json j;
    j["replay_seed"] = config.replay_seed.ToHex();
    j["config_id"] = config.config_id;
    j["config_name"] = config.config_name;
    j["creation_timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        config.creation_timestamp.time_since_epoch()).count();
    j["description"] = config.description;
    j["configuration_data"] = config.configuration_data;
    j["gpu_architecture"] = config.gpu_architecture;
    j["is_baseline_config"] = config.is_baseline_config;
    j["parent_config_id"] = config.parent_config_id;
    return j;
}

DeterministicConfig JsonToDeterministicConfig(const json& json_config) {
    DeterministicConfig config;

    if (json_config.contains("replay_seed")) {
        config.replay_seed = core::UInt256::FromHex(json_config["replay_seed"].get<std::string>());
    }

    config.config_id = json_config.value("config_id", "");
    config.config_name = json_config.value("config_name", "");
    config.description = json_config.value("description", "");
    config.configuration_data = json_config.value("configuration_data", json{});
    config.gpu_architecture = json_config.value("gpu_architecture", "");
    config.is_baseline_config = json_config.value("is_baseline_config", false);
    config.parent_config_id = json_config.value("parent_config_id", "");

    if (json_config.contains("creation_timestamp")) {
        auto timestamp = json_config["creation_timestamp"].get<int64_t>();
        config.creation_timestamp = std::chrono::high_resolution_clock::time_point{
            std::chrono::milliseconds(timestamp)};
    }

    return config;
}

std::vector<std::string> FindConfigurationDependencies(const json& config) {
    std::vector<std::string> dependencies;

    // This is a simplified implementation
    // In practice, you would scan the configuration for references to other configurations
    if (config.contains("parent_config_id") && !config["parent_config_id"].get<std::string>().empty()) {
        dependencies.push_back(config["parent_config_id"].get<std::string>());
    }

    return dependencies;
}

bool ValidateConfigurationDependencies(const json& config, const std::unordered_map<std::string, json>& available_configs) {
    auto dependencies = FindConfigurationDependencies(config);

    for (const auto& dep : dependencies) {
        if (available_configs.find(dep) == available_configs.end()) {
            return false;
        }
    }

    return true;
}

std::vector<std::string> ParseParameterPath(const std::string& path) {
    std::vector<std::string> parts;
    std::stringstream ss(path);
    std::string part;

    while (std::getline(ss, part, '.')) {
        if (!part.empty()) {
            parts.push_back(part);
        }
    }

    return parts;
}

std::string NormalizeParameterPath(const std::string& path) {
    std::string normalized = path;

    // Remove leading/trailing dots
    normalized.erase(0, normalized.find_first_not_of('.'));
    normalized.erase(normalized.find_last_not_of('.') + 1);

    // Replace multiple consecutive dots with single dot
    std::regex multiple_dots("\\.+");
    normalized = std::regex_replace(normalized, multiple_dots, ".");

    return normalized;
}

bool IsValidParameterPath(const std::string& path) {
    if (path.empty()) {
        return false;
    }

    std::regex valid_path_regex("^[a-zA-Z][a-zA-Z0-9_]*(\\.[a-zA-Z][a-zA-Z0-9_]*)*$");
    return std::regex_match(path, valid_path_regex);
}

} // namespace config_utils

} // namespace puzzle71::gpu::performance