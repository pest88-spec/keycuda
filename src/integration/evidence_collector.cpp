/**
 * Integration Evidence Collection and Verification System Implementation
 *
 * @origin       https://github.com/Puzzle71Solver/Puzzle71Solver
 * @origin_path  src/integration/evidence_collector.cpp
 * @origin_commit <current_commit>
 * @origin_license MIT
 * @extracted_date   2025-10-10
 * @extracted_by     Puzzle71Solver Team
 * @modifications    Created for third-party dependency integration optimization
 * @spdx_license_identifier MIT
 */

#include "evidence_collector.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <random>
#include <fstream>
#include <filesystem>
#include <mutex>
#include <openssl/sha.h>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

EvidenceCollector::EvidenceCollector(const std::string& storage_path)
    : storage_path_(storage_path) {

    // Create storage directory if it doesn't exist
    if (!storage_path_.empty()) {
        std::filesystem::create_directories(storage_path_);
        std::filesystem::create_directories(storage_path_ + "/sessions");
        std::filesystem::create_directories(storage_path_ + "/files");
    }

    // Load existing evidence
    load_evidence();
    load_sessions();
}

EvidenceCollector::~EvidenceCollector() {
    // End any active session
    if (!current_session_id_.empty()) {
        end_session("Auto-end session on destruction");
    }
}

std::string EvidenceCollector::start_session(const std::string& session_type,
                                            const std::string& description,
                                            const std::string& initiated_by) {
    std::lock_guard<std::mutex> lock(evidence_mutex_);

    CollectionSession session;
    session.session_id = generate_session_id();
    session.session_type = session_type;
    session.description = description;
    session.started_at = std::chrono::system_clock::now();
    session.initiated_by = initiated_by;

    sessions_.push_back(session);
    current_session_id_ = session.session_id;

    save_session(session);

    return session.session_id;
}

bool EvidenceCollector::end_session(const std::string& summary) {
    std::lock_guard<std::mutex> lock(evidence_mutex_);

    if (current_session_id_.empty()) {
        return false; // No active session
    }

    auto it = std::find_if(sessions_.begin(), sessions_.end(),
        [this](const CollectionSession& session) {
            return session.session_id == current_session_id_;
        });

    if (it != sessions_.end()) {
        it->completed_at = std::chrono::system_clock::now();
        if (!summary.empty()) {
            it->session_metadata["summary"] = summary;
        }

        save_session(*it);
    }

    current_session_id_.clear();
    return true;
}

std::string EvidenceCollector::collect_source_inclusion(const std::string& library_name,
                                                       const std::vector<std::string>& source_paths,
                                                       const std::string& integration_path,
                                                       const std::string& description) {
    EvidenceItem evidence;
    evidence.evidence_id = generate_evidence_id();
    evidence.type = EvidenceType::SOURCE_INCLUSION;
    evidence.description = description.empty() ? "Source inclusion evidence for " + library_name : description;
    evidence.collected_at = std::chrono::system_clock::now();
    evidence.collected_by = "system";

    // Add metadata
    evidence.metadata["library_name"] = library_name;
    evidence.metadata["integration_path"] = integration_path;
    evidence.metadata["source_count"] = std::to_string(source_paths.size());

    // Calculate checksums for all source files
    std::string combined_checksum;
    for (const auto& path : source_paths) {
        if (std::filesystem::exists(path)) {
            std::string file_checksum = calculate_file_checksum(path);
            combined_checksum += file_checksum;
            evidence.file_paths.push_back(path);
            evidence.metadata["checksum_" + std::filesystem::path(path).filename().string()] = file_checksum;
        }
    }

    // Calculate overall checksum
    evidence.checksum = calculate_directory_checksum(integration_path);

    // Add to current session
    if (!current_session_id_.empty()) {
        auto it = std::find_if(sessions_.begin(), sessions_.end(),
            [this](const CollectionSession& session) {
                return session.session_id == current_session_id_;
            });
        if (it != sessions_.end()) {
            it->evidence_ids.push_back(evidence.evidence_id);
        }
    }

    evidence_items_[evidence.evidence_id] = evidence;
    save_evidence(evidence);

    return evidence.evidence_id;
}

