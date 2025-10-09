/**
 * Puzzle71Solver - Integration Manifest System
 *
 * Provides comprehensive build configuration management through integration manifests,
 * enabling structured tracking of library integration metadata, dependencies,
 * build configurations, and version management across the entire integration process.
 *
 * @author       Puzzle71Solver Team
 * @created      2025-10-09
 * @license      MIT
 */

#pragma once

#include <string>
#include <vector>
#include <map>
#include <filesystem>
#include <memory>
#include <chrono>
#include <optional>
#include <nlohmann/json.hpp>

namespace integration {
namespace manifest {

using json = nlohmann::json;

// Manifest version and format
enum class ManifestVersion {
    V1_0,  // Initial manifest format
    V1_1,  // Enhanced dependency tracking
    V2_0   // Full build configuration management
};

// Library integration status
enum class IntegrationStatus {
    PENDING,        // Library is pending integration
    IN_PROGRESS,    // Integration is in progress
    COMPLETED,      // Integration completed successfully
    FAILED,         // Integration failed
    SKIPPED,        // Integration was skipped
    ROLLED_BACK     // Integration was rolled back
};

// Build configuration type
enum class BuildType {
    DEBUG,
    RELEASE,
    RELWITHDEBINFO,
    MINSIZEREL,
    CUSTOM
};

// Dependency type
enum class DependencyType {
    REQUIRED,       // Required dependency
    OPTIONAL,       // Optional dependency
    DEVELOPMENT,    // Development-only dependency
    TEST,          // Test-only dependency
    CONFLICTING     // Conflicting dependency (should not be used together)
};

// Integration metadata
struct IntegrationMetadata {
    std::string library_name;
    std::string version;
    std::string origin_url;
    std::string origin_commit;
    std::string origin_license;
    std::string extracted_date;
    std::string extracted_by;
    IntegrationStatus status = IntegrationStatus::PENDING;
    std::chrono::system_clock::time_point integration_date;
    std::string integration_description;
    std::map<std::string, std::string> custom_metadata;
};

// Build configuration
struct BuildConfiguration {
    std::string config_name;
    BuildType build_type;
    std::map<std::string, std::string> cmake_variables;
    std::vector<std::string> compiler_flags;
    std::vector<std::string> linker_flags;
    std::vector<std::string> preprocessor_definitions;
    std::map<std::string, std::string> environment_variables;
    std::string custom_build_script;
    bool is_default = false;
};

// Dependency information
struct DependencyInfo {
    std::string name;
    std::string version_constraint;
    DependencyType type;
    bool is_optional = false;
    std::vector<std::string> alternatives;
    std::map<std::string, std::string> metadata;
};

// Component information (for selective inclusion)
struct ComponentInfo {
    std::string name;
    std::string description;
    bool is_included = true;
    bool is_required = false;
    std::vector<std::string> source_files;
    std::vector<std::string> header_files;
    std::vector<std::string> dependencies;
    std::map<std::string, std::string> metadata;
};

// Integration manifest structure
struct IntegrationManifest {
    // Manifest metadata
    std::string manifest_id;
    ManifestVersion version = ManifestVersion::V2_0;
    std::string schema_version = "2.0";
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point updated_at;
    std::string created_by;
    std::string description;
    std::string project_name;

    // Integration information
    std::vector<IntegrationMetadata> libraries;
    std::map<std::string, std::vector<DependencyInfo>> dependencies;
    std::map<std::string, std::vector<ComponentInfo>> components;

    // Build configurations
    std::vector<BuildConfiguration> build_configurations;
    std::string default_build_config;

    // Validation and integrity
    std::map<std::string, std::string> file_hashes;
    std::map<std::string, std::string> digests;
    std::vector<std::string> validation_rules;

    // Integration history
    std::vector<std::string> integration_log;
    std::map<std::string, std::chrono::system_clock::time_point> milestones;

    // System metadata
    std::string integration_root;
    std::vector<std::string> supported_platforms;
    std::vector<std::string> required_tools;
    std::map<std::string, std::string> system_requirements;
};

// Manifest operation result
struct ManifestOperationResult {
    bool successful = false;
    std::string operation;
    std::string description;
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
    std::map<std::string, std::string> metadata;
    std::chrono::milliseconds operation_time{0};
};

// Validation result
struct ValidationResult {
    bool valid = false;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    std::map<std::string, std::vector<std::string>> dependency_issues;
    std::vector<std::string> missing_files;
    std::vector<std::string> hash_mismatches;
    std::map<std::string, std::string> recommendations;
};

class IntegrationManifestManager {
public:
    IntegrationManifestManager();
    explicit IntegrationManifestManager(const std::filesystem::path& manifest_path);
    ~IntegrationManifestManager();

    // Manifest management
    bool create_manifest(const std::string& project_name, const std::filesystem::path& integration_root);
    bool load_manifest(const std::filesystem::path& manifest_path);
    bool save_manifest(const std::filesystem::path& manifest_path = {});
    bool validate_manifest();

    // Library management
    ManifestOperationResult add_library(const IntegrationMetadata& library);
    ManifestOperationResult remove_library(const std::string& library_name);
    ManifestOperationResult update_library(const std::string& library_name, const IntegrationMetadata& updated_metadata);
    std::optional<IntegrationMetadata> get_library(const std::string& library_name) const;
    std::vector<IntegrationMetadata> get_all_libraries() const;

    // Dependency management
    ManifestOperationResult add_dependency(const std::string& library_name, const DependencyInfo& dependency);
    ManifestOperationResult remove_dependency(const std::string& library_name, const std::string& dependency_name);
    ManifestOperationResult update_dependency(const std::string& library_name, const std::string& dependency_name, const DependencyInfo& updated_dependency);
    std::vector<DependencyInfo> get_library_dependencies(const std::string& library_name) const;
    std::vector<std::string> get_dependency_order() const;

