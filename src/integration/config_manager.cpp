/**
 * Deterministic Integration Configuration Management Implementation
 *
 * @origin       https://github.com/Puzzle71Solver/Puzzle71Solver
 * @origin_path  src/integration/config_manager.cpp
 * @origin_commit <current_commit>
 * @origin_license MIT
 * @extracted_date   2025-10-10
 * @extracted_by     Puzzle71Solver Team
 * @modifications    Created for third-party dependency integration optimization
 * @spdx_license_identifier MIT
 */

#include "config_manager.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <random>
#include <fstream>
#include <filesystem>
#include <functional>
#include <regex>
#include <cctype>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace {
    std::string to_lower(const std::string& str) {
        std::string result = str;
        std::transform(result.begin(), result.end(), result.begin(),
                      [](unsigned char c) { return std::tolower(c); });
        return result;
    }
}

ConfigManager::ConfigManager(
    const std::string& config_file_path,
    const std::string& versions_path,
    bool auto_save
) : config_file_path_(config_file_path),
    versions_path_(versions_path),
    auto_save_enabled_(auto_save) {

    // Create directories if they don't exist
    if (!config_file_path_.empty()) {
        std::filesystem::path config_path(config_file_path_);
        std::filesystem::create_directories(config_path.parent_path());
    }
    if (!versions_path_.empty()) {
        std::filesystem::create_directories(versions_path_);
    }

    // Load existing configuration
    load_config_from_file();
    load_versions_from_storage();

    // Add default validation rules
    add_validation_rule(ValidationRule(
        R"(.*\..*\.version)",  // Library version patterns
        [](const std::string& value) {
            return std::regex_match(value, std::regex(R"(\d+\.\d+\.\d+)"));
        },
        "Version must be in semantic versioning format (x.y.z)"
    ));

    add_validation_rule(ValidationRule(
        R"(.*\.enabled)",
        [](const std::string& value) {
            return value == "true" || value == "false";
        },
        "Enabled flag must be 'true' or 'false'"
    ));
}

ConfigManager::~ConfigManager() {
    if (auto_save_enabled_) {
        force_save();
    }
}

std::string ConfigManager::get(const std::string& key, const std::string& default_value) const {
    std::lock_guard<std::mutex> lock(config_mutex_);

    auto it = config_.find(key);
    if (it != config_.end()) {
        return it->second.value;
    }
    return default_value;
}

bool ConfigManager::set(const std::string& key,
                       const std::string& value,
                       const std::string& description,
                       const std::string& category,
                       const std::string& created_by,
                       const std::string& reason) {

    if (!validate_entry(key, value)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(config_mutex_);

    ConfigChange change;
    change.key = key;
    change.changed_by = created_by;
    change.reason = reason;
    change.timestamp = std::chrono::system_clock::now();

    auto it = config_.find(key);
    if (it != config_.end()) {
        // Update existing entry
        change.type = ConfigChange::ChangeType::UPDATE;
        change.old_value = it->second.value;
        change.new_value = value;

        it->second.value = value;
        it->second.description = description;
        it->second.modified_at = change.timestamp;
        it->second.modified_by = created_by;
    } else {
        // Create new entry
        change.type = ConfigChange::ChangeType::CREATE;
        change.old_value = "";
        change.new_value = value;

        ConfigEntry entry;
        entry.key = key;
        entry.value = value;
        entry.description = description;
        entry.category = category;
        entry.created_at = change.timestamp;
        entry.modified_at = change.timestamp;
        entry.created_by = created_by;
        entry.modified_by = created_by;

        config_[key] = entry;
    }

    log_change(change);

    if (auto_save_enabled_) {
        return save_config_to_file();
    }

    return true;
}

bool ConfigManager::remove(const std::string& key,
                          const std::string& deleted_by,
                          const std::string& reason) {
    std::lock_guard<std::mutex> lock(config_mutex_);

    auto it = config_.find(key);
    if (it == config_.end()) {
        return false; // Key doesn't exist
    }

    ConfigChange change;
    change.type = ConfigChange::ChangeType::DELETE;
    change.key = key;
    change.old_value = it->second.value;
    change.new_value = "";
    change.changed_by = deleted_by;
    change.reason = reason;
    change.timestamp = std::chrono::system_clock::now();

    config_.erase(it);
    log_change(change);

    if (auto_save_enabled_) {
        return save_config_to_file();
    }

    return true;
}

bool ConfigManager::has(const std::string& key) const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    return config_.find(key) != config_.end();
}

