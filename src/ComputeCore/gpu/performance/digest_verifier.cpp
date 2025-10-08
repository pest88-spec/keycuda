#include "ComputeCore/gpu/performance/digest_verifier.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <thread>
#include <atomic>
#include <filesystem>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/err.h>

namespace puzzle71::gpu::performance {

DigestVerifier::DigestVerifier(const VerificationConfig& config)
    : config_(config)
    , total_verification_time_(std::chrono::microseconds(0))
    , successful_verifications_(0)
    , failed_verifications_(0)
    , monitoring_active_(false)
    , stop_monitoring_(false) {

    // Initialize OpenSSL
    OpenSSL_add_all_algorithms();
    ERR_load_crypto_strings();

    // Set default configuration
    if (config_.max_verification_time.count() == 0) {
        config_.max_verification_time = std::chrono::milliseconds(250);
    }

    if (config_.thread_pool_size == 0) {
        config_.thread_pool_size = std::thread::hardware_concurrency();
    }

    if (config_.cache_expiry_time.count() == 0) {
        config_.cache_expiry_time = std::chrono::minutes(30);
    }
}

DigestVerifier::~DigestVerifier() {
    StopMonitoring();
    if (monitoring_thread_.joinable()) {
        monitoring_thread_.join();
    }

    // Cleanup OpenSSL
    EVP_cleanup();
    ERR_free_strings();
}

DigestRecord DigestVerifier::VerifyArtifact(const std::string& artifact_path, const std::string& expected_digest) {
    std::lock_guard<std::mutex> lock(verifier_mutex_);

    DigestRecord record;
    record.artifact_path = artifact_path;
    record.artifact_type = DetermineArtifactType(artifact_path);
    record.creation_time = std::chrono::system_clock::now();
    record.status = VerificationStatus::ERROR;

    auto start_time = std::chrono::high_resolution_clock::now();

    try {
        // Check if file exists
        if (!std::filesystem::exists(artifact_path)) {
            record.status = VerificationStatus::MISSING;
            record.verification_error = "File does not exist";
            return record;
        }

        // Get file size
        record.file_size_bytes = GetFileSize(artifact_path);

        // Calculate SHA-256 digest
        std::string calculated_digest = CalculateSHA256Digest(artifact_path);
        record.sha256_digest = calculated_digest;

        // Compare with expected digest if provided
        if (!expected_digest.empty()) {
            if (calculated_digest == expected_digest) {
                record.status = VerificationStatus::VERIFIED;
                successful_verifications_++;
            } else {
                record.status = VerificationStatus::FAILED;
                record.verification_error = "Digest mismatch";
                failed_verifications_++;
            }
        } else {
            // Just calculate digest without verification
            record.status = VerificationStatus::VERIFIED;
            successful_verifications_++;
        }

        // Check against stored digest if available
        auto it = digest_database_.find(artifact_path);
        if (it != digest_database_.end()) {
            if (it->second.sha256_digest != calculated_digest) {
                record.status = VerificationStatus::FAILED;
                record.verification_error = "Digest differs from stored value";
                tampered_files_.push_back(artifact_path);
            }
        }

    } catch (const std::exception& e) {
        record.status = VerificationStatus::ERROR;
        record.verification_error = std::string("Exception: ") + e.what();
        failed_verifications_++;
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    record.verification_duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    record.verification_time = end_time;

    // Update performance metrics
    verification_times_[artifact_path] = record.verification_duration;
    total_verification_time_ += record.verification_duration;

    // Check SLA compliance
    if (ExceedsSLA(record.verification_duration)) {
        record.status = VerificationStatus::TIMEOUT;
    }

    return record;
}

VerificationStatus DigestVerifier::VerifyDigestMatch(const std::string& artifact_path, const std::string& expected_digest) {
    auto record = VerifyArtifact(artifact_path, expected_digest);
    return record.status;
}

std::string DigestVerifier::CalculateSHA256Digest(const std::string& artifact_path) {
    // Check cache first
    if (config_.enable_caching && IsCached(artifact_path)) {
        std::string cached_digest = GetCachedDigest(artifact_path);
        if (!cached_digest.empty()) {
            return cached_digest;
        }
    }

    // Calculate digest
    std::string digest = ComputeSHA256Hash(artifact_path);

    // Cache the result
    if (config_.enable_caching) {
        CacheDigest(artifact_path, digest);
    }

    return digest;
}

std::vector<DigestRecord> DigestVerifier::VerifyArtifacts(const std::vector<std::string>& artifact_paths) {
    std::vector<DigestRecord> records;

    if (config_.enable_parallel_verification && artifact_paths.size() > 1) {
        // Parallel verification
        std::vector<std::thread> threads;
        std::mutex records_mutex;

        auto verify_worker = [&](size_t start, size_t end) {
            for (size_t i = start; i < end; ++i) {
                auto record = VerifyArtifact(artifact_paths[i]);
                {
                    std::lock_guard<std::mutex> lock(records_mutex);
                    records.push_back(record);
                }
            }
        };

        size_t chunk_size = std::max(size_t(1), artifact_paths.size() / config_.thread_pool_size);
        for (size_t i = 0; i < artifact_paths.size(); i += chunk_size) {
            size_t end = std::min(i + chunk_size, artifact_paths.size());
            threads.emplace_back(verify_worker, i, end);
        }

        for (auto& thread : threads) {
            thread.join();
        }
    } else {
        // Sequential verification
        for (const auto& path : artifact_paths) {
            records.push_back(VerifyArtifact(path));
        }
    }

    return records;
}

VerificationReport DigestVerifier::VerifyDirectory(const std::string& directory_path, bool recursive) {
    auto artifact_paths = DiscoverArtifacts(directory_path, recursive);
    auto records = VerifyArtifacts(artifact_paths);

    return GenerateReportFromRecords(records);
}

VerificationReport DigestVerifier::VerifyProjectArtifacts(const std::string& project_root) {
    return VerifyDirectory(project_root, true);
}

bool DigestVerifier::StoreDigestRecord(const DigestRecord& record) {
    std::lock_guard<std::mutex> lock(verifier_mutex_);
    digest_database_[record.artifact_path] = record;
    return true;
}

bool DigestVerifier::LoadDigestDatabase(const std::string& database_file) {
    std::lock_guard<std::mutex> lock(verifier_mutex_);

    try {
        std::ifstream file(database_file);
        if (!file.is_open()) {
            return false;
        }

        std::string content((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());

        database_file_path_ = database_file;
        return LoadDatabaseFromJson(content);
    } catch (const std::exception& e) {
        return false;
    }
}

bool DigestVerifier::SaveDigestDatabase(const std::string& database_file) const {
    std::lock_guard<std::mutex> lock(verifier_mutex_);

    try {
        std::ofstream file(database_file);
        if (!file.is_open()) {
            return false;
        }

        std::string json_content = SerializeDatabaseToJson();
        file << json_content;
        return file.good();
    } catch (const std::exception& e) {
        return false;
    }
}

bool DigestVerifier::UpdateDigestForArtifact(const std::string& artifact_path) {
    auto record = VerifyArtifact(artifact_path);
    return StoreDigestRecord(record);
}

void DigestVerifier::EnableContinuousMonitoring(bool enabled) {
    std::lock_guard<std::mutex> lock(verifier_mutex_);
    config_.enable_continuous_monitoring = enabled;
}

void DigestVerifier::StartMonitoring() {
    std::lock_guard<std::mutex> lock(verifier_mutex_);

    if (!monitoring_active_ && config_.enable_continuous_monitoring) {
        monitoring_active_ = true;
        stop_monitoring_ = false;
        monitoring_thread_ = std::thread(&DigestVerifier::MonitoringLoop, this);
    }
}

void DigestVerifier::StopMonitoring() {
    {
        std::lock_guard<std::mutex> lock(verifier_mutex_);
        monitoring_active_ = false;
        stop_monitoring_ = true;
    }

    if (monitoring_thread_.joinable()) {
        monitoring_thread_.join();
    }
}

bool DigestVerifier::IsMonitoringActive() const {
    std::lock_guard<std::mutex> lock(verifier_mutex_);
    return monitoring_active_;
}

void DigestVerifier::AddWatchPath(const std::string& path) {
    std::lock_guard<std::mutex> lock(verifier_mutex_);
    watch_paths_.push_back(path);
}

bool DigestVerifier::DetectTampering(const DigestRecord& current_record, const DigestRecord& stored_record) {
    return current_record.sha256_digest != stored_record.sha256_digest;
}

std::vector<std::string> DigestVerifier::GetTamperedFiles() const {
    std::lock_guard<std::mutex> lock(verifier_mutex_);
    return tampered_files_;
}

void DigestVerifier::ReportTampering(const std::string& artifact_path, const std::string& details) {
    std::lock_guard<std::mutex> lock(verifier_mutex_);
    tampered_files_.push_back(artifact_path);
    integrity_violations_.push_back("Tampering detected in " + artifact_path + ": " + details);
}

bool DigestVerifier::VerifyWithinSLA(const std::string& artifact_path) const {
    auto it = verification_times_.find(artifact_path);
    if (it != verification_times_.end()) {
        return !ExceedsSLA(it->second);
    }
    return false;
}

std::chrono::microseconds DigestVerifier::GetVerificationTime(const std::string& artifact_path) const {
    auto it = verification_times_.find(artifact_path);
    return it != verification_times_.end() ? it->second : std::chrono::microseconds(0);
}

double DigestVerifier::GetSLAComplianceRate() const {
    if (verification_times_.empty()) return 0.0;

    size_t compliant_count = 0;
    for (const auto& [path, time] : verification_times_) {
        if (!ExceedsSLA(time)) {
            compliant_count++;
        }
    }

    return (static_cast<double>(compliant_count) / verification_times_.size()) * 100.0;
}

VerificationReport DigestVerifier::GeneratePerformanceReport() const {
    std::lock_guard<std::mutex> lock(verifier_mutex_);

    VerificationReport report;
    report.report_timestamp = std::to_string(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
    report.total_artifacts_verified = successful_verifications_ + failed_verifications_;
    report.verified_artifacts = successful_verifications_;
    report.failed_verifications = failed_verifications_;
    report.corrupted_artifacts = std::count_if(digest_database_.begin(), digest_database_.end(),
        [](const auto& pair) { return pair.second.status == VerificationStatus::CORRUPTED; });
    report.missing_artifacts = std::count_if(digest_database_.begin(), digest_database_.end(),
        [](const auto& pair) { return pair.second.status == VerificationStatus::MISSING; });
    report.timeout_artifacts = std::count_if(digest_database_.begin(), digest_database_.end(),
        [](const auto& pair) { return pair.second.status == VerificationStatus::TIMEOUT; });

    // Calculate performance metrics
    report.total_verification_time = total_verification_time_;
    report.max_verification_time = std::max_element(verification_times_.begin(), verification_times_.end(),
        [](const auto& a, const auto& b) { return a.second < b.second; })->second;
    report.avg_verification_time = GetAverageVerificationTime();

    // SLA compliance
    report.meets_sla_requirements = GetSLAComplianceRate() >= 95.0;

    // Integrity score
    report.integrity_score = CalculateIntegrityScore();
    report.overall_integrity_valid = report.integrity_score >= 95.0;

    // Generate issues and recommendations
    report.critical_issues = GetIntegrityViolations();
    report.tampered_files = GetTamperedFiles();

    if (!report.meets_sla_requirements) {
        report.recommendations.push_back("Optimize verification performance to meet SLA requirements");
    }
    if (report.integrity_score < 95.0) {
        report.recommendations.push_back("Address integrity violations to improve overall security");
    }

    return report;
}

bool DigestVerifier::VerifyIntegrityChain(const std::string& artifact_path) {
    // This is a simplified implementation
    // In practice, you would implement full chain of trust verification
    auto record = VerifyArtifact(artifact_path);
    return record.status == VerificationStatus::VERIFIED;
}

std::vector<std::string> DigestVerifier::GetIntegrityViolations() const {
    std::lock_guard<std::mutex> lock(verifier_mutex_);
    return integrity_violations_;
}

double DigestVerifier::CalculateIntegrityScore() const {
    if (digest_database_.empty()) return 100.0;

    size_t verified_count = std::count_if(digest_database_.begin(), digest_database_.end(),
        [](const auto& pair) { return pair.second.status == VerificationStatus::VERIFIED; });

    return (static_cast<double>(verified_count) / digest_database_.size()) * 100.0;
}

void DigestVerifier::SetVerificationConfig(const VerificationConfig& config) {
    std::lock_guard<std::mutex> lock(verifier_mutex_);
    config_ = config;
}

void DigestVerifier::SetMaxVerificationTime(std::chrono::microseconds max_time) {
    std::lock_guard<std::mutex> lock(verifier_mutex_);
    config_.max_verification_time = max_time;
}

void DigestVerifier::AddTrustedDirectory(const std::string& directory) {
    std::lock_guard<std::mutex> lock(verifier_mutex_);
    config_.trusted_directories.push_back(directory);
}

void DigestVerifier::AddExcludePattern(const std::string& pattern) {
    std::lock_guard<std::mutex> lock(verifier_mutex_);
    config_.exclude_patterns.push_back(pattern);
}

std::string DigestVerifier::GenerateDigestReport(const std::string& format) const {
    auto report = GeneratePerformanceReport();

    if (format == "json") {
        return VerificationReportToJson(report).dump(4);
    } else {
        // Human-readable format
        std::ostringstream oss;
        oss << "SHA-256 Digest Verification Report\n";
        oss << "=================================\n";
        oss << "Generated: " << report.report_timestamp << "\n\n";

        oss << "Summary:\n";
        oss << "  Total Artifacts: " << report.total_artifacts_verified << "\n";
        oss << "  Verified: " << report.verified_artifacts << "\n";
        oss << "  Failed: " << report.failed_verifications << "\n";
        oss << "  Corrupted: " << report.corrupted_artifacts << "\n";
        oss << "  Missing: " << report.missing_artifacts << "\n";
        oss << "  Timeouts: " << report.timeout_artifacts << "\n";
        oss << "  Integrity Score: " << std::fixed << std::setprecision(2) << report.integrity_score << "%\n";
        oss << "  SLA Compliance: " << std::setprecision(1) << GetSLAComplianceRate() << "%\n";
        oss << "  Meets SLA: " << (report.meets_sla_requirements ? "Yes" : "No") << "\n";
        oss << "  Overall Valid: " << (report.overall_integrity_valid ? "Yes" : "No") << "\n";

        if (!report.tampered_files.empty()) {
            oss << "\nTampered Files:\n";
            for (const auto& file : report.tampered_files) {
                oss << "  - " << file << "\n";
            }
        }

        return oss.str();
    }
}

bool DigestVerifier::ExportVerificationReport(const std::string& file_path, const std::string& format) const {
    try {
        std::ofstream file(file_path);
        if (!file.is_open()) {
            return false;
        }

        std::string content = GenerateDigestReport(format);
        file << content;
        return file.good();
    } catch (const std::exception& e) {
        return false;
    }
}

void DigestVerifier::ClearCache() {
    std::lock_guard<std::mutex> lock(verifier_mutex_);
    digest_cache_.clear();
    cache_timestamps_.clear();
}

std::vector<std::string> DigestVerifier::GetVerifiedArtifacts() const {
    std::lock_guard<std::mutex> lock(verifier_mutex_);

    std::vector<std::string> verified_artifacts;
    for (const auto& [path, record] : digest_database_) {
        if (record.status == VerificationStatus::VERIFIED) {
            verified_artifacts.push_back(path);
        }
    }

    return verified_artifacts;
}

// Private methods

std::string DigestVerifier::ComputeSHA256Hash(const std::string& file_path) const {
    std::vector<uint8_t> file_contents;
    if (!ReadFileContents(file_path, file_contents)) {
        throw std::runtime_error("Failed to read file: " + file_path);
    }

    return ComputeSHA256Hash(file_contents);
}

std::string DigestVerifier::ComputeSHA256Hash(const std::vector<uint8_t>& data) const {
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        throw std::runtime_error("Failed to create EVP_MD_CTX");
    }

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("Failed to initialize SHA-256 digest");
    }

    if (EVP_DigestUpdate(ctx, data.data(), data.size()) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("Failed to update SHA-256 digest");
    }

    std::vector<uint8_t> hash(EVP_MD_size(EVP_sha256()));
    unsigned int hash_len = 0;

    if (EVP_DigestFinal_ex(ctx, hash.data(), &hash_len) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("Failed to finalize SHA-256 digest");
    }

    EVP_MD_CTX_free(ctx);

    return CryptoUtils::BytesToHex(hash);
}

bool DigestVerifier::ReadFileContents(const std::string& file_path, std::vector<uint8_t>& contents) const {
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    file.seekg(0, std::ios::end);
    size_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    contents.resize(file_size);
    if (!file.read(reinterpret_cast<char*>(contents.data()), file_size)) {
        return false;
    }

    return true;
}

ArtifactType DigestVerifier::DetermineArtifactType(const std::string& file_path) const {
    std::filesystem::path path(file_path);
    std::string extension = path.extension().string();

    if (extension == ".cpp" || extension == ".cu" || extension == ".h" || extension == ".cuh") {
        return ArtifactType::SOURCE_CODE;
    } else if (extension == ".exe" || extension == ".dll" || extension == ".so" || extension == ".a") {
        return ArtifactType::EXECUTABLE;
    } else if (extension == ".json" || extension == ".xml" || extension == ".yaml" || extension == ".conf") {
        return ArtifactType::CONFIGURATION;
    } else if (extension == ".dat" || extension == ".bin" || extension == ".data") {
        return ArtifactType::DATA;
    } else if (extension == ".md" || extension == ".txt" || extension == ".pdf" || extension == ".doc") {
        return ArtifactType::DOCUMENTATION;
    } else if (extension == ".test" || extension == ".spec" || file_path.find("test") != std::string::npos) {
        return ArtifactType::TEST;
    } else {
        return ArtifactType::BUILD_ARTIFACT;
    }
}

size_t DigestVerifier::GetFileSize(const std::string& file_path) const {
    try {
        return std::filesystem::file_size(file_path);
    } catch (const std::filesystem::filesystem_error&) {
        return 0;
    }
}

bool DigestVerifier::IsCached(const std::string& file_path) const {
    auto it = digest_cache_.find(file_path);
    return it != digest_cache_.end() && !IsCacheExpired(file_path);
}

std::string DigestVerifier::GetCachedDigest(const std::string& file_path) const {
    auto it = digest_cache_.find(file_path);
    if (it != digest_cache_.end() && !IsCacheExpired(file_path)) {
        return it->second.first;
    }
    return "";
}

void DigestVerifier::CacheDigest(const std::string& file_path, const std::string& digest) {
    digest_cache_[file_path] = std::make_pair(digest, std::chrono::system_clock::now());
    cache_timestamps_[file_path] = std::chrono::system_clock::now();
}

bool DigestVerifier::IsCacheExpired(const std::string& file_path) const {
    auto it = cache_timestamps_.find(file_path);
    if (it == cache_timestamps_.end()) {
        return true;
    }

    auto now = std::chrono::system_clock::now();
    auto age = std::chrono::duration_cast<std::chrono::minutes>(now - it->second);
    return age >= config_.cache_expiry_time;
}

std::vector<std::string> DigestVerifier::DiscoverArtifacts(const std::string& directory_path, bool recursive) const {
    std::vector<std::string> artifacts;

    try {
        auto iterator = recursive ? std::filesystem::recursive_directory_iterator(directory_path)
                                 : std::filesystem::directory_iterator(directory_path);

        for (const auto& entry : iterator) {
            if (entry.is_regular_file()) {
                std::string file_path = entry.path().string();
                if (!ShouldExcludeFile(file_path)) {
                    artifacts.push_back(file_path);
                }
            }
        }
    } catch (const std::filesystem::filesystem_error&) {
        // Handle filesystem errors
    }

    return artifacts;
}

bool DigestVerifier::ShouldExcludeFile(const std::string& file_path) const {
    for (const auto& pattern : config_.exclude_patterns) {
        if (file_path.find(pattern) != std::string::npos) {
            return true;
        }
    }
    return false;
}

void DigestVerifier::MonitoringLoop() {
    while (!stop_monitoring_) {
        for (const auto& path : watch_paths_) {
            ProcessWatchPath(path);
        }

        // Sleep for monitoring interval
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}

void DigestVerifier::ProcessWatchPath(const std::string& path) {
    // Check if path is a directory
    if (std::filesystem::is_directory(path)) {
        auto artifacts = DiscoverArtifacts(path, true);
        for (const auto& artifact : artifacts) {
            // Check for changes
            auto current_size = GetFileSize(artifact);
            auto it = digest_database_.find(artifact);
            if (it != digest_database_.end()) {
                if (it->second.file_size_bytes != current_size) {
                    // File size changed, re-verify
                    auto record = VerifyArtifact(artifact);
                    if (DetectTampering(record, it->second)) {
                        ReportTampering(artifact, "File size changed and digest mismatch");
                    }
                }
            }
        }
    } else {
        // Single file
        auto record = VerifyArtifact(path);
        auto it = digest_database_.find(path);
        if (it != digest_database_.end()) {
            if (DetectTampering(record, it->second)) {
                ReportTampering(path, "Digest mismatch detected during monitoring");
            }
        }
    }
}

bool DigestVerifier::ExceedsSLA(std::chrono::microseconds verification_time) const {
    return verification_time > config_.max_verification_time;
}

void DigestVerifier::UpdatePerformanceMetrics(std::chrono::microseconds verification_time, bool successful) {
    total_verification_time_ += verification_time;
    if (successful) {
        successful_verifications_++;
    } else {
        failed_verifications_++;
    }
}

std::chrono::microseconds DigestVerifier::GetAverageVerificationTime() const {
    size_t total_verifications = successful_verifications_ + failed_verifications_;
    return total_verifications > 0 ? total_verification_time_ / total_verifications : std::chrono::microseconds(0);
}

// Serialization helpers

json DigestVerifier::DigestRecordToJson(const DigestRecord& record) const {
    json j;
    j["artifact_path"] = record.artifact_path;
    j["sha256_digest"] = record.sha256_digest;
    j["artifact_type"] = static_cast<int>(record.artifact_type);
    j["verification_time"] = std::chrono::system_clock::to_time_t(record.verification_time);
    j["creation_time"] = std::chrono::system_clock::to_time_t(record.creation_time);
    j["file_size_bytes"] = record.file_size_bytes;
    j["status"] = static_cast<int>(record.status);
    j["verification_duration_us"] = record.verification_duration.count();
    j["verification_error"] = record.verification_error;
    j["metadata"] = record.metadata;
    return j;
}

json DigestVerifier::VerificationReportToJson(const VerificationReport& report) const {
    json j;
    j["report_timestamp"] = report.report_timestamp;
    j["total_verification_time_us"] = report.total_verification_time.count();
    j["total_artifacts_verified"] = report.total_artifacts_verified;
    j["verified_artifacts"] = report.verified_artifacts;
    j["failed_verifications"] = report.failed_verifications;
    j["corrupted_artifacts"] = report.corrupted_artifacts;
    j["missing_artifacts"] = report.missing_artifacts;
    j["timeout_artifacts"] = report.timeout_artifacts;
    j["integrity_score"] = report.integrity_score;
    j["meets_sla_requirements"] = report.meets_sla_requirements;
    j["overall_integrity_valid"] = report.overall_integrity_valid;
    j["max_verification_time_us"] = report.max_verification_time.count();
    j["avg_verification_time_us"] = report.avg_verification_time.count();
    j["critical_issues"] = report.critical_issues;
    j["tampered_files"] = report.tampered_files;
    j["recommendations"] = report.recommendations;

    j["verification_records"] = json::array();
    for (const auto& record : report.verification_records) {
        j["verification_records"].push_back(DigestRecordToJson(record));
    }

    return j;
}

DigestRecord DigestVerifier::JsonToDigestRecord(const json& j) const {
    DigestRecord record;
    record.artifact_path = j.value("artifact_path", "");
    record.sha256_digest = j.value("sha256_digest", "");
    record.artifact_type = static_cast<ArtifactType>(j.value("artifact_type", 0));
    record.verification_time = std::chrono::system_clock::from_time_t(j.value("verification_time", 0));
    record.creation_time = std::chrono::system_clock::from_time_t(j.value("creation_time", 0));
    record.file_size_bytes = j.value("file_size_bytes", 0);
    record.status = static_cast<VerificationStatus>(j.value("status", 0));
    record.verification_duration = std::chrono::microseconds(j.value("verification_duration_us", 0));
    record.verification_error = j.value("verification_error", "");
    record.metadata = j.value("metadata", json::object());
    return record;
}

VerificationReport DigestVerifier::JsonToVerificationReport(const json& j) const {
    VerificationReport report;
    report.report_timestamp = j.value("report_timestamp", "");
    report.total_verification_time = std::chrono::microseconds(j.value("total_verification_time_us", 0));
    report.total_artifacts_verified = j.value("total_artifacts_verified", 0);
    report.verified_artifacts = j.value("verified_artifacts", 0);
    report.failed_verifications = j.value("failed_verifications", 0);
    report.corrupted_artifacts = j.value("corrupted_artifacts", 0);
    report.missing_artifacts = j.value("missing_artifacts", 0);
    report.timeout_artifacts = j.value("timeout_artifacts", 0);
    report.integrity_score = j.value("integrity_score", 0.0);
    report.meets_sla_requirements = j.value("meets_sla_requirements", false);
    report.overall_integrity_valid = j.value("overall_integrity_valid", false);
    report.max_verification_time = std::chrono::microseconds(j.value("max_verification_time_us", 0));
    report.avg_verification_time = std::chrono::microseconds(j.value("avg_verification_time_us", 0));
    report.critical_issues = j.value("critical_issues", std::vector<std::string>{});
    report.tampered_files = j.value("tampered_files", std::vector<std::string>{});
    report.recommendations = j.value("recommendations", std::vector<std::string>{});

    if (j.contains("verification_records")) {
        for (const auto& record_json : j["verification_records"]) {
            report.verification_records.push_back(JsonToDigestRecord(record_json));
        }
    }

    return report;
}

bool DigestVerifier::LoadDatabaseFromJson(const std::string& json_content) {
    try {
        json j = json::parse(json_content);
        if (j.contains("digest_records")) {
            for (const auto& record_json : j["digest_records"]) {
                auto record = JsonToDigestRecord(record_json);
                digest_database_[record.artifact_path] = record;
            }
        }
        return true;
    } catch (const json::exception&) {
        return false;
    }
}

std::string DigestVerifier::SerializeDatabaseToJson() const {
    json j;
    j["database_version"] = "1.0";
    j["generated_timestamp"] = std::to_string(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));

    j["digest_records"] = json::array();
    for (const auto& [path, record] : digest_database_) {
        j["digest_records"].push_back(DigestRecordToJson(record));
    }

    return j.dump(4);
}

VerificationReport DigestVerifier::GenerateReportFromRecords(const std::vector<DigestRecord>& records) {
    VerificationReport report;
    report.report_timestamp = std::to_string(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
    report.verification_records = records;

    // Calculate statistics
    report.total_artifacts_verified = records.size();
    report.verified_artifacts = std::count_if(records.begin(), records.end(),
        [](const DigestRecord& r) { return r.status == VerificationStatus::VERIFIED; });
    report.failed_verifications = std::count_if(records.begin(), records.end(),
        [](const DigestRecord& r) { return r.status == VerificationStatus::FAILED; });
    report.corrupted_artifacts = std::count_if(records.begin(), records.end(),
        [](const DigestRecord& r) { return r.status == VerificationStatus::CORRUPTED; });
    report.missing_artifacts = std::count_if(records.begin(), records.end(),
        [](const DigestRecord& r) { return r.status == VerificationStatus::MISSING; });
    report.timeout_artifacts = std::count_if(records.begin(), records.end(),
        [](const DigestRecord& r) { return r.status == VerificationStatus::TIMEOUT; });

    // Calculate performance metrics
    report.total_verification_time = std::accumulate(records.begin(), records.end(),
        std::chrono::microseconds(0),
        [](std::chrono::microseconds acc, const DigestRecord& r) {
            return acc + r.verification_duration;
        });

    report.max_verification_time = std::max_element(records.begin(), records.end(),
        [](const DigestRecord& a, const DigestRecord& b) {
            return a.verification_duration < b.verification_duration;
        })->verification_duration;

    report.avg_verification_time = report.total_artifacts_verified > 0 ?
        report.total_verification_time / report.total_artifacts_verified : std::chrono::microseconds(0);

    // Check SLA compliance
    report.meets_sla_requirements = std::all_of(records.begin(), records.end(),
        [this](const DigestRecord& r) { return !ExceedsSLA(r.verification_duration); });

    // Calculate integrity score
    report.integrity_score = report.total_artifacts_verified > 0 ?
        (static_cast<double>(report.verified_artifacts) / report.total_artifacts_verified) * 100.0 : 0.0;
    report.overall_integrity_valid = report.integrity_score >= 95.0;

    // Generate recommendations
    if (!report.meets_sla_requirements) {
        report.recommendations.push_back("Optimize verification performance to meet SLA requirements");
    }
    if (report.integrity_score < 95.0) {
        report.recommendations.push_back("Address verification failures to improve integrity score");
    }

    return report;
}

// ScopedDigestVerifier implementation

ScopedDigestVerifier::ScopedDigestVerifier(DigestVerifier& verifier, const std::string& artifact_path)
    : verifier_(verifier), artifact_path_(artifact_path), verification_completed_(false) {
    verification_record_ = verifier_.VerifyArtifact(artifact_path);
    verification_completed_ = true;
}

ScopedDigestVerifier::~ScopedDigestVerifier() {
    // Verification is completed in constructor, destructor handles cleanup if needed
}

bool ScopedDigestVerifier::IsVerified() const {
    return verification_completed_ && verification_record_.status == VerificationStatus::VERIFIED;
}

VerificationStatus ScopedDigestVerifier::GetStatus() const {
    return verification_record_.status;
}

std::chrono::microseconds ScopedDigestVerifier::GetVerificationTime() const {
    return verification_record_.verification_duration;
}

// CryptoUtils implementation

std::string CryptoUtils::BytesToHex(const std::vector<uint8_t>& bytes) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (uint8_t byte : bytes) {
        oss << std::setw(2) << static_cast<int>(byte);
    }
    return oss.str();
}

std::vector<uint8_t> CryptoUtils::HexToBytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < hex.length(); i += 2) {
        std::string byte_string = hex.substr(i, 2);
        uint8_t byte = static_cast<uint8_t>(std::stoul(byte_string, nullptr, 16));
        bytes.push_back(byte);
    }
    return bytes;
}