std::string EvidenceCollector::collect_attribution_compliance(const std::string& library_name,
                                                             const std::vector<std::string>& files_with_attribution,
                                                             const std::vector<std::string>& files_missing_attribution,
                                                             double coverage_percentage) {
    EvidenceItem evidence;
    evidence.evidence_id = generate_evidence_id();
    evidence.type = EvidenceType::ATTRIBUTION_COMPLIANCE;
    evidence.description = "Attribution compliance evidence for " + library_name;
    evidence.collected_at = std::chrono::system_clock::now();
    evidence.collected_by = "system";

    // Add metadata
    evidence.metadata["library_name"] = library_name;
    evidence.metadata["files_with_attribution"] = std::to_string(files_with_attribution.size());
    evidence.metadata["files_missing_attribution"] = std::to_string(files_missing_attribution.size());
    evidence.metadata["coverage_percentage"] = std::to_string(coverage_percentage);
    evidence.metadata["compliance_status"] = coverage_percentage >= 95.0 ? "COMPLIANT" : "NON_COMPLIANT";

    // Add file paths
    for (const auto& path : files_with_attribution) {
        evidence.file_paths.push_back(path);
    }
    for (const auto& path : files_missing_attribution) {
        evidence.file_paths.push_back(path);
    }

    // Add to current session
    if (!current_session_id_.empty()) {
        auto it = std::find_if(sessions_.begin(), sessions_.end(),
            [this](const CollectionSession& session) {
                return session.session_id == current_session_id_;
            });
        if (it != sessions_.end()) {
            it->evidence_ids.push_back(evidence.evidence_id);
        }
    }

    evidence_items_[evidence.evidence_id] = evidence;
    save_evidence(evidence);

    return evidence.evidence_id;
}

std::string EvidenceCollector::collect_build_verification(const std::string& build_command,
                                                        const std::string& build_output,
                                                        bool build_success,
                                                        double build_time,
                                                        const std::vector<std::string>& artifacts) {
    EvidenceItem evidence;
    evidence.evidence_id = generate_evidence_id();
    evidence.type = EvidenceType::BUILD_VERIFICATION;
    evidence.description = "Build verification evidence";
    evidence.collected_at = std::chrono::system_clock::now();
    evidence.collected_by = "system";

    // Add metadata
    evidence.metadata["build_command"] = build_command;
    evidence.metadata["build_success"] = build_success ? "true" : "false";
    evidence.metadata["build_time_seconds"] = std::to_string(build_time);
    evidence.metadata["artifacts_count"] = std::to_string(artifacts.size());

    // Store build output in a file
    std::string output_file = storage_path_ + "/files/" + evidence.evidence_id + "_build_output.txt";
    std::ofstream output_file_stream(output_file);
    if (output_file_stream.is_open()) {
        output_file_stream << build_output;
        output_file_stream.close();
        evidence.metadata["build_output_file"] = output_file;
    }

    // Add artifact paths
    for (const auto& artifact : artifacts) {
        evidence.file_paths.push_back(artifact);
    }

    // Add to current session
    if (!current_session_id_.empty()) {
        auto it = std::find_if(sessions_.begin(), sessions_.end(),
            [this](const CollectionSession& session) {
                return session.session_id == current_session_id_;
            });
        if (it != sessions_.end()) {
            it->evidence_ids.push_back(evidence.evidence_id);
        }
    }

    evidence_items_[evidence.evidence_id] = evidence;
    save_evidence(evidence);

    return evidence.evidence_id;
}

