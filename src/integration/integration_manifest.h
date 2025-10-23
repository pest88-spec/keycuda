#pragma once

/**
 * @file integration_manifest.h
 * @brief Integration manifest system for build configuration management
 *
 * This file defines the integration manifest system that provides comprehensive
 * build configuration management for third-party dependencies integration.
 * It handles library manifests, dependency manifests, and integration
 * configuration with validation and consistency checking.
 *
 * Created: 2025-10-22
 * Feature: Third-Party Dependencies Integration Optimization
 */

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <chrono>
#include <memory>
#include <functional>
#include <variant>

namespace integration {
namespace manifest {

/**
 * @brief Library information in integration manifest
 */
struct LibraryInfo {
    std::string library_name;
    std::string library_version;
    std::string origin_url;
    std::string commit_hash;
    std::string source_path;
    std::string build_path;
    std::vector<std::string> cmake_targets;
    std::vector<std::string> dependencies;
    std::map<std::string, std::string> build_options;
    std::vector<std::string> include_directories;
    std::vector<std::string> link_libraries;
    bool is_enabled;
    std::chrono::system_clock::time_point last_updated;
    std::map<std::string, std::string> metadata;
};

/**
 * @brief Build configuration section
 */
struct BuildConfiguration {
    std::string build_type;
    std::string toolchain_version;
    std::vector<std::string> compiler_flags;
    std::vector<std::string> cmake_definitions;
    std::map<std::string, std::string> environment_variables;
    std::string install_prefix;
    std::vector<std::string> build_dependencies;
    std::map<std::string, std::string> build_options;
    bool is_offline_build;
    std::string cuda_architecture;
    std::vector<std::string> required_tools;
};

/**
 * @brief Version constraint information
 */
struct VersionConstraint {
    std::string library_name;
    std::string constraint_type;  // "exact", "minimum", "maximum", "range"
    std::string version_spec;
    std::optional<std::string> reason;
};

/**
 * @brief Integration manifest types
 */
enum class ManifestType {
    LIBRARY_MANIFEST,     ///< Individual library manifest
    DEPENDENCY_MANIFEST,  ///< Overall dependency manifest
    PROJECT_MANIFEST,     ///< Project-wide integration manifest
    BUILD_MANIFEST        ///< Build-specific manifest
};

/**
 * @brief Base integration manifest
 */
struct IntegrationManifest {
    std::string manifest_id;
    ManifestType manifest_type;
    std::string schema_version;
    std::chrono::system_clock::time_point created_at;
    std::string created_by;
    std::string description;
    std::vector<LibraryInfo> libraries;
    BuildConfiguration build_config;
    std::vector<VersionConstraint> version_constraints;
    std::map<std::string, std::string> metadata;
    std::vector<std::string> tags;
    std::string manifest_version;
};

/**
 * @brief Dependency manifest (as defined in data-model.md)
 */
struct DependencyManifest {
    std::string manifest_version;
    std::vector<LibraryInfo> libraries;
    std::map<std::string, std::string> version_constraints;
    std::vector<std::string> build_sources;
    std::vector<std::string> exclude_patterns;
    std::map<std::string, std::string> custom_attribution;
};

/**
 * @brief Integration configuration (as defined in data-model.md)
 */
struct IntegrationConfiguration {
    bool enable_strict_attribution;
    bool preserve_original_structure;
    bool add_namespace_prefix;
    std::string namespace_prefix;
    std::vector<std::string> source_modifications;
    std::map<std::string, std::string> compile_flags;
    bool enable_offline_builds;
};

/**
 * @brief Manifest validation result
 */
struct ValidationResult {
    bool is_valid;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    std::vector<std::string> info;
};

/**
 * @brief Manifest manager interface
 */
class ManifestManager {
public:
    virtual ~ManifestManager() = default;

    /**
     * @brief Initialize the manifest manager
     * @param manifests_dir Directory containing manifest files
     * @return True if initialization successful
     */
    virtual bool initialize(const std::string& manifests_dir = "build/integration-manifests") = 0;

    /**
     * @brief Create a new library manifest
     * @param library_name Name of the library
     * @param library_version Version of the library
     * @param source_path Source path for the library
     * @return Created manifest
     */
    virtual IntegrationManifest create_library_manifest(
        const std::string& library_name,
        const std::string& library_version,
        const std::string& source_path) = 0;

    /**
     * @brief Load manifest from file
     * @param manifest_id ID of the manifest to load
     * @return Loaded manifest or empty optional if not found
     */
    virtual std::optional<IntegrationManifest> load_manifest(const std::string& manifest_id) = 0;

    /**
     * @brief Save manifest to file
     * @param manifest Manifest to save
     * @return True if save successful
     */
    virtual bool save_manifest(const IntegrationManifest& manifest) = 0;

    /**
     * @brief Get all manifests
     * @param type Optional type filter
     * @return All manifests of specified type (or all if no type specified)
     */
    virtual std::vector<IntegrationManifest> get_all_manifests(
        std::optional<ManifestType> type = std::nullopt) = 0;

    /**
     * @brief Find manifests by library name
     * @param library_name Name of the library
     * @return Manifests for the specified library
     */
    virtual std::vector<IntegrationManifest> find_manifests_by_library(
        const std::string& library_name) = 0;

    /**
     * @brief Update library in manifest
     * @param manifest_id ID of the manifest
     * @param library_info Updated library information
     * @return True if update successful
     */
    virtual bool update_library(const std::string& manifest_id,
                               const LibraryInfo& library_info) = 0;

    /**
     * @brief Remove library from manifest
     * @param manifest_id ID of the manifest
     * @param library_name Name of the library to remove
     * @return True if removal successful
     */
    virtual bool remove_library(const std::string& manifest_id,
                               const std::string& library_name) = 0;

