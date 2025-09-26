#pragma once

#include <array>
#include <optional>

#include "core/uint256.h"

namespace puzzle71::crypto {

struct PublicKey {
    std::array<unsigned char, 65> uncompressed{};
    std::array<unsigned char, 33> compressed{};
    bool valid{false};
};

// Attempts to derive a public key using bitcoin-core/secp256k1.
// Returns std::nullopt if derivation failed or library is unavailable.
std::optional<PublicKey> DerivePublicKey(const core::UInt256& priv_key);

}  // namespace puzzle71::crypto

