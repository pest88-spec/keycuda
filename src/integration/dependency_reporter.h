// T049: Dependency Reporting and Documentation Generation
// Header file for comprehensive dependency reporting, documentation generation, and export capabilities

#ifndef INTEGRATION_DEPENDENCY_REPORTER_H
#define INTEGRATION_DEPENDENCY_REPORTER_H

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <chrono>
#include <ctime>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace integration {

// Structure for dependency information
struct DependencyInfo {
    std::string name;
    std::string version;
    std::string source_type;
    std::string license;
    std::string status;
    std::map<std::string, std::string> metadata;
    std::vector<std::string> dependencies;
    std::vector<std::string> conflicts_with;
    std::string description;
    std::string url;
    std::string sha256;
    std::string extracted_date;
    std::string last_update;
    double compatibility_score = 0.0;
    std::string available_version;
    std::vector<std::string> security_issues;
};

// Structure for dependency reports
struct DependencyReport {
    std::string report_id;
    std::string generated_timestamp;
    std::string generated_by;
    int total_dependencies = 0;
    int active_dependencies = 0;
    int outdated_dependencies = 0;
    int security_issues = 0;
    std::vector<DependencyInfo> dependencies;
    std::map<std::string, int> license_types;
    std::map<std::string, int> source_types;
    std::vector<std::string> recommendations;
    std::map<std::string, std::string> metadata;
};

// Structure for dependency metrics
struct DependencyMetrics {
    int total_dependencies = 0;
    int active_dependencies = 0;
    int outdated_dependencies = 0;
    int security_issues = 0;
    double average_compatibility_score = 0.0;
    std::map<std::string, int> license_types;
    std::map<std::string, int> source_types;
    std::map<std::string, int> status_distribution;
    std::vector<std::string> most_recent_updates;
    std::vector<std::string> oldest_dependencies;
    std::map<std::string, double> license_compliance_score;
    std::vector<std::string> update_recommendations;
};

// Structure for dependency snapshots for trend analysis
struct DependencySnapshot {
    std::string timestamp;
    int total_dependencies = 0;
    int active_dependencies = 0;
    int outdated_dependencies = 0;
    int security_issues = 0;
    double average_compatibility_score = 0.0;
    std::map<std::string, int> license_types;
    std::map<std::string, int> source_types;
};

// Structure for trend analysis results
struct DependencyTrend {
    std::string metric_name;
    std::string trend_direction;  // "increasing", "decreasing", "stable"
    double change_value = 0.0;
    double change_percentage = 0.0;
    std::string time_period;
    std::vector<double> data_points;
    std::string analysis;
};

// Structure for dependency documentation
struct DependencyDocumentation {
    std::string doc_id;
    std::string generated_timestamp;
    std::string version;
    std::string content;
    std::string format;  // "markdown", "html", "text"
    std::vector<std::string> sections;
    std::map<std::string, std::string> metadata;
};

// Structure for custom report templates
struct ReportTemplate {
    std::string template_id;
    std::string name;
    std::string description;
    std::string template_content;
    std::string output_format;
    std::vector<std::string> required_variables;
    std::map<std::string, std::string> default_values;
};

// Structure for report export options
struct ExportOptions {
    std::string format;  // "json", "markdown", "html", "csv", "pdf"
    std::string output_path;
    bool include_metadata = true;
    bool include_trends = true;
    bool include_recommendations = true;
    std::string template_id;
    std::map<std::string, std::string> custom_options;
};

class DependencyReporter {
public:
    explicit DependencyReporter(const std::string& cache_dir);
    ~DependencyReporter();

    // Core reporting functions
    bool initialize();
    DependencyReport generateDependencyReport(const std::vector<DependencyInfo>& dependencies) const;
    json generateJSONReport(const std::vector<DependencyInfo>& dependencies) const;
    std::string generateMarkdownReport(const std::vector<DependencyInfo>& dependencies) const;
    std::string generateHTMLReport(const std::vector<DependencyInfo>& dependencies) const;
    std::string generateCSVReport(const std::vector<DependencyInfo>& dependencies) const;

    // Metrics and analysis
    DependencyMetrics generateDependencyMetrics(const std::vector<DependencyInfo>& dependencies) const;
    std::vector<DependencyTrend> analyzeDependencyTrends(const std::vector<DependencySnapshot>& snapshots) const;
    std::vector<std::string> generateRecommendations(const std::vector<DependencyInfo>& dependencies) const;
    std::vector<std::string> identifySecurityIssues(const std::vector<DependencyInfo>& dependencies) const;

    // Documentation generation
    DependencyDocumentation generateDependencyDocumentation(const std::vector<DependencyInfo>& dependencies) const;
    std::string generateIntegrationGuide(const std::vector<DependencyInfo>& dependencies) const;
    std::string generateMaintenanceGuide(const std::vector<DependencyInfo>& dependencies) const;
    std::string generateTroubleshootingGuide(const std::vector<DependencyInfo>& dependencies) const;

    // Export functionality
    bool exportReportToFile(const std::vector<DependencyInfo>& dependencies,
                           const std::string& file_path,
                           const std::string& format) const;
    bool exportReportWithOptions(const std::vector<DependencyInfo>& dependencies,
                                const ExportOptions& options) const;
    std::vector<std::string> getSupportedFormats() const;