std::string EvidenceCollector::collect_integrity_validation(const std::string& validation_type,
                                                           const std::vector<std::string>& validated_items,
                                                           const std::map<std::string, bool>& validation_results) {
    EvidenceItem evidence;
    evidence.evidence_id = generate_evidence_id();
    evidence.type = EvidenceType::INTEGRITY_VALIDATION;
    evidence.description = "Integrity validation evidence: " + validation_type;
    evidence.collected_at = std::chrono::system_clock::now();
    evidence.collected_by = "system";

    // Add metadata
    evidence.metadata["validation_type"] = validation_type;
    evidence.metadata["items_validated"] = std::to_string(validated_items.size());

    size_t passed_count = 0;
    for (const auto& [item, passed] : validation_results) {
        if (passed) passed_count++;
        evidence.metadata["validation_" + item] = passed ? "PASS" : "FAIL";
    }

    evidence.metadata["validation_passed_count"] = std::to_string(passed_count);
    evidence.metadata["validation_failed_count"] = std::to_string(validated_items.size() - passed_count);
    evidence.metadata["overall_status"] = passed_count == validated_items.size() ? "PASS" : "FAIL";

    // Add validated item paths
    for (const auto& item : validated_items) {
        evidence.file_paths.push_back(item);
    }

    // Add to current session
    if (!current_session_id_.empty()) {
        auto it = std::find_if(sessions_.begin(), sessions_.end(),
            [this](const CollectionSession& session) {
                return session.session_id == current_session_id_;
            });
        if (it != sessions_.end()) {
            it->evidence_ids.push_back(evidence.evidence_id);
        }
    }

    evidence_items_[evidence.evidence_id] = evidence;
    save_evidence(evidence);

    return evidence.evidence_id;
}

std::string EvidenceCollector::collect_license_verification(const std::string& library_name,
                                                          const std::vector<std::string>& detected_licenses,
                                                          bool license_compliance,
                                                          const std::vector<std::string>& license_files) {
    EvidenceItem evidence;
    evidence.evidence_id = generate_evidence_id();
    evidence.type = EvidenceType::LICENSE_VERIFICATION;
    evidence.description = "License verification evidence for " + library_name;
    evidence.collected_at = std::chrono::system_clock::now();
    evidence.collected_by = "system";

    // Add metadata
    evidence.metadata["library_name"] = library_name;
    evidence.metadata["license_compliance"] = license_compliance ? "COMPLIANT" : "NON_COMPLIANT";

    // Add detected licenses
    std::string licenses_list;
    for (size_t i = 0; i < detected_licenses.size(); ++i) {
        if (i > 0) licenses_list += ", ";
        licenses_list += detected_licenses[i];
        evidence.metadata["detected_license_" + std::to_string(i)] = detected_licenses[i];
    }
    evidence.metadata["detected_licenses_list"] = licenses_list;

    // Add license file paths
    for (const auto& license_file : license_files) {
        evidence.file_paths.push_back(license_file);
    }

    // Add to current session
    if (!current_session_id_.empty()) {
        auto it = std::find_if(sessions_.begin(), sessions_.end(),
            [this](const CollectionSession& session) {
                return session.session_id == current_session_id_;
            });
        if (it != sessions_.end()) {
            it->evidence_ids.push_back(evidence.evidence_id);
        }
    }

    evidence_items_[evidence.evidence_id] = evidence;
    save_evidence(evidence);

    return evidence.evidence_id;
}

bool EvidenceCollector::verify_evidence(const std::string& evidence_id,
                                       const std::string& verified_by,
                                       const std::string& verification_notes) {
    std::lock_guard<std::mutex> lock(evidence_mutex_);

    auto it = evidence_items_.find(evidence_id);
    if (it == evidence_items_.end()) {
        return false;
    }

    it->second.status = EvidenceStatus::VERIFIED;
    it->second.verified_at = std::chrono::system_clock::now();
    it->second.verified_by = verified_by;
    it->second.notes = verification_notes;

    return save_evidence(it->second);
}

EvidenceCollector::EvidenceItem EvidenceCollector::get_evidence(const std::string& evidence_id) const {
    std::lock_guard<std::mutex> lock(evidence_mutex_);

    auto it = evidence_items_.find(evidence_id);
    if (it != evidence_items_.end()) {
        return it->second;
    }
    return EvidenceItem();
}

