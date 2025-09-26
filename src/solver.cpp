#include "solver.h"

#include "config/puzzle71_config.h"
#include "checkpoint_manifest.h"
#include "scan/puzzle71_partition.h"
#include "scheduler/range_scheduler.h"
#include "utils/digest_verifier.h"
#include "utils/checkpoint_crypto.h"
#include "utils/prometheus_exporter.h"
#include "utils/telemetry_logger.h"

#include "KeyhuntCore/adapters/bitcrack/conversions.h"
#include "KeyhuntCore/adapters/bitcrack/keyfinder_adapter.h"
#include "models/target_constants.h"
#include "crypto/secp256k1_adapter.h"
#include "AddressUtil/AddressUtil.h"


#include <algorithm>
#include <chrono>
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

std::vector<unsigned char> GenerateRandomBytes(std::size_t size) {
    std::vector<unsigned char> data(size);
    if (RAND_bytes(data.data(), static_cast<int>(data.size())) != 1) {
        throw std::runtime_error("Failed to generate random bytes");
    }
    return data;
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

std::string BuildTelemetryStubPayload(std::uint32_t device_id,
                                      const core::UInt256& shard_start,
                                      const core::UInt256& shard_end) {
    std::ostringstream oss;
    oss << "{\"device_id\":" << device_id
        << ",\"shard_start\":\"" << shard_start.ToHex() << "\""
        << ",\"shard_end\":\"" << shard_end.ToHex() << "\""
        << ",\"status\":\"stub\"}";
    return oss.str();
}

checkpoint::Manifest BuildManifest(std::uint32_t device_id,
                                   const core::UInt256& shard_start,
                                   const core::UInt256& shard_end,
                                   const std::filesystem::path& payload_path) {
    checkpoint::Manifest manifest{};
    manifest.version = "1.0";
    manifest.path = payload_path.string();
    manifest.created_at = IsoTimestamp();
    core::UInt256 processed = core::Difference(shard_end, shard_start);
    processed.AddUint64(1);
    manifest.processed_keys = processed.ToHex();
    manifest.encryption_cipher = "AES-256-GCM";
    manifest.nonce = "";  // TODO: populate once crypto is implemented.
    manifest.salt = "";
    manifest.pbkdf2_iterations = 200000;
    manifest.payload_sha256 = "";  // To be populated after encryption/digest.
    manifest.retention_expiry = "";
    std::ostringstream shard_id;
    shard_id << "device-" << device_id;
    manifest.shard_id = shard_id.str();
    return manifest;
}

}  // namespace

Puzzle71Solver::Puzzle71Solver(SolverOptions options) : options_(std::move(options)) {}

