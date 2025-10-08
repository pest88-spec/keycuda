#include <gtest/gtest.h>

#include "checkpoint_manifest.h"
#include "utils/digest_verifier.h"

#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <openssl/evp.h>
#include <sstream>
#include <stdexcept>

namespace {

class TempDir {
public:
    TempDir() {
        dir_ = std::filesystem::temp_directory_path() /
                std::filesystem::path("puzzle71_digest_perf_" + std::to_string(std::rand()));
        std::filesystem::create_directories(dir_);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }
    const std::filesystem::path& path() const { return dir_; }

private:
    std::filesystem::path dir_;
};

std::string ComputeSha256(const std::filesystem::path& file) {
    std::ifstream ifs(file, std::ios::binary);
    if (!ifs) {
        throw std::runtime_error("unable to open payload");
    }
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        throw std::runtime_error("digest context alloc failed");
    }
    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("digest init failed");
    }
    std::array<char, 4096> buffer{};
    while (ifs) {
        ifs.read(buffer.data(), buffer.size());
        std::streamsize read = ifs.gcount();
        if (read > 0 && EVP_DigestUpdate(ctx, buffer.data(), static_cast<size_t>(read)) != 1) {
            EVP_MD_CTX_free(ctx);
            throw std::runtime_error("digest update failed");
        }
    }

    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int digest_len = 0;
    if (EVP_DigestFinal_ex(ctx, digest.data(), &digest_len) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("digest final failed");
    }
    EVP_MD_CTX_free(ctx);

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < digest_len; ++i) {
        oss << std::setw(2) << static_cast<int>(digest[i]);
    }
    return oss.str();
}

puzzle71::checkpoint::Manifest MakeManifest(const std::filesystem::path& payload,
                                             const std::string& digest) {
    puzzle71::checkpoint::Manifest manifest{};
    manifest.version = "1.0";
    manifest.path = payload.string();
    manifest.created_at = "2025-10-07T12:00:00Z";
    manifest.processed_keys = "0x0";
    manifest.shard_start = "0x0";
    manifest.shard_end = "0x0";
    manifest.next_scalar = "0x0";
    manifest.encryption_cipher = "AES-256-GCM";
    manifest.nonce = "QUJDREVGR0hJSktM";
    manifest.salt = "QUJDREVGR0hJSktMTU5PUA==";
    manifest.pbkdf2_iterations = 200000;
    manifest.payload_sha256 = digest;
    manifest.retention_expiry = "2025-11-07T12:00:00Z";
    manifest.shard_id = "device-0";
    manifest.grid_dim = 64;
    manifest.block_dim = 256;
    manifest.points_per_thread = 2;
    manifest.keys_total = 131072;
    return manifest;
}

}  // namespace

TEST(DigestVerifierPerfTest, CompletesWithinSla) {
    TempDir tmp;
    auto payload_path = tmp.path() / "payload.dat";
    auto manifest_path = tmp.path() / "manifest.json";

    {
        std::ofstream ofs(payload_path, std::ios::binary);
        std::string block(1024, 'x');
        for (int i = 0; i < 256; ++i) {
            ofs.write(block.data(), block.size());
        }
    }

    auto manifest = MakeManifest(payload_path, ComputeSha256(payload_path));
    ASSERT_TRUE(puzzle71::checkpoint::WriteManifestToFile(manifest, manifest_path));

    auto start = std::chrono::steady_clock::now();
    auto result = puzzle71::utils::VerifyManifestDigest(manifest_path.string(), payload_path.string());
    auto end = std::chrono::steady_clock::now();

    ASSERT_EQ(result.status, puzzle71::utils::DigestStatus::kOk);

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    EXPECT_LE(elapsed, 250) << "Digest verification exceeded SLA: " << elapsed << "ms";
}
