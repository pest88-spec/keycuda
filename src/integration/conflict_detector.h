/**
 * Puzzle71Solver - Dependency Conflict Detection System
 *
 * Provides comprehensive conflict detection and resolution for integration failures,
 * including dependency conflicts, version incompatibilities, resource conflicts,
 * and build system conflicts during third-party library integration.
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

namespace integration {
namespace conflict {

// Conflict severity levels
enum class ConflictSeverity {
    INFO,       // Informational, may impact future operations
    WARNING,    // Warning, should be addressed but not blocking
    ERROR,      // Error, prevents successful integration
    CRITICAL    // Critical, requires immediate attention
};

// Conflict types
enum class ConflictType {
    VERSION_CONFLICT,        // Incompatible library versions
    DEPENDENCY_CONFLICT,     // Circular or missing dependencies
    SYMBOL_CONFLICT,         // Duplicate symbols or definitions
    HEADER_CONFLICT,         // Conflicting header files
    BUILD_CONFLICT,          // CMake or build system conflicts
    RESOURCE_CONFLICT,       // File or resource conflicts
    LICENSE_CONFLICT,        // License compatibility issues
    CONFIGURATION_CONFLICT,  // Configuration conflicts
    PLATFORM_CONFLICT,       // Platform-specific conflicts
    COMPILER_CONFLICT,       // Compiler-specific conflicts
    NAMESPACE_CONFLICT,      // Namespace or naming conflicts
    UNKNOWN_CONFLICT         // Unclassified conflict
};

// Conflict resolution strategies
enum class ResolutionStrategy {
    IGNORE,           // Ignore the conflict (not recommended for production)
    PREFER_LOCAL,     // Prefer local/integrated version
    PREFER_SYSTEM,    // Prefer system version
    MERGE,           // Attempt to merge conflicting versions
    ISOLATE,         // Isolate conflicting libraries
    REPLACE,         // Replace one library with another
    REMOVE,          // Remove conflicting component
    CUSTOM           // Use custom resolution logic
};

// Individual conflict information
struct ConflictInfo {
    std::string conflict_id;
    ConflictType type;
    ConflictSeverity severity;
    std::string description;
    std::vector<std::string> affected_libraries;
    std::vector<std::string> affected_files;
    std::string details;
    std::chrono::system_clock::time_point detected_at;
    std::optional<ResolutionStrategy> suggested_resolution;
    bool auto_resolvable = false;
    bool blocking = true;
    std::map<std::string, std::string> metadata;
};

// Conflict resolution result
struct ResolutionResult {
    bool successful = false;
    ResolutionStrategy strategy_used;
    std::string resolution_description;
    std::vector<std::string> applied_changes;
    std::vector<std::string> remaining_issues;
    std::chrono::milliseconds resolution_time{0};
    std::map<std::string, std::string> resolution_metadata;
};

// Conflict detection session
struct ConflictDetectionSession {
    std::string session_id;
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point end_time;
    std::vector<std::string> scanned_libraries;
    std::vector<ConflictInfo> detected_conflicts;
    size_t total_conflicts = 0;
    size_t resolved_conflicts = 0;
    size_t blocked_conflicts = 0;
    std::chrono::milliseconds detection_time{0};
    std::map<std::string, std::string> session_metadata;
};

// Library dependency information
struct LibraryDependency {
    std::string library_name;
    std::string version;
    std::vector<std::string> required_libraries;
    std::vector<std::string> optional_libraries;
    std::vector<std::string> conflicting_libraries;
    std::vector<std::string> compatible_versions;
    std::map<std::string, std::string> version_constraints;
    std::map<std::string, std::string> metadata;
};

// Symbol information for conflict detection
struct SymbolInfo {
    std::string symbol_name;
    std::string library_name;
    std::string source_file;
    std::string symbol_type;  // function, variable, class, etc.
    bool is_exported = false;
    std::map<std::string, std::string> metadata;
};

// Configuration conflict information
struct ConfigConflict {
    std::string config_key;
    std::string local_value;
    std::string system_value;
    std::string library_name;
    std::string description;
    ConflictSeverity severity;
    std::optional<std::string> recommended_value;
};

class ConflictDetector {
public:
    ConflictDetector();
    explicit ConflictDetector(const std::filesystem::path& integration_root);
    ~ConflictDetector();

    // Configuration
    void set_integration_root(const std::filesystem::path& path);
    void set_strict_mode(bool enabled);
    void set_auto_resolution_enabled(bool enabled);
    void set_conflict_thresholds(ConflictSeverity warning_threshold, ConflictSeverity error_threshold);

    // Session management
    std::string start_detection_session();
    ConflictDetectionSession end_detection_session(const std::string& session_id);
    ConflictDetectionSession get_session_info(const std::string& session_id);
    std::vector<ConflictDetectionSession> get_active_sessions();

    // Main detection functions
    ConflictDetectionSession detect_all_conflicts(const std::vector<std::string>& libraries = {});
    std::vector<ConflictInfo> detect_library_conflicts(const std::string& library_name);
    std::vector<ConflictInfo> detect_version_conflicts(const std::string& library_name);
    std::vector<ConflictInfo> detect_dependency_conflicts(const std::string& library_name);
    std::vector<ConflictInfo> detect_symbol_conflicts(const std::string& library_name);
    std::vector<ConflictInfo> detect_header_conflicts(const std::string& library_name);
    std::vector<ConflictInfo> detect_build_conflicts(const std::string& library_name);
    std::vector<ConflictInfo> detect_resource_conflicts(const std::string& library_name);
    std::vector<ConflictInfo> detect_license_conflicts(const std::string& library_name);
    std::vector<ConflictInfo> detect_configuration_conflicts(const std::string& library_name);

    // Resolution functions
    ResolutionResult resolve_conflict(const std::string& conflict_id, ResolutionStrategy strategy);
    ResolutionResult auto_resolve_conflicts(const std::vector<std::string>& conflict_ids);
    std::vector<ResolutionResult> resolve_all_conflicts(const std::string& session_id);
    bool can_auto_resolve(const ConflictInfo& conflict);

    // Analysis and validation
    bool validate_resolution(const ResolutionResult& result);
    std::vector<std::string> get_unresolved_dependencies(const std::string& library_name);
    bool verify_integration_compatibility(const std::vector<std::string>& libraries);
    std::vector<std::string> suggest_dependency_order(const std::vector<std::string>& libraries);

    // Library information
    std::vector<std::string> get_available_libraries();
    LibraryDependency get_library_dependencies(const std::string& library_name);
    std::vector<SymbolInfo> get_library_symbols(const std::string& library_name);
    std::map<std::string, std::string> get_library_metadata(const std::string& library_name);

    // Conflict history and tracking
    std::vector<ConflictInfo> get_conflict_history(const std::string& library_name = "");
    void mark_conflict_resolved(const std::string& conflict_id, const ResolutionResult& result);
    std::vector<ConflictInfo> get_recurring_conflicts();
    std::map<ConflictType, size_t> get_conflict_statistics();

    // Export and reporting
    std::string export_conflict_report_json(const std::string& session_id);
    std::string export_conflict_report_csv(const std::string& session_id);
    bool import_conflict_resolution_rules(const std::filesystem::path& rules_file);
    bool export_conflict_resolution_rules(const std::filesystem::path& rules_file);

    // Advanced detection
    std::vector<ConflictInfo> detect_cross_platform_conflicts(const std::string& library_name);
    std::vector<ConflictInfo> detect_compiler_specific_conflicts(const std::string& library_name);
    std::vector<ConflictInfo> detect_runtime_conflicts(const std::string& library_name);

    // Integration with other systems
    void set_metrics_callback(std::function<void(const std::string&, double)> callback);
    void set_logging_callback(std::function<void(const std::string&, const std::string&)> callback);

private:
    struct Impl;
    std::unique_ptr<Impl> p_impl;

    // Internal helper methods
    std::string generate_conflict_id(ConflictType type, const std::vector<std::string>& affected_libraries);
    ConflictSeverity assess_conflict_severity(ConflictType type, const std::string& details);
    std::optional<ResolutionStrategy> suggest_resolution_strategy(const ConflictInfo& conflict);
    bool validate_resolution_strategy(ConflictType type, ResolutionStrategy strategy);

    // Detection helpers
    std::vector<std::string> find_symbol_duplicates(const std::string& library_name);
    std::vector<std::string> find_header_duplicates(const std::string& library_name);
    std::vector<std::string> find_file_duplicates(const std::string& library_name);
    bool check_version_compatibility(const std::string& lib1, const std::string& version1,
                                   const std::string& lib2, const std::string& version2);
    bool check_circular_dependencies(const std::vector<std::string>& libraries);

    // Resolution helpers
    ResolutionResult apply_resolution_strategy(const ConflictInfo& conflict, ResolutionStrategy strategy);
    bool apply_prefer_local_resolution(const ConflictInfo& conflict);
    bool apply_prefer_system_resolution(const ConflictInfo& conflict);
    bool apply_merge_resolution(const ConflictInfo& conflict);
    bool apply_isolate_resolution(const ConflictInfo& conflict);
    bool apply_replace_resolution(const ConflictInfo& conflict);
    bool apply_remove_resolution(const ConflictInfo& conflict);

    // Utility methods
    std::string conflict_type_to_string(ConflictType type);
    std::string conflict_severity_to_string(ConflictSeverity severity);
    std::string resolution_strategy_to_string(ResolutionStrategy strategy);
    ConflictType string_to_conflict_type(const std::string& type_str);
    ConflictSeverity string_to_conflict_severity(const std::string& severity_str);
    ResolutionStrategy string_to_resolution_strategy(const std::string& strategy_str);

    // File system operations
    std::vector<std::filesystem::path> find_library_files(const std::string& library_name);
    std::map<std::string, std::string> parse_cmake_variables(const std::filesystem::path& cmake_file);
    std::vector<std::string> extract_symbols_from_file(const std::filesystem::path& file_path);

    // Cache and performance optimization
    void clear_cache();
    void update_library_cache(const std::string& library_name);
    bool is_cache_valid(const std::string& library_name);
    std::chrono::seconds get_cache_duration() const { return std::chrono::minutes(30); }
};

// Utility functions
std::vector<std::string> parse_library_dependencies(const std::filesystem::path& cmake_file);
bool validate_library_compatibility(const std::string& lib1, const std::string& lib2);
std::string format_conflict_description(const ConflictInfo& conflict);
std::string generate_conflict_id(ConflictType type, const std::vector<std::string>& libraries);

// Global detector instance
ConflictDetector& get_conflict_detector();

} // namespace conflict
} // namespace integration