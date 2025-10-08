#include "ComputeCore/gpu/performance/worm_audit_logger.h"
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <random>
#include <openssl/sha.h>
#include <openssl/evp.h>

namespace puzzle71::gpu::performance {

WormAuditLogger::WormAuditLogger(const AuditLogConfig& config)
    : config_(config)
    , current_file_size_(0)
    , total_logging_overhead_(0.0)
    , stop_verification_(false)
    , last_integrity_status_(IntegrityStatus::UNKNOWN) {

    start_time_ = std::chrono::steady_clock::now();

    // Initialize OpenSSL
    OpenSSL_add_all_algorithms();

    // Initialize log file
    if (!InitializeLogFile()) {
        throw std::runtime_error("Failed to initialize audit log file");
    }

    // Start background verification if enabled
    if (config_.enable_real_time_verification) {
        verification_thread_ = std::thread(&WormAuditLogger::VerificationLoop, this);
    }
}

WormAuditLogger::~WormAuditLogger() {
    // Stop verification thread
    stop_verification_ = true;
    if (verification_thread_.joinable()) {
        verification_thread_.join();
    }

    // Close log file
    if (log_file_.is_open()) {
        log_file_.close();
    }

    // Cleanup OpenSSL
    EVP_cleanup();
}

std::string WormAuditLogger::LogOperation(const AuditLogEntry& entry) {
    std::lock_guard<std::mutex> lock(log_mutex_);

    auto start_time = std::chrono::high_resolution_clock::now();

    // Validate entry format
    if (!ValidateEntryFormat(entry)) {
        throw std::invalid_argument("Invalid audit log entry format");
    }

    // Create mutable copy and add system-generated fields
    AuditLogEntry log_entry = entry;
    if (log_entry.entry_id.empty()) {
        log_entry.entry_id = GenerateEntryId();
    }

    if (log_entry.timestamp == std::chrono::system_clock::time_point{}) {
        log_entry.timestamp = std::chrono::system_clock::now();
    }

    // Calculate entry hash
    log_entry.entry_hash = log_entry.CalculateEntryHash();
    log_entry.previous_entry_hash = last_entry_hash_;
    log_entry.chain_hash = CalculateChainHash(log_entry.entry_hash);

    // Check performance budget
    if (!IsWithinPerformanceBudget()) {
        // Skip logging if it would exceed performance budget
        return log_entry.entry_id;
    }

    // Write to file
    WriteEntryToFile(log_entry);

    // Update internal state
    last_entry_hash_ = log_entry.entry_hash;
    entry_cache_[log_entry.entry_id] = log_entry;
    entry_chain_.push_back(log_entry.entry_id);

    // Update performance metrics
    auto end_time = std::chrono::high_resolution_clock::now();
    auto logging_time = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    UpdatePerformanceMetrics(logging_time);

    return log_entry.entry_id;
}

std::string WormAuditLogger::LogKernelLaunch(const std::string& kernel_name,
                                            const json& launch_params,
                                            const json& performance_data,
                                            const std::string& operator_id) {
    AuditLogEntry entry;
    entry.level = LogLevel::INFO;
    entry.operation_type = OperationType::KERNEL_LAUNCH;
    entry.operation_description = "Kernel launch: " + kernel_name;
    entry.operator_id = operator_id;
    entry.operation_parameters = launch_params;
    entry.performance_metrics = performance_data;
    entry.successful = true;

    return LogOperation(entry);
}

std::string WormAuditLogger::LogMemoryOperation(const std::string& operation_type,
                                               size_t size_bytes,
                                               const std::string& device_id,
                                               const std::string& operator_id) {
    AuditLogEntry entry;
    entry.level = LogLevel::INFO;
    entry.operation_type = OperationType::MEMORY_OPERATION;
    entry.operation_description = "Memory operation: " + operation_type;
    entry.operator_id = operator_id;
    entry.gpu_device_id = device_id;
    entry.operation_parameters["operation"] = operation_type;
    entry.operation_parameters["size_bytes"] = size_bytes;
    entry.successful = true;

    return LogOperation(entry);
}

std::string WormAuditLogger::LogPerformanceOptimization(const std::string& optimization_name,
                                                      const json& before_metrics,
                                                      const json& after_metrics,
                                                      const std::string& operator_id) {
    AuditLogEntry entry;
    entry.level = LogLevel::INFO;
    entry.operation_type = OperationType::PERFORMANCE_OPTIMIZATION;
    entry.operation_description = "Performance optimization: " + optimization_name;
    entry.operator_id = operator_id;
    entry.operation_parameters["optimization_name"] = optimization_name;
    entry.operation_parameters["before_metrics"] = before_metrics;
    entry.operation_parameters["after_metrics"] = after_metrics;
    entry.successful = true;

    return LogOperation(entry);
}

std::string WormAuditLogger::LogError(const std::string& error_type,
                                     const std::string& error_message,
                                     const json& context,
                                     const std::string& operator_id) {
    AuditLogEntry entry;
    entry.level = LogLevel::ERROR;
    entry.operation_type = OperationType::ERROR_RECOVERY;
    entry.operation_description = "Error: " + error_type;
    entry.operator_id = operator_id;
    entry.error_message = error_message;
    entry.operation_parameters = context;
    entry.successful = false;

    return LogOperation(entry);
}

bool WormAuditLogger::UpdateConfiguration(const AuditLogConfig& new_config) {
    std::lock_guard<std::mutex> lock(log_mutex_);

    // Validate configuration
    if (new_config.max_log_file_size_mb == 0 || new_config.max_log_files == 0) {
        return false;
    }

    config_ = new_config;

    // Reinitialize log file if path changed
    if (new_config.log_file_path != current_log_file_path_) {
        if (log_file_.is_open()) {
            log_file_.close();
        }
        return InitializeLogFile();
    }

    return true;
}

AuditLogConfig WormAuditLogger::GetCurrentConfiguration() const {
    std::lock_guard<std::mutex> lock(log_mutex_);
    return config_;
}

bool WormAuditLogger::SetLogLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    config_.minimum_log_level = level;
    return true;
}

