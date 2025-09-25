#include "scheduler/range_scheduler.h"

namespace puzzle71::scheduler {

std::vector<Shard> BuildDeterministicSchedule(std::uint64_t start, std::uint64_t end,
                                              std::uint32_t device_count) {
    std::vector<Shard> shards;
    if (start >= end || device_count == 0) {
        return shards;
    }

    // TODO(T036): Implement deterministic contiguous shard assignment with replay seed capture.
    std::uint64_t span = (end - start) / device_count;
    for (std::uint32_t i = 0; i < device_count; ++i) {
        auto shard_start = start + span * i;
        auto shard_end = (i == device_count - 1) ? end : (shard_start + span - 1);
        shards.push_back({shard_start, shard_end, i});
    }
    return shards;
}

}  // namespace puzzle71::scheduler
