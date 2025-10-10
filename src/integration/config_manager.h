/**
 * Deterministic Integration Configuration Management for Puzzle71Solver
 *
 * Provides deterministic, reproducible configuration management for third-party
 * library integration operations with version control, rollback, and audit trail.
 * Ensures consistent behavior across different environments and build scenarios.
 *
 * @origin       https://github.com/Puzzle71Solver/Puzzle71Solver
 * @origin_path  src/integration/config_manager.h
 * @origin_commit <current_commit>
 * @origin_license MIT
 * @extracted_date   2025-10-10
 * @extracted_by     Puzzle71Solver Team
 * @modifications    Created for third-party dependency integration optimization
 * @spdx_license_identifier MIT
 */

#pragma once

#include <string>
#include <map>
#include <vector>
#include <memory>
#include <fstream>
#include <mutex>
#include <chrono>

/**
 * Configuration Manager Class
 *
 * Provides deterministic configuration management with version control,
 * rollback capabilities, and comprehensive audit logging for all integration
 * operations.
 */
class ConfigManager {
public:
    /**
     * Configuration entry with metadata
     */
    struct ConfigEntry {
        std::string key;
        std::string value;
        std::string description;
        std::string category;
        std::chrono::system_clock::time_point created_at;
        std::chrono::system_clock::time_point modified_at;
        std::string created_by;
        std::string modified_by;
        std::vector<std::string> tags;

        ConfigEntry() : created_at(std::chrono::system_clock::now()),
                      modified_at(std::chrono::system_clock::now()) {}
    };

    /**
     * Configuration version for rollback capability
     */
    struct ConfigVersion {
        std::string version_id;
        std::chrono::system_clock::time_point timestamp;
        std::string description;
        std::string created_by;
        std::map<std::string, ConfigEntry> snapshot;
        std::string checksum;

        ConfigVersion() : timestamp(std::chrono::system_clock::now()) {}
    };

    /**
     * Configuration change for audit trail
     */
    struct ConfigChange {
        enum class ChangeType { CREATE, UPDATE, DELETE, ROLLBACK };

        ChangeType type;
        std::string key;
        std::string old_value;
        std::string new_value;
        std::chrono::system_clock::time_point timestamp;
        std::string changed_by;
        std::string reason;

        ConfigChange() : timestamp(std::chrono::system_clock::now()) {}
    };

    /**
     * Configuration validation rule
     */
    struct ValidationRule {
        std::string key_pattern;
        std::function<bool(const std::string&)> validator;
        std::string error_message;
        bool required;

        ValidationRule(const std::string& pattern,
                      std::function<bool(const std::string&)> val,
                      const std::string& error,
                      bool req = true)
            : key_pattern(pattern), validator(val), error_message(error), required(req) {}
    };

private:
    std::string config_file_path_;
    std::string versions_path_;
    std::map<std::string, ConfigEntry> config_;
    std::vector<ConfigVersion> versions_;
    std::vector<ConfigChange> change_log_;
    std::vector<ValidationRule> validation_rules_;
    mutable std::mutex config_mutex_;
    bool auto_save_enabled_;

    /**
     * Calculate configuration checksum for integrity verification
     */
    std::string calculate_checksum(const std::map<std::string, ConfigEntry>& config) const;

    /**
     * Save configuration to file
     */
    bool save_config_to_file() const;

    /**
     * Load configuration from file
     */
    bool load_config_from_file();

    /**
     * Save version snapshot
     */
    bool save_version_to_file(const ConfigVersion& version) const;

    /**
     * Load all versions from storage
     */
    bool load_versions_from_storage();

    /**
     * Add change to audit log
     */
    void log_change(const ConfigChange& change);

    /**
     * Validate configuration entry
     */
    bool validate_entry(const std::string& key, const std::string& value) const;

    /**
     * Generate unique version ID
     */
    std::string generate_version_id() const;

    /**
     * Format timestamp for storage
     */
    std::string format_timestamp(std::chrono::system_clock::time_point tp) const;

    /**
     * Parse timestamp from storage
     */
    std::chrono::system_clock::time_point parse_timestamp(const std::string& ts) const;

public:
    /**
     * Constructor
     *
     * @param config_file_path Path to main configuration file
     * @param versions_path Directory for version storage
     * @param auto_save Enable automatic saving on changes
     */
    explicit ConfigManager(
        const std::string& config_file_path = "config/integration.json",
        const std::string& versions_path = "config/versions/",
        bool auto_save = true
    );

    /**
     * Destructor - ensures configuration is saved
     */
    ~ConfigManager();

    /**
     * Get configuration value
     *
     * @param key Configuration key
     * @param default_value Default value if key not found
     * @return Configuration value or default
     */
    std::string get(const std::string& key, const std::string& default_value = "") const;