std::map<std::string, ConfigManager::ConfigEntry> ConfigManager::get_all(const std::string& category) const {
    std::lock_guard<std::mutex> lock(config_mutex_);

    if (category.empty()) {
        return config_;
    }

    std::map<std::string, ConfigEntry> filtered;
    for (const auto& [key, entry] : config_) {
        if (entry.category == category) {
            filtered[key] = entry;
        }
    }
    return filtered;
}

std::string ConfigManager::create_version(const std::string& description,
                                         const std::string& created_by) {
    std::lock_guard<std::mutex> lock(config_mutex_);

    ConfigVersion version;
    version.version_id = generate_version_id();
    version.description = description;
    version.created_by = created_by;
    version.timestamp = std::chrono::system_clock::now();
    version.snapshot = config_;
    version.checksum = calculate_checksum(config_);

    versions_.push_back(version);

    if (!save_version_to_file(version)) {
        return ""; // Failed to save version
    }

    return version.version_id;
}

bool ConfigManager::rollback_to_version(const std::string& version_id,
                                       const std::string& rolled_back_by,
                                       const std::string& reason) {
    std::lock_guard<std::mutex> lock(config_mutex_);

    auto version_it = std::find_if(versions_.begin(), versions_.end(),
        [&version_id](const ConfigVersion& v) { return v.version_id == version_id; });

    if (version_it == versions_.end()) {
        return false; // Version not found
    }

    // Log rollback changes
    for (const auto& [key, new_entry] : version_it->snapshot) {
        ConfigChange change;
        change.type = ConfigChange::ChangeType::ROLLBACK;
        change.key = key;
        change.changed_by = rolled_back_by;
        change.reason = reason + " (to version " + version_id + ")";
        change.timestamp = std::chrono::system_clock::now();

        auto current_it = config_.find(key);
        if (current_it != config_.end()) {
            change.old_value = current_it->second.value;
        } else {
            change.old_value = "";
        }
        change.new_value = new_entry.value;

        log_change(change);
    }

    config_ = version_it->snapshot;

    if (auto_save_enabled_) {
        return save_config_to_file();
    }

    return true;
}

std::vector<ConfigManager::ConfigVersion> ConfigManager::get_versions() const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    return versions_;
}

ConfigManager::ConfigVersion ConfigManager::get_version(const std::string& version_id) const {
    std::lock_guard<std::mutex> lock(config_mutex_);

    auto it = std::find_if(versions_.begin(), versions_.end(),
        [&version_id](const ConfigVersion& v) { return v.version_id == version_id; });

    if (it != versions_.end()) {
        return *it;
    }
    return ConfigVersion(); // Return empty version if not found
}

std::vector<ConfigManager::ConfigChange> ConfigManager::get_change_log(const std::string& key,
                                                                       size_t limit) const {
    std::lock_guard<std::mutex> lock(config_mutex_);

    std::vector<ConfigChange> filtered;
    for (const auto& change : change_log_) {
        if (key.empty() || change.key == key) {
            filtered.push_back(change);
        }
    }

    // Sort by timestamp (newest first)
    std::sort(filtered.begin(), filtered.end(),
        [](const ConfigChange& a, const ConfigChange& b) {
            return a.timestamp > b.timestamp;
        });

    if (limit > 0 && filtered.size() > limit) {
        filtered.resize(limit);
    }

    return filtered;
}

void ConfigManager::add_validation_rule(const ValidationRule& rule) {
    validation_rules_.push_back(rule);
}

