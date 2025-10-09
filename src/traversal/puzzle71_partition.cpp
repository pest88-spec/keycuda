#include "traversal/puzzle71_partition.h"

#include "core/uint256.h"

namespace puzzle71::scan {

std::vector<Shard> PartitionKeyspace(const scheduler::Shard& root_shard, std::uint32_t slices) {
    std::vector<Shard> partitions;
    if (slices == 0) {
        return partitions;
    }
    core::UInt256 total = core::Difference(root_shard.end, root_shard.start);
    total.AddUint64(1);

    std::uint64_t remainder = 0;
    core::UInt256 span = total.DivUint64(slices, &remainder);

    core::UInt256 current_start = root_shard.start;
    for (std::uint32_t i = 0; i < slices; ++i) {
        core::UInt256 shard_span = span;
        if (remainder > 0) {
            shard_span.AddUint64(1);
            --remainder;
        }

        if (shard_span.Compare(core::UInt256::Zero()) == 0) {
            partitions.push_back({current_start, current_start, root_shard.device_id});
            continue;
        }

        core::UInt256 shard_end = current_start;
        shard_end.Add(shard_span);
        shard_end = shard_end.SubtractOne();
        partitions.push_back({current_start, shard_end, root_shard.device_id});

        core::UInt256 next_start = shard_end;
        next_start.AddUint64(1);
        if (next_start.Compare(root_shard.end) > 0) {
            break;
        }
        current_start = next_start;
    }
    // TODO(T037): Add lineage logging and reassignment handling.
    return partitions;
}

}  // namespace puzzle71::scan
