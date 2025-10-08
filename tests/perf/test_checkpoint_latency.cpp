#include <gtest/gtest.h>

#include "utils/checkpoint_crypto.h"

#include <chrono>
#include <string>
#include <vector>

namespace {

std::vector<unsigned char> FixedNonce() {
    std::vector<unsigned char> nonce(12);
    for (size_t i = 0; i < nonce.size(); ++i) {
        nonce[i] = static_cast<unsigned char>((i * 3) & 0xFFu);
    }
    return nonce;
}

std::vector<unsigned char> FixedSalt() {
    std::vector<unsigned char> salt(16);
    for (size_t i = 0; i < salt.size(); ++i) {
        salt[i] = static_cast<unsigned char>((i * 7) & 0xFFu);
    }
    return salt;
}

}  // namespace

TEST(CheckpointLatencyPerfTest, EncryptDecryptCompletesUnderTwoSeconds) {
    puzzle71::utils::CheckpointCryptoConfig config{};
    config.passphrase = "latency-passphrase";
    config.salt = FixedSalt();
    config.pbkdf2_iterations = 3000;

    std::string payload(1 << 20, 'c');
    auto nonce = FixedNonce();

    auto start = std::chrono::steady_clock::now();
    auto cipher = puzzle71::utils::EncryptCheckpoint(config, payload, &nonce);
    auto decrypted = puzzle71::utils::DecryptCheckpoint(config, cipher);
    auto end = std::chrono::steady_clock::now();

    EXPECT_EQ(decrypted, payload);

    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    EXPECT_LT(elapsed_ms, 2000) << "Checkpoint crypto path exceeded latency budget";
}
