/**
 * Puzzle71Solver - Dependency Conflict Detection System
 *
 * Provides comprehensive detection and resolution of dependency conflicts
 * that arise during third-party library integration processes.
 *
 * @author       Puzzle71Solver Team
 * @created      2025-10-09
 * @license      MIT
 */

#pragma once

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <set>
#include <memory>
#include <filesystem>
#include <functional>
#include <mutex>
#include <any>
#include <chrono>

namespace integration {
namespace conflict_detection {

enum class ConflictSeverity {
    INFO,      // Informational, can be ignored
    WARNING,   // Should be addressed, but not blocking
    ERROR,     // Must be resolved, blocking integration
    CRITICAL   // Cannot proceed, requires immediate attention
};

enum class ConflictType {
    VERSION_MISMATCH,     // Different versions of same library
    LICENSE_INCOMPATIBLE, // Incompatible license requirements
    SYMBOL_CLASH,         // Duplicate symbols/functions
    FILE_CONFLICT,        // Same filename in multiple libraries
    BUILD_CONFLICT,       // Build system conflicts
    DEPENDENCY_CYCLE,     // Circular dependencies
    MISSING_DEPENDENCY,   // Required dependency not found
    API_INCOMPATIBILITY,  // Incompatible API changes
    CONFIGURATION_CONFLICT, // Conflicting configuration options
    RESOURCE_CONFLICT,    // Resource allocation conflicts
    NAMESPACE_CLASH,      // C++ namespace conflicts
    INCLUDE_CONFLICT      # Conflicting include paths
};

struct Conflict {
    std::string conflict_id;
    ConflictType type;
    ConflictSeverity severity;
    std::string library1;
    std::string library2;
    std::string description;
    std::string file_path;
    std::string symbol_name;
    std::string expected_version;
    std::string actual_version;
    std::string suggested_resolution;
    bool auto_resolvable = false;
    bool resolved = false;
    std::string resolution_method;
    std::chrono::system_clock::time_point detected_at;
};

struct DependencyNode {
    std::string library_name;
    std::string version;
    std::vector<std::string> dependencies;
    std::vector<std::string> dependents;
    bool is_external = true;
    bool is_optional = false;
    std::string license_type;
    std::set<std::string> provided_symbols;
    std::set<std::string> exported_headers;
    std::set<std::string> required_symbols;
};

struct ConflictDetectionConfig {
    bool enable_version_checking = true;
    bool enable_license_checking = true;
    bool enable_symbol_scanning = true;
    bool enable_build_analysis = true;
    bool enable_dependency_graph_analysis = true;
    std::set<std::string> ignored_conflicts;
    std::map<std::string, std::string> preferred_versions;
    std::map<std::string, std::vector<std::string>> license_compatibility_matrix;
    size_t max_scan_depth = 10;
    std::chrono::milliseconds scan_timeout{300000}; // 5 minutes
};

struct ConflictResolutionStrategy {
    std::string strategy_name;
    ConflictType applicable_type;
    bool automatic_resolution = false;
    std::function<bool(const Conflict&)> resolver;
    std::string description;
    int priority = 0;
};

class ConflictDetector {
public:
    ConflictDetector();
    explicit ConflictDetector(const ConflictDetectionConfig& config);
    ~ConflictDetector();

    // Configuration
    void set_configuration(const ConflictDetectionConfig& config);
    ConflictDetectionConfig get_configuration() const;
    void set_integration_root(const std::filesystem::path& root_path);

    // Core conflict detection
    std::vector<Conflict> detect_all_conflicts();
    std::vector<Conflict> detect_conflicts_for_library(const std::string& library_name);
    std::vector<Conflict> detect_version_conflicts(const std::vector<std::string>& libraries);
    std::vector<Conflict> detect_license_conflicts(const std::vector<std::string>& libraries);
    std::vector<Conflict> detect_symbol_conflicts(const std::vector<std::string>& libraries);
    std::vector<Conflict> detect_file_conflicts(const std::vector<std::string>& libraries);
    std::vector<Conflict> detect_build_conflicts(const std::vector<std::string>& libraries);

