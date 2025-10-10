/**
 * @file           digest_verifier.cpp
 * @brief          SHA-256 digest verification system implementation for Puzzle71Solver
 * @author         Puzzle71Solver Team
 * @origin         https://github.com/Puzzle71Solver/Puzzle71Solver
 * @origin_path    src/integration/digest_verifier.cpp
 * @origin_commit  <current_commit>
 * @origin_license MIT
 * @extracted_date 2025-10-10
 * @extracted_by   Puzzle71Solver Team
 * @modifications  Created for third-party dependency integration optimization
 * @spdx_license_identifier MIT
 */

#include "digest_verifier.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <filesystem>
#include <regex>
#include <openssl/sha.h>
#include <openssl/evp.h>

namespace puzzle71 {
namespace integration {

// FileDigest implementation
std::string FileDigest::to_json() const {
    nlohmann::json j;
    j["file_path"] = file_path;
    j["digest"] = digest;
    j["expected_digest"] = expected_digest;
    j["algorithm"] = static_cast<int>(algorithm);
    j["status"] = static_cast<int>(status);
    j["calculated_at"] = std::chrono::duration_cast<std::chrono::seconds>(
        calculated_at.time_since_epoch()).count();
    j["verified_at"] = std::chrono::duration_cast<std::chrono::seconds>(
        verified_at.time_since_epoch()).count();
    j["file_size_bytes"] = file_size_bytes;
    j["file_hash"] = file_hash;
    return j.dump();
}

FileDigest FileDigest::from_json(const nlohmann::json& j) {
    FileDigest fd;
    fd.file_path = j.value("file_path", "");
    fd.digest = j.value("digest", "");
    fd.expected_digest = j.value("expected_digest", "");
    fd.algorithm = static_cast<DigestAlgorithm>(j.value("algorithm", 0));
    fd.status = static_cast<VerificationStatus>(j.value("status", 0));

    auto calc_time = j.value("calculated_at", int64_t{0});
    fd.calculated_at = std::chrono::system_clock::from_time_t(calc_time);

    auto verify_time = j.value("verified_at", int64_t{0});
    fd.verified_at = std::chrono::system_clock::from_time_t(verify_time);

    fd.file_size_bytes = j.value("file_size_bytes", size_t{0});
    fd.file_hash = j.value("file_hash", "");
    return fd;
}

// LibraryDigestManifest implementation
std::pair<size_t, size_t> LibraryDigestManifest::get_verification_stats() const {
    size_t verified = std::count_if(file_digests.begin(), file_digests.end(),
        [](const FileDigest& fd) { return fd.is_verified(); });
    return {verified, file_digests.size()};
}

double LibraryDigestManifest::get_verification_percentage() const {
    auto [verified, total] = get_verification_stats();
    return total > 0 ? (static_cast<double>(verified) / total) * 100.0 : 0.0;
}

std::string LibraryDigestManifest::to_json() const {
    nlohmann::json j;
    j["library_name"] = library_name;
    j["version"] = version;
    j["origin_commit"] = origin_commit;
    j["created_at"] = std::chrono::duration_cast<std::chrono::seconds>(
        created_at.time_since_epoch()).count();
    j["algorithm"] = static_cast<int>(algorithm);
    j["manifest_signature"] = manifest_signature;
    j["is_comprehensive"] = is_comprehensive;

    nlohmann::json digests = nlohmann::json::array();
    for (const auto& fd : file_digests) {
        digests.push_back(nlohmann::json::parse(fd.to_json()));
    }
    j["file_digests"] = digests;
    j["metadata"] = metadata;

    return j.dump();
}

LibraryDigestManifest LibraryDigestManifest::from_json(const nlohmann::json& j) {
    LibraryDigestManifest manifest;
    manifest.library_name = j.value("library_name", "");
    manifest.version = j.value("version", "");
    manifest.origin_commit = j.value("origin_commit", "");
    manifest.algorithm = static_cast<DigestAlgorithm>(j.value("algorithm", 0));
    manifest.manifest_signature = j.value("manifest_signature", "");
    manifest.is_comprehensive = j.value("is_comprehensive", false);

    auto created_time = j.value("created_at", int64_t{0});
    manifest.created_at = std::chrono::system_clock::from_time_t(created_time);

    if (j.contains("file_digests") && j["file_digests"].is_array()) {
        for (const auto& fd_json : j["file_digests"]) {
            manifest.file_digests.push_back(FileDigest::from_json(fd_json));
        }
    }

    if (j.contains("metadata") && j["metadata"].is_object()) {
        for (auto& [key, value] : j["metadata"].items()) {
            manifest.metadata[key] = value.get<std::string>();
        }
    }

    return manifest;
}

// VerificationPolicy implementation
VerificationPolicy VerificationPolicy::default_policy() {
    VerificationPolicy policy;
    policy.require_verification = true;
    policy.allow_legacy_algorithms = false;
    policy.verify_on_access = false;
    policy.continuous_monitoring = true;
    policy.verification_interval = std::chrono::hours(24);
    policy.exclude_patterns = {".*.tmp", ".*.swp", ".*.log"};
    policy.critical_files = {};
    policy.tolerance_threshold = 0.0;
    policy.auto_recovery = true;
    return policy;
}

VerificationPolicy VerificationPolicy::strict_policy() {
    VerificationPolicy policy = default_policy();
    policy.allow_legacy_algorithms = false;
    policy.verify_on_access = true;
    policy.continuous_monitoring = true;
    policy.verification_interval = std::chrono::hours(1);
    policy.exclude_patterns = {};
    policy.critical_files = {"*.h", "*.cpp", "*.cu", "*.c"};
    policy.tolerance_threshold = 0.0;
    policy.auto_recovery = false;
    return policy;
}

VerificationPolicy VerificationPolicy::development_policy() {
    VerificationPolicy policy = default_policy();
    policy.allow_legacy_algorithms = true;
    policy.verify_on_access = false;
    policy.continuous_monitoring = false;
    policy.verification_interval = std::chrono::hours(168); // 1 week
    policy.exclude_patterns = {".*.tmp", ".*.swp", ".*.log", ".*.o", ".*.so", ".*.a"};
    policy.critical_files = {};
    policy.tolerance_threshold = 0.1; // Allow 10% failure rate
    policy.auto_recovery = true;
    return policy;
}

// VerificationResult implementation
double VerificationResult::get_success_rate() const {
    return total_files > 0 ? (static_cast<double>(verified_files) / total_files) * 100.0 : 0.0;
}

bool VerificationResult::is_acceptable(double tolerance) const {
    double failure_rate = total_files > 0 ?
        (static_cast<double>(failed_files + error_files) / total_files) : 0.0;
    return failure_rate <= tolerance;
}

std::string VerificationResult::to_json() const {
    nlohmann::json j;
    j["overall_success"] = overall_success;
    j["total_files"] = total_files;
    j["verified_files"] = verified_files;
    j["failed_files"] = failed_files;
    j["error_files"] = error_files;
    j["verification_time_ms"] = verification_time.count();
    j["verified_at"] = std::chrono::duration_cast<std::chrono::seconds>(
        verified_at.time_since_epoch()).count();

    nlohmann::json failed = nlohmann::json::array();
    for (const auto& fd : failed_verifications) {
        failed.push_back(nlohmann::json::parse(fd.to_json()));
    }
    j["failed_verifications"] = failed;

    j["warning_messages"] = warning_messages;
    return j.dump();
}

// DigestVerifier implementation
DigestVerifier& DigestVerifier::instance() {
    static DigestVerifier instance;
    return instance;
}

bool DigestVerifier::initialize(const std::string& storage_path,
                               const VerificationPolicy& policy) {
    std::lock_guard<std::mutex> lock(mutex_);

    storage_path_ = storage_path;
    policy_ = policy;

    // Create storage directory
    if (!storage_path_.empty()) {
        std::filesystem::create_directories(storage_path_);
    }

    // Load existing manifests
    for (const auto& entry : std::filesystem::directory_iterator(storage_path_)) {
        if (entry.path().extension() == ".json") {
            std::string library_name = entry.path().stem().string();
            if (auto manifest = load_manifest(library_name)) {
                libraries_[library_name] = *manifest;
            }
        }
    }

    return true;
}

void DigestVerifier::set_policy(const VerificationPolicy& policy) {
    std::lock_guard<std::mutex> lock(mutex_);
    policy_ = policy;
}

const VerificationPolicy& DigestVerifier::get_policy() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return policy_;
}

std::string DigestVerifier::calculate_digest(const std::string& file_path,
                                            DigestAlgorithm algorithm) const {
    return calculate_file_digest(file_path, algorithm);
}

LibraryDigestManifest DigestVerifier::calculate_directory_manifest(
    const std::string& directory_path,
    const std::string& library_name,
    const std::string& version,
    const std::string& origin_commit,
    DigestAlgorithm algorithm,
    const std::vector<std::string>& exclude_patterns) {

    LibraryDigestManifest manifest;
    manifest.library_name = library_name;
    manifest.version = version;
    manifest.origin_commit = origin_commit;
    manifest.algorithm = algorithm;
    manifest.created_at = std::chrono::system_clock::now();
    manifest.is_comprehensive = true;

    // Find all files in directory
    auto files = find_files(directory_path, exclude_patterns);

    for (const auto& file_path : files) {
        FileDigest fd;
        fd.file_path = file_path;
        fd.algorithm = algorithm;
        fd.calculated_at = std::chrono::system_clock::now();
        fd.status = VerificationStatus::Pending;

        // Calculate file digest
        fd.digest = calculate_file_digest(directory_path + "/" + file_path, algorithm);
        fd.expected_digest = fd.digest; // Self-verify

        // Get file size
        std::error_code ec;
        fd.file_size_bytes = std::filesystem::file_size(directory_path + "/" + file_path, ec);
        if (ec) {
            fd.file_size_bytes = 0;
        }

        // Set initial verification status
        fd.status = !fd.digest.empty() ? VerificationStatus::Verified : VerificationStatus::Error;
        fd.verified_at = std::chrono::system_clock::now();

        manifest.file_digests.push_back(fd);
    }

    // Add metadata
    manifest.metadata["total_files"] = std::to_string(files.size());
    manifest.metadata["directory_path"] = directory_path;
    manifest.metadata["excluded_patterns"] = std::to_string(exclude_patterns.size());

    return manifest;
}

VerificationStatus DigestVerifier::verify_file(const std::string& file_path,
                                             const std::string& expected_digest,
                                             DigestAlgorithm algorithm) {
    std::string actual_digest = calculate_file_digest(file_path, algorithm);

    if (actual_digest.empty()) {
        return VerificationStatus::Error;
    }

    return (actual_digest == expected_digest) ? VerificationStatus::Verified : VerificationStatus::Failed;
}

VerificationResult DigestVerifier::verify_library(const std::string& library_name,
                                                 const LibraryDigestManifest& manifest) {
    VerificationResult result;
    result.verified_at = std::chrono::system_clock::now();
    result.total_files = manifest.file_digests.size();

    auto start_time = std::chrono::high_resolution_clock::now();

    for (const auto& fd : manifest.file_digests) {
        // Construct full file path (this would need proper path handling)
        std::string full_path = "src/extracted/" + library_name + "/" + fd.file_path;

        VerificationStatus status = verify_file(full_path, fd.expected_digest, fd.algorithm);

        if (status == VerificationStatus::Verified) {
            result.verified_files++;
        } else if (status == VerificationStatus::Failed) {
            result.failed_files++;
            FileDigest failed_fd = fd;
            failed_fd.status = status;
            failed_fd.verified_at = std::chrono::system_clock::now();
            result.failed_verifications.push_back(failed_fd);
        } else {
            result.error_files++;
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    result.verification_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    // Determine overall success based on policy
    result.overall_success = result.is_acceptable(policy_.tolerance_threshold);

    // Add warnings if needed
    if (result.failed_files > 0) {
        result.warning_messages.push_back(std::to_string(result.failed_files) + " files failed verification");
    }
    if (result.error_files > 0) {
        result.warning_messages.push_back(std::to_string(result.error_files) + " files had verification errors");
    }

    notify_callbacks(result);
    return result;
}

std::map<std::string, VerificationResult> DigestVerifier::verify_all_libraries() {
    std::lock_guard<std::mutex> lock(mutex_);

    std::map<std::string, VerificationResult> results;

    for (const auto& [library_name, manifest] : libraries_) {
        results[library_name] = verify_library(library_name, manifest);
    }

    return results;
}

bool DigestVerifier::register_library(const LibraryDigestManifest& manifest) {
    std::lock_guard<std::mutex> lock(mutex_);

    libraries_[manifest.library_name] = manifest;
    return save_manifest(manifest);
}

bool DigestVerifier::unregister_library(const std::string& library_name) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = libraries_.find(library_name);
    if (it != libraries_.end()) {
        libraries_.erase(it);

        // Remove manifest file
        std::string manifest_path = storage_path_ + "/" + library_name + ".json";
        std::filesystem::remove(manifest_path);

        return true;
    }

    return false;
}

std::optional<LibraryDigestManifest> DigestVerifier::get_library_manifest(const std::string& library_name) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = libraries_.find(library_name);
    if (it != libraries_.end()) {
        return it->second;
    }

    return std::nullopt;
}

std::map<std::string, LibraryDigestManifest> DigestVerifier::get_all_libraries() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return libraries_;
}

