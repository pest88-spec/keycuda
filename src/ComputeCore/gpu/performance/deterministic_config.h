#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <chrono>
#include <mutex>
#include <nlohmann/json.hpp>
#include "core/uint256.h"
#include "ComputeCore/gpu/performance/performance_logger.h"

namespace puzzle71::gpu::performance {

using json = nlohmann::json;

struct DeterministicConfig {
    core::UInt256 replay_seed;                    // Seed for reproducible results
    std::string config_id;                        // Unique configuration identifier
    std::string config_name;                      // Human-readable configuration name
    std::chrono::high_resolution_clock::time_point creation_timestamp;
    std::string description;                      // Configuration description
    json configuration_data;                      // GPU/kernel parameters
    std::string gpu_architecture;                 // Target GPU architecture
    bool is_baseline_config;                      // Whether this is a baseline configuration
    std::string parent_config_id;                 // Parent configuration for derivations

    DeterministicConfig()
        : creation_timestamp(std::chrono::high_resolution_clock::now()),
          is_baseline_config(false) {}

    DeterministicConfig(const core::UInt256& seed, const std::string& name, const std::string& desc = "")
        : replay_seed(seed), config_name(name), description(desc),
          creation_timestamp(std::chrono::high_resolution_clock::now()),
          is_baseline_config(false) {}
};

struct ConfigurationSnapshot {
    std::string config_id;
    std::chrono::high_resolution_clock::time_point timestamp;
    json state_snapshot;                          // Complete configuration state
    std::vector<std::string> modified_parameters; // Parameters modified since last snapshot
    std::string snapshot_reason;                  // Reason for creating snapshot
    bool is_rollback_point;                       // Whether this can be rolled back to

    ConfigurationSnapshot()
        : timestamp(std::chrono::high_resolution_clock::now()),
          is_rollback_point(false) {}
};

class DeterministicConfigManager {
public:
    static std::unique_ptr<DeterministicConfigManager> Create();

    // Core configuration management
    virtual std::string CreateConfiguration(
        const core::UInt256& replay_seed,
        const std::string& config_name,
        const json& configuration_data,
        const std::string& description = ""
    ) = 0;

    virtual bool LoadConfiguration(const std::string& config_id) = 0;
    virtual bool SaveConfiguration(const std::string& config_id) = 0;
    virtual bool DeleteConfiguration(const std::string& config_id) = 0;

    // Configuration access and modification
    virtual DeterministicConfig GetCurrentConfiguration() const = 0;
    virtual bool UpdateConfigurationParameter(const std::string& parameter_path, const json& value) = 0;
    virtual json GetConfigurationParameter(const std::string& parameter_path) const = 0;
    virtual bool ResetConfigurationToDefault() = 0;

    // Reproducibility support
    virtual bool SetReplaySeed(const core::UInt256& seed) = 0;
    virtual core::UInt256 GetReplaySeed() const = 0;
    virtual std::string GenerateDeterministicConfigId(const core::UInt256& seed, const std::string& name) const = 0;

    // Configuration comparison and validation
    virtual bool CompareConfigurations(const std::string& config_id1, const std::string& config_id2) const = 0;
    virtual std::vector<std::string> GetConfigurationDifferences(const std::string& config_id1, const std::string& config_id2) const = 0;
    virtual bool ValidateConfiguration(const DeterministicConfig& config) const = 0;

    // Snapshot management
    virtual std::string CreateSnapshot(const std::string& reason = "Manual snapshot") = 0;
    virtual bool RestoreSnapshot(const std::string& snapshot_id) = 0;
    virtual std::vector<ConfigurationSnapshot> GetSnapshots() const = 0;
    virtual bool DeleteSnapshot(const std::string& snapshot_id) = 0;

    // Configuration derivation
    virtual std::string DeriveConfiguration(
        const std::string& parent_config_id,
        const core::UInt256& new_seed,
        const std::unordered_map<std::string, json>& parameter_overrides
    ) = 0;

    // Configuration storage and retrieval
    virtual bool ExportConfiguration(const std::string& config_id, const std::string& file_path) const = 0;
    virtual std::string ImportConfiguration(const std::string& file_path) = 0;
    virtual std::vector<std::string> ListConfigurations() const = 0;

    // Batch operations
    virtual bool ApplyConfigurationBatch(const std::unordered_map<std::string, json>& parameters) = 0;
    virtual std::vector<std::string> ValidateConfigurationBatch(const std::unordered_map<std::string, json>& parameters) const = 0;

    // Configuration history and audit
    virtual json GetConfigurationHistory(const std::string& config_id) const = 0;
    virtual bool LogConfigurationChange(const std::string& config_id, const std::string& change_description, const json& old_value, const json& new_value) = 0;

    // Configuration templates
    virtual std::string CreateFromTemplate(const std::string& template_name, const core::UInt256& seed, const std::unordered_map<std::string, json>& overrides = {}) = 0;
    virtual std::vector<std::string> GetAvailableTemplates() const = 0;

    virtual ~DeterministicConfigManager() = default;

protected:
    mutable std::mutex config_mutex_;
    std::unordered_map<std::string, DeterministicConfig> configurations_;
    std::unordered_map<std::string, ConfigurationSnapshot> snapshots_;
    DeterministicConfig current_config_;
    std::vector<json> change_history_;
    std::unordered_map<std::string, DeterministicConfig> templates_;
};

// Reference implementation of deterministic configuration manager
class ReferenceDeterministicConfigManager : public DeterministicConfigManager {
public:
    ReferenceDeterministicConfigManager();
    ~ReferenceDeterministicConfigManager() override = default;