void WormAuditLogger::EnableRealTimeVerification(bool enabled) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    config_.enable_real_time_verification = enabled;
}

void WormAuditLogger::SetPerformanceOverheadLimit(double max_overhead_percent) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    config_.max_logging_overhead_percent = max_overhead_percent;
}

IntegrityVerificationResult WormAuditLogger::VerifyLogIntegrity() const {
    std::lock_guard<std::mutex> lock(log_mutex_);

    IntegrityVerificationResult result;
    result.total_entries_verified = entry_chain_.size();
    result.valid_entries = 0;
    result.tampered_entries = 0;
    result.corrupted_entries = 0;
    result.verification_timestamp = std::chrono::system_clock::now();

    // Verify entry chain integrity
    if (!VerifyEntryChain()) {
        result.chain_integrity_valid = false;
        result.verification_score = 0.0;
        return result;
    }

    // Verify individual entries
    std::string previous_hash = "";
    for (const auto& entry_id : entry_chain_) {
        auto it = entry_cache_.find(entry_id);
        if (it == entry_cache_.end()) {
            result.corrupted_entries++;
            result.integrity_violations.push_back("Missing entry: " + entry_id);
            continue;
        }

        const auto& entry = it->second;

        // Verify hash chain
        if (!previous_hash.empty() && entry.previous_entry_hash != previous_hash) {
            result.tampered_entries++;
            result.tampered_entry_ids.push_back(entry_id);
            result.integrity_violations.push_back("Hash chain broken at entry: " + entry_id);
        }

        // Verify entry hash
        std::string calculated_hash = entry.CalculateEntryHash();
        if (calculated_hash != entry.entry_hash) {
            result.tampered_entries++;
            result.tampered_entry_ids.push_back(entry_id);
            result.integrity_violations.push_back("Entry hash mismatch: " + entry_id);
        } else {
            result.valid_entries++;
        }

        previous_hash = entry.entry_hash;
    }

    result.chain_integrity_valid = (result.tampered_entries == 0 && result.corrupted_entries == 0);
    result.verification_score = static_cast<double>(result.valid_entries) / result.total_entries_verified;

    return result;
}

