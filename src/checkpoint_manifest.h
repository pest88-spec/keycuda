#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace puzzle71::checkpoint {

struct Manifest {
    std::string version;
    std::string path;
    std::string created_at;
    std::string processed_keys;
    std::string encryption_cipher;
    std::string nonce;
    std::string salt;
    unsigned int pbkdf2_iterations{200000};
    std::string payload_sha256;
    std::string retention_expiry;
    std::string shard_id;
};

std::string SerializeManifest(const Manifest& manifest);
std::optional<Manifest> DeserializeManifest(const std::string& json);
std::optional<Manifest> LoadManifestFromFile(const std::filesystem::path& path);
bool WriteManifestToFile(const Manifest& manifest, const std::filesystem::path& path);

}  // namespace puzzle71::checkpoint
