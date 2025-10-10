/**
 * Puzzle71Solver - Integration Manifest System
 *
 * Provides comprehensive build configuration management through integration manifests,
 * enabling standardized library integration with complete dependency tracking and
 * build system orchestration.
 *
 * @author       Puzzle71Solver Team
 * @created      2025-10-10
 * @license      MIT
 */

#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <filesystem>
#include <chrono>
#include <optional>
#include <functional>

namespace integration {
namespace manifest {

enum class ComponentType {
    CORE,           // Essential for basic functionality
    OPTIONAL,       // Optional components that can be excluded
    DEVELOPMENT,    // Development-only components (tests, examples)
    DOCUMENTATION,  // Documentation files
    BUILD_TOOLS,    // Build scripts and tools
    RUNTIME,        // Runtime dependencies
    TEST_FRAMEWORK  // Testing frameworks
};

enum class BuildType {
    STATIC,         // Static linking
    SHARED,         // Shared/dynamic linking
    HEADER_ONLY,    // Header-only libraries
    INTERFACE,      // Interface libraries
    OBJECT,         // Object libraries
    UTILITY         // Utility libraries
};

enum class IntegrationStatus {
    PENDING,        // Integration planned but not started
    IN_PROGRESS,    // Integration currently in progress
    COMPLETED,      // Integration successfully completed
    FAILED,         // Integration failed
    SKIPPED,        // Integration skipped (optional component)
    BLOCKED,        // Integration blocked by dependencies
    VERIFIED        // Integration completed and verified
};

struct Component {
    std::string name;
    ComponentType type;
    std::vector<std::string> source_files;
    std::vector<std::string> header_files;
    std::vector<std::string> include_directories;
    std::vector<std::string> library_dependencies;
    std::vector<std::string> system_dependencies;
    std::map<std::string, std::string> build_options;
    std::map<std::string, std::string> compile_definitions;
    std::vector<std::string> compile_flags;
    std::vector<std::string> link_flags;
    bool is_enabled = true;
    std::string description;
    size_t estimated_size_bytes = 0;
    std::chrono::system_clock::time_point last_modified;
};

struct LibraryManifest {
    // Basic information
    std::string name;
    std::string version;
    std::string description;
    std::string origin_url;
    std::string origin_commit;
    std::string origin_tag;
    std::string license_type;
    std::string spdx_identifier;

    // Integration metadata
    std::filesystem::path integration_path;
    std::filesystem::path build_path;
    IntegrationStatus integration_status = IntegrationStatus::PENDING;
    std::chrono::system_clock::time_point integration_date;
    std::chrono::system_clock::time_point last_verified;
    std::string integration_method;  // "extraction", "symlink", "copy"

    // Build configuration
    BuildType build_type = BuildType::STATIC;
    std::string cmake_target_name;
    std::vector<std::string> cmake_options;
    std::map<std::string, std::string> cmake_variables;
    std::vector<std::string> pre_build_commands;
    std::vector<std::string> post_build_commands;

    // Components
    std::vector<Component> components;
    std::map<std::string, ComponentType> component_types;
    std::set<std::string> enabled_components;
    std::set<std::string> disabled_components;

    // Dependencies
    std::vector<std::string> library_dependencies;
    std::vector<std::string> system_dependencies;
    std::map<std::string, std::string> version_constraints;
    std::map<std::string, std::vector<std::string>> dependency_alternatives;

    // File structure
    std::vector<std::string> source_files;
    std::vector<std::string> header_files;
    std::vector<std::string> include_directories;
    std::vector<std::string> library_directories;
    std::vector<std::string> binary_files;
    std::vector<std::string> data_files;
    std::vector<std::string> documentation_files;

    // Attribution and compliance
    std::string author;
    std::string maintainer;
    std::vector<std::string> contributors;
    std::string copyright_notice;
    std::map<std::string, std::string> attribution_metadata;

    // Quality and metrics
    bool has_unit_tests = false;
    bool has_documentation = false;
    bool has_examples = false;
    double test_coverage = 0.0;
    size_t lines_of_code = 0;
    size_t cyclomatic_complexity = 0;

