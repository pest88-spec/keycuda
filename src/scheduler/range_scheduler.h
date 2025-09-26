#pragma once

#include <cstdint>
#include <vector>

#include "core/uint256.h"

namespace puzzle71::scheduler {

struct Shard {
    core::UInt256 start;
    core::UInt256 end;
    std::uint32_t device_id;
};

std::vector<Shard> BuildDeterministicSchedule(const core::UInt256& start,
                                              const core::UInt256& end,
                                              std::uint32_t device_count);

}  // namespace puzzle71::scheduler