bool DigestVerifier::is_library_registered(const std::string& library_name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return libraries_.find(library_name) != libraries_.end();
}

bool DigestVerifier::update_file_digest(const std::string& library_name,
                                       const std::string& file_path,
                                       const std::string& new_digest) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = libraries_.find(library_name);
    if (it == libraries_.end()) {
        return false;
    }

    // Find and update the file digest
    for (auto& fd : it->second.file_digests) {
        if (fd.file_path == file_path) {
            fd.digest = new_digest;
            fd.expected_digest = new_digest;
            fd.calculated_at = std::chrono::system_clock::now();
            fd.status = VerificationStatus::Verified;
            fd.verified_at = std::chrono::system_clock::now();

            return save_manifest(it->second);
        }
    }

    return false;
}

bool DigestVerifier::export_manifest(const std::string& library_name,
                                    const std::string& export_path,
                                    bool include_signature) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto manifest = get_library_manifest(library_name);
    if (!manifest) {
        return false;
    }

    // Add signature if requested (placeholder)
    if (include_signature) {
        manifest->manifest_signature = "placeholder_signature";
    }

    std::ofstream file(export_path);
    if (!file.is_open()) {
        return false;
    }

    file << manifest->to_json();
    return true;
}

bool DigestVerifier::import_manifest(const std::string& import_path,
                                    bool verify_signature,
                                    const std::string& library_name) {
    std::ifstream file(import_path);
    if (!file.is_open()) {
        return false;
    }

    try {
        nlohmann::json j;
        file >> j;

        LibraryDigestManifest manifest = LibraryDigestManifest::from_json(j);

        // Verify signature if requested (placeholder)
        if (verify_signature && !manifest.manifest_signature.empty()) {
            // In a real implementation, verify the cryptographic signature
        }

        // Use provided library name or extract from manifest
        std::string lib_name = library_name.empty() ? manifest.library_name : library_name;
        if (lib_name.empty()) {
            return false;
        }

        manifest.library_name = lib_name;

        return register_library(manifest);
    } catch (const std::exception& e) {
        return false;
    }
}