    /**
     * @brief Validate manifest
     * @param manifest Manifest to validate
     * @return Validation result
     */
    virtual ValidationResult validate_manifest(const IntegrationManifest& manifest) = 0;

    /**
     * @brief Generate CMake configuration from manifests
     * @return CMake configuration content
     */
    virtual std::string generate_cmake_configuration() = 0;

    /**
     * @brief Get dependency manifest
     * @return Complete dependency manifest
     */
    virtual DependencyManifest get_dependency_manifest() = 0;

    /**
     * @brief Get integration configuration
     * @return Current integration configuration
     */
    virtual IntegrationConfiguration get_integration_configuration() = 0;

    /**
     * @brief Set integration configuration
     * @param config New integration configuration
     * @return True if configuration set successfully
     */
    virtual bool set_integration_configuration(const IntegrationConfiguration& config) = 0;

    /**
     * @brief Check for manifest conflicts
     * @return List of detected conflicts
     */
    virtual std::vector<std::string> check_conflicts() = 0;

    /**
     * @brief Resolve manifest conflicts
     * @param conflicts List of conflicts to resolve
     * @return True if conflicts resolved successfully
     */
    virtual bool resolve_conflicts(const std::vector<std::string>& conflicts) = 0;

    /**
     * @brief Get build configuration from manifests
     * @return Consolidated build configuration
     */
    virtual BuildConfiguration get_build_configuration() = 0;

    /**
     * @brief Export manifests to different format
     * @param format Export format ("json", "yaml", "cmake")
     * @return Exported content
     */
    virtual std::string export_manifests(const std::string& format) = 0;

    /**
     * @brief Import manifests from format
     * @param content Content to import
     * @param format Import format ("json", "yaml", "cmake")
     * @return True if import successful
     */
    virtual bool import_manifests(const std::string& content, const std::string& format) = 0;
};

/**
 * @brief Standard manifest manager implementation
 */
class StandardManifestManager : public ManifestManager {
public:
    StandardManifestManager();
    ~StandardManifestManager() override;

    // ManifestManager interface
    bool initialize(const std::string& manifests_dir = "build/integration-manifests") override;
    IntegrationManifest create_library_manifest(
        const std::string& library_name,
        const std::string& library_version,
        const std::string& source_path) override;
    std::optional<IntegrationManifest> load_manifest(const std::string& manifest_id) override;
    bool save_manifest(const IntegrationManifest& manifest) override;
    std::vector<IntegrationManifest> get_all_manifests(
        std::optional<ManifestType> type = std::nullopt) override;
    std::vector<IntegrationManifest> find_manifests_by_library(
        const std::string& library_name) override;
    bool update_library(const std::string& manifest_id,
                       const LibraryInfo& library_info) override;
    bool remove_library(const std::string& manifest_id,
                       const std::string& library_name) override;
    ValidationResult validate_manifest(const IntegrationManifest& manifest) override;
    std::string generate_cmake_configuration() override;
    DependencyManifest get_dependency_manifest() override;
    IntegrationConfiguration get_integration_configuration() override;
    bool set_integration_configuration(const IntegrationConfiguration& config) override;
    std::vector<std::string> check_conflicts() override;
    bool resolve_conflicts(const std::vector<std::string>& conflicts) override;
    BuildConfiguration get_build_configuration() override;
    std::string export_manifests(const std::string& format) override;
    bool import_manifests(const std::string& content, const std::string& format) override;

private:
    struct Impl;
    std::unique_ptr<Impl> p_impl;
};

/**
 * @brief Create standard manifest manager instance
 * @return Unique pointer to manifest manager
 */
std::unique_ptr<ManifestManager> create_manifest_manager();

/**
 * @brief Convert manifest type to string
 * @param type Manifest type
 * @return String representation
 */
std::string manifest_type_to_string(ManifestType type);

/**
 * @brief Convert string to manifest type
 * @param type_str String representation
 * @return Manifest type
 */
ManifestType string_to_manifest_type(const std::string& type_str);

/**
 * @brief Generate manifest ID
 * @param library_name Library name
 * @param timestamp Optional timestamp (uses current time if not provided)
 * @return Generated manifest ID
 */
std::string generate_manifest_id(const std::string& library_name,
                                std::optional<std::chrono::system_clock::time_point> timestamp = std::nullopt);

/**
 * @brief Serialize manifest to JSON
 * @param manifest Manifest to serialize
 * @return JSON string
 */
std::string manifest_to_json(const IntegrationManifest& manifest);

/**
 * @brief Deserialize manifest from JSON
 * @param json JSON string
 * @return Deserialized manifest or empty optional if invalid
 */
std::optional<IntegrationManifest> manifest_from_json(const std::string& json);

/**
 * @brief Format validation result for display
 * @param result Validation result
 * @return Formatted string
 */
std::string format_validation_result(const ValidationResult& result);

/**
 * @brief Check library compatibility
 * @param lib1 First library
 * @param lib2 Second library
 * @return Compatibility message
 */
std::string check_library_compatibility(const LibraryInfo& lib1, const LibraryInfo& lib2);

/**
 * @brief Merge build configurations
 * @param configs List of build configurations
 * @return Merged build configuration
 */
BuildConfiguration merge_build_configurations(const std::vector<BuildConfiguration>& configs);

/**
 * @brief Extract version from string
 * @param version_string Version string (e.g., "v1.2.3", "1.2.3")
 * @return Normalized version string
 */
std::string normalize_version(const std::string& version_string);

/**
 * @brief Compare versions
 * @param version1 First version
 * @param version2 Second version
 * @return -1 if version1 < version2, 0 if equal, 1 if version1 > version2
 */
int compare_versions(const std::string& version1, const std::string& version2);

} // namespace manifest
} // namespace integration