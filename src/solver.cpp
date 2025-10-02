#include "solver.h"

#include "config/puzzle71_config.h"
#include "checkpoint_manifest.h"
#include "scan/puzzle71_partition.h"
#include "scheduler/range_scheduler.h"
#include "utils/digest_verifier.h"
#include "utils/checkpoint_crypto.h"
#include "utils/prometheus_exporter.h"
#include "utils/telemetry_logger.h"

#include <nlohmann/json.hpp>

#include "KeyhuntCore/adapters/bitcrack/conversions.h"
#include "KeyhuntCore/adapters/bitcrack/keyfinder_adapter.h"
#include "KeyhuntCore/adapters/bitcrack/gpu_context.h"
#include "KeyhuntCore/shards/shard_walker.h"
#include "KeyhuntCore/gpu/batch_planner.h"
#include "KeyhuntCore/gpu/gpu_executor.h"
#include "models/target_constants.h"
#include "crypto/secp256k1_adapter.h"
#include "AddressUtil/AddressUtil.h"


#include <algorithm>
#include <chrono>
#include <numeric>
#include <random>
#include <unordered_set>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/evp.h>

#include <cuda_runtime.h>

namespace puzzle71 {

namespace {

std::string IsoTimestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buffer;
}

std::string IsoTimestampPlusDays(int days) {
    auto now = std::chrono::system_clock::now() + std::chrono::hours(24 * days);
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buffer;
}

void FillRandomBytes(unsigned char* dest,
                     std::size_t size,
                     std::mt19937_64* deterministic_rng) {
    if (deterministic_rng != nullptr) {
        std::size_t offset = 0;
        while (offset < size) {
            auto value = (*deterministic_rng)();
            for (int i = 0; i < 8 && offset < size; ++i) {
                dest[offset++] = static_cast<unsigned char>(value & 0xFFu);
                value >>= 8;
            }
        }
    } else {
        if (RAND_bytes(dest, static_cast<int>(size)) != 1) {
            throw std::runtime_error("Failed to generate random bytes");
        }
    }
}

std::vector<unsigned char> GenerateRandomBytes(std::size_t size,
                                               std::mt19937_64* deterministic_rng) {
    std::vector<unsigned char> data(size);
    FillRandomBytes(data.data(), data.size(), deterministic_rng);
    return data;
}

std::uint32_t DetectCudaDeviceCount() {
    int device_count = 0;
    cudaError_t status = cudaGetDeviceCount(&device_count);
    if (status != cudaSuccess) {
        std::ostringstream oss;
        oss << "cudaGetDeviceCount failed: " << cudaGetErrorString(status);
        throw std::runtime_error(oss.str());
    }
    if (device_count <= 0) {
        throw std::runtime_error("No CUDA devices detected");
    }
    return static_cast<std::uint32_t>(device_count);
}

dim3 MakeDim3(const std::vector<std::uint32_t>& dims) {
    unsigned int x = dims.size() > 0 ? dims[0] : 1;
    unsigned int y = dims.size() > 1 ? dims[1] : 1;
    unsigned int z = dims.size() > 2 ? dims[2] : 1;
    if (x == 0) x = 1;
    if (y == 0) y = 1;
    if (z == 0) z = 1;
    return dim3(x, y, z);
}

gpu::BatchConfig BuildDeterministicBatchConfig(const puzzle71::config::ReplayConfig& cfg) {
    gpu::BatchConfig batch{};
    batch.grid = MakeDim3(cfg.grid_dim);
    batch.block = MakeDim3(cfg.block_dim);
    constexpr int kMaxPointsPerThread = 4096;
    if (cfg.points_per_thread == 0) {
        batch.points_per_thread = 1;
    } else if (cfg.points_per_thread > static_cast<std::uint64_t>(kMaxPointsPerThread)) {
        batch.points_per_thread = kMaxPointsPerThread;
    } else {
        batch.points_per_thread = static_cast<int>(cfg.points_per_thread);
    }
    std::uint64_t threads = static_cast<std::uint64_t>(batch.grid.x) * batch.block.x;
    batch.keys_total = threads * static_cast<std::uint64_t>(batch.points_per_thread);
    return batch;
}

gpu::BatchConfig AdjustDeterministicBatch(const gpu::BatchConfig& base,
                                          const core::UInt256& remaining) {
    gpu::BatchConfig cfg = base;
    constexpr int kMaxPointsPerThread = 4096;
    if (cfg.block.x == 0) {
        cfg.block.x = 32;
    }
    if (cfg.grid.x == 0) {
        cfg.grid.x = 1;
    }
    if (cfg.points_per_thread <= 0) {
        cfg.points_per_thread = 1;
    }
    if (cfg.points_per_thread > kMaxPointsPerThread) {
        cfg.points_per_thread = kMaxPointsPerThread;
    }

    if (!remaining.FitsInUint64()) {
        cfg.keys_total = static_cast<std::uint64_t>(cfg.block.x) * cfg.grid.x *
                         static_cast<std::uint64_t>(cfg.points_per_thread);
        return cfg;
    }

    std::uint64_t remaining64 = remaining.ToUint64();
    if (remaining64 == 0) {
        cfg.keys_total = 0;
        return cfg;
    }

    std::uint64_t threads = static_cast<std::uint64_t>(cfg.block.x) * cfg.grid.x;
    if (threads == 0) {
        cfg.block = dim3(32, 1, 1);
        cfg.grid = dim3(1, 1, 1);
        threads = 32;
    }

    auto compute_keys_total = [&]() {
        return static_cast<std::uint64_t>(cfg.block.x) * cfg.grid.x *
               static_cast<std::uint64_t>(cfg.points_per_thread);
    };

    std::uint64_t keys_total = compute_keys_total();
    if (keys_total <= remaining64) {
        cfg.keys_total = keys_total;
        return cfg;
    }

    // Reduce points per thread first
    std::uint64_t new_points = (remaining64 + threads - 1) / threads;
    if (new_points == 0) {
        new_points = 1;
    }
    if (new_points > static_cast<std::uint64_t>(kMaxPointsPerThread)) {
        new_points = kMaxPointsPerThread;
    }
    cfg.points_per_thread = static_cast<int>(new_points);
    keys_total = compute_keys_total();

    if (keys_total > remaining64) {
        // Reduce blocks to stay within remaining keys
        std::uint64_t required_threads = (remaining64 + cfg.points_per_thread - 1) /
                                         static_cast<std::uint64_t>(cfg.points_per_thread);
        if (required_threads == 0) {
            required_threads = 1;
        }

        unsigned int original_block = cfg.block.x;
        if (original_block == 0) {
            original_block = 32;
        }
        unsigned int warp = 32;
        if (required_threads < original_block) {
            unsigned int aligned = static_cast<unsigned int>(required_threads);
            aligned = ((aligned + warp - 1) / warp) * warp;
            if (aligned == 0) {
                aligned = warp;
            }
            cfg.block.x = std::max(1u, std::min(original_block, aligned));
        }

        std::uint64_t required_blocks = (required_threads + cfg.block.x - 1) / cfg.block.x;
        if (required_blocks == 0) {
            required_blocks = 1;
        }
        if (required_blocks < cfg.grid.x) {
            cfg.grid.x = static_cast<unsigned int>(required_blocks);
        }

        keys_total = compute_keys_total();
        if (keys_total > remaining64) {
            keys_total = remaining64;
        }
    }

    cfg.keys_total = keys_total;
    return cfg;
}

std::string Base64Encode(const unsigned char* data, std::size_t length) {
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO* bio = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, bio);
    BIO_write(b64, data, static_cast<int>(length));
    BIO_flush(b64);
    BUF_MEM* buffer_ptr = nullptr;
    BIO_get_mem_ptr(b64, &buffer_ptr);
    std::string result(buffer_ptr->data, buffer_ptr->length);
    BIO_free_all(b64);
    return result;
}

