#include "utils/checkpoint_crypto.h"

#include <stdexcept>

namespace puzzle71::utils {

CheckpointCiphertext EncryptCheckpoint(const CheckpointCryptoConfig& /*config*/,
                                       std::string_view /*plaintext*/) {
    // TODO(T032): Implement AES-256-GCM encryption with PBKDF2-derived key.
    throw std::runtime_error("Checkpoint encryption not yet implemented");
}

std::string DecryptCheckpoint(const CheckpointCryptoConfig& /*config*/,
                              const CheckpointCiphertext& /*cipher*/) {
    // TODO(T032): Implement AES-256-GCM decryption with authentication checks.
    throw std::runtime_error("Checkpoint decryption not yet implemented");
}

}  // namespace puzzle71::utils
