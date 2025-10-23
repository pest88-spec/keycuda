// T012: Audit Logging System with Integrity Verification
// Provides comprehensive audit logging for all integration operations

#pragma once

#include <string>
#include <fstream>
#include <map>

struct AuditStatistics {
    int total_operations = 0;
    int successful_operations = 0;
    int failed_operations = 0;
    double success_rate = 0.0;
    std::map<std::string, int> operations_by_type;
};

struct AuditEntry {
    std::string timestamp;
    std::string operation_type;
    std::string library_name;
    std::string description;
    std::string source_path;
    std::string target_path;
    std::string file_path;
    std::string details;
    bool success = false;
    std::string checksum;
};

namespace integration {
namespace audit {

class AuditLogger {
public:
    explicit AuditLogger(const std::string& log_file = "build/integration-audit.log");
    ~AuditLogger();

    // Logging methods
    void log_operation(const AuditEntry& entry);
    void log_extraction(const std::string& library_name,
                       const std::string& source_path,
                       const std::string& target_path,
                       int files_count,
                       bool success);
    void log_attribution(const std::string& file_path,
                        const std::string& attribution_type,
                        const std::string& details);
    void log_integrity_check(const std::string& file_path,
                              const std::string& expected_hash,
                              const std::string& actual_hash,
                              bool passed);

    // Verification
    bool verify_log_integrity() const;

    // Reporting
    void generate_audit_report(const std::string& output_file) const;

    // Configuration
    void set_integrity_file(const std::string& file) { integrity_file_ = file; }

private:
    void write_log_header();
    void write_log_entry(const AuditEntry& entry);
    void write_integrity_entry(const AuditEntry& entry);

    std::string calculate_entry_checksum(const AuditEntry& entry) const;
    std::string calculate_string_hash(const std::string& str) const;
    AuditStatistics calculate_statistics() const;
    std::string get_current_timestamp() const;

    std::string log_file_;
    std::string integrity_file_ = "build/integrity.log";
    std::ofstream log_stream_;
};

} // namespace audit
} // namespace integration