    // Template system
    bool loadTemplate(const std::string& template_id, const std::string& template_content);
    bool saveTemplate(const std::string& template_id, const ReportTemplate& template_info);
    std::string generateCustomReport(const std::vector<DependencyInfo>& dependencies,
                                   const std::string& template_content) const;
    std::vector<ReportTemplate> getAvailableTemplates() const;
    bool removeTemplate(const std::string& template_id);

    // Filtering and sorting
    std::vector<DependencyInfo> filterDependencies(const std::vector<DependencyInfo>& dependencies,
                                                  const std::string& field,
                                                  const std::string& value) const;
    std::vector<DependencyInfo> sortDependencies(const std::vector<DependencyInfo>& dependencies,
                                                const std::string& field,
                                                bool ascending = true) const;
    std::vector<DependencyInfo> searchDependencies(const std::vector<DependencyInfo>& dependencies,
                                                  const std::string& query) const;

    // History and snapshots
    bool saveSnapshot(const std::vector<DependencyInfo>& dependencies,
                     const std::string& snapshot_id = "") const;
    std::vector<DependencySnapshot> getSnapshots(int limit = 100) const;
    DependencySnapshot getLatestSnapshot() const;
    bool deleteSnapshot(const std::string& snapshot_id) const;

    // Configuration
    void setReportTemplate(const std::string& format, const std::string& template_content);
    void setDefaultExportPath(const std::string& path);
    void setIncludeRecommendations(bool include);
    void setIncludeTrends(bool include);
    void setIncludeMetadata(bool include);

    // Status and utilities
    bool isInitialized() const { return initialized_; }
    std::string getReportVersion() const { return "1.0.0"; }
    std::vector<std::string> getSupportedFields() const;
    void clearCache();

private:
    std::string cache_dir_;
    std::string default_export_path_;
    bool initialized_ = false;
    std::map<std::string, std::string> report_templates_;
    bool include_recommendations_ = true;
    bool include_trends_ = true;
    bool include_metadata_ = true;

    // Internal helper functions
    bool loadDefaultTemplates();
    std::string generateReportID() const;
    std::string getCurrentTimestamp() const;
    std::string formatFileSize(size_t bytes) const;
    std::string calculateDuration(const std::string& start_date, const std::string& end_date) const;
    std::string formatCompatibilityScore(double score) const;

    // Template rendering helpers
    std::string renderTemplate(const std::string& template_content,
                              const std::vector<DependencyInfo>& dependencies,
                              const DependencyReport& report) const;
    std::string replaceTemplateVariables(const std::string& content,
                                        const std::map<std::string, std::string>& variables) const;

    // Metrics calculation helpers
    double calculateAverageCompatibilityScore(const std::vector<DependencyInfo>& dependencies) const;
    std::map<std::string, int> calculateLicenseDistribution(const std::vector<DependencyInfo>& dependencies) const;
    std::map<std::string, int> calculateSourceDistribution(const std::vector<DependencyInfo>& dependencies) const;
    std::map<std::string, int> calculateStatusDistribution(const std::vector<DependencyInfo>& dependencies) const;

    // Trend analysis helpers
    std::vector<DependencyTrend> analyzeNumericTrends(const std::vector<DependencySnapshot>& snapshots) const;
    std::vector<DependencyTrend> analyzeCategoricalTrends(const std::vector<DependencySnapshot>& snapshots) const;
    double calculateTrendSlope(const std::vector<double>& values) const;

    // Validation helpers
    bool validateDependencies(const std::vector<DependencyInfo>& dependencies) const;
    bool validateTemplate(const std::string& template_content) const;
    bool validateExportOptions(const ExportOptions& options) const;

    // File I/O helpers
    bool writeToFile(const std::string& content, const std::string& file_path) const;
    std::string readFromFile(const std::string& file_path) const;
    bool ensureDirectoryExists(const std::string& dir_path) const;

    // JSON helpers
    json dependencyToJSON(const DependencyInfo& dep) const;
    DependencyInfo jsonToDependency(const json& json_dep) const;
    json metricsToJSON(const DependencyMetrics& metrics) const;
    json trendToJSON(const DependencyTrend& trend) const;

    // HTML/CSS helpers
    std::string generateHTMLHeader(const std::string& title) const;
    std::string generateHTMLFooter() const;
    std::string generateCSSStyles() const;
    std::string escapeHTML(const std::string& text) const;

    // Documentation helpers
    std::string generateTableOfContents(const std::vector<std::string>& sections) const;
    std::string generateDependencyTable(const std::vector<DependencyInfo>& dependencies) const;
    std::string generateDependencyDetails(const DependencyInfo& dep) const;
    std::string generateDependencyGraph(const std::vector<DependencyInfo>& dependencies) const;

    // Performance helpers
    std::vector<DependencyInfo> cacheDependencies(const std::vector<DependencyInfo>& dependencies) const;
    bool isCacheValid(const std::string& cache_key) const;
    std::string getCacheKey(const std::vector<DependencyInfo>& dependencies) const;
};

} // namespace integration

#endif // INTEGRATION_DEPENDENCY_REPORTER_H