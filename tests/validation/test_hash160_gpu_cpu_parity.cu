#include <gtest/gtest.h>

#include "cudaMath/sha256.cuh"
#include "cudaMath/ripemd160.cuh"
#include "compare/kernels/hash160_fused.h"
#include "utils/endianness.h"

#include <cuda_runtime.h>
#include <openssl/evp.h>
#include <secp256k1.h>

#include <array>
#include <stdexcept>
#include <vector>

namespace {

constexpr std::size_t kSampleCount = 1024;

std::array<std::uint8_t, 32> ToScalarBytes(std::uint64_t value) {
    std::array<std::uint8_t, 32> bytes{};
    for (int i = 31; i >= 24; --i) {
        bytes[i] = static_cast<std::uint8_t>(value & 0xFF);
        value >>= 8;
    }
    return bytes;
}

std::array<std::uint32_t, 8> BytesToBigEndianWords(const unsigned char* bytes) {
    std::array<std::uint32_t, 8> words{};
    for (std::size_t i = 0; i < words.size(); ++i) {
        std::size_t offset = i * 4;
        words[i] = (static_cast<std::uint32_t>(bytes[offset]) << 24) |
                   (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
                   (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
                   static_cast<std::uint32_t>(bytes[offset + 3]);
    }
    return words;
}

std::array<std::uint8_t, 32> Sha256(const unsigned char* data, std::size_t len) {
    std::array<std::uint8_t, 32> out{};
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        throw std::runtime_error("Failed to allocate EVP context");
    }
    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1 ||
        EVP_DigestUpdate(ctx, data, len) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("SHA256 digest failed");
    }
    unsigned int out_len = 0;
    if (EVP_DigestFinal_ex(ctx, out.data(), &out_len) != 1 || out_len != out.size()) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("SHA256 finalization failed");
    }
    EVP_MD_CTX_free(ctx);
    return out;
}

std::array<std::uint8_t, 20> Ripemd160(const std::uint8_t* data, std::size_t len) {
    std::array<std::uint8_t, 20> out{};
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        throw std::runtime_error("Failed to allocate EVP context");
    }
    if (EVP_DigestInit_ex(ctx, EVP_ripemd160(), nullptr) != 1 ||
        EVP_DigestUpdate(ctx, data, len) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("RIPEMD160 digest failed");
    }
    unsigned int out_len = 0;
    if (EVP_DigestFinal_ex(ctx, out.data(), &out_len) != 1 || out_len != out.size()) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("RIPEMD160 finalization failed");
    }
    EVP_MD_CTX_free(ctx);
    return out;
}

constexpr std::array<std::uint32_t, 5> kRipemdIv = {
    0x67452301u,
    0xefcdab89u,
    0x98badcfeu,
    0x10325476u,
    0xc3d2e1f0u};

std::array<std::uint32_t, 5> FinalizeDigest(const std::array<std::uint32_t, 5>& pre_final) {
    std::array<std::uint32_t, 5> out{};
    for (std::size_t i = 0; i < out.size(); ++i) {
        const std::uint32_t value = pre_final[i] + kRipemdIv[(i + 1) % out.size()];
        out[i] = puzzle71::utils::ByteSwap32(value);
    }
    return out;
}

std::array<std::uint8_t, 20> WordsToBigEndianBytes(const std::array<std::uint32_t, 5>& words) {
    std::array<std::uint8_t, 20> bytes{};
    for (std::size_t i = 0; i < words.size(); ++i) {
        const std::uint32_t word = words[i];
        const std::size_t offset = i * 4;
        bytes[offset + 0] = static_cast<std::uint8_t>((word >> 24) & 0xFF);
        bytes[offset + 1] = static_cast<std::uint8_t>((word >> 16) & 0xFF);
        bytes[offset + 2] = static_cast<std::uint8_t>((word >> 8) & 0xFF);
        bytes[offset + 3] = static_cast<std::uint8_t>(word & 0xFF);
    }
    return bytes;
}

__global__ void Hash160CompressedKernel(const unsigned int* x_words,
                                         const unsigned int* y_lsw,
                                         std::uint32_t* out,
                                         std::size_t count) {
    std::size_t idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= count) {
        return;
    }

    unsigned int x[8];
    for (int i = 0; i < 8; ++i) {
        x[i] = x_words[idx * 8 + i];
    }

    std::uint32_t digest[5];
    puzzle71::compare::Hash160Compressed(x, y_lsw[idx], digest);
    for (int i = 0; i < 5; ++i) {
        out[idx * 5 + i] = digest[i];
    }
}

}  // namespace

