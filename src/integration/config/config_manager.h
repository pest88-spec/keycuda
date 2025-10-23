// T006: Deterministic Integration Configuration Management
// Ensures consistent, reproducible integration configurations

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <nlohmann/json.hpp>

namespace integration {
namespace config {

constexpr const char* CONFIG_VERSION = "1.0.0";

struct LibraryConfig {
    std::string name;
    std::string version;
    std::string source_path;
    std::string integrated_path;
    std::string sha256;
    std::string license;
    bool enabled = true;
    bool namespace_adaptation = false;
    std::vector<std::string> exclude_patterns;
};

class ConfigManager {
public:
    explicit ConfigManager(const std::string& config_file = "integration-config.json");

    // Configuration persistence
    bool load_config();
    bool save_config() const;
    bool validate_config() const;

    // Library configuration management
    void add_library_config(const LibraryConfig& lib_config);
    LibraryConfig get_library_config(const std::string& name) const;
    std::vector<LibraryConfig> get_all_library_configs() const;

    // Accessors
    const nlohmann::json& get_raw_config() const { return config_; }
    std::string get_error_message() const { return error_message_; }

    // Configuration values
    bool is_offline_mode() const {
        return config_.value("build", nlohmann::json::object())
                     .value("offline_mode", true);
    }

    bool is_sha256_verification_enabled() const {
        return config_.value("integration", nlohmann::json::object())
                     .value("sha256_verification", true);
    }

    std::string get_base_directory() const {
        return config_.value("integration", nlohmann::json::object())
                     .value("base_directory", "src/extracted");
    }

private:
    void create_default_config();
    std::string calculate_checksum() const;
    std::string get_current_timestamp() const;

    std::string config_file_;
    nlohmann::json config_;
    mutable std::string error_message_;
};

// Global accessors
ConfigManager& get_config_manager();
void set_config_file(const std::string& filename);

} // namespace config
} // namespace integration