    /**
     * Set configuration value
     *
     * @param key Configuration key
     * @param value Configuration value
     * @param description Optional description
     * @param category Configuration category
     * @param created_by Who created this entry
     * @param reason Reason for change
     * @return True if value was set successfully
     */
    bool set(const std::string& key,
             const std::string& value,
             const std::string& description = "",
             const std::string& category = "general",
             const std::string& created_by = "system",
             const std::string& reason = "");

    /**
     * Delete configuration entry
     *
     * @param key Configuration key to delete
     * @param deleted_by Who deleted this entry
     * @param reason Reason for deletion
     * @return True if entry was deleted
     */
    bool remove(const std::string& key,
                const std::string& deleted_by = "system",
                const std::string& reason = "");

    /**
     * Check if configuration key exists
     *
     * @param key Configuration key
     * @return True if key exists
     */
    bool has(const std::string& key) const;

    /**
     * Get all configuration entries
     *
     * @param category Filter by category (optional)
     * @return Map of configuration entries
     */
    std::map<std::string, ConfigEntry> get_all(const std::string& category = "") const;

    /**
     * Create configuration snapshot (version)
     *
     * @param description Version description
     * @param created_by Who created this version
     * @return Version ID if successful, empty string otherwise
     */
    std::string create_version(const std::string& description = "",
                              const std::string& created_by = "system");

    /**
     * Rollback to specific version
     *
     * @param version_id Target version ID
     * @param rolled_back_by Who performed rollback
     * @param reason Reason for rollback
     * @return True if rollback successful
     */
    bool rollback_to_version(const std::string& version_id,
                             const std::string& rolled_back_by = "system",
                             const std::string& reason = "");

    /**
     * Get all available versions
     *
     * @return Vector of configuration versions
     */
    std::vector<ConfigVersion> get_versions() const;

    /**
     * Get version by ID
     *
     * @param version_id Version ID
     * @return Version information (empty if not found)
     */
    ConfigVersion get_version(const std::string& version_id) const;

    /**
     * Get configuration change log
     *
     * @param key Filter by configuration key (optional)
     * @param limit Maximum number of changes to return (0 = all)
     * @return Vector of configuration changes
     */
    std::vector<ConfigChange> get_change_log(const std::string& key = "", size_t limit = 0) const;

    /**
     * Add validation rule
     *
     * @param rule Validation rule to add
     */
    void add_validation_rule(const ValidationRule& rule);

    /**
     * Validate entire configuration
     *
     * @return Pair of (is_valid, error_messages)
     */
    std::pair<bool, std::vector<std::string>> validate_all() const;

    /**
     * Export configuration to file
     *
     * @param file_path Export file path
     * @param include_metadata Include creation/modification metadata
     * @param category Filter by category (optional)
     * @return True if export successful
     */
    bool export_to_file(const std::string& file_path,
                        bool include_metadata = true,
                        const std::string& category = "") const;

    /**
     * Import configuration from file
     *
     * @param file_path Import file path
     * @param merge_mode true=merge with existing, false=replace all
     * @param imported_by Who performed import
     * @return True if import successful
     */
    bool import_from_file(const std::string& file_path,
                          bool merge_mode = true,
                          const std::string& imported_by = "system");

    /**
     * Get configuration statistics
     */
    struct ConfigStats {
        size_t total_entries;
        size_t total_versions;
        size_t total_changes;
        std::map<std::string, size_t> entries_by_category;
        std::chrono::system_clock::time_point last_modified;

        ConfigStats() : total_entries(0), total_versions(0), total_changes(0),
                       last_modified(std::chrono::system_clock::time_point{}) {}
    };

    ConfigStats get_statistics() const;

    /**
     * Enable/disable auto-save
     *
     * @param enabled Enable auto-save
     */
    void set_auto_save(bool enabled);

    /**
     * Force save configuration
     *
     * @return True if save successful
     */
    bool force_save() const;

    /**
     * Clear all configuration data
     *
     * @param cleared_by Who cleared the configuration
     * @param reason Reason for clearing
     */
    void clear_all(const std::string& cleared_by = "system",
                   const std::string& reason = "");

    /**
     * Search configuration entries
     *
     * @param query Search query
     * @param search_values Search in values too (not just keys)
     * @param case_sensitive Case sensitive search
     * @return Vector of matching configuration keys
     */
    std::vector<std::string> search(const std::string& query,
                                    bool search_values = true,
                                    bool case_sensitive = false) const;
};

/**
 * RAII Configuration Scope for temporary configuration changes
 */
class ConfigScope {
private:
    ConfigManager& config_;
    std::vector<std::pair<std::string, std::string>> original_values_;
    bool committed_;

public:
    ConfigScope(ConfigManager& config);
    ~ConfigScope();

    /**
     * Set temporary configuration value
     */
    void set_temp(const std::string& key, const std::string& value);

    /**
     * Commit temporary changes permanently
     */
    void commit();

    /**
     * Discard temporary changes (automatic in destructor)
     */
    void rollback();
};