nlohmann::json DigestVerifier::get_statistics() const {
    std::lock_guard<std::mutex> lock(mutex_);

    nlohmann::json stats;
    stats["total_libraries"] = libraries_.size();
    stats["storage_path"] = storage_path_;
    stats["policy"] = policy_.to_json();

    size_t total_files = 0;
    size_t verified_files = 0;

    for (const auto& [library_name, manifest] : libraries_) {
        auto [verified, total] = manifest.get_verification_stats();
        total_files += total;
        verified_files += verified;
    }

    stats["total_files"] = total_files;
    stats["verified_files"] = verified_files;
    stats["verification_percentage"] = total_files > 0 ?
        (static_cast<double>(verified_files) / total_files) * 100.0 : 0.0;

    return stats;
}

std::string DigestVerifier::generate_integrity_report(const std::vector<std::string>& library_names,
                                                      bool include_failed_details) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::stringstream ss;
    ss << "Library Integrity Report\n";
    ss << "========================\n\n";

    std::vector<std::string> libs_to_check = library_names.empty() ?
        [this]() {
            std::vector<std::string> names;
            for (const auto& [name, _] : libraries_) names.push_back(name);
            return names;
        }() : library_names;

    for (const auto& library_name : libs_to_check) {
        auto manifest = get_library_manifest(library_name);
        if (!manifest) {
            ss << "Library '" << library_name << "': NOT REGISTERED\n\n";
            continue;
        }

        auto [verified, total] = manifest->get_verification_stats();
        double percentage = manifest->get_verification_percentage();

        ss << "Library '" << library_name << "':\n";
        ss << "  Version: " << manifest->version << "\n";
        ss << "  Files: " << verified << "/" << total << " verified ("
           << std::fixed << std::setprecision(1) << percentage << "%)\n";

        if (include_failed_details && verified < total) {
            ss << "  Failed files:\n";
            for (const auto& fd : manifest->file_digests) {
                if (!fd.is_verified()) {
                    ss << "    " << fd.file_path << " ("
                       << status_to_string(fd.status) << ")\n";
                }
            }
        }

        ss << "\n";
    }

    return ss.str();
}

