#pragma once

#include <vector>

#include "scheduler/range_scheduler.h"

namespace puzzle71::scan {

using Shard = scheduler::Shard;

std::vector<Shard> PartitionKeyspace(const scheduler::Shard& root_shard, std::uint32_t slices);

}  // namespace puzzle71::scan
