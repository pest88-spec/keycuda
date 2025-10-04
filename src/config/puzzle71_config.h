#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace puzzle71::config {

struct ReplayConfig {
    std::array<std::uint32_t, 3> grid_dim{1, 1, 1};
    std::array<std::uint32_t, 3> block_dim{32, 1, 1};
    std::uint64_t points_per_thread{0};
    std::uint64_t deterministic_seed{0};
};

struct OperatorConfig {
    std::string operator_id;
    std::string operator_purpose;
};

struct CheckpointConfig {
    std::string output_dir{"checkpoints"};
    std::uint64_t interval_keys{0};
    std::uint32_t rotation_minutes{0};
};

struct TelemetryConfig {
    std::string jsonl_dir{"telemetry"};
    std::string prometheus_dir{"metrics"};
    double throughput_floor_mkeys{0.0};
    double alert_latency_ms{0.0};
};

struct Puzzle71Config {
    std::string target_address;
    std::string hash160;
    ReplayConfig replay;
    OperatorConfig operator_meta;
    CheckpointConfig checkpoint;
    TelemetryConfig telemetry;
};

std::optional<Puzzle71Config> LoadConfig(const std::string& path);

}  // namespace puzzle71::config