// Private methods implementation
std::string DigestVerifier::calculate_file_digest(const std::string& file_path,
                                                DigestAlgorithm algorithm) const {
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        return "";
    }

    const EVP_MD* md = nullptr;
    switch (algorithm) {
        case DigestAlgorithm::SHA256:
            md = EVP_sha256();
            break;
        case DigestAlgorithm::SHA512:
            md = EVP_sha512();
            break;
        case DigestAlgorithm::MD5:
            md = EVP_md5();
            break;
        case DigestAlgorithm::SHA1:
            md = EVP_sha1();
            break;
        default:
            return "";
    }

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        return "";
    }

    if (EVP_DigestInit_ex(ctx, md, nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        return "";
    }

    char buffer[4096];
    while (file.read(buffer, sizeof(buffer))) {
        EVP_DigestUpdate(ctx, buffer, file.gcount());
    }

    if (file.gcount() > 0) {
        EVP_DigestUpdate(ctx, buffer, file.gcount());
    }

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len = 0;
    EVP_DigestFinal_ex(ctx, hash, &hash_len);
    EVP_MD_CTX_free(ctx);

    std::stringstream ss;
    for (unsigned int i = 0; i < hash_len; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }

    return ss.str();
}

std::vector<std::string> DigestVerifier::find_files(
    const std::string& directory_path,
    const std::vector<std::string>& exclude_patterns) const {

    std::vector<std::string> files;

    if (!std::filesystem::exists(directory_path)) {
        return files;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory_path)) {
        if (entry.is_regular_file()) {
            std::string relative_path = std::filesystem::relative(entry.path(), directory_path).string();

            if (!matches_exclude_pattern(relative_path, exclude_patterns)) {
                files.push_back(relative_path);
            }
        }
    }

    return files;
}

