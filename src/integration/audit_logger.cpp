/**
 * Audit Logging System with Integrity Verification Implementation
 *
 * @origin       https://github.com/Puzzle71Solver/Puzzle71Solver
 * @origin_path  src/integration/audit_logger.cpp
 * @origin_commit <current_commit>
 * @origin_license MIT
 * @extracted_date   2025-10-10
 * @extracted_by     Puzzle71Solver Team
 * @modifications    Created for third-party dependency integration optimization
 * @spdx_license_identifier MIT
 */

#include "audit_logger.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <filesystem>
#include <random>
#include <regex>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

AuditLogger::AuditLogger(
    const std::string& log_directory,
    bool integrity_enabled,
    size_t max_entries_per_file,
    size_t max_log_files,
    bool auto_rotate
) : log_directory_(log_directory),
    integrity_enabled_(integrity_enabled),
    max_entries_per_file_(max_entries_per_file),
    max_log_files_(max_log_files),
    auto_rotate_enabled_(auto_rotate) {

    // Create log directory if it doesn't exist
    if (!log_directory_.empty()) {
        std::filesystem::create_directories(log_directory_);
    }

    // Initialize current log file
    current_log_file_ = get_current_log_file_path();

    // Load existing entries
    load_entries_from_file(current_log_file_);

    // Calculate last entry hash for integrity chain
    if (!entries_.empty()) {
        last_entry_hash_ = entries_.back().entry_hash;
    }
}

AuditLogger::~AuditLogger() {
    std::lock_guard<std::mutex> lock(log_mutex_);
    if (auto_rotate_enabled_) {
        rotate_log_file_if_needed();
    }
}

std::string AuditLogger::generate_entry_id() const {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(100000, 999999);

    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);

    std::stringstream ss;
    ss << "audit_" << std::put_time(std::gmtime(&time_t), "%Y%m%d_%H%M%S") << "_" << dis(gen);
    return ss.str();
}

std::string AuditLogger::calculate_entry_hash(const AuditEntry& entry) const {
    std::stringstream ss;

    // Include all entry fields in hash calculation
    ss << entry.entry_id
       << entry_type_to_string(entry.entry_type)
       << severity_to_string(entry.severity)
       << format_timestamp(entry.timestamp)
       << entry.user_id
       << entry.session_id
       << entry.operation
       << entry.resource
       << entry.previous_hash;

    // Include details in hash
    for (const auto& [key, value] : entry.details) {
        ss << key << ":" << value << ";";
    }

    std::string entry_data = ss.str();

    // Calculate SHA-256 hash
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, entry_data.c_str(), entry_data.length());

    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_Final(hash, &sha256);

    std::stringstream hash_ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        hash_ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }

    return hash_ss.str();
}

bool AuditLogger::log_entry(LogEntryType entry_type,
                           Severity severity,
                           const std::string& user_id,
                           const std::string& session_id,
                           const std::string& operation,
                           const std::string& resource,
                           const std::map<std::string, std::string>& details) {
    std::lock_guard<std::mutex> lock(log_mutex_);

    AuditEntry entry;
    entry.entry_id = generate_entry_id();
    entry.entry_type = entry_type;
    entry.severity = severity;
    entry.timestamp = std::chrono::system_clock::now();
    entry.user_id = user_id;
    entry.session_id = session_id;
    entry.operation = operation;
    entry.resource = resource;
    entry.details = details;
    entry.previous_hash = last_entry_hash_;

    // Calculate entry hash
    if (integrity_enabled_) {
        entry.entry_hash = calculate_entry_hash(entry);
        last_entry_hash_ = entry.entry_hash;
    }

    entries_.push_back(entry);

    // Write to file
    bool success = write_entry_to_file(entry);

    // Rotate log file if needed
    if (auto_rotate_enabled_ && success) {
        rotate_log_file_if_needed();
    }

    return success;
}

bool AuditLogger::log_integration_operation(const std::string& user_id,
                                           const std::string& session_id,
                                           const std::string& operation,
                                           const std::string& library_name,
                                           const std::map<std::string, std::string>& details) {
    std::map<std::string, std::string> operation_details = details;
    operation_details["library_name"] = library_name;
    operation_details["operation_type"] = "integration";

    return log_entry(LogEntryType::INTEGRATION_START,
                     Severity::INFO,
                     user_id,
                     session_id,
                     operation,
                     library_name,
                     operation_details);
}