std::pair<bool, std::vector<std::string>> ConfigManager::validate_all() const {
    std::lock_guard<std::mutex> lock(config_mutex_);

    std::vector<std::string> errors;
    bool all_valid = true;

    for (const auto& [key, entry] : config_) {
        if (!validate_entry(key, entry.value)) {
            all_valid = false;
            errors.push_back("Invalid value for key '" + key + "': " + entry.value);
        }
    }

    return {all_valid, errors};
}

bool ConfigManager::export_to_file(const std::string& file_path,
                                  bool include_metadata,
                                  const std::string& category) const {
    std::lock_guard<std::mutex> lock(config_mutex_);

    std::ofstream file(file_path);
    if (!file.is_open()) {
        return false;
    }

    json export_data;
    export_data["export_timestamp"] = format_timestamp(std::chrono::system_clock::now());
    export_data["entries"] = json::object();

    auto entries_to_export = get_all(category);
    for (const auto& [key, entry] : entries_to_export) {
        json entry_json;
        entry_json["value"] = entry.value;

        if (include_metadata) {
            entry_json["description"] = entry.description;
            entry_json["category"] = entry.category;
            entry_json["created_at"] = format_timestamp(entry.created_at);
            entry_json["modified_at"] = format_timestamp(entry.modified_at);
            entry_json["created_by"] = entry.created_by;
            entry_json["modified_by"] = entry.modified_by;

            json tags_json = json::array();
            for (const auto& tag : entry.tags) {
                tags_json.push_back(tag);
            }
            entry_json["tags"] = tags_json;
        }

        export_data["entries"][key] = entry_json;
    }

    file << std::setw(4) << export_data << std::endl;
    return true;
}

