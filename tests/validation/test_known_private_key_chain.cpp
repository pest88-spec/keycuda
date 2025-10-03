#include <gtest/gtest.h>

#include <openssl/evp.h>
#include <openssl/provider.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" {
#include "secp256k1.h"
}

namespace {

// OpenSSL 3.0+ requires explicit loading of legacy provider for RIPEMD160
struct OpenSSLProviderLoader {
    OSSL_PROVIDER* legacy{nullptr};
    OSSL_PROVIDER* default_provider{nullptr};

    OpenSSLProviderLoader() {
        legacy = OSSL_PROVIDER_load(nullptr, "legacy");
        default_provider = OSSL_PROVIDER_load(nullptr, "default");
        if (!legacy || !default_provider) {
            throw std::runtime_error("Failed to load OpenSSL providers (legacy/default)");
        }
    }

    ~OpenSSLProviderLoader() {
        if (legacy) OSSL_PROVIDER_unload(legacy);
        if (default_provider) OSSL_PROVIDER_unload(default_provider);
    }

    // Singleton instance
    static OpenSSLProviderLoader& instance() {
        static OpenSSLProviderLoader loader;
        return loader;
    }
};

// Initialize providers at program start
static auto& g_openssl_providers = OpenSSLProviderLoader::instance();

std::array<std::uint8_t, 32> ParseHexScalar(const std::string& hex_string) {
    auto trim_whitespace = [](char c) {
        return c == ' ' || c == '\n' || c == '\t' || c == '\r';
    };

    std::string hex;
    hex.reserve(hex_string.size());
    for (char c : hex_string) {
        if (trim_whitespace(c)) continue;
        hex.push_back(c);
    }
    if (hex.size() >= 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) {
        hex = hex.substr(2);
    }
    if (hex.size() != 64) {
        throw std::runtime_error("Scalar must be exactly 32 bytes (64 hex chars)");
    }
    std::array<std::uint8_t, 32> out{};
    for (std::size_t i = 0; i < out.size(); ++i) {
        auto hex_byte = hex.substr(2 * i, 2);
        out[i] = static_cast<std::uint8_t>(std::stoul(hex_byte, nullptr, 16));
    }
    return out;
}

std::array<std::uint8_t, 32> Sha256(const std::uint8_t* data, std::size_t len) {
    std::array<std::uint8_t, 32> out{};
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) throw std::runtime_error("Failed to allocate EVP context");
    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("EVP_DigestInit_ex failed");
    }
    if (EVP_DigestUpdate(ctx, data, len) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("EVP_DigestUpdate failed");
    }
    unsigned int out_len = 0;
    if (EVP_DigestFinal_ex(ctx, out.data(), &out_len) != 1 || out_len != out.size()) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("EVP_DigestFinal_ex failed");
    }
    EVP_MD_CTX_free(ctx);
    return out;
}

std::array<std::uint8_t, 20> Ripemd160(const std::uint8_t* data, std::size_t len) {
    std::array<std::uint8_t, 20> out{};
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) throw std::runtime_error("Failed to allocate EVP context");
    if (EVP_DigestInit_ex(ctx, EVP_ripemd160(), nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("EVP_DigestInit_ex failed");
    }
    if (EVP_DigestUpdate(ctx, data, len) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("EVP_DigestUpdate failed");
    }
    unsigned int out_len = 0;
    if (EVP_DigestFinal_ex(ctx, out.data(), &out_len) != 1 || out_len != out.size()) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("EVP_DigestFinal_ex failed");
    }
    EVP_MD_CTX_free(ctx);
    return out;
}

