#include "crypto/secp256k1_adapter.h"

#include <array>
#include <cstring>

#ifdef SECP256K1_AVAILABLE
extern "C" {
#include "../extracted/secp256k1-zkp/include/secp256k1.h"
}
#endif

namespace puzzle71::crypto {

namespace {

#ifdef SECP256K1_AVAILABLE
secp256k1_context* GetContext() {
    static secp256k1_context* ctx = [] {
        secp256k1_context* context = secp256k1_context_create(SECP256K1_CONTEXT_VERIFY);
        return context;
    }();
    return ctx;
}
#endif

std::array<unsigned char, 32> UInt256ToBytes(const core::UInt256& value) {
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
#ifdef SECP256K1_AVAILABLE
    auto* ctx = GetContext();
    if (!ctx) {
        return std::nullopt;
    }

    auto priv_bytes = UInt256ToBytes(priv_key);

    secp256k1_pubkey pubkey;
    if (!secp256k1_ec_pubkey_create(ctx, &pubkey, priv_bytes.data())) {
        return std::nullopt;
    }

    PublicKey out;
    size_t comp_len = out.compressed.size();
    if (!secp256k1_ec_pubkey_serialize(ctx, out.compressed.data(), &comp_len, &pubkey,
                                       SECP256K1_EC_COMPRESSED)) {
        return std::nullopt;
    }
    size_t uncomp_len = out.uncompressed.size();
    if (!secp256k1_ec_pubkey_serialize(ctx, out.uncompressed.data(), &uncomp_len, &pubkey,
                                       SECP256K1_EC_UNCOMPRESSED)) {
        return std::nullopt;
    }
    out.valid = true;
    return out;
#else
    (void)priv_key;
    return std::nullopt;
#endif
}

}  // namespace puzzle71::crypto

