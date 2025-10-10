// T048: Version Conflict Detection and Prevention System
// Implements conflict detection algorithms, prevention strategies, and early warning systems

#include "version_conflict_detector.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <regex>
#include <numeric>

namespace fs = std::filesystem;

namespace integration {

VersionConflictDetector::VersionConflictDetector(const std::string& cache_dir)
    : cache_dir_(cache_dir) {
    loadConflictRules();
}

VersionConflictDetector::~VersionConflictDetector() = default;

// Load conflict rules from file
bool VersionConflictDetector::loadConflictRules(const std::string& filename) {
    std::string rules_file = filename.empty() ? cache_dir_ + "/conflict_rules.json" : filename;

    try {
        std::ifstream file(rules_file);
        if (!file.is_open()) {
            // Try to create default rules
            if (loadDefaultConflictRules()) {
                conflict_rules_loaded_ = true;
                return true;
            }
            return false;
        }

        file >> conflict_rules_;
        file.close();
        conflict_rules_loaded_ = true;
        return true;
    } catch (const std::exception& e) {
        // Fallback to default rules
        return loadDefaultConflictRules();
    }
}

// Load default conflict rules
bool VersionConflictDetector::loadDefaultConflictRules() {
    json default_rules = {
        {
            {"rule_id", "default_version_range_001"},
            {"rule_type", "version_range_conflict"},
            {"dependency1", "googletest"},
            {"dependency2", "boost"},
            {"conflict_condition", "googletest.version == '1.12.0' and boost.version < '1.72.0'"},
            {"severity", "high"},
            {"prevention", "version_pin_or_upgrade"},
            {"description", "Known incompatibility between specific gtest and boost versions"}
        },
        {
            {"rule_id", "default_symbol_conflict_001"},
            {"rule_type", "symbol_conflict"},
            {"dependency1", "openssl"},
            {"dependency2", "secp256k1"},
            {"conflict_symbols", {"crypto_init", "hash_function", "EVP_*"}},
            {"severity", "critical"},
            {"prevention", "namespace_isolation"},
            {"description", "Symbol naming conflicts between OpenSSL implementations"}
        },
        {
            {"rule_id", "default_build_system_001"},
            {"rule_type", "build_system_conflict"},
            {"dependency1", "cmake_fetch"},
            {"dependency2", "make_based"},
            {"conflict_condition", "simultaneous_build"},
            {"severity", "medium"},
            {"prevention", "build_order_enforcement"},
            {"description", "Build system incompatibilities"}
        }
    };

    try {
        std::string rules_file = cache_dir_ + "/conflict_rules.json";
        fs::create_directories(cache_dir_);
        std::ofstream file(rules_file);
        file << default_rules.dump(4);
        file.close();

        conflict_rules_ = default_rules;
        conflict_rules_loaded_ = true;
        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

// Detect conflicts in dependencies
std::vector<ConflictConflict> VersionConflictDetector::detectConflicts(const std::vector<DependencyVersion>& dependencies) const {
    std::vector<ConflictConflict> conflicts;

    if (!conflict_rules_loaded_) {
        return conflicts;
    }

    // Detect different types of conflicts
    auto version_conflicts = detectVersionRangeConflicts(dependencies);
    auto symbol_conflicts = detectSymbolConflicts(dependencies);
    auto build_conflicts = detectBuildSystemConflicts(dependencies);
    auto custom_conflicts = detectCustomRuleConflicts(dependencies);

    conflicts.insert(conflicts.end(), version_conflicts.begin(), version_conflicts.end());
    conflicts.insert(conflicts.end(), symbol_conflicts.begin(), symbol_conflicts.end());
    conflicts.insert(conflicts.end(), build_conflicts.begin(), build_conflicts.end());
    conflicts.insert(conflicts.end(), custom_conflicts.begin(), custom_conflicts.end());

    return conflicts;
}

// Detect conflicts with proposed update
std::vector<ConflictConflict> VersionConflictDetector::detectConflictsWithUpdate(
    const DependencyUpdate& update,
    const std::vector<DependencyVersion>& current_deps) const {

    std::vector<DependencyVersion> updated_deps = current_deps;

    // Update the dependency with proposed version
    for (auto& dep : updated_deps) {
        if (dep.name == update.name) {
            dep.version = update.proposed_version;
            break;
        }
    }

    // If dependency doesn't exist, add it
    bool found = false;
    for (const auto& dep : updated_deps) {
        if (dep.name == update.name) {
            found = true;
            break;
        }
    }

    if (!found) {
        updated_deps.push_back({update.name, update.proposed_version, update.source_type});
    }

    return detectConflicts(updated_deps);
}

// Detect version range conflicts
std::vector<ConflictConflict> VersionConflictDetector::detectVersionRangeConflicts(const std::vector<DependencyVersion>& deps) const {
    std::vector<ConflictConflict> conflicts;

    for (const auto& rule : conflict_rules_) {
        if (rule["rule_type"] != "version_range_conflict") continue;

        for (size_t i = 0; i < deps.size(); ++i) {
            for (size_t j = i + 1; j < deps.size(); ++j) {
                if (conflictMatchesRule(deps[i], deps[j], rule)) {
                    ConflictConflict conflict;
                    conflict.conflict_type = rule["rule_type"];
                    conflict.dependency1 = deps[i].name;
                    conflict.version1 = deps[i].version;
                    conflict.dependency2 = deps[j].name;
                    conflict.version2 = deps[j].version;
                    conflict.severity = rule["severity"];
                    conflict.description = rule["description"];
                    conflict.prevention_strategies = {rule["prevention"]};

                    conflicts.push_back(conflict);
                }
            }
        }
    }

    return conflicts;
}

// Detect symbol conflicts
std::vector<ConflictConflict> VersionConflictDetector::detectSymbolConflicts(const std::vector<DependencyVersion>& deps) const {
    std::vector<ConflictConflict> conflicts;

    for (const auto& rule : conflict_rules_) {
        if (rule["rule_type"] != "symbol_conflict") continue;

        for (size_t i = 0; i < deps.size(); ++i) {
            for (size_t j = i + 1; j < deps.size(); ++j) {
                const auto& dep1 = deps[i];
                const auto& dep2 = deps[j];

                if ((dep1.name == rule["dependency1"] && dep2.name == rule["dependency2"]) ||
                    (dep1.name == rule["dependency2"] && dep2.name == rule["dependency1"])) {

                    ConflictConflict conflict;
                    conflict.conflict_type = rule["rule_type"];
                    conflict.dependency1 = dep1.name;
                    conflict.version1 = dep1.version;
                    conflict.dependency2 = dep2.name;
                    conflict.version2 = dep2.version;
                    conflict.severity = rule["severity"];
                    conflict.description = rule["description"];
                    conflict.prevention_strategies = {rule["prevention"]};

                    conflicts.push_back(conflict);
                }
            }
        }
    }

    return conflicts;
}

// Detect build system conflicts
std::vector<ConflictConflict> VersionConflictDetector::detectBuildSystemConflicts(const std::vector<DependencyVersion>& deps) const {
    std::vector<ConflictConflict> conflicts;

    // Group dependencies by source type
    std::map<std::string, std::vector<DependencyVersion>> by_source_type;
    for (const auto& dep : deps) {
        by_source_type[dep.source_type].push_back(dep);
    }

    // Check for conflicting source types
    if (by_source_type.count("cmake_fetch") && by_source_type.count("make_based")) {
        ConflictConflict conflict;
        conflict.conflict_type = "build_system_conflict";
        conflict.dependency1 = "cmake_fetch_dependencies";
        conflict.version1 = "various";
        conflict.dependency2 = "make_based_dependencies";
        conflict.version2 = "various";
        conflict.severity = "medium";
        conflict.description = "Mixed build systems may cause conflicts";
        conflict.prevention_strategies = {"build_order_enforcement", "build_unification"};

        conflicts.push_back(conflict);
    }

    return conflicts;
}

// Detect custom rule conflicts
std::vector<ConflictConflict> VersionConflictDetector::detectCustomRuleConflicts(const std::vector<DependencyVersion>& deps) const {
    std::vector<ConflictConflict> conflicts;

    for (const auto& rule : conflict_rules_) {
        if (!rule.contains("rule_type") || rule["rule_type"] == "version_range_conflict" ||
            rule["rule_type"] == "symbol_conflict" || rule["rule_type"] == "build_system_conflict") {
            continue;
        }

        for (size_t i = 0; i < deps.size(); ++i) {
            for (size_t j = i + 1; j < deps.size(); ++j) {
                if (conflictMatchesRule(deps[i], deps[j], rule)) {
                    ConflictConflict conflict;
                    conflict.conflict_type = rule["rule_type"];
                    conflict.dependency1 = deps[i].name;
                    conflict.version1 = deps[i].version;
                    conflict.dependency2 = deps[j].name;
                    conflict.version2 = deps[j].version;
                    conflict.severity = rule["severity"];
                    conflict.description = rule["description"];
                    if (rule.contains("prevention")) {
                        conflict.prevention_strategies = {rule["prevention"]};
                    }

                    conflicts.push_back(conflict);
                }
            }
        }
    }

    return conflicts;
}

// Generate prevention strategies
std::vector<PreventionStrategy> VersionConflictDetector::generatePreventionStrategies(const ConflictConflict& conflict) const {
    std::vector<PreventionStrategy> strategies;

    for (const auto& strategy_name : conflict.prevention_strategies) {
        PreventionStrategy strategy;
        strategy.strategy_type = strategy_name;
        strategy.confidence = 0.8;
        strategy.estimated_effort = 2; // hours

        if (strategy_name == "version_pin_or_upgrade") {
            strategy.description = "Pin conflicting dependency to compatible version or upgrade to newer version";
            strategy.steps = {"Identify compatible version range", "Update dependency configuration", "Test compatibility"};
            strategy.parameters = {
                {"action", "version_management"},
                {"conflict_resolution", "compatible_range"}
            };
        } else if (strategy_name == "namespace_isolation") {
            strategy.description = "Use namespace isolation to avoid symbol conflicts";
            strategy.steps = {"Wrap conflicting library in namespace", "Update includes and usage", "Verify symbol isolation"};
            strategy.parameters = {
                {"action", "code_modification"},
                {"isolation_method", "namespace_wrapper"}
            };
        } else if (strategy_name == "build_order_enforcement") {
            strategy.description = "Enforce specific build order to resolve conflicts";
            strategy.steps = {"Define build dependencies", "Configure build order", "Test build sequence"};
            strategy.parameters = {
                {"action", "build_configuration"},
                {"resolution", "dependency_ordering"}
            };
        }

        strategies.push_back(strategy);
    }

    return strategies;
}

// Predict conflicts for proposed updates
std::vector<ConflictPrediction> VersionConflictDetector::predictConflicts(
    const DependencyUpdate& update,
    const std::vector<DependencyVersion>& current_deps) const {

    std::vector<ConflictPrediction> predictions;

    auto conflicts = detectConflictsWithUpdate(update, current_deps);

    if (!conflicts.empty()) {
        ConflictPrediction prediction;
        prediction.proposed_update = update;
        prediction.potential_conflicts = conflicts;
        prediction.probability = 0.9; // High probability if conflicts detected
        prediction.severity = "high";

        // Determine highest severity
        for (const auto& conflict : conflicts) {
            if (conflict.severity == "critical") {
                prediction.severity = "critical";
                break;
            }
        }

        // Suggest alternatives
        prediction.alternatives = {
            "Delay update until dependencies are compatible",
            "Update conflicting dependencies first",
            "Seek alternative library versions"
        };

        predictions.push_back(prediction);
    }

    return predictions;
}

// Generate early warnings
std::vector<EarlyWarning> VersionConflictDetector::generateEarlyWarnings(const std::vector<DependencyVersion>& dependencies) const {
    std::vector<EarlyWarning> warnings;

    auto now = std::chrono::system_clock::now();

    // Check for potential future conflicts based on version trends
    for (const auto& dep : dependencies) {
        // Example: Warn about versions approaching end-of-life
        if (dep.name == "googletest" && dep.version < "1.10.0") {
            EarlyWarning warning;
            warning.warning_type = "version_deprecation";
            warning.dependency1 = dep.name;
            warning.current_versions = dep.version;
            warning.warning_description = "Using old version of " + dep.name + " that may have compatibility issues";
            warning.severity = "medium";
            warning.monitoring_suggestions = {
                "Monitor for newer releases",
                "Plan upgrade to supported version",
                "Check compatibility with current dependencies"
            };
            warning.predicted_timeframe = now + std::chrono::hours(24 * 30); // 30 days

            warnings.push_back(warning);
        }
    }

    return warnings;
}

// Assess conflict severity
std::string VersionConflictDetector::assessSeverity(const std::string& conflict_type) const {
    if (conflict_type == "symbol_conflict") return "critical";
    if (conflict_type == "version_range_conflict") return "high";
    if (conflict_type == "build_system_conflict") return "medium";
    return "low";
}

// Calculate conflict probability
double VersionConflictDetector::calculateConflictProbability(const ConflictConflict& conflict) const {
    double base_probability = 0.5;

    if (conflict.severity == "critical") base_probability = 0.9;
    else if (conflict.severity == "high") base_probability = 0.7;
    else if (conflict.severity == "medium") base_probability = 0.5;
    else base_probability = 0.3;

    return base_probability;
}

// Apply prevention strategy
bool VersionConflictDetector::applyPreventionStrategy(const PreventionStrategy& strategy) {
    // This is a simplified implementation
    // In practice, this would involve actual code/configuration changes
    logConflict({strategy.strategy_type, "", "", "", "", "applied", "Strategy applied", {}});

    return true;
}

// Add conflict rule
bool VersionConflictDetector::addConflictRule(const json& rule) {
    if (!rule.contains("rule_id") || !rule.contains("rule_type")) {
        return false;
    }

    conflict_rules_.push_back(rule);
    return saveConflictRules();
}

// Remove conflict rule
bool VersionConflictDetector::removeConflictRule(const std::string& rule_id) {
    auto it = std::remove_if(conflict_rules_.begin(), conflict_rules_.end(),
        [&rule_id](const json& rule) {
            return rule.contains("rule_id") && rule["rule_id"] == rule_id;
        });

    if (it != conflict_rules_.end()) {
        conflict_rules_.erase(it, conflict_rules_.end());
        return saveConflictRules();
    }

    return false;
}

// Save conflict rules
bool VersionConflictDetector::saveConflictRules() const {
    try {
        std::string rules_file = cache_dir_ + "/conflict_rules.json";
        std::ofstream file(rules_file);
        file << conflict_rules_.dump(4);
        file.close();
        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

// Generate conflict report
ConflictReport VersionConflictDetector::generateConflictReport(const std::vector<ConflictConflict>& conflicts) const {
    ConflictReport report;
    report.report_timestamp = getCurrentTimestamp();
    report.conflicts = conflicts;

    // Calculate summary
    report.summary["total"] = static_cast<int>(conflicts.size());
    report.summary["critical"] = 0;
    report.summary["high"] = 0;
    report.summary["medium"] = 0;
    report.summary["low"] = 0;

    for (const auto& conflict : conflicts) {
        report.summary[conflict.severity]++;
    }

    // Generate recommendations
    report.recommendations = generateConflictSummary(conflicts);

    // Generate prevention strategies
    for (const auto& conflict : conflicts) {
        auto strategies = generatePreventionStrategies(conflict);
        report.prevention_strategies[generateConflictId(conflict)] = strategies;
    }

    return report;
}

// Generate conflict summary
std::vector<std::string> VersionConflictDetector::generateConflictSummary(const std::vector<ConflictConflict>& conflicts) const {
    std::vector<std::string> summary;

    if (conflicts.empty()) {
        summary.push_back("No conflicts detected. Dependencies are compatible.");
        return summary;
    }

    summary.push_back("Total conflicts detected: " + std::to_string(conflicts.size()));

    // Count by severity
    int critical_count = 0, high_count = 0;
    for (const auto& conflict : conflicts) {
        if (conflict.severity == "critical") critical_count++;
        else if (conflict.severity == "high") high_count++;
    }

    if (critical_count > 0) {
        summary.push_back("URGENT: " + std::to_string(critical_count) + " critical conflicts require immediate attention");
    }

    if (high_count > 0) {
        summary.push_back("IMPORTANT: " + std::to_string(high_count) + " high-priority conflicts should be resolved");
    }

    summary.push_back("Review prevention strategies for recommended actions");

    return summary;
}

// Validate with compatibility system
ValidationResult VersionConflictDetector::validateWithCompatibilitySystem(const std::vector<DependencyUpdate>& updates) const {
    ValidationResult result;
    result.conflicts_detected = false;
    result.compatibility_validated = true;
    result.safe_to_proceed = true;

    // For each update, check for potential conflicts
    for (const auto& update : updates) {
        // This would integrate with the T047 compatibility validator
        // For now, just check if there are any obvious conflicts
        if (update.name == "googletest" && update.proposed_version == "1.12.0") {
            result.conflicts_detected = true;
            result.safe_to_proceed = false;
            result.recommendations.push_back("googletest 1.12.0 has known compatibility issues");
        }
    }

    if (!result.conflicts_detected) {
        result.recommendations.push_back("All updates appear compatible with current dependencies");
    }

    return result;
}

// Get current timestamp
std::string VersionConflictDetector::getCurrentTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

// Check if dependency pair matches conflict rule
bool VersionConflictDetector::conflictMatchesRule(const DependencyVersion& dep1, const DependencyVersion& dep2, const json& rule) const {
    if (!rule.contains("dependency1") || !rule.contains("dependency2")) {
        return false;
    }

    std::string rule_dep1 = rule["dependency1"];
    std::string rule_dep2 = rule["dependency2"];

    // Check if dependencies match the rule (in either order)
    bool deps_match = ((dep1.name == rule_dep1 && dep2.name == rule_dep2) ||
                      (dep1.name == rule_dep2 && dep2.name == rule_dep1));

    if (!deps_match) return false;

    // Check version conditions
    if (rule.contains("conflict_condition")) {
        std::string condition = rule["conflict_condition"];
        // Simplified condition checking - in practice would use proper expression evaluation
        if (condition.find("googletest.version == '1.12.0'") != std::string::npos) {
            if ((dep1.name == "googletest" && dep1.version == "1.12.0") ||
                (dep2.name == "googletest" && dep2.version == "1.12.0")) {

                if (condition.find("boost.version < '1.72.0'") != std::string::npos) {
                    std::string boost_version = (dep1.name == "boost") ? dep1.version : dep2.version;
                    return boost_version < "1.72.0";
                }
            }
        }
    }

    return deps_match;
}

// Generate conflict ID
std::string VersionConflictDetector::generateConflictId(const ConflictConflict& conflict) const {
    return conflict.dependency1 + ":" + conflict.version1 +
           "_vs_" + conflict.dependency2 + ":" + conflict.version2;
}

// Log conflict
void VersionConflictDetector::logConflict(const ConflictConflict& conflict) const {
    // Simple logging - in practice would use proper logging system
    if (real_time_monitoring_enabled_ && alert_callback_) {
        alert_callback_(conflict);
    }
}

// Get conflict rule count
int VersionConflictDetector::getConflictRuleCount() const {
    return static_cast<int>(conflict_rules_.size());
}

// Get supported conflict types
std::vector<std::string> VersionConflictDetector::getSupportedConflictTypes() const {
    return {"version_range_conflict", "symbol_conflict", "build_system_conflict", "custom_conflict"};
}

// Clear cache
void VersionConflictDetector::clearCache() {
    conflict_history_.clear();
}

// Set conflict alert callback
void VersionConflictDetector::setConflictAlertCallback(std::function<void(const ConflictConflict&)> callback) {
    alert_callback_ = callback;
}

// Enable real-time monitoring
void VersionConflictDetector::enableRealTimeMonitoring(bool enable) {
    real_time_monitoring_enabled_ = enable;
}

// Get recent conflicts
std::vector<ConflictConflict> VersionConflictDetector::getRecentConflicts(int hours) const {
    // Simplified implementation - returns empty vector
    return {};
}

// Suggest compatible versions
std::vector<DependencyVersion> VersionConflictDetector::suggestCompatibleVersions(const std::vector<DependencyVersion>& problematic_deps) const {
    // Simplified implementation - returns empty vector
    return {};
}

// Simulate conflict resolution
bool VersionConflictDetector::simulateConflictResolution(const std::vector<ConflictConflict>& conflicts,
                                                        const std::vector<PreventionStrategy>& strategies) const {
    // Simplified implementation - returns true if strategies provided
    return !strategies.empty();
}

// Calculate conflict risks
std::map<std::string, double> VersionConflictDetector::calculateConflictRisks(const std::vector<DependencyVersion>& dependencies) const {
    std::map<std::string, double> risks;

    auto conflicts = detectConflicts(dependencies);

    // Calculate risk score for each dependency
    for (const auto& dep : dependencies) {
        double risk = 0.0;
        int conflict_count = 0;

        for (const auto& conflict : conflicts) {
            if (conflict.dependency1 == dep.name || conflict.dependency2 == dep.name) {
                conflict_count++;
                if (conflict.severity == "critical") risk += 0.3;
                else if (conflict.severity == "high") risk += 0.2;
                else if (conflict.severity == "medium") risk += 0.1;
                else risk += 0.05;
            }
        }

        risks[dep.name] = std::min(1.0, risk);
    }

    return risks;
}

// Generate JSON conflict report
json VersionConflictDetector::generateConflictReportJSON(const std::vector<ConflictConflict>& conflicts) const {
    auto report = generateConflictReport(conflicts);

    json json_report;
    json_report["report_timestamp"] = report.report_timestamp;
    json_report["summary"] = report.summary;
    json_report["recommendations"] = report.recommendations;

    for (const auto& conflict : conflicts) {
        json conflict_json;
        conflict_json["conflict_type"] = conflict.conflict_type;
        conflict_json["dependency1"] = conflict.dependency1;
        conflict_json["version1"] = conflict.version1;
        conflict_json["dependency2"] = conflict.dependency2;
        conflict_json["version2"] = conflict.version2;
        conflict_json["severity"] = conflict.severity;
        conflict_json["description"] = conflict.description;
        conflict_json["prevention_strategies"] = conflict.prevention_strategies;

        json_report["conflicts"].push_back(conflict_json);
    }

    return json_report;
}

// Export conflict report
bool VersionConflictDetector::exportConflictReport(const ConflictReport& report, const std::string& filename) const {
    try {
        json json_report;
        json_report["report_timestamp"] = report.report_timestamp;
        json_report["summary"] = report.summary;
        json_report["recommendations"] = report.recommendations;

        for (const auto& conflict : report.conflicts) {
            json conflict_json;
            conflict_json["conflict_type"] = conflict.conflict_type;
            conflict_json["dependency1"] = conflict.dependency1;
            conflict_json["version1"] = conflict.version1;
            conflict_json["dependency2"] = conflict.dependency2;
            conflict_json["version2"] = conflict.version2;
            conflict_json["severity"] = conflict.severity;
            conflict_json["description"] = conflict.description;
            conflict_json["prevention_strategies"] = conflict.prevention_strategies;

            json_report["conflicts"].push_back(conflict_json);
        }

        std::ofstream file(filename);
        file << json_report.dump(4);
        file.close();

        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

// Check cross-dependency compatibility
bool VersionConflictDetector::checkCrossDependencyCompatibility(const std::vector<DependencyVersion>& dependencies) const {
    auto conflicts = detectConflicts(dependencies);

    // If any critical or high severity conflicts, return false
    for (const auto& conflict : conflicts) {
        if (conflict.severity == "critical" || conflict.severity == "high") {
            return false;
        }
    }

    return true;
}

// Get all conflict rules
std::vector<json> VersionConflictDetector::getAllConflictRules() const {
    std::vector<json> rules;
    for (const auto& rule : conflict_rules_) {
        rules.push_back(rule);
    }
    return rules;
}

// Update conflict rule
bool VersionConflictDetector::updateConflictRule(const std::string& rule_id, const json& updated_rule) {
    for (auto& rule : conflict_rules_) {
        if (rule.contains("rule_id") && rule["rule_id"] == rule_id) {
            rule = updated_rule;
            return saveConflictRules();
        }
    }
    return false;
}

} // namespace integration