    // Component management
    ManifestOperationResult add_component(const std::string& library_name, const ComponentInfo& component);
    ManifestOperationResult remove_component(const std::string& library_name, const std::string& component_name);
    ManifestOperationResult update_component(const std::string& library_name, const std::string& component_name, const ComponentInfo& updated_component);
    ManifestOperationResult include_component(const std::string& library_name, const std::string& component_name, bool include = true);
    std::vector<ComponentInfo> get_library_components(const std::string& library_name) const;
    std::vector<ComponentInfo> get_included_components(const std::string& library_name) const;

    // Build configuration management
    ManifestOperationResult add_build_configuration(const BuildConfiguration& config);
    ManifestOperationResult remove_build_configuration(const std::string& config_name);
    ManifestOperationResult update_build_configuration(const std::string& config_name, const BuildConfiguration& updated_config);
    std::optional<BuildConfiguration> get_build_configuration(const std::string& config_name) const;
    std::vector<BuildConfiguration> get_all_build_configurations() const;
    void set_default_build_configuration(const std::string& config_name);

    // Validation and integrity
    ValidationResult validate_integrity() const;
    ValidationResult validate_dependencies() const;
    ValidationResult validate_build_system() const;
    ValidationResult validate_components() const;
    ValidationResult validate_complete() const;

    // File and hash management
    ManifestOperationResult update_file_hashes();
    ManifestOperationResult verify_file_integrity();
    std::map<std::string, std::string> get_file_hashes() const;
    bool verify_file_hash(const std::filesystem::path& file_path, const std::string& expected_hash) const;

    // Import and export
    ManifestOperationResult import_manifest(const std::filesystem::path& import_path);
    ManifestOperationResult export_manifest(const std::filesystem::path& export_path) const;
    ManifestOperationResult merge_manifest(const IntegrationManifest& other_manifest);

    // Template and generation
    static IntegrationManifest create_template(const std::string& project_name);
    ManifestOperationResult generate_from_integration(const std::filesystem::path& integration_root);
    ManifestOperationResult update_from_filesystem();

    // Version management
    ManifestOperationResult upgrade_schema(ManifestVersion target_version);
    std::string get_schema_version() const;
    bool is_compatible_with(const IntegrationManifest& other) const;

    // Analysis and reporting
    std::vector<std::string> find_missing_dependencies() const;
    std::vector<std::string> find_circular_dependencies() const;
    std::vector<std::string> find_conflicting_dependencies() const;
    std::map<std::string, size_t> get_component_sizes() const;
    std::string generate_dependency_graph() const;
    std::string generate_integration_report() const;

    // CMake generation
    ManifestOperationResult generate_cmake_files(const std::filesystem::path& output_dir) const;
    std::string generate_cmake_config(const std::string& library_name) const;
    std::string generate_cmake_dependencies() const;

    // Manifest access
    const IntegrationManifest& get_manifest() const;
    void set_manifest(const IntegrationManifest& manifest);

    // Utility methods
    std::string to_json() const;
    bool from_json(const std::string& json_string);
    std::string to_yaml() const;
    bool from_yaml(const std::string& yaml_string);

    // Configuration
    void set_auto_save(bool enabled);
    void set_auto_validate(bool enabled);
    void set_integrity_check_enabled(bool enabled);
    void set_dependency_check_enabled(bool enabled);

private:
    struct Impl;
    std::unique_ptr<Impl> p_impl;

    // Internal helpers
    std::string generate_manifest_id() const;
    std::string calculate_file_hash(const std::filesystem::path& file_path) const;
    bool validate_library_metadata(const IntegrationMetadata& metadata) const;
    bool validate_dependency_info(const DependencyInfo& dependency) const;
    bool validate_component_info(const ComponentInfo& component) const;
    bool validate_build_configuration(const BuildConfiguration& config) const;

    // Schema handling
    std::string serialize_manifest(const IntegrationManifest& manifest) const;
    IntegrationManifest deserialize_manifest(const std::string& data) const;
    bool validate_schema(const IntegrationManifest& manifest) const;

    // Dependency analysis
    std::vector<std::string> topological_sort(const std::map<std::string, std::vector<std::string>>& graph) const;
    bool has_circular_dependency(const std::map<std::string, std::vector<std::string>>& graph) const;
    std::vector<std::string> find_dependency_path(const std::string& from, const std::string& to) const;

    // File system operations
    std::vector<std::filesystem::path> find_library_files(const std::string& library_name) const;
    std::vector<std::string> extract_symbols_from_file(const std::filesystem::path& file_path) const;
    std::map<std::string, std::string> parse_cmake_file(const std::filesystem::path& cmake_file) const;

    // String utilities
    std::string manifest_version_to_string(ManifestVersion version) const;
    std::string integration_status_to_string(IntegrationStatus status) const;
    std::string build_type_to_string(BuildType type) const;
    std::string dependency_type_to_string(DependencyType type) const;

    ManifestVersion string_to_manifest_version(const std::string& version) const;
    IntegrationStatus string_to_integration_status(const std::string& status) const;
    BuildType string_to_build_type(const std::string& type) const;
    DependencyType string_to_dependency_type(const std::string& type) const;
};

// Utility functions
std::string generate_manifest_id();
bool validate_manifest_schema(const std::string& manifest_json);
std::vector<std::string> calculate_dependency_order(const std::vector<DependencyInfo>& dependencies);
std::string create_dependency_graph_dot(const std::map<std::string, std::vector<DependencyInfo>>& dependencies);

// Global manifest manager instance
IntegrationManifestManager& get_integration_manifest_manager();

} // namespace manifest
} // namespace integration