bool ConfigManager::import_from_file(const std::string& file_path,
                                    bool merge_mode,
                                    const std::string& imported_by) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        return false;
    }

    try {
        json import_data;
        file >> import_data;

        if (!merge_mode) {
            clear_all(imported_by, "Import replaced all configuration");
        }

        for (auto& [key, entry_json] : import_data["entries"].items()) {
            std::string value = entry_json["value"];
            std::string description = entry_json.value("description", "");
            std::string category = entry_json.value("category", "general");

            set(key, value, description, category, imported_by, "Imported from " + file_path);
        }

        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

ConfigManager::ConfigStats ConfigManager::get_statistics() const {
    std::lock_guard<std::mutex> lock(config_mutex_);

    ConfigStats stats;
    stats.total_entries = config_.size();
    stats.total_versions = versions_.size();
    stats.total_changes = change_log_.size();

    // Count entries by category
    for (const auto& [key, entry] : config_) {
        stats.entries_by_category[entry.category]++;
    }

    // Find last modified timestamp
    for (const auto& [key, entry] : config_) {
        if (entry.modified_at > stats.last_modified) {
            stats.last_modified = entry.modified_at;
        }
    }

    return stats;
}

void ConfigManager::set_auto_save(bool enabled) {
    auto_save_enabled_ = enabled;
}

bool ConfigManager::force_save() const {
    return save_config_to_file();
}

void ConfigManager::clear_all(const std::string& cleared_by, const std::string& reason) {
    std::lock_guard<std::mutex> lock(config_mutex_);

    for (const auto& [key, entry] : config_) {
        ConfigChange change;
        change.type = ConfigChange::ChangeType::DELETE;
        change.key = key;
        change.old_value = entry.value;
        change.new_value = "";
        change.changed_by = cleared_by;
        change.reason = reason;
        change.timestamp = std::chrono::system_clock::now();

        log_change(change);
    }

    config_.clear();

    if (auto_save_enabled_) {
        save_config_to_file();
    }
}

std::vector<std::string> ConfigManager::search(const std::string& query,
                                               bool search_values,
                                               bool case_sensitive) const {
    std::lock_guard<std::mutex> lock(config_mutex_);

    std::vector<std::string> results;
    std::string search_query = case_sensitive ? query : to_lower(query);

    for (const auto& [key, entry] : config_) {
        std::string search_key = case_sensitive ? key : to_lower(key);
        std::string search_value = case_sensitive ? entry.value : to_lower(entry.value);

        if (search_key.find(search_query) != std::string::npos ||
            (search_values && search_value.find(search_query) != std::string::npos)) {
            results.push_back(key);
        }
    }

    return results;
}

// Private methods implementation
std::string ConfigManager::calculate_checksum(const std::map<std::string, ConfigEntry>& config) const {
    std::stringstream ss;
    for (const auto& [key, entry] : config) {
        ss << key << ":" << entry.value << ":" << entry.modified_at.time_since_epoch().count() << ";";
    }

    // Simple checksum - in production, use proper cryptographic hash
    std::hash<std::string> hasher;
    return std::to_string(hasher(ss.str()));
}

bool ConfigManager::save_config_to_file() const {
    if (config_file_path_.empty()) {
        return false;
    }

    std::ofstream file(config_file_path_);
    if (!file.is_open()) {
        return false;
    }

    json config_data;
    config_data["metadata"] = json::object();
    config_data["metadata"]["last_saved"] = format_timestamp(std::chrono::system_clock::now());
    config_data["metadata"]["checksum"] = calculate_checksum(config_);
    config_data["metadata"]["total_entries"] = config_.size();

    config_data["entries"] = json::object();
    for (const auto& [key, entry] : config_) {
        json entry_json;
        entry_json["value"] = entry.value;
        entry_json["description"] = entry.description;
        entry_json["category"] = entry.category;
        entry_json["created_at"] = format_timestamp(entry.created_at);
        entry_json["modified_at"] = format_timestamp(entry.modified_at);
        entry_json["created_by"] = entry.created_by;
        entry_json["modified_by"] = entry.modified_by;

        json tags_json = json::array();
        for (const auto& tag : entry.tags) {
            tags_json.push_back(tag);
        }
        entry_json["tags"] = tags_json;

        config_data["entries"][key] = entry_json;
    }

    file << std::setw(4) << config_data << std::endl;
    return true;
}

bool ConfigManager::load_config_from_file() {
    if (config_file_path_.empty() || !std::filesystem::exists(config_file_path_)) {
        return false;
    }

    std::ifstream file(config_file_path_);
    if (!file.is_open()) {
        return false;
    }

    try {
        json config_data;
        file >> config_data;

        config_.clear();

        for (auto& [key, entry_json] : config_data["entries"].items()) {
            ConfigEntry entry;
            entry.key = key;
            entry.value = entry_json["value"];
            entry.description = entry_json.value("description", "");
            entry.category = entry_json.value("category", "general");
            entry.created_at = parse_timestamp(entry_json.value("created_at", ""));
            entry.modified_at = parse_timestamp(entry_json.value("modified_at", ""));
            entry.created_by = entry_json.value("created_by", "unknown");
            entry.modified_by = entry_json.value("modified_by", "unknown");

            if (entry_json.contains("tags")) {
                for (const auto& tag : entry_json["tags"]) {
                    entry.tags.push_back(tag);
                }
            }

            config_[key] = entry;
        }

        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

bool ConfigManager::save_version_to_file(const ConfigVersion& version) const {
    if (versions_path_.empty()) {
        return false;
    }

    std::ofstream file(versions_path_ + version.version_id + ".json");
    if (!file.is_open()) {
        return false;
    }

    json version_data;
    version_data["version_id"] = version.version_id;
    version_data["description"] = version.description;
    version_data["created_by"] = version.created_by;
    version_data["timestamp"] = format_timestamp(version.timestamp);
    version_data["checksum"] = version.checksum;

    version_data["entries"] = json::object();
    for (const auto& [key, entry] : version.snapshot) {
        json entry_json;
        entry_json["value"] = entry.value;
        entry_json["description"] = entry.description;
        entry_json["category"] = entry.category;
        entry_json["created_at"] = format_timestamp(entry.created_at);
        entry_json["modified_at"] = format_timestamp(entry.modified_at);
        entry_json["created_by"] = entry.created_by;
        entry_json["modified_by"] = entry.modified_by;

        version_data["entries"][key] = entry_json;
    }

    file << std::setw(4) << version_data << std::endl;
    return true;
}

bool ConfigManager::load_versions_from_storage() {
    if (versions_path_.empty() || !std::filesystem::exists(versions_path_)) {
        return false;
    }

    versions_.clear();

    for (const auto& file : std::filesystem::directory_iterator(versions_path_)) {
        if (file.path().extension() == ".json") {
            std::ifstream version_file(file.path());
            if (version_file.is_open()) {
                try {
                    json version_data;
                    version_file >> version_data;

                    ConfigVersion version;
                    version.version_id = version_data["version_id"];
                    version.description = version_data.value("description", "");
                    version.created_by = version_data.value("created_by", "unknown");
                    version.timestamp = parse_timestamp(version_data.value("timestamp", ""));
                    version.checksum = version_data.value("checksum", "");

                    for (auto& [key, entry_json] : version_data["entries"].items()) {
                        ConfigEntry entry;
                        entry.key = key;
                        entry.value = entry_json["value"];
                        entry.description = entry_json.value("description", "");
                        entry.category = entry_json.value("category", "general");
                        entry.created_at = parse_timestamp(entry_json.value("created_at", ""));
                        entry.modified_at = parse_timestamp(entry_json.value("modified_at", ""));
                        entry.created_by = entry_json.value("created_by", "unknown");
                        entry.modified_by = entry_json.value("modified_by", "unknown");

                        version.snapshot[key] = entry;
                    }

                    versions_.push_back(version);
                } catch (const std::exception& e) {
                    // Skip corrupted version files
                    continue;
                }
            }
        }
    }

    return true;
}

void ConfigManager::log_change(const ConfigChange& change) {
    change_log_.push_back(change);

    // Keep only last 1000 changes in memory
    if (change_log_.size() > 1000) {
        change_log_.erase(change_log_.begin(), change_log_.begin() + change_log_.size() - 1000);
    }
}

bool ConfigManager::validate_entry(const std::string& key, const std::string& value) const {
    for (const auto& rule : validation_rules_) {
        if (std::regex_match(key, std::regex(rule.key_pattern))) {
            if (!rule.validator(value)) {
                return false;
            }
        }
    }
    return true;
}

std::string ConfigManager::generate_version_id() const {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(100000, 999999);

    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);

    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y%m%d_%H%M%S_") << dis(gen);
    return ss.str();
}

std::string ConfigManager::format_timestamp(std::chrono::system_clock::time_point tp) const {
    auto time_t = std::chrono::system_clock::to_time_t(tp);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

std::chrono::system_clock::time_point ConfigManager::parse_timestamp(const std::string& ts) const {
    if (ts.empty()) {
        return std::chrono::system_clock::now();
    }

    std::tm tm = {};
    std::istringstream ss(ts);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

// ConfigScope implementation
ConfigScope::ConfigScope(ConfigManager& config) : config_(config), committed_(false) {
}

ConfigScope::~ConfigScope() {
    if (!committed_) {
        rollback();
    }
}

void ConfigScope::set_temp(const std::string& key, const std::string& value) {
    // Store original value if not already stored
    if (std::find_if(original_values_.begin(), original_values_.end(),
                     [&key](const auto& pair) { return pair.first == key; }) == original_values_.end()) {
        original_values_.emplace_back(key, config_.get(key));
    }

    config_.set(key, value, "Temporary configuration change", "temp", "config_scope");
}

void ConfigScope::commit() {
    committed_ = true;
    original_values_.clear();
}

void ConfigScope::rollback() {
    for (const auto& [key, original_value] : original_values_) {
        if (original_value.empty()) {
            config_.remove(key, "config_scope", "Rollback temporary configuration");
        } else {
            config_.set(key, original_value, "Restored original value", "restored", "config_scope", "Rollback temporary configuration");
        }
    }
    original_values_.clear();
}