bool WormAuditLogger::VerifyEntryIntegrity(const std::string& entry_id) const {
    std::lock_guard<std::mutex> lock(log_mutex_);

    auto it = entry_cache_.find(entry_id);
    if (it == entry_cache_.end()) {
        return false;
    }

    const auto& entry = it->second;
    std::string calculated_hash = entry.CalculateEntryHash();
    return calculated_hash == entry.entry_hash;
}

std::vector<std::string> WormAuditLogger::DetectTampering() const {
    auto verification_result = VerifyLogIntegrity();
    return verification_result.tampered_entry_ids;
}

bool WormAuditLogger::RestoreFromBackup() const {
    // Implementation would depend on backup strategy
    // This is a placeholder for backup restoration
    return false;
}

bool WormAuditLogger::RotateLogFile() {
    std::lock_guard<std::mutex> lock(log_mutex_);
    return PerformLogRotation();
}

bool WormAuditLogger::CompactLogs() {
    std::lock_guard<std::mutex> lock(log_mutex_);

    // Remove old entries beyond retention policy
    size_t max_entries = config_.max_log_files * (config_.max_log_file_size_mb * 1024 * 1024 / 1024); // Rough estimate

    if (entry_chain_.size() > max_entries) {
        size_t entries_to_remove = entry_chain_.size() - max_entries;
        for (size_t i = 0; i < entries_to_remove; ++i) {
            const auto& entry_id = entry_chain_[i];
            entry_cache_.erase(entry_id);
        }
        entry_chain_.erase(entry_chain_.begin(), entry_chain_.begin() + entries_to_remove);
        return true;
    }

    return false;
}

std::vector<AuditLogEntry> WormAuditLogger::GetEntriesByTimeRange(
    std::chrono::system_clock::time_point start,
    std::chrono::system_clock::time_point end) const {

    std::lock_guard<std::mutex> lock(log_mutex_);

    std::vector<AuditLogEntry> entries;
    for (const auto& [entry_id, entry] : entry_cache_) {
        if (entry.timestamp >= start && entry.timestamp <= end) {
            entries.push_back(entry);
        }
    }

    return entries;
}

std::vector<AuditLogEntry> WormAuditLogger::GetEntriesByOperator(const std::string& operator_id) const {
    std::lock_guard<std::mutex> lock(log_mutex_);

    std::vector<AuditLogEntry> entries;
    for (const auto& [entry_id, entry] : entry_cache_) {
        if (entry.operator_id == operator_id) {
            entries.push_back(entry);
        }
    }

    return entries;
}

std::vector<AuditLogEntry> WormAuditLogger::GetEntriesByOperationType(OperationType type) const {
    std::lock_guard<std::mutex> lock(log_mutex_);

    std::vector<AuditLogEntry> entries;
    for (const auto& [entry_id, entry] : entry_cache_) {
        if (entry.operation_type == type) {
            entries.push_back(entry);
        }
    }

    return entries;
}

double WormAuditLogger::GetAverageLoggingOverhead() const {
    std::lock_guard<std::mutex> lock(log_mutex_);

    if (logging_latencies_.empty()) {
        return 0.0;
    }

    auto total_time = std::accumulate(logging_latencies_.begin(), logging_latencies_.end(),
        std::chrono::microseconds(0));

    auto avg_latency = total_time / logging_latencies_.size();
    auto total_runtime = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start_time_);

    return (static_cast<double>(avg_latency.count()) / total_runtime.count()) * 100.0;
}

std::chrono::microseconds WormAuditLogger::GetMaxLoggingLatency() const {
    std::lock_guard<std::mutex> lock(log_mutex_);

    if (logging_latencies_.empty()) {
        return std::chrono::microseconds(0);
    }

    return *std::max_element(logging_latencies_.begin(), logging_latencies_.end());
}

size_t WormAuditLogger::GetTotalLogEntries() const {
    std::lock_guard<std::mutex> lock(log_mutex_);
    return entry_cache_.size();
}