std::string ComputeFileSha256Hex(const std::filesystem::path& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        throw std::runtime_error("Unable to open file for SHA256: " + path.string());
    }

    // Use OpenSSL 3.0 compatible EVP interface
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        throw std::runtime_error("Failed to create EVP context");
    }

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("Failed to initialize digest");
    }

    std::vector<char> buffer(4096);
    while (ifs) {
        ifs.read(buffer.data(), buffer.size());
        std::streamsize read = ifs.gcount();
        if (read > 0) {
            if (EVP_DigestUpdate(ctx, buffer.data(), static_cast<std::size_t>(read)) != 1) {
                EVP_MD_CTX_free(ctx);
                throw std::runtime_error("Failed to update digest");
            }
        }
    }

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len = 0;
    if (EVP_DigestFinal_ex(ctx, hash, &hash_len) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("Failed to finalize digest");
    }

    EVP_MD_CTX_free(ctx);

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < hash_len; ++i) {
        oss << std::setw(2) << static_cast<int>(hash[i]);
    }
    return oss.str();
}

core::UInt256 ParseKeyspaceHex(std::string_view hex) {
    auto parsed = core::UInt256::FromHex(hex);
    if (!parsed) {
        throw std::runtime_error("Unable to parse hex value: " + std::string(hex));
    }
    return *parsed;
}

