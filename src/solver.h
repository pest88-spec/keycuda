#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "config/puzzle71_config.h"

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

    SolverOptions options_;
    std::vector<ParityRecord> parity_records_;
};

}  // namespace puzzle71
