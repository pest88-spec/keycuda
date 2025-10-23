// T007: Integration Evidence Collection and Verification System
// Collects and maintains evidence of integration operations

#include "evidence_collector.h"
#include <fstream>
#include <filesystem>

namespace integration {
namespace evidence {

EvidenceCollector::EvidenceCollector(const std::string& output_dir)
    : output_dir_(output_dir) {
    std::filesystem::create_directories(output_dir_);
    create_evidence_log();
}

void EvidenceCollector::record_extraction(const std::string& library_name,
                                        const std::string& source_path,
                                        const std::string& target_path,
                                        const std::vector<std::string>& files_copied) {
    nlohmann::json evidence;
    evidence["type"] = "extraction";
    evidence["timestamp"] = get_current_timestamp();
    evidence["library_name"] = library_name;
    evidence["source_path"] = source_path;
    evidence["target_path"] = target_path;
    evidence["files_copied"] = files_copied;
    evidence["file_count"] = files_copied.size();

    // Calculate SHA-256 for extracted files
    std::map<std::string, std::string> file_hashes;
    for (const auto& file : files_copied) {
        file_hashes[file] = calculate_file_hash(target_path + "/" + file);
    }
    evidence["file_hashes"] = file_hashes;

    save_evidence(evidence);
}

void EvidenceCollector::record_attribution_added(const std::string& file_path,
                                               const std::string& attribution_type,
                                               const std::string& content) {
    nlohmann::json evidence;
    evidence["type"] = "attribution_added";
    evidence["timestamp"] = get_current_timestamp();
    evidence["file_path"] = file_path;
    evidence["attribution_type"] = attribution_type;
    evidence["content_hash"] = calculate_string_hash(content);

    save_evidence(evidence);
}

void EvidenceCollector::record_integrity_check(const std::string& file_path,
                                             const std::string& expected_hash,
                                             const std::string& actual_hash,
                                             bool passed) {
    nlohmann::json evidence;
    evidence["type"] = "integrity_check";
    evidence["timestamp"] = get_current_timestamp();
    evidence["file_path"] = file_path;
    evidence["expected_hash"] = expected_hash;
    evidence["actual_hash"] = actual_hash;
    evidence["check_passed"] = passed;

    save_evidence(evidence);
}

void EvidenceCollector::record_build_integration(const std::string& library_name,
                                                const std::string& build_command,
                                                bool success) {
    nlohmann::json evidence;
    evidence["type"] = "build_integration";
    evidence["timestamp"] = get_current_timestamp();
    evidence["library_name"] = library_name;
    evidence["build_command"] = build_command;
    evidence["build_success"] = success;

    save_evidence(evidence);
}

bool EvidenceCollector::verify_integration_evidence(const std::string& library_name) const {
    std::ifstream evidence_file(output_dir_ + "/evidence_log.json");
    if (!evidence_file.is_open()) {
        return false;
    }

    try {
        nlohmann::json evidence_log;
        evidence_file >> evidence_log;

        // Check for required evidence types
        bool has_extraction = false;
        bool has_attribution = false;
        bool has_integrity = false;
        bool has_build = false;

        for (const auto& entry : evidence_log["evidence_entries"]) {
            if (entry.value("library_name", "") == library_name ||
                entry.value("file_path", "").find(library_name) != std::string::npos) {

                std::string type = entry.value("type", "");
                if (type == "extraction") has_extraction = true;
                if (type == "attribution_added") has_attribution = true;
                if (type == "integrity_check") has_integrity = true;
                if (type == "build_integration") has_build = true;
            }
        }

        return has_extraction && has_attribution && has_integrity && has_build;
    } catch (const std::exception&) {
        return false;
    }
}

void EvidenceCollector::generate_evidence_report(const std::string& output_file) const {
    nlohmann::json report;
    report["generated"] = get_current_timestamp();
    report["evidence_collector_version"] = "1.0.0";
    report["summary"] = generate_summary();

    // Load evidence log
    std::ifstream evidence_file(output_dir_ + "/evidence_log.json");
    if (evidence_file.is_open()) {
        nlohmann::json evidence_log;
        evidence_file >> evidence_log;
        report["evidence_entries"] = evidence_log["evidence_entries"];
    }

    std::ofstream file(output_file);
    if (file.is_open()) {
        file << report.dump(2) << std::endl;
        file.close();
    }
}

nlohmann::json EvidenceCollector::generate_summary() const {
    nlohmann::json summary;

    std::ifstream evidence_file(output_dir_ + "/evidence_log.json");
    if (!evidence_file.is_open()) {
        return summary;
    }

    try {
        nlohmann::json evidence_log;
        evidence_file >> evidence_log;

        int total_entries = 0;
        std::map<std::string, int> type_counts;
        std::map<std::string, int> library_counts;

        for (const auto& entry : evidence_log["evidence_entries"]) {
            total_entries++;
            std::string type = entry.value("type", "");
            std::string library = entry.value("library_name", "unknown");

            type_counts[type]++;
            library_counts[library]++;
        }

        summary["total_entries"] = total_entries;
        summary["entries_by_type"] = type_counts;
        summary["entries_by_library"] = library_counts;
    } catch (const std::exception& e) {
        summary["error"] = e.what();
    }

    return summary;
}

std::string EvidenceCollector::calculate_file_hash(const std::string& filepath) const {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return "";
    }

    // Simple hash implementation (in production, use proper SHA-256)
    std::hash<std::string> hasher;
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    auto hash_value = hasher(content);

    std::stringstream ss;
    ss << std::hex << hash_value;
    return ss.str();
}

std::string EvidenceCollector::calculate_string_hash(const std::string& str) const {
    std::hash<std::string> hasher;
    auto hash_value = hasher(str);

    std::stringstream ss;
    ss << std::hex << hash_value;
    return ss.str();
}

void EvidenceCollector::create_evidence_log() {
    std::string log_file = output_dir_ + "/evidence_log.json";

    std::ifstream file(log_file);
    if (file.is_open()) {
        return; // Already exists
    }

    // Create new evidence log
    nlohmann::json log;
    log["created"] = get_current_timestamp();
    log["version"] = "1.0.0";
    log["evidence_entries"] = nlohmann::json::array();

    std::ofstream new_file(log_file);
    if (new_file.is_open()) {
        new_file << log.dump(2) << std::endl;
        new_file.close();
    }
}

void EvidenceCollector::save_evidence(const nlohmann::json& evidence) {
    std::string log_file = output_dir_ + "/evidence_log.json";

    // Load existing log
    nlohmann::json log;
    std::ifstream file(log_file);
    if (file.is_open()) {
        file >> log;
        file.close();
    }

    // Add new evidence
    log["evidence_entries"].push_back(evidence);
    log["last_updated"] = get_current_timestamp();

    // Save updated log
    std::ofstream out_file(log_file);
    if (out_file.is_open()) {
        out_file << log.dump(2) << std::endl;
        out_file.close();
    }
}

std::string EvidenceCollector::get_current_timestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

} // namespace evidence
} // namespace integration