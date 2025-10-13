#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "config/puzzle71_config.h"
#include "checkpoint_manifest.h"
#include "core/uint256.h"

namespace puzzle71 {

struct SolverOptions {
    std::string keyspace_start_hex;
    std::string keyspace_end_hex;
    std::string target_address;
    std::string operator_id;
    std::string operator_purpose;
    bool enable_checkpoint{false};
    bool dry_run{false};
    bool super_mode{false};  // Skip Puzzle #71 security restrictions
    bool verbose{false};
    std::optional<std::string> replay_manifest_path;
    std::optional<std::string> resume_manifest_path;
    std::optional<std::string> telemetry_jsonl_dir;
    std::optional<std::string> prometheus_dir;
    std::string luck_file{"luck.txt"};
    std::optional<std::string> parity_test_scalar_hex;
    std::vector<int> device_ids;
    std::optional<puzzle71::config::ReplayConfig> replay_config;
};

class Puzzle71Solver {
public:
    explicit Puzzle71Solver(SolverOptions options);

    void Run();

    struct ParityRecord {
        core::UInt256 scalar;
        std::array<std::uint32_t,5> digest{};
        std::string address;
        bool is_compressed{false};
    };

    const std::vector<ParityRecord>& parity_records() const { return parity_records_; }

private:
    void AppendLuckEntry(const std::string& scalar_hex, const std::string& address);

    // P1-H001: Refactored helper functions
    struct TargetHashResult {
        std::array<std::uint32_t, 5> target_hash;
        std::optional<core::UInt256> parity_scalar_override;
    };
    TargetHashResult InitializeTargetHash();

    struct ManifestsResult {
        std::optional<checkpoint::Manifest> replay_manifest;
        std::optional<checkpoint::Manifest> resume_manifest;
        bool resume_consumed;
    };
    ManifestsResult InitializeManifests();

    struct KeyspaceResult {
        core::UInt256 keyspace_start;
        core::UInt256 keyspace_end;
    };
    KeyspaceResult ValidateAndParseKeyspace(const std::optional<checkpoint::Manifest>& replay_manifest);

    std::vector<int> InitializeDeviceList(const std::optional<checkpoint::Manifest>& replay_manifest);

    // P1-H001 Phase 2: Medium complexity functions
    struct RunMetrics {
        std::uint64_t total_keys{0};
        double total_elapsed_ms{0.0};
        std::uint64_t batches{0};
        double peak_keys_per_sec{0.0};
        std::uint64_t dropped_candidates{0};
    };
    void PrintSummary(const RunMetrics& metrics,
                     const std::chrono::steady_clock::time_point& wall_start,
                     const std::optional<checkpoint::Manifest>& replay_manifest);

    struct SchedulerResult {
        std::vector<scheduler::Shard> schedule;
        std::optional<gpu::BatchConfig> deterministic_launch_config;
        std::optional<std::mt19937_64> deterministic_rng;
    };
    SchedulerResult InitializeScheduler(
        const core::UInt256& keyspace_start,
        const core::UInt256& keyspace_end,
        const std::vector<int>& device_ids,
        const std::optional<checkpoint::Manifest>& replay_manifest);

    SolverOptions options_;
    std::vector<ParityRecord> parity_records_;
};

}  // namespace puzzle71
