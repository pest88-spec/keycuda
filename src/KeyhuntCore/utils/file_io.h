/**
 * @file file_io.h
 * @brief Enhanced file I/O operations with SHA-256 digest support for checkpoint manifests
 *
 * Provides tamper-proof checkpoint manifest writing and verification using SHA-256 digests.
 * Replaces FIXME stubs with proper SHA-256 digest computation from json_serializer.cpp (T009).
 */

#pragma once

#include <string>
#include <cstdint>
#include <filesystem>

// Forward declarations for structures from json_serializer
namespace keyhunt {
namespace utils {

struct GPUKernelConfiguration {
    std::string kernelName;
    std::string gpuArchitecture;
    uint32_t gridDimX;
    uint32_t gridDimY;
    uint32_t gridDimZ;
    uint32_t blockDimX;
    uint32_t blockDimY;
    uint32_t blockDimZ;
    uint32_t pointsPerThread;
    uint32_t sharedMemoryBytes;
    uint32_t registerBudget;
    uint32_t streamId;
};

} // namespace utils
} // namespace keyhunt

namespace keyhunt {
namespace utils {

/**
 * @brief Checkpoint manifest structure with SHA-256 protection support
 *
 * Enhanced version of the basic checkpoint manifest that integrates with
 * SHA-256 digest computation for tamper-proof protection.
 */
struct CheckpointManifest {
    std::string version;
    std::string path;
    std::string created_at;
    std::string processed_keys;
    std::string shard_start;
    std::string shard_end;
    std::string next_scalar;
    std::string encryption_cipher;
    std::string nonce;
    std::string salt;
    unsigned int pbkdf2_iterations{200000};
    std::string payload_sha256;
    std::string retention_expiry;
    std::string shard_id;
    unsigned int grid_dim{0};
    unsigned int block_dim{0};
    unsigned int points_per_thread{0};
    std::uint64_t keys_total{0};
};

/**
 * @brief Write checkpoint manifest with SHA-256 protection
 * @param manifest Checkpoint manifest data to write
 * @param filePath Output file path for the protected manifest
 * @return true if successfully written with SHA-256 protection, false otherwise
 *
 * Replaces FIXME stub for SHA-256 digest computation by calling computeSHA256Digest()
 * from json_serializer.cpp (T009).
 */
bool writeCheckpointManifestWithDigest(const CheckpointManifest& manifest, const std::string& filePath);

/**
 * @brief Load checkpoint manifest with SHA-256 verification
 * @param filePath Input file path for the protected manifest
 * @param manifest Output manifest structure (populated if verification succeeds)
 * @return true if successfully loaded and verified, false otherwise
 *
 * Verifies SHA-256 digest during load to detect tampering.
 */
bool loadCheckpointManifestWithVerification(const std::string& filePath, CheckpointManifest& manifest);

/**
 * @brief Verify checkpoint manifest integrity without loading full data
 * @param filePath Input file path for the protected manifest
 * @return true if SHA-256 digest verification succeeds, false otherwise
 *
 * Quick integrity check to detect tampering without full deserialization.
 */
bool verifyCheckpointManifestIntegrity(const std::string& filePath);

/**
 * @brief Create checkpoint manifest from GPU batch configuration
 * @param deviceId GPU device ID
 * @param batchConfig GPU batch configuration
 * @param shardStart Shard start key (hex string)
 * @param shardEnd Shard end key (hex string)
 * @param nextScalar Next scalar for continuation (hex string)
 * @param payloadPath Path to encrypted checkpoint payload
 * @return CheckpointManifest structure populated with provided data
 */
CheckpointManifest createCheckpointManifest(
    uint32_t deviceId,
    const GPUKernelConfiguration& batchConfig,
    const std::string& shardStart,
    const std::string& shardEnd,
    const std::string& nextScalar,
    const std::string& payloadPath);

/**
 * @brief Compute number of processed keys from shard range
 * @param shardStart Starting key (hex string)
 * @param shardEnd Ending key (hex string)
 * @return Hex string representing number of keys processed
 */
std::string computeProcessedKeys(const std::string& shardStart, const std::string& shardEnd);

/**
 * @brief Get current timestamp in ISO 8601 format
 * @return Current timestamp as string (e.g., "2025-10-11T10:30:00Z")
 */
std::string getCurrentTimestamp();

/**
 * @brief Update manifest with encryption metadata
 * @param manifest Manifest to update
 * @param nonce Base64-encoded nonce from encryption
 * @param salt Base64-encoded salt from key derivation
 * @param payloadSha256 SHA-256 hash of encrypted payload
 */
void updateManifestWithEncryptionMetadata(
    CheckpointManifest& manifest,
    const std::string& nonce,
    const std::string& salt,
    const std::string& payloadSha256);

} // namespace utils
} // namespace keyhunt