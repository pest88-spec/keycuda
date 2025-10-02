#pragma once

#include <cuda_runtime.h>

#include <array>
#include <cstdint>

#include "utils/endianness.h"

extern __device__ void sha256PublicKey(const unsigned int x[8],
                                       const unsigned int y[8],
                                       unsigned int digest[8]);
extern __device__ void sha256PublicKeyCompressed(const unsigned int x[8],
                                                 unsigned int y_parity,
                                                 unsigned int digest[8]);
extern __device__ void ripemd160sha256NoFinal(const unsigned int x[8],
                                              unsigned int digest[5]);

namespace puzzle71::compare {

/**
 * Device constant holding the target HASH160 digest. The symbol lives in
 * puzzle71_kernel.cu and is updated at runtime before launching the fused
 * kernel. Using __constant__ memory gives each SM fast broadcast access.
 */
extern __device__ __constant__ std::uint32_t kTargetHash160[5];

cudaError_t UploadTargetHash160(const std::array<std::uint32_t, 5>& host_hash);

struct DeviceHashContext {
    std::uint32_t target_hash160[5]{};
};

inline void LoadTargetHash160(DeviceHashContext* ctx,
                              const std::array<std::uint32_t, 5>& host_hash) {
    for (std::size_t i = 0; i < host_hash.size(); ++i) {
        ctx->target_hash160[i] = host_hash[i];
    }
}

/**
 * Compute HASH160 for an uncompressed public key (04 || X || Y) entirely on
 * device using the BitCrack math helpers. The routine mirrors BitCrack's
 * fused kernel and stops prior to the RIPEMD final round so the output can be
 * compared against the pre-final digest stored in kTargetHash160.
 */
__device__ inline void Hash160Uncompressed(const unsigned int x[8],
                                           const unsigned int y[8],
                                           std::uint32_t digest[5]) {
    unsigned int sha_digest[8];
    ::sha256PublicKey(x, y, sha_digest);
    for (int i = 0; i < 8; ++i) {
        sha_digest[i] = puzzle71::utils::ByteSwap32(sha_digest[i]);
    }
    unsigned int ripemd_out[5];
    ::ripemd160sha256NoFinal(sha_digest, ripemd_out);
    for (int i = 0; i < 5; ++i) {
        digest[i] = ripemd_out[i];
    }
}

/**
 * HASH160 for a compressed public key (02/03 || X). The y parity is supplied
 * as the least significant word of the Y coordinate as done in BitCrack.
 */
__device__ inline void Hash160Compressed(const unsigned int x[8],
                                         unsigned int y_lsw,
                                         std::uint32_t digest[5]) {
    unsigned int sha_digest[8];
    ::sha256PublicKeyCompressed(x, y_lsw, sha_digest);
    for (int i = 0; i < 8; ++i) {
        sha_digest[i] = puzzle71::utils::ByteSwap32(sha_digest[i]);
    }
    unsigned int ripemd_out[5];
    ::ripemd160sha256NoFinal(sha_digest, ripemd_out);
    for (int i = 0; i < 5; ++i) {
        digest[i] = ripemd_out[i];
    }
}

__device__ inline bool HashMatchesTarget(const std::uint32_t digest[5]) {
    for (int i = 0; i < 5; ++i) {
        if (digest[i] != kTargetHash160[i]) {
            return false;
        }
    }
    return true;
}

}  // namespace puzzle71::compare
