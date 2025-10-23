// T014: Unified Integration Management Foundation
// Central coordinator for all integration operations

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <nlohmann/json.hpp>

// Forward declarations
namespace integration { namespace metrics { class MetricsCollector; } }
namespace integration { namespace config { class ConfigManager; } }
namespace integration { namespace attribution { class AttributionVerifier; } }
namespace integration { namespace evidence { class EvidenceCollector; } }
namespace integration { namespace audit { class AuditLogger; } }
namespace integration { namespace integrity { class IntegrityVerifier; } }

namespace integration {
namespace manager {

class IntegrationManager {
public:
    IntegrationManager();

    // Core management operations
    bool initialize();
    bool extract_library(const std::string& library_name,
                         const std::string& source_path,
                         const std::string& target_path);
    bool verify_integration(const std::string& library_name);
    bool detect_conflicts();
    bool generate_reports();

    // Accessors
    const std::string& get_last_error() const { return last_error_; }

    // Component accessors
    metrics::MetricsCollector& get_metrics_collector() { return *metrics_collector_; }
    config::ConfigManager& get_config_manager() { return *config_manager_; }
    attribution::AttributionVerifier& get_attribution_verifier() { return *attribution_verifier_; }
    evidence::EvidenceCollector& get_evidence_collector() { return *evidence_collector_; }
    audit::AuditLogger& get_audit_logger() { return *audit_logger_; }
    integrity::IntegrityVerifier& get_integrity_verifier() { return *integrity_verifier_; }

private:
    bool perform_extraction(const std::string& source,
                           const std::string& target,
                           std::vector<std::string>& files);
    std::string join_strings(const std::vector<std::string>& strings,
                           const std::string& delimiter);
    std::string get_current_timestamp() const;

    std::unique_ptr<metrics::MetricsCollector> metrics_collector_;
    std::unique_ptr<config::ConfigManager> config_manager_;
    std::unique_ptr<attribution::AttributionVerifier> attribution_verifier_;
    std::unique_ptr<evidence::EvidenceCollector> evidence_collector_;
    std::unique_ptr<audit::AuditLogger> audit_logger_;
    std::unique_ptr<integrity::IntegrityVerifier> integrity_verifier_;

    std::string last_error_;
};

// Global accessor
IntegrationManager& get_integration_manager();

} // namespace manager
} // namespace integration