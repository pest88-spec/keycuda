#pragma once

#include <cstdint>
#include <vector>

namespace puzzle71::scheduler {

struct Shard {
    std::uint64_t start;
    std::uint64_t end;
    std::uint32_t device_id;
};

std::vector<Shard> BuildDeterministicSchedule(std::uint64_t start, std::uint64_t end,
                                              std::uint32_t device_count);

}  // namespace puzzle71::scheduler
