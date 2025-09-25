#include "config/puzzle71_config.h"

#include <fstream>
#include <nlohmann/json.hpp>

namespace puzzle71::config {

std::optional<Puzzle71Config> LoadConfig(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs) {
        return std::nullopt;
    }

    try {
        nlohmann::json j;
        ifs >> j;
        Puzzle71Config cfg{};
        cfg.target_address = j["project_constants"]["puzzle"]["target_address"].get<std::string>();
        cfg.hash160 = j["project_constants"]["puzzle"]["hash160"].get<std::string>();
        cfg.replay.grid_dim = j["replay"]["grid_dim"].get<std::vector<std::uint32_t>>();
        cfg.replay.block_dim = j["replay"]["block_dim"].get<std::vector<std::uint32_t>>();
        cfg.replay.points_per_thread = j["replay"]["points_per_thread"].get<std::uint64_t>();
        cfg.replay.deterministic_seed = j["replay"]["deterministic_seed"].get<std::uint64_t>();
        cfg.operator_meta.operator_id = j["operator_defaults"]["operator_id"].get<std::string>();
        cfg.operator_meta.operator_purpose = j["operator_defaults"]["operator_purpose"].get<std::string>();
        return cfg;
    } catch (...) {
        return std::nullopt;
    }
}

}  // namespace puzzle71::config