void Puzzle71Solver::Run() {
    if (options_.dry_run) {
        std::cout << "Dry run: solver execution skipped." << std::endl;
        return;
    }

    parity_records_.clear();

    const core::UInt256 keyspace_start = ParseKeyspaceHex(options_.keyspace_start_hex);
    const core::UInt256 keyspace_end = ParseKeyspaceHex(options_.keyspace_end_hex);
    if (keyspace_start.Compare(keyspace_end) >= 0) {
        throw std::runtime_error("Invalid keyspace: start must be < end");
    }

    const std::uint32_t device_count = 1;  // TODO: enumerate CUDA devices.
    auto schedule = scheduler::BuildDeterministicSchedule(keyspace_start, keyspace_end, device_count);
    if (schedule.empty()) {
        throw std::runtime_error("Scheduler returned no shards");
    }

    for (const auto& shard : schedule) {
        auto partitions = scan::PartitionKeyspace(shard, /*slices=*/1);
        for (const auto& partition : partitions) {
            // In a real implementation we would allocate device buffers and launch kernels here.
            core::UInt256 current = partition.start;

            bitcrack_adapter::Initialize(partition.start, partition.end, 1'048'576ULL, true, constants::kTargetHash160);

            while (bitcrack_adapter::HasWorkScheduled()) {
                const core::UInt256 chunk_start = current;

                bitcrack_adapter::RunStep();
                auto gpu_results = bitcrack_adapter::FetchResults();
                const auto& step_metrics = bitcrack_adapter::LastStepMetrics();
                std::uint64_t processed = step_metrics.keys_processed;
                if (processed == 0) {
                    std::cerr << "GPU pipeline reported zero progress for shard [" << chunk_start.ToHex() << "]" << std::endl;
                    break;
                }

                for (const auto& candidate : gpu_results) {
                    auto secp_point_x = bitcrack_adapter::ToBitCrack(candidate.x);
                    auto secp_point_y = bitcrack_adapter::ToBitCrack(candidate.y);
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
                    for (std::size_t i = 0; i < constants::kTargetHash160.size(); ++i) {
                        if (digest[i] != constants::kTargetHash160[i]) {
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

                    auto expect_x = bitcrack_adapter::UInt256ToBytes(candidate.x);
                    auto expect_y = bitcrack_adapter::UInt256ToBytes(candidate.y);
                    bool pubkey_match = std::equal(expect_x.begin(), expect_x.end(), derived->uncompressed.begin() + 1) &&
                                        std::equal(expect_y.begin(), expect_y.end(), derived->uncompressed.begin() + 33);
                    if (!pubkey_match) {
                        throw std::runtime_error("CPU parity mismatch for candidate");
                    }

                    std::string address = Address::fromPublicKey(point, candidate.is_compressed);
                    AppendLuckEntry(candidate.private_key.ToHex(), address);

                    ParityRecord record{};
                    record.scalar = candidate.private_key;
                    record.digest = candidate.digest;
                    record.address = address;
                    record.is_compressed = candidate.is_compressed;
                    parity_records_.push_back(std::move(record));
                }

                puzzle71::telemetry::TelemetryOptions telemetry_opts{};
                if (options_.telemetry_jsonl_dir) {
                    telemetry_opts.jsonl_dir = *options_.telemetry_jsonl_dir;
                }
                telemetry_opts.operator_id = options_.operator_id;
                telemetry_opts.operator_purpose = options_.operator_purpose;
                core::UInt256 chunk_end = core::Incremented(chunk_start, processed);
                chunk_end = chunk_end.SubtractOne();
                puzzle71::telemetry::LogTelemetryLine(
                    telemetry_opts,
                    BuildTelemetryStubPayload(partition.device_id, chunk_start, chunk_end));

                
                if (options_.enable_checkpoint) {
                try {
                    auto checkpoint_dir = std::filesystem::path("checkpoints");
                    std::filesystem::create_directories(checkpoint_dir);
                    auto timestamp = IsoTimestamp();
                    std::filesystem::path payload_path = checkpoint_dir /
                        ("payload-" + chunk_start.ToHex() + "-" + timestamp + ".chk");
                    std::filesystem::path manifest_path = checkpoint_dir /
                        ("manifest-" + chunk_start.ToHex() + "-" + timestamp + ".json");

                    auto salt = GenerateRandomBytes(16);
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

                    auto cipher = utils::EncryptCheckpoint(crypto_config, payload_stream.str());
                    {
                        std::ofstream payload_file(payload_path, std::ios::binary);
                        payload_file.write(reinterpret_cast<const char*>(cipher.nonce.data()), cipher.nonce.size());
                        payload_file.write(reinterpret_cast<const char*>(cipher.tag.data()), cipher.tag.size());
                        payload_file.write(reinterpret_cast<const char*>(cipher.ciphertext.data()), cipher.ciphertext.size());
                    }

                    auto manifest = BuildManifest(partition.device_id, chunk_start, chunk_end, payload_path);
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

                current = step_metrics.next_scalar;
                if (!step_metrics.work_remaining || current.Compare(partition.end) > 0) {
                    break;
                }
            }
            bitcrack_adapter::Shutdown();
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
