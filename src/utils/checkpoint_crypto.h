#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace puzzle71::utils {

struct CheckpointCryptoConfig {
    std::string passphrase;
    std::vector<unsigned char> salt;
    unsigned int pbkdf2_iterations{200000};
};

struct CheckpointCiphertext {
    std::vector<unsigned char> nonce;
    std::vector<unsigned char> ciphertext;
    std::vector<unsigned char> tag;
};

CheckpointCiphertext EncryptCheckpoint(const CheckpointCryptoConfig& config,
                                       std::string_view plaintext,
                                       const std::vector<unsigned char>* nonce_override = nullptr);

std::string DecryptCheckpoint(const CheckpointCryptoConfig& config,
                              const CheckpointCiphertext& cipher);

}  // namespace puzzle71::utils
