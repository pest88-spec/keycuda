#include <gtest/gtest.h>

#include "checkpoint_manifest.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace {

class TempFile {
public:
    TempFile() {
        auto base = std::filesystem::temp_directory_path();
        path_ = base / std::filesystem::path("puzzle71_manifest_" + std::to_string(std::rand())) ;
    }

    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

puzzle71::checkpoint::Manifest MakeValidManifest() {
    puzzle71::checkpoint::Manifest manifest{};
    manifest.version = "1.0";
    manifest.path = "payload.chk";
    manifest.created_at = "2025-10-07T12:00:00Z";
    manifest.processed_keys = "0x10";
    manifest.shard_start = "0x1";
    manifest.shard_end = "0x2";
    manifest.next_scalar = "0x3";
    manifest.encryption_cipher = "AES-256-GCM";
    manifest.nonce = "QUJDREVGR0hJSktM";
    manifest.salt = "QUJDREVGR0hJSktMTU5PUA==";
    manifest.pbkdf2_iterations = 200000;
    manifest.payload_sha256 = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    manifest.retention_expiry = "2025-11-07T12:00:00Z";
    manifest.shard_id = "device-0";
    manifest.grid_dim = 128;
    manifest.block_dim = 256;
    manifest.points_per_thread = 2;
    manifest.keys_total = 1024;
    return manifest;
}

}  // namespace

TEST(CheckpointManifestUnitTest, SerializeDeserializeRoundTrip) {
    auto manifest = MakeValidManifest();

    auto serialized = puzzle71::checkpoint::SerializeManifest(manifest);
    auto round_trip = puzzle71::checkpoint::DeserializeManifest(serialized);
    ASSERT_TRUE(round_trip.has_value());
    EXPECT_EQ(round_trip->version, manifest.version);
    EXPECT_EQ(round_trip->path, manifest.path);
    EXPECT_EQ(round_trip->processed_keys, manifest.processed_keys);
    EXPECT_EQ(round_trip->keys_total, manifest.keys_total);
}

TEST(CheckpointManifestUnitTest, PersistsManifestToDisk) {
    TempFile manifest_file;

    auto manifest = MakeValidManifest();
    manifest.payload_sha256 = "abcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd";

    ASSERT_TRUE(puzzle71::checkpoint::WriteManifestToFile(manifest, manifest_file.path()));

    auto loaded = puzzle71::checkpoint::LoadManifestFromFile(manifest_file.path());
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->version, manifest.version);
    EXPECT_EQ(loaded->payload_sha256, manifest.payload_sha256);
}