bool AuditLogger::log_configuration_change(const std::string& user_id,
                                          const std::string& session_id,
                                          const std::string& config_key,
                                          const std::string& old_value,
                                          const std::string& new_value) {
    std::map<std::string, std::string> details;
    details["config_key"] = config_key;
    details["old_value"] = old_value;
    details["new_value"] = new_value;
    details["change_type"] = "configuration";

    return log_entry(LogEntryType::CONFIGURATION_CHANGE,
                     Severity::INFO,
                     user_id,
                     session_id,
                     "config_change",
                     config_key,
                     details);
}

bool AuditLogger::log_integrity_check(const std::string& user_id,
                                     const std::string& resource,
                                     bool check_passed,
                                     const std::string& check_details) {
    std::map<std::string, std::string> details;
    details["resource"] = resource;
    details["check_passed"] = check_passed ? "true" : "false";
    details["check_details"] = check_details;
    details["check_type"] = "integrity";

    return log_entry(LogEntryType::INTEGRITY_CHECK,
                     check_passed ? Severity::INFO : Severity::WARNING,
                     user_id,
                     "",
                     "integrity_check",
                     resource,
                     details);
}

bool AuditLogger::log_security_event(const std::string& user_id,
                                    const std::string& event_type,
                                    const std::string& description,
                                    Severity severity) {
    std::map<std::string, std::string> details;
    details["event_type"] = event_type;
    details["description"] = description;
    details["security_level"] = severity_to_string(severity);

    return log_entry(LogEntryType::SECURITY_EVENT,
                     severity,
                     user_id,
                     "",
                     "security_event",
                     event_type,
                     details);
}

AuditLogger::IntegrityResult AuditLogger::verify_log_integrity() const {
    std::lock_guard<std::mutex> lock(log_mutex_);

    IntegrityResult result;
    result.total_entries_checked = entries_.size();
    std::string expected_hash;

    for (size_t i = 0; i < entries_.size(); ++i) {
        const auto& entry = entries_[i];

        // Verify hash chain
        if (i == 0) {
            // First entry should have empty previous hash
            if (!entry.previous_hash.empty()) {
                result.is_valid = false;
                result.integrity_violations.push_back("First entry has non-empty previous hash");
                result.tampered_entries.push_back(i);
            }
        } else {
            // Verify previous hash matches
            if (entry.previous_hash != expected_hash) {
                result.is_valid = false;
                result.integrity_violations.push_back("Hash chain broken at entry " + std::to_string(i));
                result.tampered_entries.push_back(i);
            }
        }

        // Recalculate and verify entry hash
        if (integrity_enabled_) {
            std::string recalculated_hash = calculate_entry_hash(entry);
            if (recalculated_hash != entry.entry_hash) {
                result.is_valid = false;
                result.integrity_violations.push_back("Entry hash mismatch at entry " + std::to_string(i));
                result.tampered_entries.push_back(i);
            }
        }

        expected_hash = entry.entry_hash;
    }

    result.last_valid_hash = expected_hash;
    result.verification_time = std::chrono::system_clock::now();

    return result;
}

std::vector<AuditLogger::AuditEntry> AuditLogger::get_entries(LogEntryType entry_type,
                                                              Severity severity,
                                                              std::chrono::system_clock::time_point* start_time,
                                                              std::chrono::system_clock::time_point* end_time,
                                                              size_t limit) const {
    std::lock_guard<std::mutex> lock(log_mutex_);

    std::vector<AuditEntry> filtered_entries;

    for (const auto& entry : entries_) {
        // Filter by entry type
        if (entry_type != LogEntryType::SYSTEM_EVENT && entry.entry_type != entry_type) {
            continue;
        }

        // Filter by severity
        if (severity != Severity::DEBUG && entry.severity != severity) {
            continue;
        }

        // Filter by time range
        if (start_time && entry.timestamp < *start_time) {
            continue;
        }
        if (end_time && entry.timestamp > *end_time) {
            continue;
        }

        filtered_entries.push_back(entry);

        // Apply limit
        if (limit > 0 && filtered_entries.size() >= limit) {
            break;
        }
    }

    return filtered_entries;
}

