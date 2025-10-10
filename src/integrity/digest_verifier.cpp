/**
 * SHA-256 Digest Verification System Implementation
 *
 * @origin       https://github.com/Puzzle71Solver/Puzzle71Solver
 * @origin_path  src/integrity/digest_verifier.cpp
 * @origin_commit <current_commit>
 * @origin_license MIT
 * @extracted_date   2025-10-10
 * @extracted_by     Puzzle71Solver Team
 * @modifications    Created for third-party dependency integration optimization
 * @spdx_license_identifier MIT
 */

#include "digest_verifier.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <filesystem>
#include <regex>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

DigestVerifier::DigestVerifier(
    const std::string& storage_path,
    const std::string& manifest_file_name,
    bool auto_save
) : storage_path_(storage_path),
    manifest_file_path_(storage_path_ + "/" + manifest_file_name),
    auto_save_enabled_(auto_save) {

    // Create storage directory if it doesn't exist
    if (!storage_path_.empty()) {
        std::filesystem::create_directories(storage_path_);
    }

    // Load existing manifest
    load_manifest();
}

DigestVerifier::~DigestVerifier() {
    if (auto_save_enabled_) {
        force_save();
    }
}

std::string DigestVerifier::calculate_file_digest(const std::string& file_path) const {
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        return "";
    }

    SHA256_CTX sha256;
    SHA256_Init(&sha256);

    char buffer[8192];
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

std::string DigestVerifier::calculate_data_digest(const std::vector<uint8_t>& data) const {
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, data.data(), data.size());

    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_Final(hash, &sha256);

    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }

    return ss.str();
}

bool DigestVerifier::calculate_file_digest(const std::string& file_path, bool update_existing) {
    if (!std::filesystem::exists(file_path)) {
        return false;
    }

    std::string relative_path = get_relative_path(file_path);

    // Check if digest already exists and we shouldn't update
    if (!update_existing && manifest_.file_digests.find(relative_path) != manifest_.file_digests.end()) {
        return true;
    }

    FileDigest digest_info;
    digest_info.file_path = relative_path;
    digest_info.sha256_digest = calculate_file_digest(file_path);
    digest_info.calculated_at = std::chrono::system_clock::now();
    digest_info.file_size = std::filesystem::file_size(file_path);
    digest_info.file_permissions = get_file_permissions(file_path);

    if (digest_info.sha256_digest.empty()) {
        return false;
    }

    manifest_.file_digests[relative_path] = digest_info;
    manifest_.updated_at = std::chrono::system_clock::now();

    if (auto_save_enabled_) {
        return save_manifest();
    }

    return true;
}

size_t DigestVerifier::calculate_directory_digests(const std::string& directory_path,
                                                   const std::vector<std::string>& exclusion_patterns) {
    if (!std::filesystem::exists(directory_path)) {
        return 0;
    }

    // Update exclusion patterns
    manifest_.exclusion_patterns = exclusion_patterns;

    size_t processed_count = 0;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory_path)) {
        if (entry.is_regular_file()) {
            std::string file_path = entry.path().string();
            std::string relative_path = get_relative_path(file_path);

            if (should_include_file(relative_path)) {
                if (calculate_file_digest(file_path, true)) {
                    processed_count++;
                }
            }
        }
    }

    return processed_count;
}

size_t DigestVerifier::calculate_file_digests(const std::vector<std::string>& file_paths) {
    size_t processed_count = 0;

    for (const auto& file_path : file_paths) {
        if (calculate_file_digest(file_path, true)) {
            processed_count++;
        }
    }

    return processed_count;
}

DigestVerifier::VerificationResult DigestVerifier::verify_file_digest(const std::string& file_path) const {
    VerificationResult result;
    result.file_path = file_path;

    if (!std::filesystem::exists(file_path)) {
        result.status_message = "File does not exist";
        return result;
    }

    std::string relative_path = get_relative_path(file_path);
    auto it = manifest_.file_digests.find(relative_path);

    if (it == manifest_.file_digests.end()) {
        result.status_message = "No digest found for file";
        return result;
    }

    result.expected_digest = it->second.sha256_digest;
    result.actual_digest = calculate_file_digest(file_path);

    if (result.actual_digest == result.expected_digest) {
        result.is_valid = true;
        result.status_message = "Digest verification successful";
    } else {
        result.is_valid = false;
        result.status_message = "Digest mismatch - file may be modified";
    }

    result.verified_at = std::chrono::system_clock::now();

    return result;
}