TEST(Hash160ParityTest, GpuMatchesCpuForSampledKeys) {
    auto* ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);
    ASSERT_NE(ctx, nullptr);

    std::vector<std::array<std::uint8_t, 20>> cpu_digests;
    cpu_digests.reserve(kSampleCount);

    std::vector<unsigned int> x_words_host;
    std::vector<unsigned int> y_lsw_host;
    x_words_host.reserve(kSampleCount * 8);
    y_lsw_host.reserve(kSampleCount);

    for (std::size_t i = 0; i < kSampleCount; ++i) {
        auto priv_bytes = ToScalarBytes(static_cast<std::uint64_t>(i + 1));
        secp256k1_pubkey pubkey;
        ASSERT_EQ(1, secp256k1_ec_pubkey_create(ctx, &pubkey, priv_bytes.data()));

        std::array<unsigned char, 33> compressed{};
        std::array<unsigned char, 65> uncompressed{};
        size_t comp_len = compressed.size();
        size_t uncomp_len = uncompressed.size();
        ASSERT_EQ(1, secp256k1_ec_pubkey_serialize(ctx, compressed.data(), &comp_len, &pubkey, SECP256K1_EC_COMPRESSED));
        ASSERT_EQ(1, secp256k1_ec_pubkey_serialize(ctx, uncompressed.data(), &uncomp_len, &pubkey, SECP256K1_EC_UNCOMPRESSED));

        auto sha = Sha256(compressed.data(), comp_len);
        auto hash160_bytes = Ripemd160(sha.data(), sha.size());
        cpu_digests.push_back(hash160_bytes);

        const unsigned char* x_bytes = uncompressed.data() + 1;
        const unsigned char* y_bytes = uncompressed.data() + 33;
        auto x_words = BytesToBigEndianWords(x_bytes);
        auto y_words = BytesToBigEndianWords(y_bytes);

        for (int j = 0; j < 8; ++j) {
            x_words_host.push_back(x_words[j]);
        }
        y_lsw_host.push_back(y_words[7]);
    }

    unsigned int* d_x_words = nullptr;
    unsigned int* d_y_lsw = nullptr;
    std::uint32_t* d_digests = nullptr;

    ASSERT_EQ(cudaSuccess, cudaMalloc(&d_x_words, x_words_host.size() * sizeof(unsigned int)));
    ASSERT_EQ(cudaSuccess, cudaMalloc(&d_y_lsw, y_lsw_host.size() * sizeof(unsigned int)));
    ASSERT_EQ(cudaSuccess, cudaMalloc(&d_digests, cpu_digests.size() * 5 * sizeof(std::uint32_t)));

    ASSERT_EQ(cudaSuccess, cudaMemcpy(d_x_words, x_words_host.data(), x_words_host.size() * sizeof(unsigned int), cudaMemcpyHostToDevice));
    ASSERT_EQ(cudaSuccess, cudaMemcpy(d_y_lsw, y_lsw_host.data(), y_lsw_host.size() * sizeof(unsigned int), cudaMemcpyHostToDevice));

    const int threads_per_block = 128;
    const int blocks = static_cast<int>((kSampleCount + threads_per_block - 1) / threads_per_block);
    Hash160CompressedKernel<<<blocks, threads_per_block>>>(d_x_words, d_y_lsw, d_digests, kSampleCount);
    ASSERT_EQ(cudaSuccess, cudaDeviceSynchronize());

    std::vector<std::uint32_t> gpu_prefinal(cpu_digests.size() * 5);
    ASSERT_EQ(cudaSuccess, cudaMemcpy(gpu_prefinal.data(), d_digests, gpu_prefinal.size() * sizeof(std::uint32_t), cudaMemcpyDeviceToHost));

    ASSERT_EQ(cudaSuccess, cudaFree(d_x_words));
    ASSERT_EQ(cudaSuccess, cudaFree(d_y_lsw));
    ASSERT_EQ(cudaSuccess, cudaFree(d_digests));

    secp256k1_context_destroy(ctx);

    for (std::size_t i = 0; i < cpu_digests.size(); ++i) {
        std::array<std::uint32_t, 5> gpu_pre{};
        for (int j = 0; j < 5; ++j) {
            gpu_pre[j] = gpu_prefinal[i * 5 + j];
        }
        const auto gpu_final_words = FinalizeDigest(gpu_pre);
        const auto gpu_final_bytes = WordsToBigEndianBytes(gpu_final_words);
        for (std::size_t j = 0; j < gpu_final_bytes.size(); ++j) {
            EXPECT_EQ(cpu_digests[i][j], gpu_final_bytes[j])
                << "Mismatch at sample " << i << " digest byte " << j;
        }
    }
}