bool DigestVerifier::matches_exclude_pattern(const std::string& file_path,
                                            const std::vector<std::string>& patterns) const {
    for (const auto& pattern : patterns) {
        try {
            std::regex regex_pattern(pattern);
            if (std::regex_match(file_path, regex_pattern)) {
                return true;
            }
        } catch (const std::regex_error&) {
            // Invalid regex pattern, skip
        }
    }
    return false;
}

void DigestVerifier::notify_callbacks(const VerificationResult& result) {
    for (const auto& [id, callback] : callbacks_) {
        try {
            callback(result);
        } catch (const std::exception&) {
            // Ignore callback errors
        }
    }
}

std::string DigestVerifier::algorithm_to_string(DigestAlgorithm algorithm) const {
    switch (algorithm) {
        case DigestAlgorithm::SHA256: return "SHA256";
        case DigestAlgorithm::SHA512: return "SHA512";
        case DigestAlgorithm::MD5: return "MD5";
        case DigestAlgorithm::SHA1: return "SHA1";
        default: return "UNKNOWN";
    }
}

std::string DigestVerifier::status_to_string(VerificationStatus status) const {
    switch (status) {
        case VerificationStatus::Verified: return "VERIFIED";
        case VerificationStatus::Failed: return "FAILED";
        case VerificationStatus::Error: return "ERROR";
        case VerificationStatus::Pending: return "PENDING";
        case VerificationStatus::NotApplicable: return "NOT_APPLICABLE";
        default: return "UNKNOWN";
    }
}

