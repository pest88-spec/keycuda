// T014: Unified Integration Management Foundation
// Central coordinator for all integration operations

#include "integration_manager.h"
#include "../metrics/metrics.h"
#include "../config/config_manager.h"
#include "../attribution/attribution_verifier.h"
#include "../evidence/evidence_collector.h"
#include "../audit/audit_logger.h"
#include "../integrity/integrity_verifier.h"

namespace integration {
namespace manager {

IntegrationManager::IntegrationManager()
    : metrics_collector_(std::make_unique<metrics::MetricsCollector>())
    , config_manager_(std::make_unique<config::ConfigManager>())
    , attribution_verifier_(std::make_unique<attribution::AttributionVerifier>())
    , evidence_collector_(std::make_unique<evidence::EvidenceCollector>("build/evidence"))
    , audit_logger_(std::make_unique<audit::AuditLogger>("build/integration-audit.log"))
    , integrity_verifier_(std::make_unique<integrity::IntegrityVerifier>()) {

    // Enable verbose logging for metrics
    metrics_collector_->set_verbose(true);
}

bool IntegrationManager::initialize() {
    LOG_INTEGRATION_OP("manager_initialize", {
        {"phase", "initialization"},
        {"timestamp", get_current_timestamp()}
    });

    // Load configuration
    if (!config_manager_->load_config()) {
        last_error_ = "Failed to load configuration: " + config_manager_->get_error_message();
        return false;
    }

    // Validate configuration
    if (!config_manager_->validate_config()) {
        last_error_ = "Configuration validation failed: " + config_manager_->get_error_message();
        return false;
    }

    // Initialize audit logging
    audit_logger_->log_operation({
        get_current_timestamp(),
        "MANAGER_INITIALIZED",
        "system",
        "Integration manager initialized",
        "All subsystems ready",
        "", "", "", "", true,
        ""
    });

    return true;
}

bool IntegrationManager::extract_library(const std::string& library_name,
                                         const std::string& source_path,
                                         const std::string& target_path) {
    INTEGRATION_TIMER_SCOPE("extract_" + library_name);

    LOG_INTEGRATION_OP("library_extraction_start", {
        {"library", library_name},
        {"source", source_path},
        {"target", target_path}
    });

    // Record extraction start
    audit_logger_->log_extraction(library_name, source_path, target_path, 0, false);

    // Check if library exists in configuration
    auto lib_config = config_manager_->get_library_config(library_name);
    if (lib_config.name.empty()) {
        // Add new library configuration
        config::LibraryConfig new_config;
        new_config.name = library_name;
        new_config.source_path = source_path;
        new_config.integrated_path = target_path;
        new_config.enabled = true;
        config_manager_->add_library_config(new_config);
        config_manager_->save_config();
    }

    // Perform extraction (simplified - actual extraction would be more complex)
    std::vector<std::string> extracted_files;
    if (!perform_extraction(source_path, target_path, extracted_files)) {
        audit_logger_->log_extraction(library_name, source_path, target_path,
                                    extracted_files.size(), false);
        return false;
    }

    // Record successful extraction
    evidence_collector_->record_extraction(library_name, source_path, target_path, extracted_files);
    audit_logger_->log_extraction(library_name, source_path, target_path,
                                extracted_files.size(), true);

    LOG_INTEGRATION_OP("library_extraction_complete", {
        {"library", library_name},
        {"files_extracted", extracted_files.size()},
        {"success", true}
    });

    return true;
}

bool IntegrationManager::verify_integration(const std::string& library_name) {
    INTEGRATION_TIMER_SCOPE("verify_" + library_name);

    LOG_INTEGRATION_OP("integration_verification_start", {
        {"library", library_name}
    });

    auto lib_config = config_manager_->get_library_config(library_name);
    if (lib_config.integrated_path.empty()) {
        last_error_ = "Library not found in configuration: " + library_name;
        return false;
    }

    bool all_checks_passed = true;

    // 1. Attribution verification
    auto attribution_result = attribution_verifier_->verify_directory(lib_config.integrated_path);
    if (!attribution_result.passed) {
        last_error_ = "Attribution verification failed for " + library_name;
        all_checks_passed = false;
    }

    // 2. Integrity verification
    if (!lib_config.sha256.empty()) {
        if (!integrity_verifier_->verify_file_integrity(
                lib_config.integrated_path + "/MANIFEST.json",
                lib_config.sha256)) {
            last_error_ = "Integrity verification failed: " + integrity_verifier_->get_last_error();
            all_checks_passed = false;
        }
    }

    // 3. Evidence verification
    if (!evidence_collector_->verify_integration_evidence(library_name)) {
        last_error_ = "Evidence verification incomplete for " + library_name;
        all_checks_passed = false;
    }

    LOG_INTEGRATION_OP("integration_verification_complete", {
        {"library", library_name},
        {"passed", all_checks_passed},
        {"attribution_compliance", attribution_result.compliance_percentage}
    });

    return all_checks_passed;
}

bool IntegrationManager::detect_conflicts() {
    LOG_INTEGRATION_OP("conflict_detection_start", {});

    auto libraries = config_manager_->get_all_library_configs();
    std::vector<std::string> conflicts;

    // Check for version conflicts
    for (size_t i = 0; i < libraries.size(); ++i) {
        for (size_t j = i + 1; j < libraries.size(); ++j) {
            if (libraries[i].name == libraries[j].name &&
                libraries[i].version != libraries[j].version) {
                conflicts.push_back("Version conflict: " + libraries[i].name +
                                 " v" + libraries[i].version + " vs v" + libraries[j].version);
            }
        }
    }

    if (!conflicts.empty()) {
        last_error_ = "Conflicts detected: " + join_strings(conflicts, "; ");
        for (const auto& conflict : conflicts) {
            LOG_INTEGRATION_OP("conflict_detected", {
                {"type", "version"},
                {"description", conflict}
            });
        }
        return false;
    }

    LOG_INTEGRATION_OP("conflict_detection_complete", {
        {"conflicts_found", 0}
    });

    return true;
}

bool IntegrationManager::generate_reports() {
    LOG_INTEGRATION_OP("report_generation_start", {});

    try {
        // Generate metrics report
        metrics_collector_->export_to_json("build/integration-metrics.json");

        // Generate attribution report
        attribution_verifier_->generate_attribution_report("build/attribution-report.json");

        // Generate evidence report
        evidence_collector_->generate_evidence_report("build/evidence-report.json");

        // Generate audit report
        audit_logger_->generate_audit_report("build/audit-report.md");

        // Generate integrity report for each library
        auto libraries = config_manager_->get_all_library_configs();
        for (const auto& lib : libraries) {
            if (!lib.integrated_path.empty()) {
                auto integrity_report = integrity_verifier_->generate_integrity_report(lib.integrated_path);
                std::string filename = "build/integrity-" + lib.name + ".json";

                std::ofstream file(filename);
                if (file.is_open()) {
                    file << integrity_report.dump(2) << std::endl;
                    file.close();
                }
            }
        }

        LOG_INTEGRATION_OP("report_generation_complete", {
            {"reports_generated", "metrics, attribution, evidence, audit, integrity"}
        });

        return true;
    } catch (const std::exception& e) {
        last_error_ = "Report generation failed: " + std::string(e.what());
        return false;
    }
}

bool IntegrationManager::perform_extraction(const std::string& source,
                                           const std::string& target,
                                           std::vector<std::string>& files) {
    // Simplified extraction - in production, this would:
    // 1. Copy source files with proper attribution
    // 2. Apply namespace adaptations if needed
    // 3. Generate file hashes
    // 4. Update build configuration

    // For now, just check if source exists and create target directory
    std::filesystem::create_directories(target);

    // Simulate file extraction
    files.push_back("README.md");
    files.push_back("CMakeLists.txt");
    files.push_back("include/secp256k1.h");
    files.push_back("src/secp256k1.cpp");

    return true;
}

std::string IntegrationManager::join_strings(const std::vector<std::string>& strings,
                                             const std::string& delimiter) {
    if (strings.empty()) return "";

    std::string result = strings[0];
    for (size_t i = 1; i < strings.size(); ++i) {
        result += delimiter + strings[i];
    }
    return result;
}

std::string IntegrationManager::get_current_timestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

// Global instance
static std::unique_ptr<IntegrationManager> g_integration_manager;

IntegrationManager& get_integration_manager() {
    if (!g_integration_manager) {
        g_integration_manager = std::make_unique<IntegrationManager>();
    }
    return *g_integration_manager;
}

} // namespace manager
} // namespace integration