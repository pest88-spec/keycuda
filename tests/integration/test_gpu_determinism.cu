#include <gtest/gtest.h>

#include "utils/checkpoint_crypto.h"

#include <iomanip>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::vector<unsigned char> NextDeterministicBytes(std::mt19937_64& rng, std::size_t size) {
    std::vector<unsigned char> bytes(size);
    std::size_t offset = 0;
    while (offset < size) {
        auto value = rng();
        for (int i = 0; i < 8 && offset < size; ++i) {
            bytes[offset++] = static_cast<unsigned char>(value & 0xFFu);
            value >>= 8;
        }
    }
    return bytes;
}

struct CheckpointSnapshot {
    std::vector<unsigned char> salt;
    std::vector<unsigned char> nonce;
    std::string ciphertext_hex;
};

CheckpointSnapshot GenerateSnapshot(std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    auto salt = NextDeterministicBytes(rng, 16);
    auto nonce = NextDeterministicBytes(rng, 12);

    puzzle71::utils::CheckpointCryptoConfig config{};
    config.passphrase = "deterministic-passphrase";
    config.salt = salt;
    config.pbkdf2_iterations = 1000;

    const std::string payload = R"({"keys":256,"operator":"deterministic"})";
    auto cipher = puzzle71::utils::EncryptCheckpoint(config, payload, &nonce);

    std::ostringstream hex;
    hex << std::hex << std::setfill('0');
    for (unsigned char byte : cipher.ciphertext) {
        hex << std::setw(2) << static_cast<int>(byte);
    }

    return CheckpointSnapshot{std::move(salt), std::move(nonce), hex.str()};
}

}  // namespace

TEST(GpuDeterminismIntegrationTest, CheckpointArtifactsRemainDeterministic) {
    constexpr std::uint64_t kSeed = 0x71C0FFEEULL;

    auto first = GenerateSnapshot(kSeed);
    auto second = GenerateSnapshot(kSeed);

    EXPECT_EQ(first.salt, second.salt);
    EXPECT_EQ(first.nonce, second.nonce);
    EXPECT_EQ(first.ciphertext_hex, second.ciphertext_hex);
}
