#pragma once

#include <array>
#include <cstdint>

#include "core/uint256.h"

namespace reference_adapter {

struct ComputationResult {
    puzzle71::core::UInt256 private_key;
    puzzle71::core::UInt256 x;
    puzzle71::core::UInt256 y;
    bool is_compressed;
    std::array<std::uint32_t, 5> digest{};
};

}  // namespace reference_adapter

