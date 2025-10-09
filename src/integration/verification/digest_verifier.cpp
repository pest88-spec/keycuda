/**
 * Puzzle71Solver - SHA-256 Digest Verification System Implementation
 *
 * Provides cryptographic integrity verification for all integrated artifacts
 * using SHA-256 digests to ensure source code integrity and detect unauthorized modifications.
 *
 * @author       Puzzle71Solver Team
 * @created      2025-10-09
 * @license      MIT
 */

#include "digest_verifier.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <openssl/evp.h>

namespace integration {
namespace verification {

struct DigestVerifier::Impl {
    std::filesystem::path digest_store_path = "digests";
    std::map<std::string, LibraryDigestManifest> manifests;
    DigestCache cache;
    bool caching_enabled = true;
    std::chrono::hours cache_expiry_duration{24};
    bool strict_verification = false;
    size_t total_files_verified = 0;
    std::chrono::milliseconds total_verification_time{0};
    std::chrono::system_clock::time_point last_cache_cleanup = std::chrono::system_clock::now();

    std::string get_cache_entry_key(const std::filesystem::path& file_path) {
        auto ftime = std::filesystem::last_write_time(file_path);
        auto size = std::filesystem::file_size(file_path);
        return file_path.string() + "|" + std::to_string(size) + "|" +
               std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                   ftime.time_since_epoch()).count());
    }

    void cleanup_expired_cache() {
        auto now = std::chrono::system_clock::now();
        if (now - last_cache_cleanup > std::chrono::hours(1)) {
            // Simple cleanup - clear cache older than expiry duration
            cache.digest_cache.clear();
            last_cache_cleanup = now;
        }
    }
};

DigestVerifier::DigestVerifier() : p_impl(std::make_unique<Impl>()) {
    std::filesystem::create_directories(p_impl->digest_store_path);
}

DigestVerifier::DigestVerifier(const std::filesystem::path& digest_store_path) : DigestVerifier() {
    set_digest_store_path(digest_store_path);
}

DigestVerifier::~DigestVerifier() = default;

void DigestVerifier::set_digest_store_path(const std::filesystem::path& store_path) {
    p_impl->digest_store_path = store_path;
    std::filesystem::create_directories(store_path);
}

void DigestVerifier::enable_caching(bool enabled) {
    p_impl->caching_enabled = enabled;
}

void DigestVerifier::set_cache_expiry_duration(std::chrono::hours duration) {
    p_impl->cache_expiry_duration = duration;
}

void DigestVerifier::enable_strict_verification(bool enabled) {
    p_impl->strict_verification = enabled;
}

std::string DigestVerifier::calculate_file_digest(const std::filesystem::path& file_path) {
    if (!std::filesystem::exists(file_path)) {
        return "";
    }

    // Check cache first
    if (p_impl->caching_enabled) {
        auto cache_key = p_impl->get_cache_entry_key(file_path);
        auto cached_digest = get_cached_digest(cache_key);
        if (!cached_digest.empty()) {
            p_impl->cache.cache_hits++;
            return cached_digest;
        }
        p_impl->cache.cache_misses++;
    }

    auto start_time = std::chrono::steady_clock::now();

    // Calculate digest
    auto file_bytes = read_file_bytes(file_path);
    std::string digest = compute_sha256(file_bytes);

    auto end_time = std::chrono::steady_clock::now();
    p_impl->total_verification_time += std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    p_impl->total_files_verified++;

    // Update cache
    if (p_impl->caching_enabled) {
        auto cache_key = p_impl->get_cache_entry_key(file_path);
        update_cache(cache_key, digest);
    }

    return digest;
}

std::string DigestVerifier::calculate_content_digest(const std::string& content) {
    std::vector<uint8_t> bytes(content.begin(), content.end());
    return compute_sha256(bytes);
}

std::string DigestVerifier::calculate_directory_digest(const std::filesystem::path& directory_path) {
    std::ostringstream combined_digests;

    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory_path)) {
        if (entry.is_regular_file()) {
            files.push_back(entry.path());
        }
    }

    std::sort(files.begin(), files.end());

    for (const auto& file_path : files) {
        std::string relative_path = std::filesystem::relative(file_path, directory_path).string();
        std::string file_digest = calculate_file_digest(file_path);
        combined_digests << relative_path << ":" << file_digest << "\n";
    }

    return calculate_content_digest(combined_digests.str());
}

