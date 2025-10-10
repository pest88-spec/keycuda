// T048: Version Conflict Detection and Prevention System
// Header file for conflict detection algorithms, prevention strategies, and early warning systems

#ifndef INTEGRATION_VERSION_CONFLICT_DETECTOR_H
#define INTEGRATION_VERSION_CONFLICT_DETECTOR_H

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <chrono>
#include <ctime>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace integration {

// Structure for dependency version information
struct DependencyVersion {
    std::string name;
    std::string version;
    std::string source_type;
    std::map<std::string, std::string> metadata;
};

// Structure for dependency update proposals
struct DependencyUpdate {
    std::string name;
    std::string current_version;
    std::string proposed_version;
    std::string source_type;
};

// Structure for detected conflicts
struct ConflictConflict {
    std::string conflict_type;
    std::string dependency1;
    std::string version1;
    std::string dependency2;
    std::string version2;
    std::string severity;
    std::string description;
    std::vector<std::string> prevention_strategies;
    std::map<std::string, std::string> details;
};

// Structure for prevention strategies
struct PreventionStrategy {
    std::string strategy_type;
    std::string description;
    std::map<std::string, std::string> parameters;
    std::vector<std::string> steps;
    int estimated_effort;
    double confidence;
};

// Structure for conflict predictions
struct ConflictPrediction {
    DependencyUpdate proposed_update;
    std::vector<ConflictConflict> potential_conflicts;
    double probability;
    std::string severity;
    std::vector<std::string> alternatives;
};

// Structure for early warnings
struct EarlyWarning {
    std::string warning_type;
    std::string dependency1;
    std::string dependency2;
    std::string current_versions;
    std::string warning_description;
    std::string severity;
    std::vector<std::string> monitoring_suggestions;
    std::chrono::system_clock::time_point predicted_timeframe;
};

// Structure for integration results
struct ValidationResult {
    bool conflicts_detected;
    bool compatibility_validated;
    std::vector<ConflictConflict> detected_conflicts;
    std::vector<std::string> recommendations;
    bool safe_to_proceed;
};

// Structure for conflict reports
struct ConflictReport {
    std::string report_timestamp;
    std::vector<ConflictConflict> conflicts;
    std::map<std::string, int> summary;
    std::vector<std::string> recommendations;
    std::map<std::string, std::vector<PreventionStrategy>> prevention_strategies;
};

class VersionConflictDetector {
public:
    explicit VersionConflictDetector(const std::string& cache_dir);
    ~VersionConflictDetector();

    // Core detection functions
    bool loadConflictRules(const std::string& filename = "");
    std::vector<ConflictConflict> detectConflicts(const std::vector<DependencyVersion>& dependencies) const;
    std::vector<ConflictConflict> detectConflictsWithUpdate(const DependencyUpdate& update,
                                                          const std::vector<DependencyVersion>& current_deps) const;

    // Prevention and prediction
    std::vector<PreventionStrategy> generatePreventionStrategies(const ConflictConflict& conflict) const;
    std::vector<ConflictPrediction> predictConflicts(const DependencyUpdate& update,
                                                     const std::vector<DependencyVersion>& current_deps) const;
    std::vector<EarlyWarning> generateEarlyWarnings(const std::vector<DependencyVersion>& dependencies) const;

    // Severity and assessment
    std::string assessSeverity(const std::string& conflict_type) const;
    double calculateConflictProbability(const ConflictConflict& conflict) const;
    bool applyPreventionStrategy(const PreventionStrategy& strategy);

    // Rule management
    bool addConflictRule(const json& rule);
    bool removeConflictRule(const std::string& rule_id);
    bool updateConflictRule(const std::string& rule_id, const json& updated_rule);
    std::vector<json> getAllConflictRules() const;

    // Integration and compatibility
    ValidationResult validateWithCompatibilitySystem(const std::vector<DependencyUpdate>& updates) const;
    bool checkCrossDependencyCompatibility(const std::vector<DependencyVersion>& dependencies) const;

    // Reporting and export
    ConflictReport generateConflictReport(const std::vector<ConflictConflict>& conflicts) const;
    json generateConflictReportJSON(const std::vector<ConflictConflict>& conflicts) const;
    bool exportConflictReport(const ConflictReport& report, const std::string& filename) const;
    std::vector<std::string> generateConflictSummary(const std::vector<ConflictConflict>& conflicts) const;

    // Monitoring and alerts
    void setConflictAlertCallback(std::function<void(const ConflictConflict&)> callback);
    void enableRealTimeMonitoring(bool enable);
    std::vector<ConflictConflict> getRecentConflicts(int hours = 24) const;

    // Status and utilities
    bool isInitialized() const { return conflict_rules_loaded_; }
    int getConflictRuleCount() const;
    std::vector<std::string> getSupportedConflictTypes() const;
    void clearCache();

    // Advanced features
    std::vector<DependencyVersion> suggestCompatibleVersions(const std::vector<DependencyVersion>& problematic_deps) const;
    bool simulateConflictResolution(const std::vector<ConflictConflict>& conflicts,
                                   const std::vector<PreventionStrategy>& strategies) const;
    std::map<std::string, double> calculateConflictRisks(const std::vector<DependencyVersion>& dependencies) const;

private:
    std::string cache_dir_;
    json conflict_rules_;
    bool conflict_rules_loaded_ = false;
    std::function<void(const ConflictConflict&)> alert_callback_;
    bool real_time_monitoring_enabled_ = false;
    std::vector<ConflictConflict> conflict_history_;

    // Internal helper functions
    bool loadDefaultConflictRules();
    std::vector<ConflictConflict> detectVersionRangeConflicts(const std::vector<DependencyVersion>& deps) const;
    std::vector<ConflictConflict> detectSymbolConflicts(const std::vector<DependencyVersion>& deps) const;
    std::vector<ConflictConflict> detectBuildSystemConflicts(const std::vector<DependencyVersion>& deps) const;
    std::vector<ConflictConflict> detectCustomRuleConflicts(const std::vector<DependencyVersion>& deps) const;

    std::string getCurrentTimestamp() const;
    bool conflictMatchesRule(const DependencyVersion& dep1, const DependencyVersion& dep2, const json& rule) const;
    PreventionStrategy createStrategyFromRule(const json& rule, const ConflictConflict& conflict) const;
    void logConflict(const ConflictConflict& conflict) const;
    std::string generateConflictId(const ConflictConflict& conflict) const;
  bool saveConflictRules() const;
};

} // namespace integration

#endif // INTEGRATION_VERSION_CONFLICT_DETECTOR_H