#include "utils/checkpoint_crypto.h"

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <stdexcept>
#include <vector>

namespace puzzle71::utils {

namespace {

constexpr std::size_t kKeyLength = 32;   // AES-256
constexpr std::size_t kNonceLength = 12; // GCM standard
constexpr std::size_t kTagLength = 16;

std::vector<unsigned char> DeriveKey(const CheckpointCryptoConfig& config) {
    if (config.passphrase.empty()) {
        throw std::runtime_error("Checkpoint passphrase is empty");
    }
    if (config.salt.empty()) {
        throw std::runtime_error("Checkpoint salt is empty");
    }

    std::vector<unsigned char> key(kKeyLength);
    if (PKCS5_PBKDF2_HMAC(config.passphrase.c_str(), static_cast<int>(config.passphrase.size()),
                          config.salt.data(), static_cast<int>(config.salt.size()),
                          config.pbkdf2_iterations, EVP_sha512(), key.size(), key.data()) != 1) {
        throw std::runtime_error("Failed to derive checkpoint key");
    }
    return key;
}

}  // namespace

CheckpointCiphertext EncryptCheckpoint(const CheckpointCryptoConfig& config,
                                       std::string_view plaintext) {
    auto key = DeriveKey(config);

    CheckpointCiphertext cipher;
    cipher.nonce.resize(kNonceLength);
    if (RAND_bytes(cipher.nonce.data(), static_cast<int>(cipher.nonce.size())) != 1) {
        throw std::runtime_error("Failed to generate nonce");
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        throw std::runtime_error("Failed to allocate cipher context");
    }

    const EVP_CIPHER* aes = EVP_aes_256_gcm();
    if (EVP_EncryptInit_ex(ctx, aes, nullptr, nullptr, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to init AES-256-GCM");
    }

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(cipher.nonce.size()), nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to set IV length");
    }

    if (EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), cipher.nonce.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to set key/nonce");
    }

    cipher.ciphertext.resize(plaintext.size());
    int out_len = 0;
    if (EVP_EncryptUpdate(ctx, cipher.ciphertext.data(), &out_len,
                          reinterpret_cast<const unsigned char*>(plaintext.data()),
                          static_cast<int>(plaintext.size())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Encryption failed");
    }
    int total_len = out_len;

    if (EVP_EncryptFinal_ex(ctx, cipher.ciphertext.data() + out_len, &out_len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to finalize encryption");
    }
    total_len += out_len;
    cipher.ciphertext.resize(total_len);

    cipher.tag.resize(kTagLength);
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, static_cast<int>(cipher.tag.size()),
                            cipher.tag.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to obtain GCM tag");
    }

    EVP_CIPHER_CTX_free(ctx);
    return cipher;
}

std::string DecryptCheckpoint(const CheckpointCryptoConfig& config,
                              const CheckpointCiphertext& cipher) {
    if (cipher.nonce.size() != kNonceLength || cipher.tag.size() != kTagLength) {
        throw std::runtime_error("Invalid ciphertext metadata");
    }

    auto key = DeriveKey(config);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        throw std::runtime_error("Failed to allocate cipher context");
    }

    const EVP_CIPHER* aes = EVP_aes_256_gcm();
    if (EVP_DecryptInit_ex(ctx, aes, nullptr, nullptr, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to init AES-256-GCM for decryption");
    }

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(cipher.nonce.size()), nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to set IV length");
    }

    if (EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), cipher.nonce.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to set key/nonce for decryption");
    }

    std::string plaintext(cipher.ciphertext.size(), '\0');
    int out_len = 0;
    if (EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char*>(plaintext.data()), &out_len,
                          cipher.ciphertext.data(), static_cast<int>(cipher.ciphertext.size())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Decryption failed");
    }
    int total_len = out_len;

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, static_cast<int>(cipher.tag.size()),
                            const_cast<unsigned char*>(cipher.tag.data())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to set GCM tag");
    }

    if (EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(plaintext.data()) + out_len,
                            &out_len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Authentication failed during decryption");
    }
    total_len += out_len;
    plaintext.resize(static_cast<std::size_t>(total_len));

    EVP_CIPHER_CTX_free(ctx);
    return plaintext;
}

}  // namespace puzzle71::utils