std::string BuildTelemetryPayload(const telemetry::TelemetryOptions& options,
                                  std::uint32_t device_id,
                                  const core::UInt256& shard_start,
                                  const core::UInt256& shard_end,
                                  std::uint64_t processed_keys,
                                  const core::UInt256& next_scalar,
                                  std::uint64_t elapsed_ms,
                                  std::size_t candidate_count) {
    nlohmann::json shard;
    shard["start"] = shard_start.ToHex();
    shard["end"] = shard_end.ToHex();
    shard["next_scalar"] = next_scalar.ToHex();

    nlohmann::json payload;
    payload["timestamp"] = IsoTimestamp();
    payload["device_id"] = device_id;
    if (!options.operator_id.empty()) {
        payload["operator_id"] = options.operator_id;
    }
    if (!options.operator_purpose.empty()) {
        payload["operator_purpose"] = options.operator_purpose;
    }
    payload["shard"] = std::move(shard);
    payload["processed_keys"] = processed_keys;
    {
        std::ostringstream oss;
        oss << "0x" << std::hex << processed_keys;
        payload["processed_keys_hex"] = oss.str();
    }
    payload["elapsed_ms"] = elapsed_ms;
    const auto duration_ms = std::max<std::uint64_t>(elapsed_ms, 1);
    const double keys_per_sec = static_cast<double>(processed_keys) * 1000.0 /
                                static_cast<double>(duration_ms);
    payload["keys_per_sec"] = keys_per_sec;
    payload["candidate_count"] = candidate_count;
    payload["status"] = "ok";

    return payload.dump();
}

checkpoint::Manifest BuildManifest(std::uint32_t device_id,
                                   const core::UInt256& shard_start,
                                   const core::UInt256& shard_end,
                                   const std::filesystem::path& payload_path,
                                   const gpu::BatchConfig& batch_config,
                                   const core::UInt256& next_scalar) {
    checkpoint::Manifest manifest{};
    manifest.version = "1.0";
    manifest.path = payload_path.filename().string();
    manifest.created_at = IsoTimestamp();
    core::UInt256 processed = core::Difference(shard_end, shard_start);
    processed.AddUint64(1);
    manifest.processed_keys = processed.ToHex();
    manifest.shard_start = shard_start.ToHex();
    manifest.shard_end = shard_end.ToHex();
    manifest.next_scalar = next_scalar.ToHex();
    manifest.encryption_cipher = "AES-256-GCM";
    manifest.nonce = "";  // TODO: populate once crypto is implemented.
    manifest.salt = "";
    manifest.pbkdf2_iterations = 200000;
    manifest.payload_sha256 = "";  // To be populated after encryption/digest.
    manifest.retention_expiry = "";
    std::ostringstream shard_id;
    shard_id << "device-" << device_id;
    manifest.shard_id = shard_id.str();
    manifest.grid_dim = batch_config.grid.x;
    manifest.block_dim = batch_config.block.x;
    manifest.points_per_thread = batch_config.points_per_thread;
    manifest.keys_total = batch_config.keys_total;
    return manifest;
}