std::string Base58CheckEncode(const std::uint8_t* payload, std::size_t len) {
    static constexpr char kAlphabet[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    std::size_t leading_zeros = 0;
    while (leading_zeros < len && payload[leading_zeros] == 0) {
        ++leading_zeros;
    }

    std::vector<std::uint8_t> input(payload, payload + len);
    std::vector<std::uint8_t> encoded;

    while (!input.empty() && std::any_of(input.begin(), input.end(), [](std::uint8_t b) { return b != 0; })) {
        int carry = 0;
        std::vector<std::uint8_t> next;
        next.reserve(input.size());
        for (auto byte : input) {
            int value = (carry << 8) | byte;
            int quotient = value / 58;
            carry = value % 58;
            if (!next.empty() || quotient != 0) {
                next.push_back(static_cast<std::uint8_t>(quotient));
            }
        }
        encoded.push_back(static_cast<std::uint8_t>(carry));
        input = std::move(next);
    }

    std::string result(leading_zeros, '1');
    for (auto it = encoded.rbegin(); it != encoded.rend(); ++it) {
        result.push_back(kAlphabet[*it]);
    }
    return result;
}

std::array<std::uint32_t, 5> BytesToDigestWords(const std::array<std::uint8_t, 20>& bytes) {
    std::array<std::uint32_t, 5> out{};
    for (std::size_t i = 0; i < out.size(); ++i) {
        std::uint32_t value = 0;
        value |= static_cast<std::uint32_t>(bytes[i * 4 + 0]) << 24;
        value |= static_cast<std::uint32_t>(bytes[i * 4 + 1]) << 16;
        value |= static_cast<std::uint32_t>(bytes[i * 4 + 2]) << 8;
        value |= static_cast<std::uint32_t>(bytes[i * 4 + 3]);
        out[i] = value;
    }
    return out;
}

class Secp256k1Context {
public:
    Secp256k1Context() {
        ctx_ = secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);
        if (!ctx_) {
            throw std::runtime_error("Failed to create secp256k1 context");
        }
    }

    ~Secp256k1Context() {
        if (ctx_) {
            secp256k1_context_destroy(ctx_);
        }
    }

    secp256k1_context* get() const { return ctx_; }

private:
    secp256k1_context* ctx_{nullptr};
};

}  // namespace

TEST(KnownPrivateKeyChain, Puzzle40Reference) {
    const std::string private_key_hex =
        "000000000000000000000000000000000000000000000000000000e9ae4933d6";
    const std::array<std::uint32_t, 5> expected_digest_words = {
        0x95a156cd, 0x21b4a69d, 0xe969eb67, 0x16864f4c, 0x8b82a82a};
    const std::string expected_address = "1EeAxcprB2PpCnr34VfZdFrkUWuxyiNEFv";

    const auto priv_bytes = ParseHexScalar(private_key_hex);

    Secp256k1Context ctx;
    secp256k1_pubkey pubkey;
    ASSERT_TRUE(secp256k1_ec_pubkey_create(ctx.get(), &pubkey, priv_bytes.data()))
        << "secp256k1_ec_pubkey_create failed";

    std::array<std::uint8_t, 33> compressed{};
    std::array<std::uint8_t, 65> uncompressed{};
    size_t compressed_len = compressed.size();
    ASSERT_TRUE(secp256k1_ec_pubkey_serialize(ctx.get(), compressed.data(), &compressed_len,
                                              &pubkey, SECP256K1_EC_COMPRESSED));
    size_t uncompressed_len = uncompressed.size();
    ASSERT_TRUE(secp256k1_ec_pubkey_serialize(ctx.get(), uncompressed.data(), &uncompressed_len,
                                              &pubkey, SECP256K1_EC_UNCOMPRESSED));

    const auto sha = Sha256(compressed.data(), compressed_len);
    const auto hash160 = Ripemd160(sha.data(), sha.size());

    const auto digest_words = BytesToDigestWords(hash160);
    for (std::size_t i = 0; i < expected_digest_words.size(); ++i) {
        EXPECT_EQ(digest_words[i], expected_digest_words[i])
            << "Digest word mismatch at index " << i;
    }

    std::array<std::uint8_t, 25> payload{};
    payload[0] = 0x00;
    std::copy(hash160.begin(), hash160.end(), payload.begin() + 1);
    const auto checksum_full = Sha256(Sha256(payload.data(), 21).data(), 32);
    std::copy(checksum_full.begin(), checksum_full.begin() + 4, payload.begin() + 21);

    const std::string derived_address = Base58CheckEncode(payload.data(), payload.size());
    EXPECT_EQ(derived_address, expected_address) << "Address mismatch";
}