std::vector<AuditLogger::AuditEntry> AuditLogger::search_entries(const std::string& query,
                                                                bool search_details,
                                                                bool case_sensitive) const {
    std::lock_guard<std::mutex> lock(log_mutex_);

    std::vector<AuditEntry> matching_entries;
    std::string search_query = case_sensitive ? query : to_lower(query);

    for (const auto& entry : entries_) {
        std::string operation_str = case_sensitive ? entry.operation : to_lower(entry.operation);
        std::string resource_str = case_sensitive ? entry.resource : to_lower(entry.resource);
        std::string user_id_str = case_sensitive ? entry.user_id : to_lower(entry.user_id);

        bool matches = operation_str.find(search_query) != std::string::npos ||
                      resource_str.find(search_query) != std::string::npos ||
                      user_id_str.find(search_query) != std::string::npos;

        if (!matches && search_details) {
            for (const auto& [key, value] : entry.details) {
                std::string key_str = case_sensitive ? key : to_lower(key);
                std::string value_str = case_sensitive ? value : to_lower(value);

                if (key_str.find(search_query) != std::string::npos ||
                    value_str.find(search_query) != std::string::npos) {
                    matches = true;
                    break;
                }
            }
        }

        if (matches) {
            matching_entries.push_back(entry);
        }
    }

    return matching_entries;
}

AuditLogger::AuditStats AuditLogger::get_statistics() const {
    std::lock_guard<std::mutex> lock(log_mutex_);

    AuditStats stats;
    stats.total_entries = entries_.size();

    for (const auto& entry : entries_) {
        // Count by type
        stats.entries_by_type[static_cast<int>(entry.entry_type)]++;

        // Count by severity
        stats.entries_by_severity[static_cast<int>(entry.severity)]++;

        // Track time range
        if (stats.oldest_entry == std::chrono::system_clock::time_point{} ||
            entry.timestamp < stats.oldest_entry) {
            stats.oldest_entry = entry.timestamp;
        }
        if (stats.newest_entry == std::chrono::system_clock::time_point{} ||
            entry.timestamp > stats.newest_entry) {
            stats.newest_entry = entry.timestamp;
        }

        // Count operations
        stats.operations_by_count[entry.operation]++;

        // Estimate log size (rough calculation)
        stats.total_log_size_bytes += serialize_entry(entry).length();
    }

    return stats;
}

bool AuditLogger::export_log(const std::string& export_path,
                            std::chrono::system_clock::time_point* start_time,
                            std::chrono::system_clock::time_point* end_time,
                            bool include_integrity_info,
                            const std::string& format) const {
    std::lock_guard<std::mutex> lock(log_mutex_);

    std::ofstream file(export_path);
    if (!file.is_open()) {
        return false;
    }

    auto entries_to_export = get_entries(LogEntryType::SYSTEM_EVENT,
                                        Severity::DEBUG,
                                        start_time,
                                        end_time,
                                        0); // No limit

    if (format == "json") {
        json export_data;
        export_data["export_metadata"] = json::object();
        export_data["export_metadata"]["exported_at"] = format_timestamp(std::chrono::system_clock::now());
        export_data["export_metadata"]["total_entries"] = entries_to_export.size();
        export_data["export_metadata"]["integrity_enabled"] = integrity_enabled_;
        export_data["export_metadata"]["include_integrity_info"] = include_integrity_info;

        if (start_time) {
            export_data["export_metadata"]["start_time"] = format_timestamp(*start_time);
        }
        if (end_time) {
            export_data["export_metadata"]["end_time"] = format_timestamp(*end_time);
        }

        export_data["entries"] = json::array();
        for (const auto& entry : entries_to_export) {
            json entry_json;
            entry_json["entry_id"] = entry.entry_id;
            entry_json["entry_type"] = entry_type_to_string(entry.entry_type);
            entry_json["severity"] = severity_to_string(entry.severity);
            entry_json["timestamp"] = format_timestamp(entry.timestamp);
            entry_json["user_id"] = entry.user_id;
            entry_json["session_id"] = entry.session_id;
            entry_json["operation"] = entry.operation;
            entry_json["resource"] = entry.resource;
            entry_json["details"] = entry.details;

            if (include_integrity_info) {
                entry_json["previous_hash"] = entry.previous_hash;
                entry_json["entry_hash"] = entry.entry_hash;
                entry_json["signature"] = entry.signature;
            }

            export_data["entries"].push_back(entry_json);
        }

        file << std::setw(4) << export_data << std::endl;
    } else if (format == "csv") {
        file << "entry_id,entry_type,severity,timestamp,user_id,session_id,operation,resource";
        if (include_integrity_info) {
            file << ",previous_hash,entry_hash";
        }
        file << "\n";

        for (const auto& entry : entries_to_export) {
            file << "\"" << entry.entry_id << "\","
                 << entry_type_to_string(entry.entry_type) << ","
                 << severity_to_string(entry.severity) << ","
                 << format_timestamp(entry.timestamp) << ","
                 << "\"" << entry.user_id << "\","
                 << "\"" << entry.session_id << "\","
                 << "\"" << entry.operation << "\","
                 << "\"" << entry.resource << "\"";

            if (include_integrity_info) {
                file << "," << entry.previous_hash << "," << entry.entry_hash;
            }

            file << "\n";
        }
    }

    return true;
}

