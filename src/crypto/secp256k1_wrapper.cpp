/**
 * @file secp256k1_wrapper.cpp
 * @brief CPU validation wrapper for bitcoin-core/secp256k1
 *
 * Provides CPU reference implementations for GPU kernel validation.
 * Uses bitcoin-core/secp256k1 as authoritative implementation.
 *
 * Constitution Compliance:
 * - Principle III (No Crypto Reimplementation): Uses bitcoin-core/secp256k1 as authority
 * - Principle VI (Scientific Validation): Provides CPU reference for GPU validation
 */

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <array>
#include <vector>

#ifdef SECP256K1_AVAILABLE
extern "C" {
#ifdef SECP256K1_ZKP_EXTRACTED
#include "../extracted/secp256k1-zkp/include/secp256k1.h"
#else
#include "../../third_party/bitcoin-core-secp256k1/include/secp256k1.h"
#endif
}
#endif

// OpenSSL for SHA256 and RIPEMD160
#include <openssl/sha.h>
#include <openssl/ripemd.h>

namespace keyhunt {
namespace crypto {

/**
 * @brief Compute public key from private key using secp256k1 (CPU reference)
 * @param privKey 32-byte private key (big-endian)
 * @param pubKey Output: 65-byte uncompressed public key (0x04 + X + Y)
 * @return true if successful, false if private key invalid
 *
 * Uses bitcoin-core/secp256k1 as authoritative CPU implementation.
 * This serves as the reference for validating GPU kernel results.
 */
bool computePublicKeyCPU(const unsigned char* privKey, unsigned char* pubKey) {
#ifdef SECP256K1_AVAILABLE
    // Create secp256k1 context (or use cached context for performance)
    static secp256k1_context* ctx = secp256k1_context_create(
        SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);

    if (!ctx) {
        throw std::runtime_error("Failed to create secp256k1 context");
    }

    // Verify private key is valid
    if (!secp256k1_ec_seckey_verify(ctx, privKey)) {
        return false;  // Invalid private key
    }

    // Compute public key
    secp256k1_pubkey pubkey;
    if (!secp256k1_ec_pubkey_create(ctx, &pubkey, privKey)) {
        return false;  // Failed to create public key
    }

    // Serialize to uncompressed format (65 bytes: 0x04 + X + Y)
    size_t outputLen = 65;
    if (!secp256k1_ec_pubkey_serialize(ctx, pubKey, &outputLen, &pubkey,
                                       SECP256K1_EC_UNCOMPRESSED)) {
        return false;  // Serialization failed
    }

    // Verify output format
    if (outputLen != 65 || pubKey[0] != 0x04) {
        throw std::runtime_error("Unexpected public key format");
    }

    return true;
#else
    // secp256k1 not available - cannot provide CPU reference
    (void)privKey;
    (void)pubKey;
    throw std::runtime_error("secp256k1 library not available - cannot compute CPU reference");
#endif
}

/**
 * @brief Compute Hash160 (RIPEMD160(SHA256(data))) for Bitcoin address
 * @param data Input data (typically 65-byte uncompressed public key)
 * @param dataLen Length of input data
 * @param hash160 Output: 20-byte hash160 result
 *
 * Uses OpenSSL for SHA256 and RIPEMD160 (industry-standard implementations).
 * This is the standard Bitcoin address generation pipeline.
 */
void computeHash160CPU(const unsigned char* data, size_t dataLen, unsigned char* hash160) {
    // Step 1: SHA256(data)
    unsigned char sha256Hash[32];
    SHA256(data, dataLen, sha256Hash);

    // Step 2: RIPEMD160(SHA256(data))
    RIPEMD160(sha256Hash, 32, hash160);
}

/**
 * @brief Compute Bitcoin address from public key (complete pipeline)
 * @param pubKey 65-byte uncompressed public key (0x04 + X + Y)
 * @param hash160 Output: 20-byte hash160 result
 * @return true if successful
 *
 * Complete pipeline: Public Key → SHA256 → RIPEMD160 → Hash160
 * This is the CPU reference for validating GPU address generation kernels.
 */
bool computeBitcoinAddressHashCPU(const unsigned char* pubKey, unsigned char* hash160) {
    // Validate input format
    if (pubKey[0] != 0x04) {
        return false;  // Not an uncompressed public key
    }

    // Compute Hash160
    computeHash160CPU(pubKey, 65, hash160);
    return true;
}

/**
 * @brief Batch CPU validation: compute multiple public keys and hash160s
 * @param privKeys Input: array of 32-byte private keys
 * @param pubKeys Output: array of 65-byte public keys
 * @param hash160s Output: array of 20-byte hash160s
 * @param count Number of keys to process
 * @return Number of successfully processed keys
 *
 * Batch processing wrapper for validation test suites.
 * Processes multiple keys sequentially on CPU for comparison with GPU results.
 */
size_t batchComputePublicKeysAndHashesCPU(
    const std::vector<std::array<unsigned char, 32>>& privKeys,
    std::vector<std::array<unsigned char, 65>>& pubKeys,
    std::vector<std::array<unsigned char, 20>>& hash160s)
{
    size_t successCount = 0;
    size_t count = privKeys.size();

    // Resize output vectors
    pubKeys.resize(count);
    hash160s.resize(count);

    for (size_t i = 0; i < count; i++) {
        // Compute public key
        if (!computePublicKeyCPU(privKeys[i].data(), pubKeys[i].data())) {
            continue;  // Skip invalid private key
        }

        // Compute hash160
        if (!computeBitcoinAddressHashCPU(pubKeys[i].data(), hash160s[i].data())) {
            continue;  // Skip if hash computation failed
        }

        successCount++;
    }

    return successCount;
}

/**
 * @brief Compare two hash160 values for equality
 * @param hash1 First hash160 (20 bytes)
 * @param hash2 Second hash160 (20 bytes)
 * @return true if hashes match
 */
bool compareHash160(const unsigned char* hash1, const unsigned char* hash2) {
    return memcmp(hash1, hash2, 20) == 0;
}

/**
 * @brief Generate random private key for testing (seeded PRNG)
 * @param seed Random seed for reproducibility
 * @param privKey Output: 32-byte private key
 *
 * Uses simple LCG PRNG for deterministic test key generation.
 * NOT cryptographically secure - only for testing!
 */
void generateRandomPrivateKey(uint64_t seed, unsigned char* privKey) {
    // Simple Linear Congruential Generator (LCG) for reproducible random keys
    uint64_t state = seed;
    for (int i = 0; i < 32; i += 8) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        for (int j = 0; j < 8 && i + j < 32; j++) {
            privKey[i + j] = static_cast<unsigned char>((state >> (j * 8)) & 0xFF);
        }
    }

    // Ensure private key is in valid range (1 to n-1 for secp256k1)
    // This is a simplified check - proper implementation would verify against secp256k1 curve order
    if (privKey[0] == 0 && privKey[1] == 0 && privKey[2] == 0 && privKey[3] == 0) {
        privKey[3] = 1;  // Ensure non-zero
    }
}

} // namespace crypto
} // namespace keyhunt