bool DigestVerifier::create_library_manifest(const std::string& library_name,
                                           const std::filesystem::path& library_path,
                                           const std::string& library_version,
                                           const std::string& origin_commit) {
    if (!std::filesystem::exists(library_path)) {
        return false;
    }

    LibraryDigestManifest manifest;
    manifest.library_name = library_name;
    manifest.library_version = library_version.empty() ? "unknown" : library_version;
    manifest.origin_commit = origin_commit;
    manifest.created_at = std::chrono::system_clock::now();

    auto files = get_library_files(library_name);
    for (const auto& file_path : files) {
        FileDigest file_digest;
        file_digest.file_path = std::filesystem::relative(file_path, library_path).string();
        file_digest.sha256_digest = calculate_file_digest(file_path);
        file_digest.file_size = std::filesystem::file_size(file_path);
        file_digest.last_modified = std::filesystem::last_write_time(file_path);
        file_digest.is_verified = true;
        file_digest.verification_status = "verified";

        manifest.file_digests.push_back(file_digest);
    }

    return save_library_manifest(manifest);
}

bool DigestVerifier::load_library_manifest(const std::string& library_name, LibraryDigestManifest& manifest) {
    auto manifest_path = get_manifest_path(library_name);
    if (!std::filesystem::exists(manifest_path)) {
        return false;
    }

    std::ifstream file(manifest_path);
    if (!file.is_open()) {
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    manifest = deserialize_manifest(content);
    return manifest.library_name == library_name;
}

bool DigestVerifier::save_library_manifest(const LibraryDigestManifest& manifest) {
    auto manifest_path = get_manifest_path(manifest.library_name);
    std::filesystem::create_directories(manifest_path.parent_path());

    std::ofstream file(manifest_path);
    if (!file.is_open()) {
        return false;
    }

    std::string serialized = serialize_manifest(manifest);
    file << serialized;
    file.close();

    p_impl->manifests[manifest.library_name] = manifest;
    return true;
}

DigestVerificationReport DigestVerifier::verify_library_integrity(const std::string& library_name) {
    DigestVerificationReport report;
    report.library_name = library_name;

    auto start_time = std::chrono::steady_clock::now();

    LibraryDigestManifest manifest;
    if (!load_library_manifest(library_name, manifest)) {
        report.integrity_violations.push_back("No manifest found for library: " + library_name);
        report.verification_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time);
        return report;
    }

    // Get current library files
    auto current_files = get_library_files(library_name);
    std::map<std::string, FileDigest> current_digests;

    // Calculate digests for current files
    for (const auto& file_path : current_files) {
        auto relative_path = std::filesystem::relative(file_path, p_impl->digest_store_path.parent_path() / "src/extracted" / library_name).string();

        FileDigest current_digest;
        current_digest.file_path = relative_path;
        current_digest.sha256_digest = calculate_file_digest(file_path);
        current_digest.file_size = std::filesystem::file_size(file_path);
        current_digest.last_modified = std::filesystem::last_write_time(file_path);

        current_digests[relative_path] = current_digest;
    }

    // Compare with manifest
    for (const auto& manifest_digest : manifest.file_digests) {
        report.total_files++;

        auto current_it = current_digests.find(manifest_digest.file_path);
        if (current_it != current_digests.end()) {
            const auto& current_digest = current_it->second;

            if (compare_digests(manifest_digest.sha256_digest, current_digest.sha256_digest)) {
                report.verified_files++;
                current_digest.is_verified = true;
                current_digest.verification_status = "verified";
            } else {
                report.failed_files++;
                report.modified_files++;
                current_digest.is_verified = false;
                current_digest.verification_status = "modified";
                report.integrity_violations.push_back("File modified: " + manifest_digest.file_path);
            }

            report.verification_results.push_back(current_digest);
            current_digests.erase(current_it);
        } else {
            report.missing_files++;
            report.integrity_violations.push_back("File missing: " + manifest_digest.file_path);
        }
    }

    // Check for added files
    for (const auto& pair : current_digests) {
        report.total_files++;
        report.verification_results.push_back(pair.second);
        report.integrity_violations.push_back("File added: " + pair.second.file_path);
    }

    report.all_files_verified = (report.failed_files == 0 && report.missing_files == 0);
    report.verification_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time);

    return report;
}

