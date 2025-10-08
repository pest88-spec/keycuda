#include <gtest/gtest.h>

#include "utils/checkpoint_crypto.h"

#include <array>
#include <cstring>
#include <vector>

namespace {

std::vector<unsigned char> FixedNonce() {
    std::vector<unsigned char> nonce(12);
    for (size_t i = 0; i < nonce.size(); ++i) {
        nonce[i] = static_cast<unsigned char>(i + 1);
    }
    return nonce;
}

std::vector<unsigned char> FixedSalt() {
    std::vector<unsigned char> salt(16);
    for (size_t i = 0; i < salt.size(); ++i) {
        salt[i] = static_cast<unsigned char>(0xA0 + i);
    }
    return salt;
}

}  // namespace

TEST(CheckpointCryptoUnitTest, EncryptDecryptRoundTrip) {
    puzzle71::utils::CheckpointCryptoConfig config{};
    config.passphrase = "deterministic-passphrase";
    config.salt = FixedSalt();
    config.pbkdf2_iterations = 1000;

    const std::string payload = "checkpoint-payload";
    auto nonce = FixedNonce();

    auto cipher = puzzle71::utils::EncryptCheckpoint(config, payload, &nonce);
    ASSERT_EQ(cipher.nonce, nonce);
    ASSERT_FALSE(cipher.ciphertext.empty());

    auto decrypted = puzzle71::utils::DecryptCheckpoint(config, cipher);
    EXPECT_EQ(decrypted, payload);
}

TEST(CheckpointCryptoUnitTest, DetectsCiphertextTampering) {
    puzzle71::utils::CheckpointCryptoConfig config{};
    config.passphrase = "guard-passphrase";
    config.salt = FixedSalt();
    config.pbkdf2_iterations = 2000;

    const std::string payload = "tamper-detection";
    auto nonce = FixedNonce();

    auto cipher = puzzle71::utils::EncryptCheckpoint(config, payload, &nonce);
    ASSERT_FALSE(cipher.ciphertext.empty());
    cipher.ciphertext[0] ^= 0xFF;

    EXPECT_THROW(
        {
            auto result = puzzle71::utils::DecryptCheckpoint(config, cipher);
            (void)result;
        },
        std::runtime_error);
}
