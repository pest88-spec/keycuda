#pragma once

#include "KeyhuntCore/shards/shard_walker.h"
#include "puzzle71_kernel.h"

#include <cuda_runtime.h>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace puzzle71::gpu {

struct BatchConfig {
    dim3 grid{1, 1, 1};
    dim3 block{32, 1, 1};
    int points_per_thread{1};
    std::uint64_t keys_total{0};
};

constexpr std::uint64_t kMaxKeysPerBatch = 1ULL << 28;          // 268,435,456 keys
constexpr std::uint64_t kMaxThreadsPerBatch = 1ULL << 20;        // 1,048,576 threads
constexpr std::size_t kMaxCandidateBuffer = 4096;                // bounded result slots

class BatchPlanner {
public:
    explicit BatchPlanner(int device_id);

    BatchConfig Plan(const shards::ShardWalker& walker,
                     std::uint64_t desired_keys_hint = 1'048'576) const;

    void SetDeterministicLaunchConfig(const puzzle71::kernel::KernelLaunchConfig& config);

    // Empirical limits for secp256k1 batch operations (VanitySearch/BitCrack proven)
    // Restore to proven value that achieved 900 Mkeys/s on H20
    static constexpr int kMaxPointsPerThread = 64;

private:
    int device_id_{0};
    cudaDeviceProp props_{};
    std::optional<puzzle71::kernel::KernelLaunchConfig> deterministic_launch_;
};

std::uint64_t ComputeThreadCount(dim3 grid, dim3 block);
void ClampBatchConfig(BatchConfig& cfg, std::uint64_t keys_limit);

}  // namespace puzzle71::gpu