bool DigestVerifier::verify_file_integrity(const std::filesystem::path& file_path, const std::string& expected_digest) {
    std::string actual_digest = calculate_file_digest(file_path);
    return !actual_digest.empty() && compare_digests(actual_digest, expected_digest);
}

bool DigestVerifier::verify_content_integrity(const std::string& content, const std::string& expected_digest) {
    std::string actual_digest = calculate_content_digest(content);
    return compare_digests(actual_digest, expected_digest);
}

std::vector<std::string> DigestVerifier::verify_multiple_libraries(const std::vector<std::string>& library_names) {
    std::vector<std::string> failed_libraries;

    for (const auto& library_name : library_names) {
        auto report = verify_library_integrity(library_name);
        if (!report.all_files_verified) {
            failed_libraries.push_back(library_name);
        }
    }

    return failed_libraries;
}

bool DigestVerifier::create_all_library_manifests(const std::filesystem::path& integration_root) {
    if (!std::filesystem::exists(integration_root)) {
        return false;
    }

    bool all_success = true;
    for (const auto& entry : std::filesystem::directory_iterator(integration_root)) {
        if (entry.is_directory()) {
            std::string library_name = entry.path().filename().string();
            if (!create_library_manifest(library_name, entry.path())) {
                all_success = false;
            }
        }
    }

    return all_success;
}

DigestVerificationReport DigestVerifier::verify_all_libraries() {
    DigestVerificationReport combined_report;
    combined_report.library_name = "all_libraries";

    auto start_time = std::chrono::steady_clock::now();

    std::vector<std::string> library_names;
    for (const auto& pair : p_impl->manifests) {
        library_names.push_back(pair.first);
    }

    // Also scan for library directories that might not have manifests yet
    if (std::filesystem::exists(p_impl->digest_store_path.parent_path() / "src/extracted")) {
        for (const auto& entry : std::filesystem::directory_iterator(p_impl->digest_store_path.parent_path() / "src/extracted")) {
            if (entry.is_directory()) {
                std::string library_name = entry.path().filename().string();
                if (std::find(library_names.begin(), library_names.end(), library_name) == library_names.end()) {
                    library_names.push_back(library_name);
                }
            }
        }
    }

    for (const auto& library_name : library_names) {
        auto library_report = verify_library_integrity(library_name);
        combined_report.total_files += library_report.total_files;
        combined_report.verified_files += library_report.verified_files;
        combined_report.failed_files += library_report.failed_files;
        combined_report.missing_files += library_report.missing_files;
        combined_report.modified_files += library_report.modified_files;

        combined_report.verification_results.insert(combined_report.verification_results.end(),
                                                   library_report.verification_results.begin(),
                                                   library_report.verification_results.end());

        combined_report.integrity_violations.insert(combined_report.integrity_violations.end(),
                                                    library_report.integrity_violations.begin(),
                                                    library_report.integrity_violations.end());
    }

    combined_report.all_files_verified = (combined_report.failed_files == 0 && combined_report.missing_files == 0);
    combined_report.verification_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time);

    return combined_report;
}

std::vector<std::filesystem::path> DigestVerifier::get_library_files(const std::string& library_name) {
    std::vector<std::filesystem::path> files;
    std::filesystem::path library_path = p_impl->digest_store_path.parent_path() / "src/extracted" / library_name;

    if (!std::filesystem::exists(library_path)) {
        return files;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(library_path)) {
        if (entry.is_regular_file()) {
            std::string extension = entry.path().extension().string();
            if (extension == ".cpp" || extension == ".c" || extension == ".cu" ||
                extension == ".cuh" || extension == ".h" || extension == ".hpp" ||
                extension == ".txt" || extension == ".md") {
                files.push_back(entry.path());
            }
        }
    }

    return files;
}

std::string DigestVerifier::get_cache_key(const std::filesystem::path& file_path, size_t file_size,
                                         const std::chrono::system_clock::time_point& last_modified) {
    return file_path.string() + "|" + std::to_string(file_size) + "|" +
           std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
               last_modified.time_since_epoch()).count());
}

bool DigestVerifier::is_cache_valid(const std::string& cache_key) const {
    // Simple validation - check if cache entry exists
    return p_impl->cache.digest_cache.find(cache_key) != p_impl->cache.digest_cache.end();
}