size_t WormAuditLogger::GetCurrentLogFileSize() const {
    return current_file_size_;
}

std::vector<AuditLogEntry> WormAuditLogger::SearchLogs(const std::string& query) const {
    std::lock_guard<std::mutex> lock(log_mutex_);

    std::vector<AuditLogEntry> results;
    std::string lower_query = query;
    std::transform(lower_query.begin(), lower_query.end(), lower_query.begin(), ::tolower);

    for (const auto& [entry_id, entry] : entry_cache_) {
        std::string search_text = entry.operation_description + " " + entry.entry_id;
        if (entry.operator_id.find(query) != std::string::npos) {
            search_text += " " + entry.operator_id;
        }
        if (!entry.error_message.empty()) {
            search_text += " " + entry.error_message;
        }

        std::transform(search_text.begin(), search_text.end(), search_text.begin(), ::tolower);
        if (search_text.find(lower_query) != std::string::npos) {
            results.push_back(entry);
        }
    }

    return results;
}

std::map<std::string, size_t> WormAuditLogger::GetOperationStatistics() const {
    std::lock_guard<std::mutex> lock(log_mutex_);

    std::map<std::string, size_t> stats;
    for (const auto& [entry_id, entry] : entry_cache_) {
        std::string op_type;
        switch (entry.operation_type) {
            case OperationType::KERNEL_LAUNCH: op_type = "kernel_launch"; break;
            case OperationType::MEMORY_OPERATION: op_type = "memory_operation"; break;
            case OperationType::SYNCHRONIZATION: op_type = "synchronization"; break;
            case OperationType::PERFORMANCE_OPTIMIZATION: op_type = "performance_optimization"; break;
            case OperationType::ERROR_RECOVERY: op_type = "error_recovery"; break;
            case OperationType::CONFIGURATION_CHANGE: op_type = "configuration_change"; break;
            case OperationType::ACCURACY_VALIDATION: op_type = "accuracy_validation"; break;
            case OperationType::SYSTEM_EVENT: op_type = "system_event"; break;
        }
        stats[op_type]++;
    }

    return stats;
}

std::map<LogLevel, size_t> WormAuditLogger::GetLogLevelDistribution() const {
    std::lock_guard<std::mutex> lock(log_mutex_);

    std::map<LogLevel, size_t> distribution;
    for (const auto& [entry_id, entry] : entry_cache_) {
        distribution[entry.level]++;
    }

    return distribution;
}

std::vector<std::string> WormAuditLogger::GetCriticalErrors() const {
    std::lock_guard<std::mutex> lock(log_mutex_);

    std::vector<std::string> critical_errors;
    for (const auto& [entry_id, entry] : entry_cache_) {
        if (entry.level == LogLevel::CRITICAL && !entry.error_message.empty()) {
            critical_errors.push_back(entry.error_message);
        }
    }

    return critical_errors;
}

bool WormAuditLogger::ExportLogs(const std::string& export_path, const std::string& format) const {
    std::lock_guard<std::mutex> lock(log_mutex_);

    try {
        std::ofstream file(export_path);
        if (!file.is_open()) {
            return false;
        }

        if (format == "json") {
            json export_data = json::array();
            for (const auto& entry_id : entry_chain_) {
                auto it = entry_cache_.find(entry_id);
                if (it != entry_cache_.end()) {
                    export_data.push_back(EntryToJson(it->second));
                }
            }
            file << export_data.dump(4);
        } else {
            // Human-readable format
            for (const auto& entry_id : entry_chain_) {
                auto it = entry_cache_.find(entry_id);
                if (it != entry_cache_.end()) {
                    file << it->second.ToJsonString() << std::endl;
                }
            }
        }

        return file.good();
    } catch (...) {
        return false;
    }
}

bool WormAuditLogger::CreateBackup() const {
    std::lock_guard<std::mutex> lock(log_mutex_);

    std::string backup_path = current_log_file_path_ + ".backup";
    return std::filesystem::copy_file(current_log_file_path_, backup_path);
}

