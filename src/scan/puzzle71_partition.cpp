#include "scan/puzzle71_partition.h"

namespace puzzle71::scan {

std::vector<Shard> PartitionKeyspace(const scheduler::Shard& root_shard, std::uint32_t slices) {
    std::vector<Shard> partitions;
    if (slices == 0) {
        return partitions;
    }
    std::uint64_t range = (root_shard.end - root_shard.start + 1) / slices;
    for (std::uint32_t i = 0; i < slices; ++i) {
        std::uint64_t start = root_shard.start + i * range;
        std::uint64_t end = (i == slices - 1) ? root_shard.end : (start + range - 1);
        partitions.push_back({start, end, root_shard.device_id});
    }
    // TODO(T037): Add lineage logging and reassignment handling.
    return partitions;
}

}  // namespace puzzle71::scan
