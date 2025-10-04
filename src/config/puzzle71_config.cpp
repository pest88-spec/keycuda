#include "config/puzzle71_config.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <nlohmann/json.hpp>

namespace puzzle71::config {
namespace {
std::array<std::uint32_t, 3> ReadDim(const nlohmann::json& node,
                                     std::array<std::uint32_t, 3> fallback) {
    if (!node.is_array()) {
        return fallback;
    }
    for (std::size_t i = 0; i < fallback.size() && i < node.size(); ++i) {
        fallback[i] = node.at(i).get<std::uint32_t>();
    }
    return fallback;
}

std::string ToUpper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return value;
}
}  // namespace

std::optional<Puzzle71Config> LoadConfig(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs) {
        return std::nullopt;
    }

    try {
        nlohmann::json j;
        ifs >> j;

        Puzzle71Config cfg{};

        if (j.contains("project_constants") && j["project_constants"].contains("puzzle")) {
            const auto& puzzle = j["project_constants"]["puzzle"];
            cfg.target_address = puzzle.value("target_address", cfg.target_address);
            cfg.hash160 = ToUpper(puzzle.value("hash160", cfg.hash160));
        }

        if (cfg.target_address.empty() || cfg.hash160.empty()) {
            return std::nullopt;
        }

        if (j.contains("operator_defaults")) {
            const auto& defaults = j["operator_defaults"];
            cfg.operator_meta.operator_id = defaults.value("operator_id", cfg.operator_meta.operator_id);
            cfg.operator_meta.operator_purpose = defaults.value("operator_purpose", cfg.operator_meta.operator_purpose);
        }

        if (j.contains("replay")) {
            const auto& replay = j["replay"];
            cfg.replay.grid_dim = ReadDim(replay.value("grid_dim", nlohmann::json::array()), cfg.replay.grid_dim);
            cfg.replay.block_dim = ReadDim(replay.value("block_dim", nlohmann::json::array()), cfg.replay.block_dim);
            cfg.replay.points_per_thread = replay.value("points_per_thread", cfg.replay.points_per_thread);
            cfg.replay.deterministic_seed = replay.value("deterministic_seed", cfg.replay.deterministic_seed);
        }

        if (j.contains("checkpointing")) {
            const auto& chk = j["checkpointing"];
            cfg.checkpoint.output_dir = chk.value("output_dir", cfg.checkpoint.output_dir);
            cfg.checkpoint.interval_keys = chk.value("interval_keys", cfg.checkpoint.interval_keys);
            cfg.checkpoint.rotation_minutes = chk.value("rotation_minutes", cfg.checkpoint.rotation_minutes);
        }

        if (j.contains("telemetry")) {
            const auto& tel = j["telemetry"];
            cfg.telemetry.jsonl_dir = tel.value("jsonl_dir", cfg.telemetry.jsonl_dir);
            cfg.telemetry.prometheus_dir = tel.value("prometheus_dir", cfg.telemetry.prometheus_dir);
            cfg.telemetry.throughput_floor_mkeys = tel.value("throughput_floor_mkeys", cfg.telemetry.throughput_floor_mkeys);
            cfg.telemetry.alert_latency_ms = tel.value("alert_latency_ms", cfg.telemetry.alert_latency_ms);
        }

        return cfg;
    } catch (...) {
        return std::nullopt;
    }
}

}  // namespace puzzle71::config