std::vector<EvidenceCollector::EvidenceItem> EvidenceCollector::get_all_evidence(EvidenceType type,
                                                                                 EvidenceStatus status) const {
    std::lock_guard<std::mutex> lock(evidence_mutex_);

    std::vector<EvidenceItem> result;
    for (const auto& [id, evidence] : evidence_items_) {
        if ((type == EvidenceType::SOURCE_INCLUSION || evidence.type == type) &&
            (status == EvidenceStatus::PENDING || evidence.status == status)) {
            result.push_back(evidence);
        }
    }

    return result;
}

std::vector<EvidenceCollector::CollectionSession> EvidenceCollector::get_sessions() const {
    std::lock_guard<std::mutex> lock(evidence_mutex_);
    return sessions_;
}

std::string EvidenceCollector::generate_report(const std::string& session_id,
                                              const std::string& format) const {
    std::lock_guard<std::mutex> lock(evidence_mutex_);

    json report;
    report["report_generated"] = format_timestamp(std::chrono::system_clock::now());
    report["report_format"] = format;

    if (!session_id.empty()) {
        // Report for specific session
        auto session_it = std::find_if(sessions_.begin(), sessions_.end(),
            [&session_id](const CollectionSession& session) {
                return session.session_id == session_id;
            });

        if (session_it != sessions_.end()) {
            report["session"] = session_to_json(*session_it);

            // Add evidence for this session
            json evidence_array = json::array();
            for (const auto& evidence_id : session_it->evidence_ids) {
                auto it = evidence_items_.find(evidence_id);
                if (it != evidence_items_.end()) {
                    evidence_array.push_back(evidence_to_json(it->second));
                }
            }
            report["evidence"] = evidence_array;
        }
    } else {
        // Report for all sessions
        json sessions_array = json::array();
        for (const auto& session : sessions_) {
            sessions_array.push_back(session_to_json(session));
        }
        report["sessions"] = sessions_array;

        // Add summary statistics
        EvidenceStats stats = get_statistics();
        report["summary"] = stats_to_json(stats);
    }

    if (format == "json") {
        return report.dump(4);
    } else {
        // Text format
        std::stringstream ss;
        ss << "Evidence Collection Report\n";
        ss << "Generated: " << report["report_generated"] << "\n\n";

        if (!session_id.empty()) {
            ss << "Session ID: " << session_id << "\n";
            // Add session details...
        } else {
            ss << "Total Sessions: " << sessions_.size() << "\n";
            ss << "Total Evidence Items: " << evidence_items_.size() << "\n";
        }

        return ss.str();
    }
}

bool EvidenceCollector::export_evidence(const std::vector<std::string>& evidence_ids,
                                       const std::string& export_path,
                                       bool include_files) {
    std::filesystem::create_directories(export_path);

    // Create manifest
    json manifest;
    manifest["export_timestamp"] = format_timestamp(std::chrono::system_clock::now());
    manifest["evidence_count"] = evidence_ids.size();
    manifest["evidence_items"] = json::array();

    for (const auto& evidence_id : evidence_ids) {
        auto evidence = get_evidence(evidence_id);
        if (!evidence.evidence_id.empty()) {
            manifest["evidence_items"].push_back(evidence_to_json(evidence));

            if (include_files) {
                // Copy associated files
                for (const auto& file_path : evidence.file_paths) {
                    if (std::filesystem::exists(file_path)) {
                        std::filesystem::path dest_path = export_path / std::filesystem::path(file_path).filename();
                        std::filesystem::copy_file(file_path, dest_path);
                    }
                }
            }
        }
    }

    // Save manifest
    std::ofstream manifest_file(export_path + "/manifest.json");
    if (manifest_file.is_open()) {
        manifest_file << std::setw(4) << manifest << std::endl;
        manifest_file.close();
        return true;
    }

    return false;
}