core::UInt256 UInt256FromBytes(const unsigned char* data, std::size_t length) {
    std::string hex;
    hex.reserve(length * 2 + 2);
    hex.append("0x");
    static constexpr char kHex[] = "0123456789abcdef";
    for (std::size_t i = 0; i < length; ++i) {
        unsigned char byte = data[i];
        hex.push_back(kHex[(byte >> 4) & 0x0F]);
        hex.push_back(kHex[byte & 0x0F]);
    }
    auto parsed = core::UInt256::FromHex(hex);
    if (!parsed) {
        throw std::runtime_error("Unable to parse byte buffer as UInt256");
    }
    return *parsed;
}

std::array<std::uint32_t, 5> DigestArray(const unsigned int digest[5]) {
    std::array<std::uint32_t, 5> out{};
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = digest[i];
    }
    return out;
}

}  // namespace

Puzzle71Solver::Puzzle71Solver(SolverOptions options) : options_(std::move(options)) {}

void Puzzle71Solver::Run() {
    parity_records_.clear();

    std::array<std::uint32_t, 5> target_hash = constants::kTargetHash160;
    std::optional<core::UInt256> parity_scalar_override;

    std::optional<checkpoint::Manifest> resume_manifest;
    bool resume_consumed = true;
    gpu::BatchConfig resume_config{};
    if (options_.resume_manifest_path) {
        auto manifest = checkpoint::LoadManifestFromFile(*options_.resume_manifest_path);
        if (!manifest) {
            throw std::runtime_error("Unable to load resume manifest: " + *options_.resume_manifest_path);
        }
        resume_consumed = false;
        resume_manifest = std::move(manifest);
    }

    if (options_.parity_test_scalar_hex) {
        parity_scalar_override = ParseKeyspaceHex(*options_.parity_test_scalar_hex);
        auto pub = crypto::DerivePublicKey(*parity_scalar_override);
        if (!pub || !pub->valid) {
            throw std::runtime_error("Unable to derive public key for parity test scalar");
        }

        const unsigned char* uncompressed = pub->uncompressed.data();
        core::UInt256 x = UInt256FromBytes(uncompressed + 1, 32);
        core::UInt256 y = UInt256FromBytes(uncompressed + 33, 32);

        secp256k1::ecpoint point(::bitcrack_adapter::ToBitCrack(x),
                                 ::bitcrack_adapter::ToBitCrack(y));

        unsigned int digest_words[5];
        Hash::hashPublicKeyCompressed(point, digest_words);
        target_hash = DigestArray(digest_words);

        options_.target_address = Address::fromPublicKey(point, /*compressed=*/true);

        std::cout << "[parity] Override scalar=" << parity_scalar_override->ToHex()
                  << " address=" << options_.target_address << std::endl;
        std::cout << "[parity] Target HASH160 words:";
        for (auto word : target_hash) {
            std::cout << " 0x" << std::hex << word;
        }
        std::cout << std::dec << std::endl;
    }

    if (!options_.parity_test_scalar_hex &&
        !constants::IsCanonicalTargetAddress(options_.target_address)) {
        std::ostringstream oss;
        oss << "Target address " << options_.target_address
            << " does not match canonical Puzzle #71 address";
        throw std::runtime_error(oss.str());
    }

    const core::UInt256 keyspace_start = ParseKeyspaceHex(options_.keyspace_start_hex);
    const core::UInt256 keyspace_end = ParseKeyspaceHex(options_.keyspace_end_hex);
    if (keyspace_start.Compare(keyspace_end) >= 0) {
        throw std::runtime_error("Invalid keyspace: start must be < end");
    }

    if (!options_.parity_test_scalar_hex) {
        const auto canonical_start = ParseKeyspaceHex(constants::kDefaultKeyspace.start_hex);
        const auto canonical_end = ParseKeyspaceHex(constants::kDefaultKeyspace.end_hex);
        if (keyspace_start.Compare(canonical_start) < 0 || keyspace_end.Compare(canonical_end) > 0) {
            throw std::runtime_error("Keyspace outside authorised Puzzle #71 range");
        }
    }

    auto device_ids = options_.device_ids;
    const std::uint32_t available_devices = DetectCudaDeviceCount();
    if (device_ids.empty()) {
        device_ids.resize(available_devices);
        std::iota(device_ids.begin(), device_ids.end(), 0);
    } else {
        std::vector<int> filtered;
        filtered.reserve(device_ids.size());
        std::unordered_set<int> seen;
        for (int id : device_ids) {
            if (id < 0 || id >= static_cast<int>(available_devices)) {
                std::ostringstream oss;
                oss << "Requested CUDA device " << id << " out of range (0-" << (available_devices - 1) << ")";
                throw std::runtime_error(oss.str());
            }
            if (seen.insert(id).second) {
                filtered.push_back(id);
            }
        }
        device_ids = std::move(filtered);
    }

    if (device_ids.empty()) {
        throw std::runtime_error("No CUDA devices available for scheduling");
    }

    std::optional<gpu::BatchConfig> deterministic_launch_config;
    std::mt19937_64 deterministic_rng;
    std::mt19937_64* deterministic_rng_ptr = nullptr;
    if (options_.replay_config) {
        deterministic_launch_config = BuildDeterministicBatchConfig(*options_.replay_config);
        deterministic_rng.seed(options_.replay_config->deterministic_seed);
        deterministic_rng_ptr = &deterministic_rng;
    }

    auto schedule = scheduler::BuildDeterministicSchedule(keyspace_start,
                                                          keyspace_end,
                                                          static_cast<std::uint32_t>(device_ids.size()));
    for (std::size_t i = 0; i < schedule.size() && i < device_ids.size(); ++i) {
        schedule[i].device_id = static_cast<std::uint32_t>(device_ids[i]);
    }
    if (schedule.empty()) {
        throw std::runtime_error("Scheduler returned no shards");
    }

    if (options_.dry_run) {
        std::cout << "Dry run: solver execution skipped." << std::endl;
        return;
    }

    for (const auto& shard : schedule) {
        auto partitions = scan::PartitionKeyspace(shard, /*slices=*/1);
        for (const auto& partition : partitions) {
            auto context = puzzle71::bitcrack_adapter::BuildGpuContext(partition,
                                                                       target_hash,
                                                                       /*compressed=*/true);
            auto& walker = context.walker;
            auto& planner = context.planner;
            auto& executor = context.executor;

            std::uint64_t desired_keys_hint = deterministic_launch_config
                                                   ? deterministic_launch_config->keys_total
                                                   : 1'048'576ULL;

            bool use_resume_config = false;
            if (resume_manifest && !resume_consumed) {
                const auto& manifest = *resume_manifest;
                if (manifest.shard_start == partition.start.ToHex() &&
                    manifest.shard_end == partition.end.ToHex()) {
                    auto resume_scalar = ParseKeyspaceHex(manifest.next_scalar);
                    walker.Reset(resume_scalar);
                    if (manifest.keys_total > 0) {
                        desired_keys_hint = manifest.keys_total;
                    }
                    resume_config.grid = dim3(manifest.grid_dim == 0 ? 1u : manifest.grid_dim, 1, 1);
                    resume_config.block = dim3(manifest.block_dim == 0 ? 32u : manifest.block_dim, 1, 1);
                    resume_config.points_per_thread = manifest.points_per_thread == 0 ? 1 : static_cast<int>(manifest.points_per_thread);
                    resume_config.keys_total = manifest.keys_total;
                    if (resume_config.keys_total == 0) {
                        resume_config.keys_total = static_cast<std::uint64_t>(resume_config.grid.x) *
                                                   static_cast<std::uint64_t>(resume_config.block.x) *
                                                   static_cast<std::uint64_t>(resume_config.points_per_thread);
                    }
                    use_resume_config = resume_config.grid.x > 0 && resume_config.block.x > 0 && resume_config.points_per_thread > 0;
                    resume_consumed = true;
                }
            }

            while (!walker.Done()) {
                const core::UInt256 chunk_start = walker.Next();
                gpu::BatchConfig batch_cfg{};
                if (use_resume_config) {
                    batch_cfg = resume_config;
                    use_resume_config = false;
                } else if (deterministic_launch_config) {
                    batch_cfg = AdjustDeterministicBatch(*deterministic_launch_config,
                                                         walker.Remaining());
                } else {
                    batch_cfg = planner.Plan(walker, desired_keys_hint);
                }
                if (batch_cfg.keys_total == 0) {
                    break;
                }

                executor.PrepareBatch(batch_cfg, chunk_start);
                auto step = executor.Execute();
                const auto& gpu_results = step.candidates;
                if (options_.parity_test_scalar_hex) {
                    std::cout << "[parity] GPU returned " << gpu_results.size() << " candidate(s)" << std::endl;
                }

                std::uint64_t processed = step.processed_keys;
                if (walker.Remaining().FitsInUint64()) {
                    std::uint64_t remaining = walker.Remaining().ToUint64();
                    if (processed > remaining) {
                        processed = remaining;
                    }
                }

                for (const auto& candidate : gpu_results) {
                    auto secp_point_x = ::bitcrack_adapter::ToBitCrack(candidate.x);
                    auto secp_point_y = ::bitcrack_adapter::ToBitCrack(candidate.y);
                    secp256k1::ecpoint point(secp_point_x, secp_point_y);

                    unsigned int digest[5] = {0};
                    if (candidate.is_compressed) {
                        Hash::hashPublicKeyCompressed(point, digest);
                    } else {
                        Hash::hashPublicKey(point, digest);
                    }

                    bool digest_match = true;
                    for (std::size_t i = 0; i < 5; ++i) {
                        if (digest[i] != candidate.digest[i]) {
                            digest_match = false;
                            break;
                        }
                    }
                    if (!digest_match) {
                        throw std::runtime_error("GPU digest mismatch for candidate");
                    }

                    bool target_match = true;
                    for (std::size_t i = 0; i < target_hash.size(); ++i) {
                        if (digest[i] != target_hash[i]) {
                            target_match = false;
                            break;
                        }
                    }
                    if (!target_match) {
                        continue;
                    }

                    auto derived = crypto::DerivePublicKey(candidate.private_key);
                    if (!derived || !derived->valid) {
                        throw std::runtime_error("bitcoin-core/secp256k1 parity unavailable; rebuild with SECP256K1_AVAILABLE=ON");
                    }

                    auto expect_x = ::bitcrack_adapter::UInt256ToBytes(candidate.x);
                    auto expect_y = ::bitcrack_adapter::UInt256ToBytes(candidate.y);
                    bool pubkey_match = std::equal(expect_x.begin(), expect_x.end(), derived->uncompressed.begin() + 1) &&
                                        std::equal(expect_y.begin(), expect_y.end(), derived->uncompressed.begin() + 33);
                    if (!pubkey_match) {
                        throw std::runtime_error("CPU parity mismatch for candidate");
                    }

                    // CRITICAL: Save private key immediately to avoid data loss
                    std::string private_key_hex = candidate.private_key.ToHex();
                    std::cout << "Found match: private_key=" << private_key_hex << std::endl;

                    // HOTFIX: Address::fromPublicKey causes memory corruption on H20
                    // Use static buffer to avoid heap corruption, save key BEFORE address generation
                    static char address_buffer[64] = {0};
                    std::string address = "UNKNOWN";

                    try {
                        // Try to generate address, but don't trust the result for memory operations
                        std::string temp_addr = Address::fromPublicKey(point, candidate.is_compressed);
                        std::cout << "  Generated address: " << temp_addr << std::endl;

                        // Copy to static buffer to avoid double-free
                        std::strncpy(address_buffer, temp_addr.c_str(), sizeof(address_buffer) - 1);
                        address = address_buffer;
                    } catch (...) {
                        std::cerr << "Warning: Address generation failed, using placeholder" << std::endl;
                        address = "ADDRESS_UNAVAILABLE";
                    }

                    // Save private key FIRST (critical data)
                    AppendLuckEntry(private_key_hex, address);

                    // Store record (non-critical)
                    try {
                        ParityRecord record{};
                        record.scalar = candidate.private_key;
                        record.digest = candidate.digest;
                        record.address = address;
                        record.is_compressed = candidate.is_compressed;
                        parity_records_.push_back(std::move(record));
                    } catch (...) {
                        // Ignore parity record failures
                    }
                }

                puzzle71::telemetry::TelemetryOptions telemetry_opts{};
                if (options_.telemetry_jsonl_dir) {
                    telemetry_opts.jsonl_dir = *options_.telemetry_jsonl_dir;
                }
                telemetry_opts.operator_id = options_.operator_id;
                telemetry_opts.operator_purpose = options_.operator_purpose;
                core::UInt256 chunk_end = core::Incremented(chunk_start, processed);
                chunk_end = chunk_end.SubtractOne();
                core::UInt256 next_scalar = core::Incremented(chunk_start, processed);
                if (next_scalar.Compare(partition.end) > 0) {
                    next_scalar = core::Incremented(partition.end, 1);
                }
                auto telemetry_payload = BuildTelemetryPayload(telemetry_opts,
                                                               partition.device_id,
                                                               chunk_start,
                                                               chunk_end,
                                                               processed,
                                                               next_scalar,
                                                               step.elapsed_ms,
                                                               gpu_results.size());
                puzzle71::telemetry::LogTelemetryLine(telemetry_opts, telemetry_payload);

                if (options_.enable_checkpoint) {
                try {
                    auto checkpoint_dir = std::filesystem::path("checkpoints");
                    std::filesystem::create_directories(checkpoint_dir);
                    auto timestamp = IsoTimestamp();
                    std::filesystem::path payload_path = checkpoint_dir /
                        ("payload-" + chunk_start.ToHex() + "-" + timestamp + ".chk");
                    std::filesystem::path manifest_path = checkpoint_dir /
                        ("manifest-" + chunk_start.ToHex() + "-" + timestamp + ".json");

                    auto salt = GenerateRandomBytes(16, deterministic_rng_ptr);
                    utils::CheckpointCryptoConfig crypto_config{
                        options_.operator_id.empty() ? std::string("default-passphrase") : options_.operator_id,
                        std::move(salt),
                        200000};

                    std::ostringstream payload_stream;
                    payload_stream << "{\"start\":\"" << chunk_start.ToHex()
                                   << "\",\"end\":\"" << chunk_end.ToHex()
                                   << "\",\"timestamp\":\"" << timestamp
                                   << "\",\"operator_id\":\"" << options_.operator_id
                                   << "\",\"operator_purpose\":\"" << options_.operator_purpose << "\"}";

                    auto nonce_bytes = GenerateRandomBytes(12, deterministic_rng_ptr);
                    auto cipher = utils::EncryptCheckpoint(crypto_config,
                                                           payload_stream.str(),
                                                           &nonce_bytes);
                    {
                        std::ofstream payload_file(payload_path, std::ios::binary);
                        payload_file.write(reinterpret_cast<const char*>(cipher.nonce.data()), cipher.nonce.size());
                        payload_file.write(reinterpret_cast<const char*>(cipher.tag.data()), cipher.tag.size());
                        payload_file.write(reinterpret_cast<const char*>(cipher.ciphertext.data()), cipher.ciphertext.size());
                    }

                    auto manifest = BuildManifest(partition.device_id,
                                                  chunk_start,
                                                  chunk_end,
                                                  payload_path,
                                                  batch_cfg,
                                                  next_scalar);
                    manifest.pbkdf2_iterations = crypto_config.pbkdf2_iterations;
                    manifest.payload_sha256 = ComputeFileSha256Hex(payload_path);
                    manifest.nonce = Base64Encode(cipher.nonce.data(), cipher.nonce.size());
                    manifest.salt = Base64Encode(crypto_config.salt.data(), crypto_config.salt.size());
                    manifest.retention_expiry = IsoTimestampPlusDays(30);
                    if (!checkpoint::WriteManifestToFile(manifest, manifest_path)) {
                        std::cerr << "Failed to write checkpoint manifest: " << manifest_path << std::endl;
                    }
                } catch (const std::exception& ex) {
                    std::cerr << "Checkpoint generation error: " << ex.what() << std::endl;
                }
                }
                walker.Advance(processed);

                if (step.elapsed_ms > 0 && processed > 0) {
                    double keys_per_sec = step.keys_per_sec;
                    if (keys_per_sec <= 0.0) {
                        keys_per_sec = static_cast<double>(processed) * 1000.0 /
                                       static_cast<double>(step.elapsed_ms);
                    }
                    const double target_ms = 25.0;
                    const double target_keys = keys_per_sec * (target_ms / 1000.0);
                    constexpr std::uint64_t kMinKeys = 512;
                    constexpr std::uint64_t kMaxKeys = 4'194'304;
                    if (target_keys > 0.0) {
                        desired_keys_hint = static_cast<std::uint64_t>(target_keys);
                        desired_keys_hint = std::clamp(desired_keys_hint, kMinKeys, kMaxKeys);
                    }
                }
            }
        }
    }

    if (options_.prometheus_dir) {
        puzzle71::telemetry::PrometheusOptions prom_opts{*options_.prometheus_dir};
        puzzle71::telemetry::WritePrometheusSnapshot(prom_opts,
                                                     "# Puzzle71Solver metrics\n"
                                                     "puzzle71_last_run_status 1\n");
    }

    if (options_.replay_manifest_path) {
        auto manifest = checkpoint::LoadManifestFromFile(*options_.replay_manifest_path);
        if (!manifest) {
            std::cerr << "Unable to load replay manifest: " << *options_.replay_manifest_path << std::endl;
        } else {
            auto result = puzzle71::utils::VerifyManifestDigest(*options_.replay_manifest_path, manifest->path);
            if (result.status != puzzle71::utils::DigestStatus::kOk) {
                std::cerr << "Replay manifest verification incomplete: " << result.message << std::endl;
            }
        }
    }

}

void Puzzle71Solver::AppendLuckEntry(const std::string& scalar_hex, const std::string& address) {
    std::filesystem::path path = options_.luck_file;
    if (!path.has_parent_path()) {
        path = std::filesystem::current_path() / path;
    }
    std::filesystem::create_directories(path.parent_path());
    std::ofstream ofs(path, std::ios::app);
    if (!ofs) {
        throw std::runtime_error("Unable to open luck.txt for append");
    }
    ofs << scalar_hex << ' ' << address << '\n';
}

}  // namespace puzzle71
