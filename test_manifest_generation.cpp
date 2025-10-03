#include <iostream>
#include <filesystem>
#include "src/checkpoint_manifest.h"

int main() {
    std::cout << "=== Testing Manifest Generation ===" << std::endl;

    // Create a test manifest
    puzzle71::checkpoint::Manifest manifest;
    manifest.version = "1.0";
    manifest.path = "/tmp/test_checkpoint.bin";
    manifest.created_at = "2025-09-27T18:30:00Z";
    manifest.processed_keys = "0x100";
    manifest.encryption_cipher = "AES-256-GCM";
    manifest.nonce = "ABCDEFGHIJKLMNOP";
    manifest.salt = "BCDEFGHIJKLMNOPQRSTUVWXYZ";
    manifest.pbkdf2_iterations = 200000;
    manifest.payload_sha256 = "a1b2c3d4e5f678901234567890123456789012345678901234567890123456";
    manifest.retention_expiry = "2025-12-27T18:30:00Z";
    manifest.shard_id = "test_shard_001";

    // Serialize manifest
    std::string json = puzzle71::checkpoint::SerializeManifest(manifest);
    std::cout << "Serialized manifest:" << std::endl;
    std::cout << json << std::endl;

    // Test deserialization
    auto deserialized = puzzle71::checkpoint::DeserializeManifest(json);
    if (deserialized) {
        std::cout << "\nDeserialization successful!" << std::endl;
        std::cout << "Version: " << deserialized->version << std::endl;
        std::cout << "Processed keys: " << deserialized->processed_keys << std::endl;
        std::cout << "Shard ID: " << deserialized->shard_id << std::endl;
    } else {
        std::cout << "\nDeserialization failed!" << std::endl;
        return 1;
    }

    // Write to file
    std::filesystem::path manifest_path = "/tmp/test_manifest_results.json";
    if (puzzle71::checkpoint::WriteManifestToFile(manifest, manifest_path)) {
        std::cout << "\nManifest written to: " << manifest_path << std::endl;

        // Read back from file
        auto loaded = puzzle71::checkpoint::LoadManifestFromFile(manifest_path);
        if (loaded) {
            std::cout << "Manifest loaded successfully from file!" << std::endl;
            std::cout << "Verification: " << (loaded->processed_keys == manifest.processed_keys ? "PASS" : "FAIL") << std::endl;
            return 0;
        } else {
            std::cout << "Failed to load manifest from file!" << std::endl;
            return 1;
        }
    } else {
        std::cout << "Failed to write manifest to file!" << std::endl;
        return 1;
    }
}