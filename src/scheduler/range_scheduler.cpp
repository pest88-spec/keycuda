#include "scheduler/range_scheduler.h"

namespace puzzle71::scheduler {

std::vector<Shard> BuildDeterministicSchedule(const core::UInt256& start,
                                              const core::UInt256& end,
                                              std::uint32_t device_count) {
    std::vector<Shard> shards;
    if (device_count == 0 || start.Compare(end) > 0) {
        return shards;
    }

    core::UInt256 total = core::Difference(end, start);
    total.AddUint64(1);  // inclusive range size

    std::uint64_t remainder = 0;
    core::UInt256 span = total.DivUint64(device_count, &remainder);

    core::UInt256 current_start = start;
    for (std::uint32_t i = 0; i < device_count; ++i) {
        core::UInt256 shard_span = span;
        if (remainder > 0) {
            shard_span.AddUint64(1);
            --remainder;
        }

        if (shard_span.Compare(core::UInt256::Zero()) == 0) {
            // No more work to distribute
            shards.push_back({current_start, current_start, i});
            continue;
        }

        core::UInt256 shard_end = current_start;
        shard_end.Add(shard_span);
        shard_end = shard_end.SubtractOne();

        shards.push_back({current_start, shard_end, i});

        core::UInt256 next_start = shard_end;
        next_start.AddUint64(1);
        if (next_start.Compare(end) > 0) {
            break;
        }
        current_start = next_start;
    }
    return shards;
}

}  // namespace puzzle71::scheduler