std::string AuditLogger::generate_report(const std::string& report_type,
                                        std::chrono::system_clock::time_point* start_time,
                                        std::chrono::system_clock::time_point* end_time,
                                        const std::string& format) const {
    if (format == "json") {
        json report;
        report["report_type"] = report_type;
        report["generated_at"] = format_timestamp(std::chrono::system_clock::now());
        report["integrity_enabled"] = integrity_enabled_;

        auto entries = get_entries(LogEntryType::SYSTEM_EVENT,
                                  Severity::DEBUG,
                                  start_time,
                                  end_time,
                                  0);

        if (report_type == "summary") {
            auto stats = get_statistics();
            report["summary"] = json::object();
            report["summary"]["total_entries"] = stats.total_entries;
            report["summary"]["time_range"] = json::object();
            report["summary"]["time_range"]["start"] = format_timestamp(stats.oldest_entry);
            report["summary"]["time_range"]["end"] = format_timestamp(stats.newest_entry);
            report["summary"]["total_size_bytes"] = stats.total_log_size_bytes;
        } else if (report_type == "security") {
            json security_events = json::array();
            for (const auto& entry : entries) {
                if (entry.entry_type == LogEntryType::SECURITY_EVENT) {
                    json event;
                    event["timestamp"] = format_timestamp(entry.timestamp);
                    event["user_id"] = entry.user_id;
                    event["event_type"] = entry.details.at("event_type");
                    event["description"] = entry.details.at("description");
                    event["severity"] = severity_to_string(entry.severity);
                    security_events.push_back(event);
                }
            }
            report["security_events"] = security_events;
        }

        return report.dump(4);
    } else {
        // Text format
        std::stringstream ss;
        ss << "Audit Log Report - " << report_type << "\n";
        ss << "========================\n\n";
        ss << "Generated: " << format_timestamp(std::chrono::system_clock::now()) << "\n";
        ss << "Integrity Enabled: " << (integrity_enabled_ ? "Yes" : "No") << "\n";

        auto entries = get_entries(LogEntryType::SYSTEM_EVENT,
                                  Severity::DEBUG,
                                  start_time,
                                  end_time,
                                  0);

        ss << "Total Entries: " << entries.size() << "\n";

        if (start_time) {
            ss << "Start Time: " << format_timestamp(*start_time) << "\n";
        }
        if (end_time) {
            ss << "End Time: " << format_timestamp(*end_time) << "\n";
        }

        return ss.str();
    }
}

