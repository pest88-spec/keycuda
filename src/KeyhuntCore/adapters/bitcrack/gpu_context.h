#pragma once

#include "KeyhuntCore/shards/shard_walker.h"
#include "KeyhuntCore/gpu/batch_planner.h"
#include "KeyhuntCore/gpu/gpu_executor.h"
#include "scheduler/range_scheduler.h"

#include <array>

#include <optional>
#include <utility>

namespace puzzle71::bitcrack_adapter {

struct GpuContext {
    shards::ShardWalker walker;
    gpu::BatchPlanner planner;
    gpu::GpuExecutor executor;

    GpuContext(shards::ShardWalker w,
               gpu::BatchPlanner p,
               gpu::GpuExecutor&& e)
        : walker(std::move(w)),
          planner(std::move(p)),
          executor(std::move(e)) {}
};

GpuContext BuildGpuContext(const scheduler::Shard& shard,
                           const std::array<std::uint32_t, 5>& target_hash,
                           bool compressed);

}  // namespace puzzle71::bitcrack_adapter
