#include "checkpoint_manifest.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>

namespace puzzle71::checkpoint {

namespace {

bool Matches(std::string_view value, std::string_view pattern) {
    if (value.empty()) {
        return false;
    }
    try {
        std::regex re(std::string(pattern), std::regex::ECMAScript);
        return std::regex_match(value.begin(), value.end(), re);
    } catch (const std::regex_error&) {
        return false;
    }
}

bool ValidateManifest(const Manifest& manifest) {
    if (manifest.version != "1.0") {
        return false;
    }
    if (manifest.path.empty()) {
        return false;
    }
    const std::string iso_pattern = R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$)";
    if (!Matches(manifest.created_at, iso_pattern)) {
        return false;
    }
    if (!Matches(manifest.processed_keys, R"(^0x[0-9a-fA-F]+$)")) {
        return false;
    }
    if (!Matches(manifest.shard_start, R"(^0x[0-9a-fA-F]+$)")) {
        return false;
    }
    if (!Matches(manifest.shard_end, R"(^0x[0-9a-fA-F]+$)")) {
        return false;
    }
    if (!Matches(manifest.next_scalar, R"(^0x[0-9a-fA-F]+$)")) {
        return false;
    }
    if (manifest.encryption_cipher != "AES-256-GCM") {
        return false;
    }
    if (!Matches(manifest.nonce, R"(^[A-Za-z0-9+/=]{16}$)")) {
        return false;
    }
    if (!Matches(manifest.salt, R"(^[A-Za-z0-9+/=]{24}$)")) {
        return false;
    }
    if (manifest.pbkdf2_iterations < 200000) {
        return false;
    }
    if (!Matches(manifest.payload_sha256, R"(^[0-9a-fA-F]{64}$)")) {
        return false;
    }
    if (!Matches(manifest.retention_expiry, iso_pattern)) {
        return false;
    }
    if (manifest.shard_id.empty()) {
        return false;
    }
    if (manifest.points_per_thread == 0) {
        return false;
    }
    if (manifest.keys_total == 0) {
        return false;
    }
    return true;
}

}  // namespace

std::string SerializeManifest(const Manifest& manifest) {
    nlohmann::json j = {
        {"version", manifest.version},
        {"path", manifest.path},
        {"created_at", manifest.created_at},
        {"processed_keys", manifest.processed_keys},
        {"shard_start", manifest.shard_start},
        {"shard_end", manifest.shard_end},
        {"next_scalar", manifest.next_scalar},
        {"encryption_cipher", manifest.encryption_cipher},
        {"nonce", manifest.nonce},
        {"salt", manifest.salt},
        {"pbkdf2_iterations", manifest.pbkdf2_iterations},
        {"payload_sha256", manifest.payload_sha256},
        {"retention_expiry", manifest.retention_expiry},
        {"shard_id", manifest.shard_id},
        {"grid_dim", manifest.grid_dim},
        {"block_dim", manifest.block_dim},
        {"points_per_thread", manifest.points_per_thread},
        {"keys_total", manifest.keys_total},
    };
    return j.dump(2);
}

std::optional<Manifest> DeserializeManifest(const std::string& json) {
    try {
        auto j = nlohmann::json::parse(json);
        Manifest manifest{};
        manifest.version = j.value("version", "");
        manifest.path = j.value("path", "");
        manifest.created_at = j.value("created_at", "");
        manifest.processed_keys = j.value("processed_keys", "");
        manifest.shard_start = j.value("shard_start", "");
        manifest.shard_end = j.value("shard_end", "");
        manifest.next_scalar = j.value("next_scalar", "");
        manifest.encryption_cipher = j.value("encryption_cipher", "");
        manifest.nonce = j.value("nonce", "");
       manifest.salt = j.value("salt", "");
       manifest.pbkdf2_iterations = j.value("pbkdf2_iterations", 200000u);
       manifest.payload_sha256 = j.value("payload_sha256", "");
       manifest.retention_expiry = j.value("retention_expiry", "");
       manifest.shard_id = j.value("shard_id", "");
        manifest.grid_dim = j.value("grid_dim", 0u);
        manifest.block_dim = j.value("block_dim", 0u);
        manifest.points_per_thread = j.value("points_per_thread", 0u);
        manifest.keys_total = j.value("keys_total", 0ull);
        if (!ValidateManifest(manifest)) {
            return std::nullopt;
        }
        return manifest;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<Manifest> LoadManifestFromFile(const std::filesystem::path& path) {
    std::ifstream ifs(path);
    if (!ifs) {
        return std::nullopt;
    }
    std::string buffer((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    return DeserializeManifest(buffer);
}

bool WriteManifestToFile(const Manifest& manifest, const std::filesystem::path& path) {
    std::ofstream ofs(path);
    if (!ofs) {
        return false;
    }
    ofs << SerializeManifest(manifest);
    return true;
}

}  // namespace puzzle71::checkpoint