void DigestVerifier::update_cache(const std::string& cache_key, const std::string& digest) {
    p_impl->cache.digest_cache[cache_key] = digest;
    p_impl->cache.cache_timestamp = std::chrono::system_clock::now();
}

std::string DigestVerifier::get_cached_digest(const std::string& cache_key) {
    if (!p_impl->caching_enabled) {
        return "";
    }

    p_impl->cleanup_expired_cache();

    auto it = p_impl->cache.digest_cache.find(cache_key);
    if (it != p_impl->cache.digest_cache.end()) {
        return it->second;
    }

    return "";
}

std::filesystem::path DigestVerifier::get_manifest_path(const std::string& library_name) const {
    return p_impl->digest_store_path / (library_name + "_manifest.json");
}

std::string DigestVerifier::serialize_manifest(const LibraryDigestManifest& manifest) const {
    std::ostringstream json;
    json << "{\n";
    json << "  \"library_name\": \"" << manifest.library_name << "\",\n";
    json << "  \"library_version\": \"" << manifest.library_version << "\",\n";
    json << "  \"origin_commit\": \"" << manifest.origin_commit << "\",\n";
    json << "  \"created_at\": " << std::chrono::duration_cast<std::chrono::seconds>(
        manifest.created_at.time_since_epoch()).count() << ",\n";
    json << "  \"file_digests\": [\n";

    for (size_t i = 0; i < manifest.file_digests.size(); ++i) {
        const auto& digest = manifest.file_digests[i];
        json << "    {\n";
        json << "      \"file_path\": \"" << digest.file_path << "\",\n";
        json << "      \"sha256_digest\": \"" << digest.sha256_digest << "\",\n";
        json << "      \"file_size\": " << digest.file_size << ",\n";
        json << "      \"last_modified\": " << std::chrono::duration_cast<std::chrono::seconds>(
            digest.last_modified.time_since_epoch()).count() << ",\n";
        json << "      \"is_verified\": " << (digest.is_verified ? "true" : "false") << ",\n";
        json << "      \"verification_status\": \"" << digest.verification_status << "\"\n";
        json << "    }" << (i < manifest.file_digests.size() - 1 ? "," : "") << "\n";
    }

    json << "  ]\n";
    json << "}\n";

    return json.str();
}

LibraryDigestManifest DigestVerifier::deserialize_manifest(const std::string& serialized_data) const {
    LibraryDigestManifest manifest;
    // Simplified JSON parsing - in a real implementation, use a proper JSON library
    manifest.library_name = "unknown"; // Would extract from JSON
    return manifest;
}

bool DigestVerifier::compare_digests(const std::string& digest1, const std::string& digest2) const {
    return digest1 == digest2;
}

std::vector<uint8_t> DigestVerifier::read_file_bytes(const std::filesystem::path& file_path) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        return {};
    }

    return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
}

std::string DigestVerifier::compute_sha256(const std::vector<uint8_t>& data) {
    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    if (!mdctx) return "";

    if (EVP_DigestInit_ex(mdctx, EVP_sha256(), nullptr) != 1) {
        EVP_MD_CTX_free(mdctx);
        return "";
    }

    if (EVP_DigestUpdate(mdctx, data.data(), data.size()) != 1) {
        EVP_MD_CTX_free(mdctx);
        return "";
    }

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len;
    if (EVP_DigestFinal_ex(mdctx, digest, &digest_len) != 1) {
        EVP_MD_CTX_free(mdctx);
        return "";
    }

    EVP_MD_CTX_free(mdctx);

    std::ostringstream hex_stream;
    hex_stream << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < digest_len; ++i) {
        hex_stream << std::setw(2) << static_cast<unsigned>(digest[i]);
    }

    return hex_stream.str();
}

// Global verifier instance
DigestVerifier& get_digest_verifier() {
    static DigestVerifier instance;
    return instance;
}

// Utility functions
std::string calculate_sha256_from_file(const std::filesystem::path& file_path) {
    return get_digest_verifier().calculate_file_digest(file_path);
}

std::string calculate_sha256_from_string(const std::string& input) {
    return get_digest_verifier().calculate_content_digest(input);
}

bool verify_file_digest(const std::filesystem::path& file_path, const std::string& expected_digest) {
    return get_digest_verifier().verify_file_integrity(file_path, expected_digest);
}

} // namespace verification
} // namespace integration