DigestVerifier::BatchVerificationResult DigestVerifier::verify_all_digests() const {
    BatchVerificationResult batch_result;
    auto start_time = std::chrono::high_resolution_clock::now();

    for (const auto& [relative_path, digest_info] : manifest_.file_digests) {
        std::string full_path = manifest_.root_directory.empty() ?
                                relative_path :
                                manifest_.root_directory + "/" + relative_path;

        VerificationResult result = verify_file_digest(full_path);
        batch_result.individual_results.push_back(result);
        batch_result.total_files++;

        if (std::filesystem::exists(full_path)) {
            if (result.is_valid) {
                batch_result.verified_files++;
            } else {
                batch_result.failed_files++;
            }
        } else {
            batch_result.missing_files++;
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    batch_result.verification_duration_ms = duration.count() / 1000.0;
    batch_result.batch_completed_at = std::chrono::system_clock::now();

    return batch_result;
}

DigestVerifier::BatchVerificationResult DigestVerifier::verify_file_digests(const std::vector<std::string>& file_paths) const {
    BatchVerificationResult batch_result;
    auto start_time = std::chrono::high_resolution_clock::now();

    for (const auto& file_path : file_paths) {
        VerificationResult result = verify_file_digest(file_path);
        batch_result.individual_results.push_back(result);
        batch_result.total_files++;

        if (std::filesystem::exists(file_path)) {
            if (result.is_valid) {
                batch_result.verified_files++;
            } else {
                batch_result.failed_files++;
            }
        } else {
            batch_result.missing_files++;
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    batch_result.verification_duration_ms = duration.count() / 1000.0;
    batch_result.batch_completed_at = std::chrono::system_clock::now();

    return batch_result;
}

bool DigestVerifier::verify_data_digest(const std::vector<uint8_t>& data, const std::string& expected_digest) const {
    std::string actual_digest = calculate_data_digest(data);
    return actual_digest == expected_digest;
}

DigestVerifier::FileDigest DigestVerifier::get_file_digest(const std::string& file_path) const {
    std::string relative_path = get_relative_path(file_path);
    auto it = manifest_.file_digests.find(relative_path);

    if (it != manifest_.file_digests.end()) {
        return it->second;
    }

    return FileDigest();
}

bool DigestVerifier::remove_file_digest(const std::string& file_path) {
    std::string relative_path = get_relative_path(file_path);
    auto it = manifest_.file_digests.find(relative_path);

    if (it != manifest_.file_digests.end()) {
        manifest_.file_digests.erase(it);
        manifest_.updated_at = std::chrono::system_clock::now();

        if (auto_save_enabled_) {
            return save_manifest();
        }

        return true;
    }

    return false;
}

void DigestVerifier::update_manifest_metadata(const std::string& key, const std::string& value) {
    manifest_.manifest_metadata[key] = value;
    manifest_.updated_at = std::chrono::system_clock::now();

    if (auto_save_enabled_) {
        save_manifest();
    }
}

void DigestVerifier::add_exclusion_pattern(const std::string& pattern) {
    if (std::find(manifest_.exclusion_patterns.begin(), manifest_.exclusion_patterns.end(), pattern) ==
        manifest_.exclusion_patterns.end()) {
        manifest_.exclusion_patterns.push_back(pattern);
        manifest_.updated_at = std::chrono::system_clock::now();

        if (auto_save_enabled_) {
            save_manifest();
        }
    }
}

void DigestVerifier::remove_exclusion_pattern(const std::string& pattern) {
    auto it = std::remove(manifest_.exclusion_patterns.begin(), manifest_.exclusion_patterns.end(), pattern);
    if (it != manifest_.exclusion_patterns.end()) {
        manifest_.exclusion_patterns.erase(it, manifest_.exclusion_patterns.end());
        manifest_.updated_at = std::chrono::system_clock::now();

        if (auto_save_enabled_) {
            save_manifest();
        }
    }
}

bool DigestVerifier::export_manifest(const std::string& export_path, const std::string& format) const {
    std::ofstream file(export_path);
    if (!file.is_open()) {
        return false;
    }

    if (format == "json") {
        json export_data;
        export_data["manifest_version"] = manifest_.manifest_version;
        export_data["created_at"] = format_timestamp(manifest_.created_at);
        export_data["updated_at"] = format_timestamp(manifest_.updated_at);
        export_data["created_by"] = manifest_.created_by;
        export_data["root_directory"] = manifest_.root_directory;
        export_data["exclusion_patterns"] = manifest_.exclusion_patterns;
        export_data["manifest_metadata"] = manifest_.manifest_metadata;

        export_data["file_digests"] = json::object();
        for (const auto& [path, digest] : manifest_.file_digests) {
            json digest_json;
            digest_json["sha256_digest"] = digest.sha256_digest;
            digest_json["algorithm"] = digest.algorithm;
            digest_json["calculated_at"] = format_timestamp(digest.calculated_at);
            digest_json["file_size"] = digest.file_size;
            digest_json["file_permissions"] = digest.file_permissions;
            digest_json["metadata"] = digest.metadata;

            export_data["file_digests"][path] = digest_json;
        }

        file << std::setw(4) << export_data << std::endl;
    } else if (format == "csv") {
        file << "file_path,sha256_digest,algorithm,calculated_at,file_size,file_permissions\n";
        for (const auto& [path, digest] : manifest_.file_digests) {
            file << "\"" << path << "\","
                 << digest.sha256_digest << ","
                 << digest.algorithm << ","
                 << format_timestamp(digest.calculated_at) << ","
                 << digest.file_size << ","
                 << "\"" << digest.file_permissions << "\"\n";
        }
    }

    return true;
}

bool DigestVerifier::import_manifest(const std::string& import_path, bool merge_mode) {
    std::ifstream file(import_path);
    if (!file.is_open()) {
        return false;
    }

    try {
        json import_data;
        file >> import_data;

        DigestManifest imported_manifest;
        imported_manifest.manifest_version = import_data.value("manifest_version", "1.0");
        imported_manifest.created_at = parse_timestamp(import_data.value("created_at", ""));
        imported_manifest.updated_at = parse_timestamp(import_data.value("updated_at", ""));
        imported_manifest.created_by = import_data.value("created_by", "imported");
        imported_manifest.root_directory = import_data.value("root_directory", "");
        imported_manifest.exclusion_patterns = import_data.value("exclusion_patterns", std::vector<std::string>{});
        imported_manifest.manifest_metadata = import_data.value("manifest_metadata", std::map<std::string, std::string>{});

        for (auto& [path, digest_json] : import_data["file_digests"].items()) {
            FileDigest digest;
            digest.file_path = path;
            digest.sha256_digest = digest_json.value("sha256_digest", "");
            digest.algorithm = digest_json.value("algorithm", "SHA-256");
            digest.calculated_at = parse_timestamp(digest_json.value("calculated_at", ""));
            digest.file_size = digest_json.value("file_size", 0);
            digest.file_permissions = digest_json.value("file_permissions", "");
            digest.metadata = digest_json.value("metadata", std::map<std::string, std::string>{});

            imported_manifest.file_digests[path] = digest;
        }

        if (!merge_mode) {
            manifest_ = imported_manifest;
        } else {
            // Merge manifests
            for (const auto& [path, digest] : imported_manifest.file_digests) {
                manifest_.file_digests[path] = digest;
            }
            if (!imported_manifest.root_directory.empty()) {
                manifest_.root_directory = imported_manifest.root_directory;
            }
            for (const auto& pattern : imported_manifest.exclusion_patterns) {
                add_exclusion_pattern(pattern);
            }
        }

        manifest_.updated_at = std::chrono::system_clock::now();

        if (auto_save_enabled_) {
            return save_manifest();
        }

        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

std::string DigestVerifier::generate_report(const std::string& format) const {
    if (format == "json") {
        json report;
        report["report_generated"] = format_timestamp(std::chrono::system_clock::now());
        report["manifest_version"] = manifest_.manifest_version;
        report["total_files"] = manifest_.file_digests.size();
        report["root_directory"] = manifest_.root_directory;

        // Calculate total size
        size_t total_size = 0;
        for (const auto& [path, digest] : manifest_.file_digests) {
            total_size += digest.file_size;
        }
        report["total_size_bytes"] = total_size;

        // File extension distribution
        std::map<std::string, size_t> extension_counts;
        for (const auto& [path, digest] : manifest_.file_digests) {
            std::string extension = std::filesystem::path(path).extension().string();
            if (extension.empty()) extension = "(no extension)";
            extension_counts[extension]++;
        }
        report["file_extension_distribution"] = extension_counts;

        return report.dump(4);
    } else {
        // Text format
        std::stringstream ss;
        ss << "Digest Verification Report\n";
        ss << "========================\n\n";
        ss << "Total Files: " << manifest_.file_digests.size() << "\n";
        ss << "Root Directory: " << manifest_.root_directory << "\n";
        ss << "Manifest Version: " << manifest_.manifest_version << "\n";
        ss << "Created: " << format_timestamp(manifest_.created_at) << "\n";
        ss << "Updated: " << format_timestamp(manifest_.updated_at) << "\n\n";

        size_t total_size = 0;
        for (const auto& [path, digest] : manifest_.file_digests) {
            total_size += digest.file_size;
        }
        ss << "Total Size: " << total_size << " bytes\n\n";

        ss << "Files by Extension:\n";
        std::map<std::string, size_t> extension_counts;
        for (const auto& [path, digest] : manifest_.file_digests) {
            std::string extension = std::filesystem::path(path).extension().string();
            if (extension.empty()) extension = "(no extension)";
            extension_counts[extension]++;
        }

        for (const auto& [ext, count] : extension_counts) {
            ss << "  " << ext << ": " << count << " files\n";
        }

        return ss.str();
    }
}

std::vector<std::string> DigestVerifier::find_files_by_digest(const std::string& sha256_digest) const {
    std::vector<std::string> matching_files;

    for (const auto& [path, digest] : manifest_.file_digests) {
        if (digest.sha256_digest == sha256_digest) {
            matching_files.push_back(path);
        }
    }

    return matching_files;
}

DigestVerifier::DigestStats DigestVerifier::get_statistics() const {
    DigestStats stats;

    stats.total_files = manifest_.file_digests.size();

    for (const auto& [path, digest] : manifest_.file_digests) {
        stats.total_size_bytes += digest.file_size;

        // File extension distribution
        std::string extension = std::filesystem::path(path).extension().string();
        if (extension.empty()) extension = "(no extension)";
        stats.file_extension_distribution[extension]++;

        // Oldest and newest digests
        if (stats.oldest_digest == std::chrono::system_clock::time_point{} ||
            digest.calculated_at < stats.oldest_digest) {
            stats.oldest_digest = digest.calculated_at;
        }
        if (stats.newest_digest == std::chrono::system_clock::time_point{} ||
            digest.calculated_at > stats.newest_digest) {
            stats.newest_digest = digest.calculated_at;
        }
    }

    // Count duplicate files
    std::map<std::string, size_t> digest_counts;
    for (const auto& [path, digest] : manifest_.file_digests) {
        digest_counts[digest.sha256_digest]++;
    }
    for (const auto& [digest, count] : digest_counts) {
        if (count > 1) {
            stats.duplicate_files_count += (count - 1);
        }
    }

    return stats;
}

std::map<std::string, std::vector<std::string>> DigestVerifier::find_duplicate_files() const {
    std::map<std::string, std::vector<std::string>> duplicates;

    for (const auto& [path, digest] : manifest_.file_digests) {
        duplicates[digest.sha256_digest].push_back(path);
    }

    // Remove entries with only one file
    auto it = duplicates.begin();
    while (it != duplicates.end()) {
        if (it->second.size() <= 1) {
            it = duplicates.erase(it);
        } else {
            ++it;
        }
    }

    return duplicates;
}

bool DigestVerifier::validate_manifest_integrity() const {
    // Check for duplicate file paths
    std::set<std::string> paths;
    for (const auto& [path, digest] : manifest_.file_digests) {
        if (paths.find(path) != paths.end()) {
            return false; // Duplicate path
        }
        paths.insert(path);
    }

    // Check for valid SHA-256 digests
    std::regex sha256_regex(R"(^[a-fA-F0-9]{64}$)");
    for (const auto& [path, digest] : manifest_.file_digests) {
        if (!std::regex_match(digest.sha256_digest, sha256_regex)) {
            return false; // Invalid digest format
        }
    }

    return true;
}

size_t DigestVerifier::cleanup_orphaned_entries() {
    size_t removed_count = 0;

    auto it = manifest_.file_digests.begin();
    while (it != manifest_.file_digests.end()) {
        std::string full_path = manifest_.root_directory.empty() ?
                                it->first :
                                manifest_.root_directory + "/" + it->first;

        if (!std::filesystem::exists(full_path)) {
            it = manifest_.file_digests.erase(it);
            removed_count++;
        } else {
            ++it;
        }
    }

    if (removed_count > 0) {
        manifest_.updated_at = std::chrono::system_clock::now();
        if (auto_save_enabled_) {
            save_manifest();
        }
    }

    return removed_count;
}

void DigestVerifier::set_auto_save(bool enabled) {
    auto_save_enabled_ = enabled;
}

bool DigestVerifier::force_save() const {
    return save_manifest();
}

bool DigestVerifier::has_unsaved_changes() const {
    // This is a simplified check - in a real implementation, you'd track changes more carefully
    return true;
}

void DigestVerifier::clear_all_digests() {
    manifest_.file_digests.clear();
    manifest_.updated_at = std::chrono::system_clock::now();

    if (auto_save_enabled_) {
        save_manifest();
    }
}

bool DigestVerifier::create_backup(const std::string& backup_path) const {
    return export_manifest(backup_path, "json");
}

bool DigestVerifier::restore_from_backup(const std::string& backup_path) {
    return import_manifest(backup_path, false);
}

// Private methods implementation
bool DigestVerifier::save_manifest() const {
    std::ofstream file(manifest_file_path_);
    if (!file.is_open()) {
        return false;
    }

    json manifest_json;
    manifest_json["manifest_version"] = manifest_.manifest_version;
    manifest_json["created_at"] = format_timestamp(manifest_.created_at);
    manifest_json["updated_at"] = format_timestamp(manifest_.updated_at);
    manifest_json["created_by"] = manifest_.created_by;
    manifest_json["root_directory"] = manifest_.root_directory;
    manifest_json["exclusion_patterns"] = manifest_.exclusion_patterns;
    manifest_json["manifest_metadata"] = manifest_.manifest_metadata;

    manifest_json["file_digests"] = json::object();
    for (const auto& [path, digest] : manifest_.file_digests) {
        json digest_json;
        digest_json["sha256_digest"] = digest.sha256_digest;
        digest_json["algorithm"] = digest.algorithm;
        digest_json["calculated_at"] = format_timestamp(digest.calculated_at);
        digest_json["file_size"] = digest.file_size;
        digest_json["file_permissions"] = digest.file_permissions;
        digest_json["metadata"] = digest.metadata;

        manifest_json["file_digests"][path] = digest_json;
    }

    file << std::setw(4) << manifest_json << std::endl;
    return true;
}

bool DigestVerifier::load_manifest() {
    if (!std::filesystem::exists(manifest_file_path_)) {
        // Create new manifest
        manifest_ = DigestManifest();
        return save_manifest();
    }

    std::ifstream file(manifest_file_path_);
    if (!file.is_open()) {
        return false;
    }

    try {
        json manifest_json;
        file >> manifest_json;

        manifest_.manifest_version = manifest_json.value("manifest_version", "1.0");
        manifest_.created_at = parse_timestamp(manifest_json.value("created_at", ""));
        manifest_.updated_at = parse_timestamp(manifest_json.value("updated_at", ""));
        manifest_.created_by = manifest_json.value("created_by", "system");
        manifest_.root_directory = manifest_json.value("root_directory", "");
        manifest_.exclusion_patterns = manifest_json.value("exclusion_patterns", std::vector<std::string>{});
        manifest_.manifest_metadata = manifest_json.value("manifest_metadata", std::map<std::string, std::string>{});

        for (auto& [path, digest_json] : manifest_json["file_digests"].items()) {
            FileDigest digest;
            digest.file_path = path;
            digest.sha256_digest = digest_json.value("sha256_digest", "");
            digest.algorithm = digest_json.value("algorithm", "SHA-256");
            digest.calculated_at = parse_timestamp(digest_json.value("calculated_at", ""));
            digest.file_size = digest_json.value("file_size", 0);
            digest.file_permissions = digest_json.value("file_permissions", "");
            digest.metadata = digest_json.value("metadata", std::map<std::string, std::string>{});

            manifest_.file_digests[path] = digest;
        }

        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

bool DigestVerifier::should_include_file(const std::string& file_path) const {
    for (const auto& pattern : manifest_.exclusion_patterns) {
        try {
            std::regex regex_pattern(pattern, std::regex_constants::icase);
            if (std::regex_match(file_path, regex_pattern)) {
                return false;
            }
        } catch (const std::regex_error&) {
            // If regex is invalid, treat as literal match
            if (file_path.find(pattern) != std::string::npos) {
                return false;
            }
        }
    }
    return true;
}

std::string DigestVerifier::get_relative_path(const std::string& file_path) const {
    if (manifest_.root_directory.empty()) {
        return file_path;
    }

    std::filesystem::path root_path(manifest_.root_directory);
    std::filesystem::path full_path(file_path);

    try {
        std::string relative_path = std::filesystem::relative(full_path, root_path).string();
        return relative_path;
    } catch (...) {
        return file_path; // Fallback to full path
    }
}

std::string DigestVerifier::format_timestamp(std::chrono::system_clock::time_point tp) const {
    auto time_t = std::chrono::system_clock::to_time_t(tp);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

std::chrono::system_clock::time_point DigestVerifier::parse_timestamp(const std::string& ts) const {
    if (ts.empty()) {
        return std::chrono::system_clock::now();
    }

    std::tm tm = {};
    std::istringstream ss(ts);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

std::string DigestVerifier::get_file_permissions(const std::filesystem::path& path) const {
    std::filesystem::perms perms = std::filesystem::status(path).permissions();

    std::string permissions = "rwxrwxrwx";

    // Owner permissions
    if ((perms & std::filesystem::perms::owner_read) == std::filesystem::perms::none) permissions[0] = '-';
    if ((perms & std::filesystem::perms::owner_write) == std::filesystem::perms::none) permissions[1] = '-';
    if ((perms & std::filesystem::perms::owner_exec) == std::filesystem::perms::none) permissions[2] = '-';

    // Group permissions
    if ((perms & std::filesystem::perms::group_read) == std::filesystem::perms::none) permissions[3] = '-';
    if ((perms & std::filesystem::perms::group_write) == std::filesystem::perms::none) permissions[4] = '-';
    if ((perms & std::filesystem::perms::group_exec) == std::filesystem::perms::none) permissions[5] = '-';

    // Other permissions
    if ((perms & std::filesystem::perms::others_read) == std::filesystem::perms::none) permissions[6] = '-';
    if ((perms & std::filesystem::perms::others_write) == std::filesystem::perms::none) permissions[7] = '-';
    if ((perms & std::filesystem::perms::others_exec) == std::filesystem::perms::none) permissions[8] = '-';

    return permissions;
}

// DigestVerificationSession implementation
DigestVerificationSession::DigestVerificationSession(const DigestVerifier& verifier)
    : verifier_(verifier), session_active_(true) {
}

DigestVerificationSession::~DigestVerificationSession() {
    if (session_active_) {
        end_session();
    }
}

bool DigestVerificationSession::verify_file(const std::string& file_path) {
    auto result = verifier_.verify_file_digest(file_path);

    if (result.is_valid) {
        verified_files_.push_back(file_path);
    } else {
        failed_files_.push_back(file_path);
    }

    return result.is_valid;
}

std::string DigestVerificationSession::get_session_summary() const {
    std::stringstream ss;
    ss << "Digest Verification Session Summary\n";
    ss << "===================================\n";
    ss << "Verified Files: " << verified_files_.size() << "\n";
    ss << "Failed Files: " << failed_files_.size() << "\n";
    ss << "Total Files: " << (verified_files_.size() + failed_files_.size()) << "\n";

    if (!failed_files_.empty()) {
        ss << "\nFailed Files:\n";
        for (const auto& file : failed_files_) {
            ss << "  " << file << "\n";
        }
    }

    return ss.str();
}

void DigestVerificationSession::end_session() {
    session_active_ = false;
}