bool WormAuditLogger::RestoreFromBackup(const std::string& backup_path) {
    std::lock_guard<std::mutex> lock(log_mutex_);

    if (!std::filesystem::exists(backup_path)) {
        return false;
    }

    // Close current file
    if (log_file_.is_open()) {
        log_file_.close();
    }

    // Copy backup to current location
    if (!std::filesystem::copy_file(backup_path, current_log_file_path_)) {
        return false;
    }

    // Reopen file
    return InitializeLogFile();
}

// Private methods

bool WormAuditLogger::InitializeLogFile() {
    current_log_file_path_ = config_.log_file_path;
    log_file_.open(current_log_file_path_, std::ios::app);
    if (!log_file_.is_open()) {
        return false;
    }

    // Get current file size
    log_file_.seekp(0, std::ios::end);
    current_file_size_ = log_file_.tellp();

    return true;
}

void WormAuditLogger::WriteEntryToFile(const AuditLogEntry& entry) {
    std::string serialized_entry = SerializeEntry(entry);
    log_file_ << serialized_entry << std::endl;
    log_file_.flush();

    current_file_size_ += serialized_entry.length() + 1; // +1 for newline

    // Check if rotation is needed
    if (ShouldRotateLogFile()) {
        PerformLogRotation();
    }
}

std::string WormAuditLogger::GenerateEntryId() const {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);

    std::ostringstream oss;
    oss << "entry_";
    for (int i = 0; i < 16; ++i) {
        oss << std::hex << dis(gen);
    }

    auto now = std::chrono::system_clock::now();
    oss << "_" << std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    return oss.str();
}

std::string WormAuditLogger::CalculateChainHash(const std::string& current_entry_hash) const {
    std::string combined = last_entry_hash_ + current_entry_hash;

    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(combined.c_str()), combined.length(), hash);

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        oss << std::setw(2) << static_cast<int>(hash[i]);
    }

    return oss.str();
}

bool WormAuditLogger::ValidateEntryFormat(const AuditLogEntry& entry) const {
    if (entry.operation_description.empty()) {
        return false;
    }

    if (entry.timestamp == std::chrono::system_clock::time_point{}) {
        return false;
    }

    return true;
}

void WormAuditLogger::UpdatePerformanceMetrics(std::chrono::microseconds logging_time) {
    logging_latencies_.push_back(logging_time);

    // Keep only last 1000 measurements to avoid memory growth
    if (logging_latencies_.size() > 1000) {
        logging_latencies_.erase(logging_latencies_.begin(), logging_latencies_.begin() + 100);
    }

    // Calculate overhead percentage
    auto total_runtime = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start_time_);
    total_logging_overhead_ = (static_cast<double>(logging_time.count()) / total_runtime.count()) * 100.0;
}

bool WormAuditLogger::IsWithinPerformanceBudget() const {
    return total_logging_overhead_ <= config_.max_logging_overhead_percent;
}

void WormAuditLogger::VerificationLoop() {
    while (!stop_verification_) {
        std::this_thread::sleep_for(config_.verification_interval);

        auto result = VerifyLogIntegrity();
        if (!result.chain_integrity_valid) {
            last_integrity_status_ = IntegrityStatus::TAMPERED;
            // Log integrity violation
            LogError("INTEGRITY_VIOLATION", "Log tampering detected", {
                {"tampered_entries", static_cast<size_t>(result.tampered_entries)},
                {"corrupted_entries", static_cast<size_t>(result.corrupted_entries)}
            });
        } else {
            last_integrity_status_ = IntegrityStatus::VALID;
        }
    }
}

bool WormAuditLogger::VerifyEntryChain() const {
    if (entry_chain_.empty()) {
        return true;
    }

    std::string previous_hash = "";
    for (const auto& entry_id : entry_chain_) {
        auto it = entry_cache_.find(entry_id);
        if (it == entry_cache_.end()) {
            return false;
        }

        const auto& entry = it->second;
        if (!previous_hash.empty() && entry.previous_entry_hash != previous_hash) {
            return false;
        }

        previous_hash = entry.entry_hash;
    }

    return true;
}

bool WormAuditLogger::ShouldRotateLogFile() const {
    return current_file_size_ >= (config_.max_log_file_size_mb * 1024 * 1024);
}