    // DeterministicConfigManager interface implementation
    std::string CreateConfiguration(
        const core::UInt256& replay_seed,
        const std::string& config_name,
        const json& configuration_data,
        const std::string& description = ""
    ) override;

    bool LoadConfiguration(const std::string& config_id) override;
    bool SaveConfiguration(const std::string& config_id) override;
    bool DeleteConfiguration(const std::string& config_id) override;

    DeterministicConfig GetCurrentConfiguration() const override;
    bool UpdateConfigurationParameter(const std::string& parameter_path, const json& value) override;
    json GetConfigurationParameter(const std::string& parameter_path) const override;
    bool ResetConfigurationToDefault() override;

    bool SetReplaySeed(const core::UInt256& seed) override;
    core::UInt256 GetReplaySeed() const override;
    std::string GenerateDeterministicConfigId(const core::UInt256& seed, const std::string& name) const override;

    bool CompareConfigurations(const std::string& config_id1, const std::string& config_id2) const override;
    std::vector<std::string> GetConfigurationDifferences(const std::string& config_id1, const std::string& config_id2) const override;
    bool ValidateConfiguration(const DeterministicConfig& config) const override;

    std::string CreateSnapshot(const std::string& reason = "Manual snapshot") override;
    bool RestoreSnapshot(const std::string& snapshot_id) override;
    std::vector<ConfigurationSnapshot> GetSnapshots() const override;
    bool DeleteSnapshot(const std::string& snapshot_id) override;

    std::string DeriveConfiguration(
        const std::string& parent_config_id,
        const core::UInt256& new_seed,
        const std::unordered_map<std::string, json>& parameter_overrides
    ) override;

    bool ExportConfiguration(const std::string& config_id, const std::string& file_path) const override;
    std::string ImportConfiguration(const std::string& file_path) override;
    std::vector<std::string> ListConfigurations() const override;

    bool ApplyConfigurationBatch(const std::unordered_map<std::string, json>& parameters) override;
    std::vector<std::string> ValidateConfigurationBatch(const std::unordered_map<std::string, json>& parameters) const override;

    json GetConfigurationHistory(const std::string& config_id) const override;
    bool LogConfigurationChange(const std::string& config_id, const std::string& change_description, const json& old_value, const json& new_value) override;

    std::string CreateFromTemplate(const std::string& template_name, const core::UInt256& seed, const std::unordered_map<std::string, json>& overrides = {}) override;
    std::vector<std::string> GetAvailableTemplates() const override;

private:
    // Internal helper methods
    std::string GenerateUniqueId() const;
    json GetFullConfigurationState() const;
    bool IsValidParameterPath(const std::string& parameter_path) const;
    json GetParameterAtPath(const json& config, const std::string& path) const;
    bool SetParameterAtPath(json& config, const std::string& path, const json& value) const;
    std::vector<std::string> SplitParameterPath(const std::string& path) const;

    // Validation helpers
    bool ValidateReplaySeed(const core::UInt256& seed) const;
    bool ValidateConfigurationData(const json& config_data) const;
    bool ValidateParameterType(const json& value, const std::string& expected_type) const;

    // File operations
    std::string GetConfigurationFilePath(const std::string& config_id) const;
    bool SaveConfigurationToFile(const DeterministicConfig& config, const std::string& file_path) const;
    bool LoadConfigurationFromFile(const std::string& file_path, DeterministicConfig& config) const;

    // Template management
    void InitializeDefaultTemplates();
    DeterministicConfig CreateTemplateConfiguration(const std::string& template_name) const;

    std::string config_storage_path_ = "./configs/";
    std::string current_config_id_;
};

// RAII helper for deterministic configuration sessions
class DeterministicConfigSession {
public:
    explicit DeterministicConfigSession(DeterministicConfigManager* manager, const core::UInt256& seed);
    ~DeterministicConfigSession();

    bool BeginSession();
    bool EndSession();
    std::string GetSessionConfigId() const;
    bool IsSessionActive() const;

private:
    DeterministicConfigManager* manager_;
    core::UInt256 session_seed_;
    std::string session_config_id_;
    bool session_active_;
    std::string snapshot_id_;
};

// Utility functions for deterministic configuration management
namespace config_utils {
    core::UInt256 GenerateDeterministicSeed(const std::string& input_string);
    core::UInt256 CombineSeeds(const core::UInt256& seed1, const core::UInt256& seed2);
    std::string HashConfiguration(const json& config);
    bool IsValidConfigurationName(const std::string& name);
    std::string SanitizeConfigurationName(const std::string& name);

    json DeterministicConfigToJson(const DeterministicConfig& config);
    DeterministicConfig JsonToDeterministicConfig(const json& json_config);

    std::vector<std::string> FindConfigurationDependencies(const json& config);
    bool ValidateConfigurationDependencies(const json& config, const std::unordered_map<std::string, json>& available_configs);

    // Parameter path utilities
    std::vector<std::string> ParseParameterPath(const std::string& path);
    std::string NormalizeParameterPath(const std::string& path);
    bool IsValidParameterPath(const std::string& path);
}

} // namespace puzzle71::gpu::performance