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
 *
 * Optimized: Reduced memory operations and improved memory access patterns.
 */
__device__ inline void Hash160Uncompressed(const unsigned int x[8],
                                           const unsigned int y[8],
                                           std::uint32_t digest[5]) {
    unsigned int sha_digest[8];
    ::sha256PublicKey(x, y, sha_digest);

    // Optimized: Combine byte swapping with RIPEMD input preparation
    // Use vectorized operations where possible
    #if defined(__CUDA_ARCH__) && __CUDA_ARCH__ >= 350
    // Use 128-bit loads/stores for better memory bandwidth
    uint4* sha_vec = reinterpret_cast<uint4*>(sha_digest);
    uint4 vec0 = sha_vec[0];
    uint4 vec1 = sha_vec[1];

    // Manual byte swap within vectors
    sha_digest[0] = __byte_perm(vec0.x, 0, 0x0123);
    sha_digest[1] = __byte_perm(vec0.y, 0, 0x0123);
    sha_digest[2] = __byte_perm(vec0.z, 0, 0x0123);
    sha_digest[3] = __byte_perm(vec0.w, 0, 0x0123);
    sha_digest[4] = __byte_perm(vec1.x, 0, 0x0123);
    sha_digest[5] = __byte_perm(vec1.y, 0, 0x0123);
    sha_digest[6] = __byte_perm(vec1.z, 0, 0x0123);
    sha_digest[7] = __byte_perm(vec1.w, 0, 0x0123);
    #else
    // Fallback for older architectures
    for (int i = 0; i < 8; ++i) {
        sha_digest[i] = puzzle71::utils::ByteSwap32(sha_digest[i]);
    }
    #endif

    unsigned int ripemd_out[5];
    ::ripemd160sha256NoFinal(sha_digest, ripemd_out);

    // Optimize: Use vectorized store for digest output
    #if defined(__CUDA_ARCH__) && __CUDA_ARCH__ >= 350
    uint4* digest_vec = reinterpret_cast<uint4*>(digest);
    uint4 digest_val = {ripemd_out[0], ripemd_out[1], ripemd_out[2], ripemd_out[3]};
    digest_vec[0] = digest_val;
    digest[4] = ripemd_out[4];
    #else
    for (int i = 0; i < 5; ++i) {
        digest[i] = ripemd_out[i];
    }
    #endif
}

/**
 * HASH160 for a compressed public key (02/03 || X). The y parity is supplied
 * as the least significant word of the Y coordinate as done in BitCrack.
 *
 * Optimized: Reduced memory operations and improved memory access patterns.
 */
__device__ inline void Hash160Compressed(const unsigned int x[8],
                                         unsigned int y_lsw,
                                         std::uint32_t digest[5]) {
    unsigned int sha_digest[8];
    ::sha256PublicKeyCompressed(x, y_lsw, sha_digest);

    // Optimized: Combine byte swapping with RIPEMD input preparation
    // Use vectorized operations where possible
    #if defined(__CUDA_ARCH__) && __CUDA_ARCH__ >= 350
    // Use 128-bit loads/stores for better memory bandwidth
    uint4* sha_vec = reinterpret_cast<uint4*>(sha_digest);
    uint4 vec0 = sha_vec[0];
    uint4 vec1 = sha_vec[1];

    // Manual byte swap within vectors using __byte_perm (faster than manual shifts)
    sha_digest[0] = __byte_perm(vec0.x, 0, 0x0123);
    sha_digest[1] = __byte_perm(vec0.y, 0, 0x0123);
    sha_digest[2] = __byte_perm(vec0.z, 0, 0x0123);
    sha_digest[3] = __byte_perm(vec0.w, 0, 0x0123);
    sha_digest[4] = __byte_perm(vec1.x, 0, 0x0123);
    sha_digest[5] = __byte_perm(vec1.y, 0, 0x0123);
    sha_digest[6] = __byte_perm(vec1.z, 0, 0x0123);
    sha_digest[7] = __byte_perm(vec1.w, 0, 0x0123);
    #else
    // Fallback for older architectures
    for (int i = 0; i < 8; ++i) {
        sha_digest[i] = puzzle71::utils::ByteSwap32(sha_digest[i]);
    }
    #endif

    unsigned int ripemd_out[5];
    ::ripemd160sha256NoFinal(sha_digest, ripemd_out);

    // Optimize: Use vectorized store for digest output
    #if defined(__CUDA_ARCH__) && __CUDA_ARCH__ >= 350
    uint4* digest_vec = reinterpret_cast<uint4*>(digest);
    uint4 digest_val = {ripemd_out[0], ripemd_out[1], ripemd_out[2], ripemd_out[3]};
    digest_vec[0] = digest_val;
    digest[4] = ripemd_out[4];
    #else
    for (int i = 0; i < 5; ++i) {
        digest[i] = ripemd_out[i];
    }
    #endif
}

__device__ inline bool HashMatchesTarget(const std::uint32_t digest[5]) {
    // Optimized: Use vectorized comparison for better performance
    #if defined(__CUDA_ARCH__) && __CUDA_ARCH__ >= 350
    // Compare first 4 values in one operation using uint4
    uint4 digest_vec = *reinterpret_cast<const uint4*>(digest);
    uint4 target_vec = *reinterpret_cast<const uint4*>(kTargetHash160);

    // Use vector comparison and reduce
    uint4 cmp = make_uint4(
        digest_vec.x == target_vec.x,
        digest_vec.y == target_vec.y,
        digest_vec.z == target_vec.z,
        digest_vec.w == target_vec.w
    );

    // Check if all comparisons are true
    bool all_match = (cmp.x & cmp.y & cmp.z & cmp.w) != 0;

    // Compare the 5th element
    all_match = all_match && (digest[4] == kTargetHash160[4]);

    return all_match;
    #else
    // Fallback for older architectures - unrolled loop for better performance
    if (digest[0] != kTargetHash160[0]) return false;
    if (digest[1] != kTargetHash160[1]) return false;
    if (digest[2] != kTargetHash160[2]) return false;
    if (digest[3] != kTargetHash160[3]) return false;
    return digest[4] == kTargetHash160[4];
    #endif
}

}  // namespace puzzle71::compare