    // Verification and integrity
    std::map<std::string, std::string> file_hashes;  // file_path -> sha256
    std::string manifest_signature;
    std::map<std::string, std::string> verification_metadata;
    bool integrity_verified = false;

    // Performance characteristics
    std::chrono::milliseconds build_time{0};
    size_t memory_usage_mb = 0;
    size_t disk_usage_mb = 0;
    std::map<std::string, std::string> performance_metrics;
};

struct ProjectManifest {
    // Project information
    std::string project_name;
    std::string project_version;
    std::string description;
    std::string build_system;  // "cmake", "make", etc.
    std::string compiler;
    std::string language_standard;

    // Integration configuration
    std::filesystem::path integration_root;
    std::filesystem::path build_root;
    std::filesystem::path manifest_directory;
    std::vector<std::string> integration_order;
    std::map<std::string, std::string> global_build_options;
    std::map<std::string, std::string> environment_variables;

    // Library manifests
    std::vector<LibraryManifest> libraries;
    std::map<std::string, LibraryManifest> library_index;
    std::map<std::string, IntegrationStatus> library_status;

    // Build configuration
    std::string default_build_type;
    std::vector<std::string> supported_build_types;
    std::map<std::string, std::vector<std::string>> platform_specific_config;
    std::set<std::string> required_tools;
    std::map<std::string, std::string> tool_versions;

    // Dependency management
    std::map<std::string, std::vector<std::string>> dependency_graph;
    std::vector<std::string> build_order;
    std::set<std::string> optional_libraries;
    std::set<std::string> required_libraries;

    // Quality and compliance
    std::map<std::string, std::string> compliance_requirements;
    std::vector<std::string> quality_checks;
    std::map<std::string, bool> compliance_status;

    // Metadata
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point last_updated;
    std::string created_by;
    std::string version_history;
    std::map<std::string, std::string> custom_metadata;
};

struct ManifestValidationResult {
    bool is_valid = false;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    std::vector<std::string> recommendations;
    std::map<std::string, std::string> validation_metadata;
    std::chrono::milliseconds validation_time{0};
};

class ManifestManager {
public:
    ManifestManager();
    explicit ManifestManager(const std::filesystem::path& manifest_directory);
    ~ManifestManager();

    // Configuration
    void set_manifest_directory(const std::filesystem::path& directory);
    std::filesystem::path get_manifest_directory() const;
    void set_auto_save(bool enabled);
    void set_validation_enabled(bool enabled);

    // Library manifest operations
    bool create_library_manifest(const LibraryManifest& manifest);
    bool load_library_manifest(const std::string& library_name, LibraryManifest& manifest);
    bool save_library_manifest(const LibraryManifest& manifest);
    bool update_library_manifest(const std::string& library_name, const LibraryManifest& updates);
    bool delete_library_manifest(const std::string& library_name);
    std::vector<LibraryManifest> get_all_library_manifests() const;
    bool has_library_manifest(const std::string& library_name) const;

    // Project manifest operations
    bool create_project_manifest(const ProjectManifest& manifest);
    bool load_project_manifest(ProjectManifest& manifest);
    bool save_project_manifest(const ProjectManifest& manifest);
    bool update_project_manifest(const ProjectManifest& updates);

    // Manifest generation from source
    LibraryManifest generate_library_manifest(const std::filesystem::path& library_path);
    ProjectManifest generate_project_manifest(const std::filesystem::path& project_root);
    bool update_manifest_from_source(const std::string& library_name);

    // Build configuration generation
    std::string generate_cmake_configuration(const std::vector<std::string>& libraries) const;
    std::string generate_build_script(const std::vector<std::string>& libraries) const;
    std::string generate_dependency_file(const std::vector<std::string>& libraries) const;
    std::vector<std::string> get_build_order(const std::vector<std::string>& libraries) const;

    // Validation and verification
    ManifestValidationResult validate_library_manifest(const LibraryManifest& manifest) const;
    ManifestValidationResult validate_project_manifest(const ProjectManifest& manifest) const;
    bool verify_library_integrity(const std::string& library_name);
    bool verify_project_integrity();
    std::vector<std::string> detect_manifest_inconsistencies() const;

