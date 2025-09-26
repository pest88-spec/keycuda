#include "checkpoint_manifest.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <optional>

namespace puzzle71::checkpoint {

std::string SerializeManifest(const Manifest& manifest) {
    nlohmann::json j = {
        {"version", manifest.version},
        {"path", manifest.path},
        {"created_at", manifest.created_at},
        {"processed_keys", manifest.processed_keys},
        {"encryption_cipher", manifest.encryption_cipher},
        {"nonce", manifest.nonce},
        {"salt", manifest.salt},
        {"pbkdf2_iterations", manifest.pbkdf2_iterations},
        {"payload_sha256", manifest.payload_sha256},
        {"retention_expiry", manifest.retention_expiry},
        {"shard_id", manifest.shard_id},
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
        manifest.encryption_cipher = j.value("encryption_cipher", "");
        manifest.nonce = j.value("nonce", "");
        manifest.salt = j.value("salt", "");
        manifest.pbkdf2_iterations = j.value("pbkdf2_iterations", 200000u);
        manifest.payload_sha256 = j.value("payload_sha256", "");
        manifest.retention_expiry = j.value("retention_expiry", "");
        manifest.shard_id = j.value("shard_id", "");
        // TODO(T033): Add schema validation against contracts/checkpoint-manifest.json.
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
