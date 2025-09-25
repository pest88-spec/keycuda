#include "checkpoint_manifest.h"

#include <nlohmann/json.hpp>

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
        {"hash160_digest", manifest.hash160_digest},
        {"retention_expiry", manifest.retention_expiry},
        {"shard_id", manifest.shard_id},
    };
    return j.dump();
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
        manifest.hash160_digest = j.value("hash160_digest", "");
        manifest.retention_expiry = j.value("retention_expiry", "");
        manifest.shard_id = j.value("shard_id", "");
        // TODO(T033): Add schema validation against contracts/checkpoint-manifest.json.
        return manifest;
    } catch (...) {
        return std::nullopt;
    }
}

}  // namespace puzzle71::checkpoint
