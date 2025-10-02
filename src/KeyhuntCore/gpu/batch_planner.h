#pragma once

#include "KeyhuntCore/shards/shard_walker.h"
#include "puzzle71_kernel.h"

#include <cuda_runtime.h>

#include <cstdint>

namespace puzzle71::gpu {

struct BatchConfig {
    dim3 grid{1, 1, 1};
    dim3 block{32, 1, 1};
    int points_per_thread{1};
    std::uint64_t keys_total{0};
};

class BatchPlanner {
public:
    explicit BatchPlanner(int device_id);

    BatchConfig Plan(const shards::ShardWalker& walker,
                     std::uint64_t desired_keys_hint = 1'048'576) const;

private:
    int device_id_{0};
    cudaDeviceProp props_{};

    static constexpr int kMaxPointsPerThread = 4096;
};

}  // namespace puzzle71::gpu

