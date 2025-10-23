// T018: Dependency Conflict Detection Implementation
// Detects and prevents integration of conflicting third-party library versions

#include "conflict_detector.h"
#include <fstream>
#include <sstream>

namespace integration {
namespace conflict {

ConflictDetector::ConflictDetector() {
    setup_conflict_rules();
}

std::vector<ConflictInfo> ConflictDetector::detect_conflicts(const std::vector<LibraryInfo>& libraries) {
    std::vector<ConflictInfo> conflicts;

    // Check for version conflicts
    auto version_conflicts = detect_version_conflicts(libraries);
    conflicts.insert(conflicts.end(), version_conflicts.begin(), version_conflicts.end());

    // Check for symbol conflicts
    auto symbol_conflicts = detect_symbol_conflicts(libraries);
    conflicts.insert(conflicts.end(), symbol_conflicts.begin(), symbol_conflicts.end());

    // Check for license conflicts
    auto license_conflicts = detect_license_conflicts(libraries);
    conflicts.insert(conflicts.end(), license_conflicts.begin(), license_conflicts.end());

    // Check for dependency conflicts
    auto dependency_conflicts = detect_dependency_conflicts(libraries);
    conflicts.insert(conflicts.end(), dependency_conflicts.begin(), dependency_conflicts.end());

    return conflicts;
}

bool ConflictDetector::can_integrate(const LibraryInfo& library,
                                     const std::vector<LibraryInfo>& existing_libraries) {
    std::vector<LibraryInfo> all_libraries = existing_libraries;
    all_libraries.push_back(library);

    auto conflicts = detect_conflicts(all_libraries);
    return conflicts.empty();
}

std::vector<ConflictInfo> ConflictDetector::detect_version_conflicts(const std::vector<LibraryInfo>& libraries) {
    std::vector<ConflictInfo> conflicts;
    std::map<std::string, std::vector<LibraryInfo>> by_name;

    // Group libraries by name
    for (const auto& lib : libraries) {
        by_name[lib.name].push_back(lib);
    }

    // Check for version conflicts within same library name
    for (const auto& pair : by_name) {
        const auto& lib_name = pair.first;
        const auto& versions = pair.second;

        if (versions.size() > 1) {
            // Sort by version (simplified)
            auto sorted_versions = versions;
            std::sort(sorted_versions.begin(), sorted_versions.end(),
                [](const LibraryInfo& a, const LibraryInfo& b) {
                    return a.version < b.version;
                });

            // Check consecutive versions for compatibility
            for (size_t i = 0; i < sorted_versions.size() - 1; ++i) {
                if (!are_versions_compatible(sorted_versions[i].version,
                                               sorted_versions[i + 1].version)) {
                    ConflictInfo conflict;
                    conflict.type = ConflictType::VERSION;
                    conflict.library1 = sorted_versions[i];
                    conflict.library2 = sorted_versions[i + 1];
                    conflict.severity = ConflictSeverity::HIGH;
                    conflict.description = "Version incompatibility between " +
                                        sorted_versions[i].version + " and " +
                                        sorted_versions[i + 1].version;
                    conflict.resolution_suggestion = "Choose one version or update dependency requirements";
                    conflicts.push_back(conflict);
                }
            }
        }
    }

    return conflicts;
}

std::vector<ConflictInfo> ConflictDetector::detect_symbol_conflicts(const std::vector<LibraryInfo>& libraries) {
    std::vector<ConflictInfo> conflicts;
    std::map<std::string, std::vector<std::string>> symbol_map;

    // Build symbol map (simplified)
    for (const auto& lib : libraries) {
        std::vector<std::string> symbols = get_exported_symbols(lib);
        for (const auto& symbol : symbols) {
            symbol_map[symbol].push_back(lib.name);
        }
    }

    // Find symbols used by multiple libraries
    for (const auto& pair : symbol_map) {
        if (pair.second.size() > 1) {
            ConflictInfo conflict;
            conflict.type = ConflictType::SYMBOL;
            conflict.severity = ConflictSeverity::HIGH;
            conflict.symbol_name = pair.first;
            conflict.conflicting_libraries = pair.second;
            conflict.description = "Symbol '" + pair.first + "' is exported by multiple libraries";
            conflict.resolution_suggestion = "Use namespace adaptation or symbol renaming";

            if (!pair.second.empty()) {
                conflict.library1.name = pair.second[0];
            }
            if (pair.second.size() > 1) {
                conflict.library2.name = pair.second[1];
            }

            conflicts.push_back(conflict);
        }
    }

    return conflicts;
}

std::vector<ConflictInfo> ConflictDetector::detect_license_conflicts(const std::vector<LibraryInfo>& libraries) {
    std::vector<ConflictInfo> conflicts;

    // Check for incompatible licenses
    for (size_t i = 0; i < libraries.size(); ++i) {
        for (size_t j = i + 1; j < libraries.size(); ++j) {
            if (are_licenses_incompatible(libraries[i].license, libraries[j].license)) {
                ConflictInfo conflict;
                conflict.type = ConflictType::LICENSE;
                conflict.library1 = libraries[i];
                conflict.library2 = libraries[j];
                conflict.severity = ConflictSeverity::MEDIUM;
                conflict.description = "License incompatibility between " +
                                    libraries[i].license + " and " + libraries[j].license;
                conflict.resolution_suggestion = "Verify license compatibility or use alternative library";

                conflicts.push_back(conflict);
            }
        }
    }

    return conflicts;
}

std::vector<ConflictInfo> ConflictDetector::detect_dependency_conflicts(const std::vector<LibraryInfo>& libraries) {
    std::vector<ConflictInfo> conflicts;
    std::map<std::string, std::vector<std::string>> dependency_map;

    // Build dependency map
    for (const auto& lib : libraries) {
        for (const auto& dep : lib.dependencies) {
            dependency_map[dep].push_back(lib.name);
        }
    }

    // Find dependencies with conflicting version requirements
    for (const auto& pair : dependency_map) {
        const auto& dep_name = pair.first;
        const auto& dependents = pair.second;

        if (dependents.size() > 1) {
            // Check if dependents require different versions of the same dependency
            std::map<std::string, std::vector<std::string>> version_requirements;
            for (const auto& dependent : dependents) {
                std::string required_version = get_dependency_version_requirement(
                    dependent, dep_name);
                if (!required_version.empty()) {
                    version_requirements[required_version].push_back(dependent);
                }
            }

            if (version_requirements.size() > 1) {
                ConflictInfo conflict;
                conflict.type = ConflictType::DEPENDENCY;
                conflict.severity = ConflictSeverity::HIGH;
                conflict.dependency_name = dep_name;
                conflict.conflicting_libraries = dependents;
                conflict.description = "Dependency '" + dep_name + "' has conflicting version requirements";
                conflict.resolution_suggestion = "Update dependency versions to be compatible";

                conflicts.push_back(conflict);
            }
        }
    }

    return conflicts;
}

std::vector<std::string> ConflictDetector::get_exported_symbols(const LibraryInfo& library) const {
    std::vector<std::string> symbols;

    // Simplified implementation - in production, would parse library symbols
    std::string cmd = "nm -D " + library.integrated_path + "/*.a 2>/dev/null | grep ' T ' | awk '{print $3}'";
    std::string result = execute_command(cmd);

    std::stringstream ss(result);
    std::string symbol;
    while (std::getline(ss, symbol)) {
        if (!symbol.empty()) {
            symbols.push_back(symbol);
        }
    }

    return symbols;
}

std::string ConflictDetector::get_dependency_version_requirement(const std::string& library,
                                                               const std::string& dependency) const {
    // Simplified implementation - would parse build configuration
    return "";
}

bool ConflictDetector::are_versions_compatible(const std::string& version1,
                                                const std::string& version2) {
    // Simplified semantic version comparison
    if (version1 == version2) return true;

    // Major version compatibility check
    auto v1_parts = split_version(version1);
    auto v2_parts = split_version(version2);

    if (v1_parts.empty() || v2_parts.empty()) return true;

    if (v1_parts[0] != v2_parts[0]) {
        return false; // Different major versions are incompatible
    }

    return true; // Same major version is considered compatible
}

bool ConflictDetector::are_licenses_incompatible(const std::string& license1,
                                               const std::string& license2) const {
    // Define incompatible license combinations
    std::set<std::string> gpl_licenses = {"GPL-2.0", "GPL-3.0"};
    std::set<std::string> proprietary_licenses = {"Commercial", "Proprietary"};

    bool is_gpl1 = gpl_licenses.find(license1) != gpl_licenses.end();
    bool is_gpl2 = gpl_licenses.find(license2) != gpl_licenses.end();
    bool is_prop1 = proprietary_licenses.find(license1) != proprietary_licenses.end();
    bool is_prop2 = proprietary_licenses.find(license2) != proprietary_licenses.end();

    // GPL is incompatible with proprietary licenses
    return (is_gpl1 && is_prop2) || (is_gpl2 && is_prop1);
}

std::vector<std::string> ConflictDetector::split_version(const std::string& version) const {
    std::vector<std::string> parts;
    std::stringstream ss(version);
    std::string part;

    while (std::getline(ss, part, '.')) {
        if (!part.empty()) {
            parts.push_back(part);
        }
    }

    return parts;
}

std::string ConflictDetector::execute_command(const std::string& cmd) const {
    std::array<char, 128> buffer;
    std::string result;

    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) {
        return "";
    }

    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }

    return result;
}

void ConflictDetector::setup_conflict_rules() {
    // Define conflict resolution rules
    conflict_rules_[ConflictType::VERSION] = {
        {"resolution_strategy", "version_selection"},
        {"automatic_resolution", false},
        {"user_intervention_required", true}
    };

    conflict_rules_[ConflictType::SYMBOL] = {
        {"resolution_strategy", "namespace_adaptation"},
        {"automatic_resolution", true},
        {"user_intervention_required", false}
    };

    conflict_rules_[ConflictType::LICENSE] = {
        {"resolution_strategy", "legal_review"},
        {"automatic_resolution", false},
        {"user_intervention_required", true}
    };

    conflict_rules_[ConflictType::DEPENDENCY] = {
        {"resolution_strategy", "version_negotiation"},
        {"automatic_resolution", false},
        {"user_intervention_required", true}
    };
}

std::string ConflictInfo::to_string() const {
    std::stringstream ss;
    ss << "[" << severity_to_string(severity) << "] "
       << type_to_string(type) << " Conflict: " << description << "\n"
       << "Resolution: " << resolution_suggestion;

    if (!conflicting_libraries.empty()) {
        ss << "\nConflicting libraries: ";
        for (size_t i = 0; i < conflicting_libraries.size(); ++i) {
            if (i > 0) ss << ", ";
            ss << conflicting_libraries[i];
        }
    }

    return ss.str();
}

std::string ConflictInfo::severity_to_string(ConflictSeverity severity) const {
    switch (severity) {
        case ConflictSeverity::LOW: return "LOW";
        case ConflictSeverity::MEDIUM: return "MEDIUM";
        case ConflictSeverity::HIGH: return "HIGH";
        default: return "UNKNOWN";
    }
}

std::string ConflictInfo::type_to_string(ConflictType type) const {
    switch (type) {
        case ConflictType::VERSION: return "VERSION";
        case ConflictType::SYMBOL: return "SYMBOL";
        case ConflictType::LICENSE: return "LICENSE";
        case ConflictType::DEPENDENCY: return "DEPENDENCY";
        default: return "UNKNOWN";
    }
}

// Global instance
static std::unique_ptr<ConflictDetector> g_conflict_detector;

ConflictDetector& get_conflict_detector() {
    if (!g_conflict_detector) {
        g_conflict_detector = std::make_unique<ConflictDetector>();
    }
    return *g_conflict_detector;
}

} // namespace conflict
} // namespace integration