    // Dependency management
    std::vector<std::string> resolve_dependencies(const std::string& library_name) const;
    std::vector<std::string> get_dependency_chain(const std::string& library_name) const;
    bool check_dependencies_satisfied(const std::vector<std::string>& libraries) const;
    std::map<std::string, std::vector<std::string>> get_dependency_graph() const;

    // Component management
    bool enable_component(const std::string& library_name, const std::string& component_name);
    bool disable_component(const std::string& library_name, const std::string& component_name);
    std::vector<Component> get_enabled_components(const std::string& library_name) const;
    std::vector<Component> get_available_components(const std::string& library_name) const;

    // Version management
    bool update_library_version(const std::string& library_name, const std::string& new_version);
    std::string get_library_version(const std::string& library_name) const;
    std::vector<std::string> get_available_versions(const std::string& library_name) const;
    bool check_version_compatibility(const std::string& library1, const std::string& version1,
                                   const std::string& library2, const std::string& version2) const;

    // Integration status management
    bool update_integration_status(const std::string& library_name, IntegrationStatus status);
    IntegrationStatus get_integration_status(const std::string& library_name) const;
    std::vector<std::string> get_libraries_by_status(IntegrationStatus status) const;

    // Build orchestration
    bool orchestrate_build(const std::vector<std::string>& libraries);
    std::vector<std::string> generate_build_commands(const std::vector<std::string>& libraries) const;
    bool execute_build_plan(const std::vector<std::string>& build_commands);
    std::map<std::string, std::chrono::milliseconds> get_build_times() const;

    // Import and export
    bool export_manifests(const std::filesystem::path& export_path, const std::vector<std::string>& libraries = {}) const;
    bool import_manifests(const std::filesystem::path& import_path);
    bool merge_manifests(const std::filesystem::path& other_manifest_directory);

    // Reporting and analytics
    std::string generate_integration_report() const;
    std::string generate_dependency_report() const;
    std::string generate_build_configuration_report() const;
    std::map<std::string, std::string> get_integration_statistics() const;

    // Event handling
    void register_manifest_change_callback(std::function<void(const std::string&, const LibraryManifest&)> callback);
    void register_status_change_callback(std::function<void(const std::string&, IntegrationStatus, IntegrationStatus)> callback);
    void register_validation_callback(std::function<void(const std::string&, const ManifestValidationResult&)> callback);

    // Utility functions
    std::string serialize_manifest(const LibraryManifest& manifest) const;
    std::string serialize_manifest(const ProjectManifest& manifest) const;
    LibraryManifest deserialize_library_manifest(const std::string& data) const;
    ProjectManifest deserialize_project_manifest(const std::string& data) const;
    std::filesystem::path get_manifest_path(const std::string& library_name) const;

private:
    struct Impl;
    std::unique_ptr<Impl> p_impl;

    // Helper methods
    std::vector<std::string> scan_library_files(const std::filesystem::path& library_path) const;
    std::string detect_build_system(const std::filesystem::path& library_path) const;
    std::map<std::string, std::string> extract_build_options(const std::filesystem::path& library_path) const;
    std::vector<std::string> extract_dependencies(const std::filesystem::path& library_path) const;
    std::string calculate_file_hash(const std::filesystem::path& file_path) const;
    bool validate_manifest_schema(const std::string& manifest_data) const;
    std::string generate_manifest_signature(const LibraryManifest& manifest) const;
    void update_dependency_graph();
    std::vector<std::string> topological_sort(const std::map<std::string, std::vector<std::string>>& graph) const;
};

// Utility functions
std::string component_type_to_string(ComponentType type);
std::string build_type_to_string(BuildType type);
std::string integration_status_to_string(IntegrationStatus status);
ComponentType string_to_component_type(const std::string& type_str);
BuildType string_to_build_type(const std::string& type_str);
IntegrationStatus string_to_integration_status(const std::string& status_str);

// Global manifest manager instance
ManifestManager& get_manifest_manager();

} // namespace manifest
} // namespace integration