bool DigestVerifier::save_manifest(const LibraryDigestManifest& manifest) const {
    if (storage_path_.empty()) {
        return false;
    }

    std::string manifest_path = storage_path_ + "/" + manifest.library_name + ".json";
    std::ofstream file(manifest_path);
    if (!file.is_open()) {
        return false;
    }

    file << manifest.to_json();
    return true;
}

std::optional<LibraryDigestManifest> DigestVerifier::load_manifest(const std::string& library_name) const {
    if (storage_path_.empty()) {
        return std::nullopt;
    }

    std::string manifest_path = storage_path_ + "/" + library_name + ".json";
    std::ifstream file(manifest_path);
    if (!file.is_open()) {
        return std::nullopt;
    }

    try {
        nlohmann::json j;
        file >> j;
        return LibraryDigestManifest::from_json(j);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// DigestUtils namespace implementation
namespace DigestUtils {
    std::string calculate_string_digest(const std::string& data, DigestAlgorithm algorithm) {
        const EVP_MD* md = nullptr;
        switch (algorithm) {
            case DigestAlgorithm::SHA256:
                md = EVP_sha256();
                break;
            case DigestAlgorithm::SHA512:
                md = EVP_sha512();
                break;
            case DigestAlgorithm::MD5:
                md = EVP_md5();
                break;
            case DigestAlgorithm::SHA1:
                md = EVP_sha1();
                break;
            default:
                return "";
        }

        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        if (!ctx) {
            return "";
        }

        if (EVP_DigestInit_ex(ctx, md, nullptr) != 1) {
            EVP_MD_CTX_free(ctx);
            return "";
        }

        EVP_DigestUpdate(ctx, data.c_str(), data.length());

        unsigned char hash[EVP_MAX_MD_SIZE];
        unsigned int hash_len = 0;
        EVP_DigestFinal_ex(ctx, hash, &hash_len);
        EVP_MD_CTX_free(ctx);

        std::stringstream ss;
        for (unsigned int i = 0; i < hash_len; ++i) {
            ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
        }

        return ss.str();
    }

    bool compare_digests(const std::string& digest1, const std::string& digest2) {
        return digest1 == digest2;
    }

    std::string format_digest(const std::string& digest, const std::string& format) {
        if (format == "hex") {
            return digest;
        } else if (format == "upper") {
            std::string upper = digest;
            std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
            return upper;
        }
        return digest;
    }

    bool validate_digest_format(const std::string& digest, DigestAlgorithm algorithm) {
        size_t expected_length = 0;
        switch (algorithm) {
            case DigestAlgorithm::SHA256:
                expected_length = 64;
                break;
            case DigestAlgorithm::SHA512:
                expected_length = 128;
                break;
            case DigestAlgorithm::MD5:
                expected_length = 32;
                break;
            case DigestAlgorithm::SHA1:
                expected_length = 40;
                break;
            default:
                return false;
        }

        if (digest.length() != expected_length) {
            return false;
        }

        return std::all_of(digest.begin(), digest.end(), [](char c) {
            return std::isxdigit(c);
        });
    }

    std::string generate_test_digest(DigestAlgorithm algorithm) {
        std::string test_data = "test_data_for_digest_verification";
        return calculate_string_digest(test_data, algorithm);
    }
}

} // namespace integration
} // namespace puzzle71