std::string CryptoUtils::GenerateSecureHash(const std::string& input) {
    std::vector<uint8_t> input_bytes(input.begin(), input.end());
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) throw std::runtime_error("Failed to create EVP_MD_CTX");

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("Failed to initialize digest");
    }

    if (EVP_DigestUpdate(ctx, input_bytes.data(), input_bytes.size()) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("Failed to update digest");
    }

    std::vector<uint8_t> hash(EVP_MD_size(EVP_sha256()));
    unsigned int hash_len = 0;

    if (EVP_DigestFinal_ex(ctx, hash.data(), &hash_len) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("Failed to finalize digest");
    }

    EVP_MD_CTX_free(ctx);
    return BytesToHex(hash);
}

bool CryptoUtils::VerifyFileSignature(const std::string& file_path, const std::string& signature, const std::string& public_key) {
    // Simplified signature verification - in practice, you'd implement proper RSA/ECDSA verification
    return false;
}

std::string CryptoUtils::GenerateHMAC(const std::string& data, const std::string& secret_key) {
    std::vector<uint8_t> data_bytes(data.begin(), data.end());
    std::vector<uint8_t> key_bytes(secret_key.begin(), secret_key.end());

    std::vector<uint8_t> hmac(HASH_SIZE);
    unsigned int hmac_len = 0;

    HMAC(EVP_sha256(), key_bytes.data(), key_bytes.size(),
         data_bytes.data(), data_bytes.size(), hmac.data(), &hmac_len);

    return BytesToHex(hmac);
}

bool CryptoUtils::VerifyHMAC(const std::string& data, const std::string& hmac, const std::string& secret_key) {
    std::string calculated_hmac = GenerateHMAC(data, secret_key);
    return calculated_hmac == hmac;
}

// Factory function

std::unique_ptr<DigestVerifier> CreateDigestVerifier(const VerificationConfig& config) {
    return std::make_unique<DigestVerifier>(config);
}

} // namespace puzzle71::gpu::performance