// Private methods implementation
std::string AuditLogger::serialize_entry(const AuditEntry& entry) const {
    json entry_json;
    entry_json["entry_id"] = entry.entry_id;
    entry_json["entry_type"] = entry_type_to_string(entry.entry_type);
    entry_json["severity"] = severity_to_string(entry.severity);
    entry_json["timestamp"] = format_timestamp(entry.timestamp);
    entry_json["user_id"] = entry.user_id;
    entry_json["session_id"] = entry.session_id;
    entry_json["operation"] = entry.operation;
    entry_json["resource"] = entry.resource;
    entry_json["details"] = entry.details;
    entry_json["previous_hash"] = entry.previous_hash;
    entry_json["entry_hash"] = entry.entry_hash;
    entry_json["signature"] = entry.signature;

    return entry_json.dump();
}

AuditLogger::AuditEntry AuditLogger::deserialize_entry(const std::string& json_str) const {
    AuditEntry entry;

    try {
        json entry_json = json::parse(json_str);

        entry.entry_id = entry_json.value("entry_id", "");
        entry.entry_type = string_to_entry_type(entry_json.value("entry_type", "SYSTEM_EVENT"));
        entry.severity = string_to_severity(entry_json.value("severity", "INFO"));
        entry.timestamp = parse_timestamp(entry_json.value("timestamp", ""));
        entry.user_id = entry_json.value("user_id", "");
        entry.session_id = entry_json.value("session_id", "");
        entry.operation = entry_json.value("operation", "");
        entry.resource = entry_json.value("resource", "");
        entry.details = entry_json.value("details", std::map<std::string, std::string>{});
        entry.previous_hash = entry_json.value("previous_hash", "");
        entry.entry_hash = entry_json.value("entry_hash", "");
        entry.signature = entry_json.value("signature", "");
    } catch (const std::exception& e) {
        // Return empty entry on parse error
        return AuditEntry();
    }

    return entry;
}

bool AuditLogger::write_entry_to_file(const AuditEntry& entry) {
    std::ofstream file(current_log_file_, std::ios::app);
    if (!file.is_open()) {
        return false;
    }

    file << serialize_entry(entry) << "\n";
    return file.good();
}

bool AuditLogger::load_entries_from_file(const std::string& filename) {
    if (!std::filesystem::exists(filename)) {
        return true; // File doesn't exist is OK
    }

    std::ifstream file(filename);
    if (!file.is_open()) {
        return false;
    }

    entries_.clear();
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty()) {
            AuditEntry entry = deserialize_entry(line);
            if (!entry.entry_id.empty()) {
                entries_.push_back(entry);
            }
        }
    }

    return true;
}

std::string AuditLogger::get_current_log_file_path() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);

    std::stringstream ss;
    ss << log_directory_ << "/audit_" << std::put_time(std::gmtime(&time_t), "%Y%m%d") << ".log";
    return ss.str();
}

