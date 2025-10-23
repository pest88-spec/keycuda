// T006: Deterministic Integration Configuration Management
// Ensures consistent, reproducible integration configurations

#include "config_manager.h"
#include <fstream>
#include <sstream>
#include <iomanip>

namespace integration {
namespace config {

ConfigManager::ConfigManager(const std::string& config_file)
    : config_file_(config_file) {
    load_config();
}

bool ConfigManager::load_config() {
    std::ifstream file(config_file_);
    if (!file.is_open()) {
        // Create default config if it doesn't exist
        create_default_config();
        return save_config();
    }

    try {
        nlohmann::json config;
        file >> config;
        config_ = config;
        return validate_config();
    } catch (const std::exception& e) {
        error_message_ = "Failed to load config: " + std::string(e.what());
        return false;
    }
}

bool ConfigManager::save_config() const {
    std::ofstream file(config_file_);
    if (!file.is_open()) {
        error_message_ = "Cannot open config file for writing";
        return false;
    }

    try {
        // Add metadata
        nlohmann::json config_with_metadata = config_;
        config_with_metadata["metadata"] = {
            {"version", CONFIG_VERSION},
            {"generated", get_current_timestamp()},
            {"checksum", calculate_checksum()}
        };

        file << config_with_metadata.dump(2) << std::endl;
        file.close();
        return true;
    } catch (const std::exception& e) {
        error_message_ = "Failed to save config: " + std::string(e.what());
        return false;
    }
}

bool ConfigManager::validate_config() const {
    if (!config_.contains("version")) {
        const_cast<ConfigManager*>(this)->error_message_ = "Missing version field";
        return false;
    }

    if (!config_.contains("integration")) {
        const_cast<ConfigManager*>(this)->error_message_ = "Missing integration section";
        return false;
    }

    auto integration = config_["integration"];
    if (!integration.contains("libraries") || !integration["libraries"].is_array()) {
        const_cast<ConfigManager*>(this)->error_message_ = "Invalid libraries configuration";
        return false;
    }

    // Validate each library configuration
    for (const auto& lib : integration["libraries"]) {
        if (!lib.contains("name") || !lib.contains("version") || !lib.contains("source_path")) {
            const_cast<ConfigManager*>(this)->error_message_ = "Library missing required fields";
            return false;
        }
    }

    return true;
}

void ConfigManager::add_library_config(const LibraryConfig& lib_config) {
    if (!config_.contains("integration")) {
        config_["integration"] = nlohmann::json::object();
    }

    if (!config_["integration"].contains("libraries")) {
        config_["integration"]["libraries"] = nlohmann::json::array();
    }

    nlohmann::json lib_json;
    lib_json["name"] = lib_config.name;
    lib_json["version"] = lib_config.version;
    lib_json["source_path"] = lib_config.source_path;
    lib_json["integrated_path"] = lib_config.integrated_path;
    lib_json["sha256"] = lib_config.sha256;
    lib_json["license"] = lib_config.license;
    lib_json["enabled"] = lib_config.enabled;
    lib_json["namespace_adaptation"] = lib_config.namespace_adaptation;
    lib_json["exclude_patterns"] = lib_config.exclude_patterns;

    config_["integration"]["libraries"].push_back(lib_json);
}

LibraryConfig ConfigManager::get_library_config(const std::string& name) const {
    LibraryConfig config;

    if (!config_.contains("integration") || !config_["integration"].contains("libraries")) {
        return config;
    }

    for (const auto& lib : config_["integration"]["libraries"]) {
        if (lib.value("name", "") == name) {
            config.name = lib.value("name", "");
            config.version = lib.value("version", "");
            config.source_path = lib.value("source_path", "");
            config.integrated_path = lib.value("integrated_path", "");
            config.sha256 = lib.value("sha256", "");
            config.license = lib.value("license", "");
            config.enabled = lib.value("enabled", true);
            config.namespace_adaptation = lib.value("namespace_adaptation", false);

            if (lib.contains("exclude_patterns") && lib["exclude_patterns"].is_array()) {
                for (const auto& pattern : lib["exclude_patterns"]) {
                    config.exclude_patterns.push_back(pattern.get<std::string>());
                }
            }
            break;
        }
    }

    return config;
}

std::vector<LibraryConfig> ConfigManager::get_all_library_configs() const {
    std::vector<LibraryConfig> configs;

    if (!config_.contains("integration") || !config_["integration"].contains("libraries")) {
        return configs;
    }

    for (const auto& lib : config_["integration"]["libraries"]) {
        LibraryConfig config = get_library_config(lib.value("name", ""));
        configs.push_back(config);
    }

    return configs;
}

void ConfigManager::create_default_config() {
    config_ = {
        {"version", CONFIG_VERSION},
        {"integration", {
            {"base_directory", "src/extracted"},
            {"attribution_required", true},
            {"sha256_verification", true},
            {"namespace_adaptation", true},
            {"libraries", nlohmann::json::array()}
        }},
        {"build", {
            {"offline_mode", true},
            {"external_dependencies", false},
            {"deterministic_build", true}
        }},
        {"validation", {
            {"required_attribution_fields", {"spdx", "copyright", "origin"}},
            {"max_file_size_mb", 100},
            {"allowed_licenses", {"MIT", "Apache-2.0", "BSD-3-Clause", "BSD-2-Clause"}}
        }}
    };
}

std::string ConfigManager::calculate_checksum() const {
    // Create a JSON object without metadata for checksum calculation
    nlohmann::json config_copy = config_;

    std::string config_str = config_copy.dump();
    std::hash<std::string> hasher;
    auto hash_value = hasher(config_str);

    std::stringstream ss;
    ss << std::hex << hash_value;
    return ss.str();
}

std::string ConfigManager::get_current_timestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

// Global instance management
static std::unique_ptr<ConfigManager> g_config_manager;

ConfigManager& get_config_manager() {
    if (!g_config_manager) {
        g_config_manager = std::make_unique<ConfigManager>("integration-config.json");
    }
    return *g_config_manager;
}

void set_config_file(const std::string& filename) {
    g_config_manager = std::make_unique<ConfigManager>(filename);
}

} // namespace config
} // namespace integration