EvidenceCollector::VerificationResult EvidenceCollector::verify_evidence_integrity(const std::string& evidence_id) const {
    VerificationResult result;
    result.verification_type = "evidence_integrity";

    auto evidence = get_evidence(evidence_id);
    if (evidence.evidence_id.empty()) {
        result.passed = false;
        result.failed_checks.push_back("Evidence not found");
        return result;
    }

    // Verify checksums
    bool checksums_valid = true;
    for (const auto& file_path : evidence.file_paths) {
        if (std::filesystem::exists(file_path)) {
            std::string current_checksum = calculate_file_checksum(file_path);
            std::string stored_checksum_key = "checksum_" + std::filesystem::path(file_path).filename().string();

            auto it = evidence.metadata.find(stored_checksum_key);
            if (it != evidence.metadata.end() && it->second != current_checksum) {
                checksums_valid = false;
                result.failed_checks.push_back("Checksum mismatch for file: " + file_path);
            } else {
                result.passed_checks.push_back("Checksum valid for file: " + file_path);
            }
        } else {
            checksums_valid = false;
            result.failed_checks.push_back("File not found: " + file_path);
        }
    }

    result.passed = checksums_valid;
    result.summary = checksums_valid ? "All integrity checks passed" : "Integrity checks failed";

    return result;
}

size_t EvidenceCollector::archive_evidence(std::chrono::system_clock::time_point older_than,
                                          const std::string& archive_path) {
    std::filesystem::create_directories(archive_path);

    size_t archived_count = 0;
    std::vector<std::string> to_archive;

    // Find evidence to archive
    for (const auto& [id, evidence] : evidence_items_) {
        if (evidence.collected_at < older_than) {
            to_archive.push_back(id);
        }
    }

    // Archive evidence items
    for (const auto& evidence_id : to_archive) {
        if (export_evidence({evidence_id}, archive_path + "/" + evidence_id, true)) {
            evidence_items_[evidence_id].status = EvidenceStatus::ARCHIVED;
            save_evidence(evidence_items_[evidence_id]);
            archived_count++;
        }
    }

    return archived_count;
}

EvidenceCollector::EvidenceStats EvidenceCollector::get_statistics() const {
    std::lock_guard<std::mutex> lock(evidence_mutex_);

    EvidenceStats stats;
    stats.total_evidence_items = evidence_items_.size();
    stats.total_sessions = sessions_.size();

    for (const auto& [id, evidence] : evidence_items_) {
        stats.items_by_type[evidence.type]++;
        stats.items_by_status[evidence.status]++;

        if (evidence.collected_at > stats.last_collection) {
            stats.last_collection = evidence.collected_at;
        }
    }

    return stats;
}

std::vector<std::string> EvidenceCollector::search_evidence(const std::string& query,
                                                           bool search_metadata) const {
    std::lock_guard<std::mutex> lock(evidence_mutex_);

    std::vector<std::string> results;
    std::string lower_query = query;
    std::transform(lower_query.begin(), lower_query.end(), lower_query.begin(), ::tolower);

    for (const auto& [id, evidence] : evidence_items_) {
        std::string lower_description = evidence.description;
        std::transform(lower_description.begin(), lower_description.end(), lower_description.begin(), ::tolower);

        if (lower_description.find(lower_query) != std::string::npos) {
            results.push_back(id);
            continue;
        }

        if (search_metadata) {
            for (const auto& [key, value] : evidence.metadata) {
                std::string lower_value = value;
                std::transform(lower_value.begin(), lower_value.end(), lower_value.begin(), ::tolower);
                if (lower_value.find(lower_query) != std::string::npos) {
                    results.push_back(id);
                    break;
                }
            }
        }
    }

    return results;
}

bool EvidenceCollector::delete_evidence(const std::string& evidence_id,
                                       const std::string& deleted_by,
                                       const std::string& reason) {
    std::lock_guard<std::mutex> lock(evidence_mutex_);

    auto it = evidence_items_.find(evidence_id);
    if (it == evidence_items_.end()) {
        return false;
    }

    // Remove from evidence items
    evidence_items_.erase(it);

    // Remove from sessions
    for (auto& session : sessions_) {
        auto evidence_it = std::find(session.evidence_ids.begin(), session.evidence_ids.end(), evidence_id);
        if (evidence_it != session.evidence_ids.end()) {
            session.evidence_ids.erase(evidence_it);
        }
    }

    // Delete evidence file
    std::string evidence_file = storage_path_ + "/" + evidence_id + ".json";
    if (std::filesystem::exists(evidence_file)) {
        std::filesystem::remove(evidence_file);
    }

    return true;
}

