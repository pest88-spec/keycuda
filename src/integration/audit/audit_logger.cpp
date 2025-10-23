// T012: Audit Logging System with Integrity Verification
// Provides comprehensive audit logging for all integration operations

#include "audit_logger.h"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <vector>
#include <iostream>
#include <chrono>

namespace integration {
namespace audit {

AuditLogger::AuditLogger(const std::string& log_file)
    : log_file_(log_file) {
    // Open log file in append mode
    log_stream_.open(log_file_, std::ios::app);
    if (!log_stream_.is_open()) {
        throw std::runtime_error("Cannot open audit log file: " + log_file_);
    }

    // Write log header if file is new
    std::ifstream check_file(log_file_);
    if (check_file.peek() == std::ifstream::traits_type::eof()) {
        write_log_header();
    }
}

AuditLogger::~AuditLogger() {
    if (log_stream_.is_open()) {
        log_stream_.close();
    }
}

void AuditLogger::log_operation(const AuditEntry& entry) {
    // Add checksum for integrity verification
    AuditEntry entry_with_checksum = entry;
    entry_with_checksum.checksum = calculate_entry_checksum(entry);

    // Write to log file
    write_log_entry(entry_with_checksum);

    // Also write to integrity log for verification
    write_integrity_entry(entry_with_checksum);
}

void AuditLogger::log_extraction(const std::string& library_name,
                                const std::string& source_path,
                                const std::string& target_path,
                                int files_count,
                                bool success) {
    AuditEntry entry;
    entry.timestamp = get_current_timestamp();
    entry.operation_type = "EXTRACTION";
    entry.library_name = library_name;
    entry.description = "Extracted " + std::to_string(files_count) + " files";
    entry.source_path = source_path;
    entry.target_path = target_path;
    entry.success = success;

    log_operation(entry);
}

void AuditLogger::log_attribution(const std::string& file_path,
                                  const std::string& attribution_type,
                                  const std::string& details) {
    AuditEntry entry;
    entry.timestamp = get_current_timestamp();
    entry.operation_type = "ATTRIBUTION";
    entry.file_path = file_path;
    entry.description = attribution_type + ": " + details;
    entry.success = true;

    log_operation(entry);
}

void AuditLogger::log_integrity_check(const std::string& file_path,
                                      const std::string& expected_hash,
                                      const std::string& actual_hash,
                                      bool passed) {
    AuditEntry entry;
    entry.timestamp = get_current_timestamp();
    entry.operation_type = "INTEGRITY_CHECK";
    entry.file_path = file_path;
    entry.description = "SHA-256 verification";
    entry.details = "Expected: " + expected_hash + ", Actual: " + actual_hash;
    entry.success = passed;

    log_operation(entry);
}

bool AuditLogger::verify_log_integrity() const {
    std::ifstream integrity_file(integrity_file_);
    if (!integrity_file.is_open()) {
        return false;
    }

    std::string line;
    while (std::getline(integrity_file, line)) {
        // Parse integrity entry and verify checksum
        size_t checksum_pos = line.find_last_of('|');
        if (checksum_pos != std::string::npos) {
            std::string entry_data = line.substr(0, checksum_pos);
            std::string stored_checksum = line.substr(checksum_pos + 1);

            std::string calculated_checksum = calculate_string_hash(entry_data);
            if (calculated_checksum != stored_checksum) {
                return false; // Integrity check failed
            }
        }
    }

    return true;
}

void AuditLogger::generate_audit_report(const std::string& output_file) const {
    std::ofstream report(output_file);
    if (!report.is_open()) {
        return;
    }

    report << "# Integration Audit Report\n\n";
    report << "Generated: " << get_current_timestamp() << "\n";
    report << "Log file: " << log_file_ << "\n";
    report << "Integrity verified: " << (verify_log_integrity() ? "PASS" : "FAIL") << "\n\n";

    // Statistics
    auto stats = calculate_statistics();
    report << "## Statistics\n";
    report << "- Total operations: " << stats.total_operations << "\n";
    report << "- Successful operations: " << stats.successful_operations << "\n";
    report << "- Failed operations: " << stats.failed_operations << "\n";
    report << "- Success rate: " << std::fixed << std::setprecision(2)
           << (stats.success_rate * 100) << "%\n\n";

    // Operations by type
    report << "## Operations by Type\n";
    for (const auto& pair : stats.operations_by_type) {
        report << "- " << pair.first << ": " << pair.second << "\n";
    }
}

void AuditLogger::write_log_header() {
    log_stream_ << "# Integration Audit Log\n";
    log_stream_ << "# Created: " << get_current_timestamp() << "\n";
    log_stream_ << "# Format: TIMESTAMP | TYPE | LIBRARY | STATUS | DESCRIPTION | DETAILS | CHECKSUM\n";
    log_stream_ << "#--------------------------------------------------------------------------------\n";
}

void AuditLogger::write_log_entry(const AuditEntry& entry) {
    log_stream_ << entry.timestamp << " | "
                 << entry.operation_type << " | "
                 << entry.library_name << " | "
                 << (entry.success ? "SUCCESS" : "FAILED") << " | "
                 << entry.description << " | "
                 << entry.details << " | "
                 << entry.checksum << "\n";

    log_stream_.flush(); // Ensure immediate write
}

void AuditLogger::write_integrity_entry(const AuditEntry& entry) {
    std::ofstream integrity_stream(integrity_file_, std::ios::app);
    if (integrity_stream.is_open()) {
        // Create compact entry for integrity checking
        std::string entry_str = entry.timestamp + "|" +
                               entry.operation_type + "|" +
                               entry.library_name + "|" +
                               (entry.success ? "1" : "0") + "|" +
                               entry.description;

        integrity_stream << entry_str << "|" << entry.checksum << "\n";
        integrity_stream.close();
    }
}

std::string AuditLogger::calculate_entry_checksum(const AuditEntry& entry) const {
    std::string entry_data = entry.timestamp + "|" +
                           entry.operation_type + "|" +
                           entry.library_name + "|" +
                           (entry.success ? "1" : "0") + "|" +
                           entry.description + "|" +
                           entry.details;

    return calculate_string_hash(entry_data);
}

std::string AuditLogger::calculate_string_hash(const std::string& str) const {
    std::hash<std::string> hasher;
    auto hash_value = hasher(str);

    std::stringstream ss;
    ss << std::hex << hash_value;
    return ss.str();
}

AuditStatistics AuditLogger::calculate_statistics() const {
    AuditStatistics stats;

    std::ifstream log_file(log_file_);
    std::string line;

    while (std::getline(log_file, line)) {
        if (line.empty() || line[0] == '#') continue; // Skip headers

        std::stringstream ss(line);
        std::string token;
        std::vector<std::string> tokens;

        while (std::getline(ss, token, '|')) {
            // Trim whitespace
            token.erase(0, token.find_first_not_of(" \t"));
            token.erase(token.find_last_not_of(" \t") + 1);
            tokens.push_back(token);
        }

        if (tokens.size() >= 4) {
            stats.total_operations++;
            if (tokens[3] == "SUCCESS") {
                stats.successful_operations++;
            } else {
                stats.failed_operations++;
            }

            if (tokens.size() >= 2) {
                stats.operations_by_type[tokens[1]]++;
            }
        }
    }

    stats.success_rate = stats.total_operations > 0 ?
        static_cast<double>(stats.successful_operations) / stats.total_operations : 0.0;

    return stats;
}

std::string AuditLogger::get_current_timestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%d %H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

} // namespace audit
} // namespace integration