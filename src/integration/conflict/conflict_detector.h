// T018: Dependency Conflict Detection Implementation
// Detects and prevents integration of conflicting third-party library versions

#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>

enum class ConflictSeverity {
    LOW,
    MEDIUM,
    HIGH
};

enum class ConflictType {
    VERSION,
    SYMBOL,
    LICENSE,
    DEPENDENCY
};

struct LibraryInfo {
    std::string name;
    std::string version;
    std::string license;
    std::string integrated_path;
    std::vector<std::string> dependencies;
    std::map<std::string, std::string> metadata;
};

struct ConflictInfo {
    ConflictType type;
    ConflictSeverity severity;
    std::string description;
    std::string resolution_suggestion;
    std::string symbol_name;
    std::string dependency_name;
    LibraryInfo library1;
    LibraryInfo library2;
    std::vector<std::string> conflicting_libraries;

    std::string to_string() const;

private:
    std::string severity_to_string(ConflictSeverity severity) const;
    std::string type_to_string(ConflictType type) const;
};

namespace integration {
namespace conflict {

class ConflictDetector {
public:
    ConflictDetector();

    // Main detection methods
    std::vector<ConflictInfo> detect_conflicts(const std::vector<LibraryInfo>& libraries);
    bool can_integrate(const LibraryInfo& library,
                         const std::vector<LibraryInfo>& existing_libraries);

    // Configuration
    void set_conflict_rule(ConflictType type, const nlohmann::json& rule) {
        conflict_rules_[type] = rule;
    }

private:
    // Specific conflict detection methods
    std::vector<ConflictInfo> detect_version_conflicts(const std::vector<LibraryInfo>& libraries);
    std::vector<ConflictInfo> detect_symbol_conflicts(const std::vector<LibraryInfo>& libraries);
    std::vector<ConflictInfo> detect_license_conflicts(const std::vector<LibraryInfo>& libraries);
    std::vector<ConflictInfo> detect_dependency_conflicts(const std::vector<LibraryInfo>& libraries);

    // Helper methods
    std::vector<std::string> get_exported_symbols(const LibraryInfo& library) const;
    std::string get_dependency_version_requirement(const std::string& library,
                                                     const std::string& dependency) const;
    bool are_versions_compatible(const std::string& version1, const std::string& version2) const;
    bool are_licenses_incompatible(const std::string& license1, const std::string& license2) const;

    // Utility methods
    std::vector<std::string> split_version(const std::string& version) const;
    std::string execute_command(const std::string& cmd) const;

    // Conflict resolution rules
    void setup_conflict_rules();
    std::map<ConflictType, nlohmann::json> conflict_rules_;
};

// Global accessor
ConflictDetector& get_conflict_detector();

} // namespace conflict
} // namespace integration