#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace puzzle71::config {

struct ReplayConfig {
    std::vector<std::uint32_t> grid_dim;
    std::vector<std::uint32_t> block_dim;
    std::uint64_t points_per_thread{0};
    std::uint64_t deterministic_seed{0};
};

struct OperatorConfig {
    std::string operator_id;
    std::string operator_purpose;
};

struct Puzzle71Config {
    std::string target_address;
    std::string hash160;
    ReplayConfig replay;
    OperatorConfig operator_meta;
};

std::optional<Puzzle71Config> LoadConfig(const std::string& path);

}  // namespace puzzle71::config