bool WormAuditLogger::PerformLogRotation() {
    if (log_file_.is_open()) {
        log_file_.close();
    }

    // Generate new log file name
    size_t sequence_number = 1;
    std::string new_log_path = GenerateLogFileName(sequence_number);
    while (std::filesystem::exists(new_log_path)) {
        sequence_number++;
        new_log_path = GenerateLogFileName(sequence_number);
    }

    // Move current file to archive name
    std::string archive_path = GenerateLogFileName(0);
    if (std::filesystem::exists(current_log_file_path_)) {
        std::filesystem::rename(current_log_file_path_, archive_path);
    }

    // Create new log file
    current_log_file_path_ = new_log_path;
    log_file_.open(current_log_file_path_);
    current_file_size_ = 0;

    return log_file_.is_open();
}

std::string WormAuditLogger::GenerateLogFileName(size_t sequence_number) const {
    auto path = std::filesystem::path(config_.log_file_path);
    std::string stem = path.stem().string();
    std::string extension = path.extension().string();

    if (sequence_number == 0) {
        return path.parent_path() / (stem + "_" + std::to_string(std::time(nullptr)) + extension);
    } else {
        return path.parent_path() / (stem + "_" + std::to_string(sequence_number) + extension);
    }
}

std::string WormAuditLogger::SerializeEntry(const AuditLogEntry& entry) const {
    return entry.ToJsonString();
}

json WormAuditLogger::EntryToJson(const AuditLogEntry& entry) const {
    json j;
    j["entry_id"] = entry.entry_id;
    j["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        entry.timestamp.time_since_epoch()).count();
    j["level"] = static_cast<int>(entry.level);
    j["operation_type"] = static_cast<int>(entry.operation_type);
    j["operation_description"] = entry.operation_description;
    j["operator_id"] = entry.operator_id;
    j["session_id"] = entry.session_id;
    j["gpu_device_id"] = entry.gpu_device_id;
    j["operation_parameters"] = entry.operation_parameters;
    j["performance_metrics"] = entry.performance_metrics;
    j["system_state"] = entry.system_state;
    j["entry_hash"] = entry.entry_hash;
    j["previous_entry_hash"] = entry.previous_entry_hash;
    j["chain_hash"] = entry.chain_hash;
    j["metadata"] = entry.metadata;
    j["execution_time_us"] = entry.execution_time.count();
    j["successful"] = entry.successful;
    j["error_message"] = entry.error_message;

    return j;
}

AuditLogEntry WormAuditLogger::JsonToEntry(const json& j) const {
    AuditLogEntry entry;
    entry.entry_id = j.value("entry_id", "");
    entry.timestamp = std::chrono::system_clock::from_time_t(
        j.value("timestamp", static_cast<int64_t>(0)) / 1000);
    entry.level = static_cast<LogLevel>(j.value("level", 0));
    entry.operation_type = static_cast<OperationType>(j.value("operation_type", 0));
    entry.operation_description = j.value("operation_description", "");
    entry.operator_id = j.value("operator_id", "");
    entry.session_id = j.value("session_id", "");
    entry.gpu_device_id = j.value("gpu_device_id", "");
    entry.operation_parameters = j.value("operation_parameters", json::object());
    entry.performance_metrics = j.value("performance_metrics", json::object());
    entry.system_state = j.value("system_state", json::object());
    entry.entry_hash = j.value("entry_hash", "");
    entry.previous_entry_hash = j.value("previous_entry_hash", "");
    entry.chain_hash = j.value("chain_hash", "");
    entry.metadata = j.value("metadata", std::map<std::string, std::string>());
    entry.execution_time = std::chrono::microseconds(j.value("execution_time_us", 0));
    entry.successful = j.value("successful", true);
    entry.error_message = j.value("error_message", "");

    return entry;
}

// AuditLogEntry implementation

std::string AuditLogEntry::ToJsonString() const {
    json j = json::object();
    j["entry_id"] = entry_id;
    j["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        timestamp.time_since_epoch()).count();
    j["level"] = static_cast<int>(level);
    j["operation_type"] = static_cast<int>(operation_type);
    j["operation_description"] = operation_description;
    j["operator_id"] = operator_id;
    j["session_id"] = session_id;
    j["gpu_device_id"] = gpu_device_id;
    j["operation_parameters"] = operation_parameters;
    j["performance_metrics"] = performance_metrics;
    j["system_state"] = system_state;
    j["entry_hash"] = entry_hash;
    j["previous_entry_hash"] = previous_entry_hash;
    j["chain_hash"] = chain_hash;
    j["metadata"] = metadata;
    j["execution_time_us"] = execution_time.count();
    j["successful"] = successful;
    j["error_message"] = error_message;

    return j.dump();
}

bool AuditLogEntry::FromJsonString(const std::string& json_string) {
    try {
        json j = json::parse(json_string);

        entry_id = j.value("entry_id", "");
        timestamp = std::chrono::system_clock::from_time_t(
            j.value("timestamp", static_cast<int64_t>(0)) / 1000);
        level = static_cast<LogLevel>(j.value("level", 0));
        operation_type = static_cast<OperationType>(j.value("operation_type", 0));
        operation_description = j.value("operation_description", "");
        operator_id = j.value("operator_id", "");
        session_id = j.value("session_id", "");
        gpu_device_id = j.value("gpu_device_id", "");
        operation_parameters = j.value("operation_parameters", json::object());
        performance_metrics = j.value("performance_metrics", json::object());
        system_state = j.value("system_state", json::object());
        entry_hash = j.value("entry_hash", "");
        previous_entry_hash = j.value("previous_entry_hash", "");
        chain_hash = j.value("chain_hash", "");
        metadata = j.value("metadata", std::map<std::string, std::string>());
        execution_time = std::chrono::microseconds(j.value("execution_time_us", 0));
        successful = j.value("successful", true);
        error_message = j.value("error_message", "");

        return true;
    } catch (...) {
        return false;
    }
}

std::string AuditLogEntry::CalculateEntryHash() const {
    std::string content = entry_id + operation_description + operator_id +
                         operation_parameters.dump() + performance_metrics.dump();

    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(content.c_str()), content.length(), hash);

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        oss << std::setw(2) << static_cast<int>(hash[i]);
    }

    return oss.str();
}