// Private methods implementation
std::string EvidenceCollector::generate_evidence_id() const {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(100000, 999999);

    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);

    std::stringstream ss;
    ss << "ev_" << std::put_time(std::gmtime(&time_t), "%Y%m%d_%H%M%S") << "_" << dis(gen);
    return ss.str();
}

std::string EvidenceCollector::generate_session_id() const {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(100000, 999999);

    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);

    std::stringstream ss;
    ss << "sess_" << std::put_time(std::gmtime(&time_t), "%Y%m%d_%H%M%S") << "_" << dis(gen);
    return ss.str();
}

std::string EvidenceCollector::calculate_file_checksum(const std::string& file_path) const {
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        return "";
    }

    SHA256_CTX sha256;
    SHA256_Init(&sha256);

    char buffer[4096];
    while (file.read(buffer, sizeof(buffer))) {
        SHA256_Update(&sha256, buffer, file.gcount());
    }

    if (file.gcount() > 0) {
        SHA256_Update(&sha256, buffer, file.gcount());
    }

    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_Final(hash, &sha256);

    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }

    return ss.str();
}

std::string EvidenceCollector::calculate_directory_checksum(const std::string& dir_path) const {
    if (!std::filesystem::exists(dir_path)) {
        return "";
    }

    SHA256_CTX sha256;
    SHA256_Init(&sha256);

    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir_path)) {
        if (entry.is_regular_file()) {
            std::string relative_path = std::filesystem::relative(entry.path(), dir_path).string();
            SHA256_Update(&sha256, relative_path.c_str(), relative_path.length());

            std::string file_checksum = calculate_file_checksum(entry.path().string());
            SHA256_Update(&sha256, file_checksum.c_str(), file_checksum.length());
        }
    }

    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_Final(hash, &sha256);

    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }

    return ss.str();
}

bool EvidenceCollector::save_evidence(const EvidenceItem& evidence) const {
    std::ofstream file(storage_path_ + "/" + evidence.evidence_id + ".json");
    if (!file.is_open()) {
        return false;
    }

    json evidence_json = evidence_to_json(evidence);
    file << std::setw(4) << evidence_json << std::endl;
    return true;
}

bool EvidenceCollector::load_evidence() {
    if (!std::filesystem::exists(storage_path_)) {
        return false;
    }

    evidence_items_.clear();

    for (const auto& entry : std::filesystem::directory_iterator(storage_path_)) {
        if (entry.path().extension() == ".json" && entry.path().filename().string().find("sess_") == std::string::npos) {
            std::ifstream file(entry.path());
            if (file.is_open()) {
                try {
                    json evidence_json;
                    file >> evidence_json;

                    EvidenceItem evidence = json_to_evidence(evidence_json);
                    evidence_items_[evidence.evidence_id] = evidence;
                } catch (const std::exception& e) {
                    // Skip corrupted files
                    continue;
                }
            }
        }
    }

    return true;
}

bool EvidenceCollector::save_session(const CollectionSession& session) const {
    std::ofstream file(storage_path_ + "/sessions/" + session.session_id + ".json");
    if (!file.is_open()) {
        return false;
    }

    json session_json = session_to_json(session);
    file << std::setw(4) << session_json << std::endl;
    return true;
}

bool EvidenceCollector::load_sessions() {
    std::string sessions_path = storage_path_ + "/sessions";
    if (!std::filesystem::exists(sessions_path)) {
        return false;
    }

    sessions_.clear();

    for (const auto& entry : std::filesystem::directory_iterator(sessions_path)) {
        if (entry.path().extension() == ".json") {
            std::ifstream file(entry.path());
            if (file.is_open()) {
                try {
                    json session_json;
                    file >> session_json;

                    CollectionSession session = json_to_session(session_json);
                    sessions_.push_back(session);
                } catch (const std::exception& e) {
                    // Skip corrupted files
                    continue;
                }
            }
        }
    }

    return true;
}

