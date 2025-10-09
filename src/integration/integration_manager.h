/**
 * Puzzle71Solver - Integration Management System
 *
 * Provides infrastructure for managing third-party library integration,
 * including attribution, verification, and build system integration.
 *
 * @author       Puzzle71Solver Team
 * @created      2025-10-09
 * @license      MIT
 */

#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <chrono>
#include <filesystem>

namespace integration {

struct LibraryInfo {
    std::string name;
    std::string version;
    std::string origin_url;
    std::string origin_commit;
    std::string license_type;
    std::string spdx_identifier;
    std::filesystem::path extract_path;
    std::vector<std::string> source_files;
    std::vector<std::string> include_dirs;
    std::vector<std::string> dependencies;
    std::vector<std::string> excluded_components;  // Tests, docs, examples
    bool is_integrated = false;
    std::chrono::sys_seconds integration_date;
    size_t estimated_size_bytes = 0;
    std::string component_type;  // "core", "optional", "development"
};

struct AttributionInfo {
    std::string project_name;
    std::string author;
    std::string origin_url;
    std::string origin_path;
    std::string origin_commit;
    std::string origin_license;
    std::chrono::sys_days extracted_date;
    std::string extracted_by;
    std::string modifications;
    std::string spdx_license_identifier;
};

struct ConflictInfo {
    std::string conflict_type;  // "version", "license", "symbol", "file"
    std::string library1;
    std::string library2;
    std::string description;
    std::string severity;      // "error", "warning", "info"
    std::string resolution;
};

struct DependencyConflict {
    std::string library_name;
    std::string conflict_type;  // "circular", "missing", "version_mismatch"
    std::vector<std::string> conflict_chain;
    std::string description;
    bool is_blocking = false;
};

struct ResourceAllocation {
    std::string library_name;
    std::string resource_type;  // "memory", "disk_space", "build_time"
    size_t allocated_amount;
    size_t used_amount;
    std::string allocation_pool;  // "core", "optional", "cache"
    float priority_score = 0.0f;
};

struct IntegrationPool {
    std::string pool_name;
    size_t total_capacity;
    size_t used_capacity;
    std::vector<std::string> allocated_libraries;
    std::map<std::string, size_t> library_allocations;
};

struct IntegrationReport {
    bool success = false;
    std::string message;
    std::vector<std::string> extracted_files;
    std::vector<std::string> attribution_headers_added;
    bool build_configuration_updated = false;
    std::chrono::milliseconds integration_time;
    std::vector<ConflictInfo> conflicts_detected;
    std::vector<DependencyConflict> dependency_conflicts;
    std::vector<ResourceAllocation> resource_allocations;
};

class IntegrationManager {
public:
    IntegrationManager();
    ~IntegrationManager();

    // Core integration operations
    bool integrate_library(const LibraryInfo& library);
    bool verify_integration(const std::string& library_name);
    bool update_library(const std::string& library_name, const std::string& new_version);
    bool remove_library(const std::string& library_name);

    // Attribution management
    bool add_attribution_header(const std::filesystem::path& file_path,
                               const AttributionInfo& attribution);
    bool verify_attribution_coverage(const std::string& library_name);
    std::vector<AttributionInfo> get_attribution_for_library(const std::string& library_name);

    // Configuration management
    bool load_configuration(const std::filesystem::path& config_path);
    bool save_configuration(const std::filesystem::path& config_path);
    bool set_integration_property(const std::string& key, const std::string& value);
    std::string get_integration_property(const std::string& key) const;

    // Verification and validation
    bool verify_library_integrity(const std::string& library_name);
    bool verify_build_system_integration();
    bool generate_integration_report(const std::string& library_name, IntegrationReport& report);

    // Logging and monitoring
    void enable_logging(bool enabled);
    void set_log_level(const std::string& level);
    std::string get_integration_logs() const;

    // T014a: Dependency monitoring and conflict detection
    std::vector<ConflictInfo> detect_conflicts(const std::string& library_name);
    std::vector<DependencyConflict> detect_dependency_conflicts(const std::vector<std::string>& libraries);
    bool resolve_conflict(const ConflictInfo& conflict);
    bool validate_dependency_chain(const std::string& library_name);
    std::map<std::string, std::vector<std::string>> get_dependency_graph() const;

    // T014b: Selective component inclusion
    bool set_excluded_components(const std::string& library_name, const std::vector<std::string>& components);
    std::vector<std::string> get_excluded_components(const std::string& library_name) const;
    bool include_component_type(const std::string& library_name, const std::string& component_type);
    std::vector<std::string> get_component_inclusion_plan(const std::string& library_name) const;

    // T014c: Integration pool allocation optimization
    bool allocate_to_pool(const std::string& library_name, const std::string& pool_name);
    ResourceAllocation get_resource_allocation(const std::string& library_name) const;
    bool optimize_pool_allocations();
    std::vector<IntegrationPool> get_integration_pools() const;
    size_t estimate_library_resources(const std::string& library_name) const;

    // Utility functions
    std::vector<LibraryInfo> get_integrated_libraries() const;
    bool is_library_integrated(const std::string& library_name) const;
    std::filesystem::path get_integration_root() const;

private:
    struct Impl;
    std::unique_ptr<Impl> p_impl;
};

// Global integration manager instance
IntegrationManager& get_integration_manager();

} // namespace integration