// ScopedAuditLogger implementation

ScopedAuditLogger::ScopedAuditLogger(WormAuditLogger& logger,
                                     OperationType operation_type,
                                     const std::string& operation_description,
                                     const std::string& operator_id)
    : logger_(logger), completed_(false) {

    entry_.operation_type = operation_type;
    entry_.operation_description = operation_description;
    entry_.operator_id = operator_id;
    entry_.timestamp = std::chrono::system_clock::now();
    start_time_ = std::chrono::high_resolution_clock::now();
}

ScopedAuditLogger::~ScopedAuditLogger() {
    if (!completed_) {
        auto end_time = std::chrono::high_resolution_clock::now();
        entry_.execution_time = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time_);
        entry_.successful = true;
        completed_ = true;
    }

    logger_.LogOperation(entry_);
}

void ScopedAuditLogger::SetSuccess(bool successful) {
    entry_.successful = successful;
}

void ScopedAuditLogger::SetError(const std::string& error_message) {
    entry_.successful = false;
    entry_.error_message = error_message;
}

void ScopedAuditLogger::AddMetadata(const std::string& key, const std::string& value) {
    entry_.metadata[key] = value;
}

void ScopedAuditLogger::AddPerformanceMetric(const std::string& metric, double value) {
    entry_.performance_metrics[metric] = value;
}

// Factory function

std::unique_ptr<WormAuditLogger> CreateWormAuditLogger(
    const std::string& log_file_path,
    LogLevel minimum_level,
    bool enable_real_time_verification) {

    AuditLogConfig config;
    config.log_file_path = log_file_path;
    config.minimum_log_level = minimum_level;
    config.enable_real_time_verification = enable_real_time_verification;

    return std::make_unique<WormAuditLogger>(config);
}

} // namespace puzzle71::gpu::performance