    // Dependency analysis
    std::vector<Conflict> detect_dependency_cycles();
    std::vector<Conflict> detect_missing_dependencies(const std::vector<std::string>& libraries);
    std::vector<Conflict> detect_api_incompatibilities(const std::vector<std::string>& libraries);
    bool validate_dependency_graph(const std::vector<std::string>& libraries);
    std::map<std::string, DependencyNode> build_dependency_graph(const std::vector<std::string>& libraries);

    // C++ specific conflict detection
    std::vector<Conflict> detect_namespace_conflicts(const std::vector<std::string>& libraries);
    std::vector<Conflict> detect_include_conflicts(const std::vector<std::string>& libraries);
    std::vector<Conflict> detect_template_instantiation_conflicts(const std::vector<std::string>& libraries);

    // Advanced analysis
    std::vector<Conflict> detect_resource_conflicts(const std::vector<std::string>& libraries);
    std::vector<Conflict> detect_configuration_conflicts(const std::vector<std::string>& libraries);
    std::vector<Conflict> detect_performance_conflicts(const std::vector<std::string>& libraries);

    // Conflict resolution
    std::vector<ConflictResolutionStrategy> get_resolution_strategies() const;
    bool resolve_conflict(const Conflict& conflict, const std::string& strategy_name);
    std::vector<Conflict> auto_resolve_conflicts(const std::vector<Conflict>& conflicts);
    bool can_auto_resolve(const Conflict& conflict) const;
    std::string suggest_resolution(const Conflict& conflict) const;

    // Prevention and monitoring
    bool register_prevention_rule(const std::string& rule_name, const std::string& rule_definition);
    std::vector<Conflict> analyze_integration_risks(const std::vector<std::string>& libraries);
    bool check_integration_feasibility(const std::vector<std::string>& libraries);
    std::vector<std::string> get_integration_recommendations(const std::vector<std::string>& libraries);

    // Symbol and API analysis
    std::map<std::string, std::set<std::string>> extract_symbols(const std::string& library_path);
    std::map<std::string, std::set<std::string>> extract_public_apis(const std::string& library_path);
    bool check_api_compatibility(const std::string& library1, const std::string& library2);

    // Build system analysis
    std::vector<Conflict> analyze_cmake_conflicts(const std::vector<std::string>& libraries);
    std::vector<Conflict> analyze_compiler_flag_conflicts(const std::vector<std::string>& libraries);
    std::vector<Conflict> analyze_linker_conflicts(const std::vector<std::string>& libraries);

    // Reporting and visualization
    std::string generate_conflict_report(const std::vector<Conflict>& conflicts) const;
    std::string generate_dependency_graph_visualization() const;
    std::vector<Conflict> get_conflicts_by_severity(ConflictSeverity severity) const;
    std::map<ConflictType, int> get_conflict_statistics() const;

    // Integration with other systems
    void register_conflict_callback(std::function<void(const Conflict&)> callback);
    bool export_conflict_data(const std::filesystem::path& output_path) const;
    bool import_conflict_data(const std::filesystem::path& input_path);

    // Caching and performance
    void clear_cache();
    bool is_cache_enabled() const;
    void set_cache_enabled(bool enabled);
    std::chrono::milliseconds get_last_scan_duration() const;

private:
    struct Impl;
    std::unique_ptr<Impl> p_impl;

    // Helper methods
    std::vector<std::string> scan_library_files(const std::string& library_path);
    std::set<std::string> extract_symbols_from_file(const std::filesystem::path& file_path);
    std::string parse_library_version(const std::filesystem::path& library_path) const;
    std::string parse_library_license(const std::filesystem::path& library_path) const;
    bool check_license_compatibility(const std::string& license1, const std::string& license2) const;
    std::vector<std::string> find_circular_dependencies(const std::map<std::string, std::vector<std::string>>& dependency_graph);
    std::string generate_conflict_id(const Conflict& conflict) const;
    ConflictSeverity determine_conflict_severity(const Conflict& conflict) const;
};

// Utility functions
std::string conflict_type_to_string(ConflictType type);
std::string conflict_severity_to_string(ConflictSeverity severity);
ConflictType string_to_conflict_type(const std::string& type_str);
ConflictSeverity string_to_conflict_severity(const std::string& severity_str);

// Global conflict detector instance
ConflictDetector& get_conflict_detector();

} // namespace conflict_detection
} // namespace integration