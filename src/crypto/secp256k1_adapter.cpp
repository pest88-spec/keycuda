#include "crypto/secp256k1_adapter.h"

#include <array>
#include <cstring>

#ifdef SECP256K1_AVAILABLE
extern "C" {
#ifdef SECP256K1_ZKP_EXTRACTED
#include "../extracted/secp256k1-zkp/include/secp256k1.h"
#else
#include "../../third_party/bitcoin-core-secp256k1/include/secp256k1.h"
#endif
}
#endif

namespace puzzle71::crypto {

namespace {

#ifdef SECP256K1_AVAILABLE
[[maybe_unused]] secp256k1_context* GetContext() {
    static secp256k1_context* ctx = [] {
        secp256k1_context* context = secp256k1_context_create(SECP256K1_CONTEXT_VERIFY);
        return context;
    }();
    return ctx;
}
#endif

[[maybe_unused]] std::array<unsigned char, 32> UInt256ToBytes(const core::UInt256& value) {
    std::array<unsigned char, 32> out{};
    for (std::size_t i = 0; i < value.limbs.size(); ++i) {
        std::uint64_t limb = value.limbs[i];
        for (std::size_t j = 0; j < 8; ++j) {
            out[31 - (i * 8 + j)] = static_cast<unsigned char>((limb >> (j * 8)) & 0xFF);
        }
    }
    return out;
}


}  // namespace

std::optional<PublicKey> DerivePublicKey(const core::UInt256& priv_key) {
    // Temporarily disabled for basic compilation testing
    // TODO: Fix secp256k1-zkp integration and re-enable
    (void)priv_key;
    return std::nullopt;
}

}  // namespace puzzle71::crypto