std::string EvidenceCollector::format_timestamp(std::chrono::system_clock::time_point tp) const {
    auto time_t = std::chrono::system_clock::to_time_t(tp);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

std::chrono::system_clock::time_point EvidenceCollector::parse_timestamp(const std::string& ts) const {
    if (ts.empty()) {
        return std::chrono::system_clock::now();
    }

    std::tm tm = {};
    std::istringstream ss(ts);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

std::string EvidenceCollector::evidence_type_to_string(EvidenceType type) const {
    switch (type) {
        case EvidenceType::SOURCE_INCLUSION:     return "SOURCE_INCLUSION";
        case EvidenceType::ATTRIBUTION_COMPLIANCE: return "ATTRIBUTION_COMPLIANCE";
        case EvidenceType::BUILD_VERIFICATION:    return "BUILD_VERIFICATION";
        case EvidenceType::INTEGRITY_VALIDATION:  return "INTEGRITY_VALIDATION";
        case EvidenceType::LICENSE_VERIFICATION:  return "LICENSE_VERIFICATION";
        case EvidenceType::DEPENDENCY_EXTRACTION: return "DEPENDENCY_EXTRACTION";
        case EvidenceType::CONFIGURATION_CHANGE:  return "CONFIGURATION_CHANGE";
        case EvidenceType::TEST_EXECUTION:        return "TEST_EXECUTION";
        case EvidenceType::DEPLOYMENT_VERIFICATION: return "DEPLOYMENT_VERIFICATION";
        default:                                  return "UNKNOWN_TYPE";
    }
}

std::string EvidenceCollector::evidence_status_to_string(EvidenceStatus status) const {
    switch (status) {
        case EvidenceStatus::COLLECTED:  return "COLLECTED";
        case EvidenceStatus::VERIFIED:   return "VERIFIED";
        case EvidenceStatus::FAILED:     return "FAILED";
        case EvidenceStatus::PENDING:    return "PENDING";
        case EvidenceStatus::ARCHIVED:   return "ARCHIVED";
        default:                         return "UNKNOWN_STATUS";
    }
}

// JSON conversion helpers
json EvidenceCollector::evidence_to_json(const EvidenceItem& evidence) const {
    json j;
    j["evidence_id"] = evidence.evidence_id;
    j["type"] = evidence_type_to_string(evidence.type);
    j["status"] = evidence_status_to_string(evidence.status);
    j["description"] = evidence.description;
    j["collected_at"] = format_timestamp(evidence.collected_at);
    j["verified_at"] = format_timestamp(evidence.verified_at);
    j["collected_by"] = evidence.collected_by;
    j["verified_by"] = evidence.verified_by;
    j["metadata"] = evidence.metadata;
    j["file_paths"] = evidence.file_paths;
    j["checksum"] = evidence.checksum;
    j["notes"] = evidence.notes;
    return j;
}

json EvidenceCollector::session_to_json(const CollectionSession& session) const {
    json j;
    j["session_id"] = session.session_id;
    j["session_type"] = session.session_type;
    j["description"] = session.description;
    j["started_at"] = format_timestamp(session.started_at);
    j["completed_at"] = format_timestamp(session.completed_at);
    j["initiated_by"] = session.initiated_by;
    j["evidence_ids"] = session.evidence_ids;
    j["session_metadata"] = session.session_metadata;
    return j;
}

json EvidenceCollector::stats_to_json(const EvidenceStats& stats) const {
    json j;
    j["total_evidence_items"] = stats.total_evidence_items;
    j["total_sessions"] = stats.total_sessions;
    j["last_collection"] = format_timestamp(stats.last_collection);

    json by_type;
    for (const auto& [type, count] : stats.items_by_type) {
        by_type[evidence_type_to_string(type)] = count;
    }
    j["items_by_type"] = by_type;

    json by_status;
    for (const auto& [status, count] : stats.items_by_status) {
        by_status[evidence_status_to_string(status)] = count;
    }
    j["items_by_status"] = by_status;

    return j;
}

EvidenceCollector::EvidenceItem EvidenceCollector::json_to_evidence(const json& j) const {
    EvidenceItem evidence;
    evidence.evidence_id = j.value("evidence_id", "");

    std::string type_str = j.value("type", "UNKNOWN_TYPE");
    if (type_str == "SOURCE_INCLUSION") evidence.type = EvidenceType::SOURCE_INCLUSION;
    else if (type_str == "ATTRIBUTION_COMPLIANCE") evidence.type = EvidenceType::ATTRIBUTION_COMPLIANCE;
    else if (type_str == "BUILD_VERIFICATION") evidence.type = EvidenceType::BUILD_VERIFICATION;
    else if (type_str == "INTEGRITY_VALIDATION") evidence.type = EvidenceType::INTEGRITY_VALIDATION;
    else if (type_str == "LICENSE_VERIFICATION") evidence.type = EvidenceType::LICENSE_VERIFICATION;
    else if (type_str == "DEPENDENCY_EXTRACTION") evidence.type = EvidenceType::DEPENDENCY_EXTRACTION;
    else if (type_str == "CONFIGURATION_CHANGE") evidence.type = EvidenceType::CONFIGURATION_CHANGE;
    else if (type_str == "TEST_EXECUTION") evidence.type = EvidenceType::TEST_EXECUTION;
    else if (type_str == "DEPLOYMENT_VERIFICATION") evidence.type = EvidenceType::DEPLOYMENT_VERIFICATION;

    std::string status_str = j.value("status", "PENDING");
    if (status_str == "COLLECTED") evidence.status = EvidenceStatus::COLLECTED;
    else if (status_str == "VERIFIED") evidence.status = EvidenceStatus::VERIFIED;
    else if (status_str == "FAILED") evidence.status = EvidenceStatus::FAILED;
    else if (status_str == "ARCHIVED") evidence.status = EvidenceStatus::ARCHIVED;
    else evidence.status = EvidenceStatus::PENDING;

    evidence.description = j.value("description", "");
    evidence.collected_at = parse_timestamp(j.value("collected_at", ""));
    evidence.verified_at = parse_timestamp(j.value("verified_at", ""));
    evidence.collected_by = j.value("collected_by", "");
    evidence.verified_by = j.value("verified_by", "");
    evidence.metadata = j.value("metadata", std::map<std::string, std::string>{});
    evidence.file_paths = j.value("file_paths", std::vector<std::string>{});
    evidence.checksum = j.value("checksum", "");
    evidence.notes = j.value("notes", "");

    return evidence;
}

EvidenceCollector::CollectionSession EvidenceCollector::json_to_session(const json& j) const {
    CollectionSession session;
    session.session_id = j.value("session_id", "");
    session.session_type = j.value("session_type", "");
    session.description = j.value("description", "");
    session.started_at = parse_timestamp(j.value("started_at", ""));
    session.completed_at = parse_timestamp(j.value("completed_at", ""));
    session.initiated_by = j.value("initiated_by", "");
    session.evidence_ids = j.value("evidence_ids", std::vector<std::string>{});
    session.session_metadata = j.value("session_metadata", std::map<std::string, std::string>{});
    return session;
}

// EvidenceSessionManager implementation
EvidenceSessionManager::EvidenceSessionManager(EvidenceCollector& collector,
                                              const std::string& session_type,
                                              const std::string& description)
    : collector_(collector), committed_(false) {
    session_id_ = collector_.start_session(session_type, description);
}

EvidenceSessionManager::~EvidenceSessionManager() {
    if (!committed_) {
        collector_.end_session("Session auto-ended");
    }
}

void EvidenceSessionManager::commit(const std::string& summary) {
    collector_.end_session(summary);
    committed_ = true;
}