std::string AuditLogger::format_timestamp(std::chrono::system_clock::time_point tp) const {
    auto time_t = std::chrono::system_clock::to_time_t(tp);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

std::chrono::system_clock::time_point AuditLogger::parse_timestamp(const std::string& ts) const {
    if (ts.empty()) {
        return std::chrono::system_clock::now();
    }

    std::tm tm = {};
    std::istringstream ss(ts);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

std::string AuditLogger::entry_type_to_string(LogEntryType type) const {
    switch (type) {
        case LogEntryType::INTEGRATION_START:   return "INTEGRATION_START";
        case LogEntryType::INTEGRATION_COMPLETE: return "INTEGRATION_COMPLETE";
        case LogEntryType::INTEGRATION_ERROR:    return "INTEGRATION_ERROR";
        case LogEntryType::CONFIGURATION_CHANGE: return "CONFIGURATION_CHANGE";
        case LogEntryType::METADATA_UPDATE:     return "METADATA_UPDATE";
        case LogEntryType::INTEGRITY_CHECK:     return "INTEGRITY_CHECK";
        case LogEntryType::BUILD_OPERATION:     return "BUILD_OPERATION";
        case LogEntryType::DEPLOYMENT_OPERATION: return "DEPLOYMENT_OPERATION";
        case LogEntryType::VERSION_UPDATE:      return "VERSION_UPDATE";
        case LogEntryType::ACCESS_CONTROL:      return "ACCESS_CONTROL";
        case LogEntryType::SECURITY_EVENT:      return "SECURITY_EVENT";
        case LogEntryType::SYSTEM_EVENT:        return "SYSTEM_EVENT";
        default:                                 return "UNKNOWN_TYPE";
    }
}

std::string AuditLogger::severity_to_string(Severity level) const {
    switch (level) {
        case Severity::DEBUG:    return "DEBUG";
        case Severity::INFO:     return "INFO";
        case Severity::WARNING:  return "WARNING";
        case Severity::ERROR:    return "ERROR";
        case Severity::CRITICAL: return "CRITICAL";
        default:                 return "UNKNOWN";
    }
}

AuditLogger::LogEntryType AuditLogger::string_to_entry_type(const std::string& str) const {
    if (str == "INTEGRATION_START") return LogEntryType::INTEGRATION_START;
    if (str == "INTEGRATION_COMPLETE") return LogEntryType::INTEGRATION_COMPLETE;
    if (str == "INTEGRATION_ERROR") return LogEntryType::INTEGRATION_ERROR;
    if (str == "CONFIGURATION_CHANGE") return LogEntryType::CONFIGURATION_CHANGE;
    if (str == "METADATA_UPDATE") return LogEntryType::METADATA_UPDATE;
    if (str == "INTEGRITY_CHECK") return LogEntryType::INTEGRITY_CHECK;
    if (str == "BUILD_OPERATION") return LogEntryType::BUILD_OPERATION;
    if (str == "DEPLOYMENT_OPERATION") return LogEntryType::DEPLOYMENT_OPERATION;
    if (str == "VERSION_UPDATE") return LogEntryType::VERSION_UPDATE;
    if (str == "ACCESS_CONTROL") return LogEntryType::ACCESS_CONTROL;
    if (str == "SECURITY_EVENT") return LogEntryType::SECURITY_EVENT;
    return LogEntryType::SYSTEM_EVENT;
}

AuditLogger::Severity AuditLogger::string_to_severity(const std::string& str) const {
    if (str == "DEBUG") return Severity::DEBUG;
    if (str == "INFO") return Severity::INFO;
    if (str == "WARNING") return Severity::WARNING;
    if (str == "ERROR") return Severity::ERROR;
    if (str == "CRITICAL") return Severity::CRITICAL;
    return Severity::INFO;
}

// AuditSession implementation
AuditSession::AuditSession(AuditLogger& logger,
                           const std::string& user_id,
                           const std::string& session_type)
    : logger_(logger), user_id_(user_id), session_active_(true) {
    session_id_ = logger_.generate_entry_id();
    start_time_ = std::chrono::system_clock::now();

    // Log session start
    std::map<std::string, std::string> details;
    details["session_type"] = session_type;
    details["session_start"] = logger_.format_timestamp(start_time_);

    logger_.log_entry(AuditLogger::LogEntryType::SYSTEM_EVENT,
                      AuditLogger::Severity::INFO,
                      user_id_,
                      session_id_,
                      "session_start",
                      session_type,
                      details);
}

AuditSession::~AuditSession() {
    if (session_active_) {
        end_session();
    }
}

bool AuditSession::log_operation(const std::string& operation,
                                const std::string& resource,
                                const std::map<std::string, std::string>& details) {
    if (!session_active_) {
        return false;
    }

    return logger_.log_entry(AuditLogger::LogEntryType::SYSTEM_EVENT,
                            AuditLogger::Severity::INFO,
                            user_id_,
                            session_id_,
                            operation,
                            resource,
                            details);
}

void AuditSession::end_session() {
    if (!session_active_) {
        return;
    }

    auto end_time = std::chrono::system_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time_);

    std::map<std::string, std::string> details;
    details["session_end"] = logger_.format_timestamp(end_time);
    details["session_duration_seconds"] = std::to_string(duration.count());

    logger_.log_entry(AuditLogger::LogEntryType::SYSTEM_EVENT,
                      AuditLogger::Severity::INFO,
                      user_id_,
                      session_id_,
                      "session_end",
                      "",
                      details);

    session_active_ = false;
}

// Helper function for case conversion
namespace {
    std::string to_lower(const std::string& str) {
        std::string result = str;
        std::transform(result.begin(), result.end(), result.begin(),
                      [](unsigned char c) { return std::tolower(c); });
        return result;
    }
}