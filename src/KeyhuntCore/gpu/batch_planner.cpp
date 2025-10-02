#include "KeyhuntCore/gpu/batch_planner.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace puzzle71::gpu {

BatchPlanner::BatchPlanner(int device_id) : device_id_(device_id) {
    auto status = cudaGetDeviceProperties(&props_, device_id_);
    if (status != cudaSuccess) {
        throw std::runtime_error("cudaGetDeviceProperties failed for planner");
    }
}

BatchConfig BatchPlanner::Plan(const shards::ShardWalker& walker,
                               std::uint64_t desired_keys_hint) const {
    BatchConfig config{};

    if (walker.Done()) {
        return config;
    }

    core::UInt256 remaining = walker.Remaining();
    bool remaining_fits = remaining.FitsInUint64();
    std::uint64_t remaining64 = remaining_fits
        ? remaining.ToUint64()
        : std::numeric_limits<std::uint64_t>::max();

    std::uint64_t target_keys = desired_keys_hint == 0 ? 1 : desired_keys_hint;
    if (remaining_fits) {
        target_keys = std::min(target_keys, remaining64);
    }

    puzzle71::kernel::KernelLaunchConfig launch =
        puzzle71::kernel::ChooseLaunchConfig(target_keys);

    config.block = launch.block;
    config.grid = launch.grid;

    std::uint64_t threads = static_cast<std::uint64_t>(config.block.x) * config.grid.x;
    if (threads == 0) {
        config.block = dim3(32, 1, 1);
        config.grid = dim3(1, 1, 1);
        threads = 32;
    }

    int points_per_thread = static_cast<int>((target_keys + threads - 1) / threads);
    if (points_per_thread <= 0) {
        points_per_thread = 1;
    }
    points_per_thread = std::clamp(points_per_thread, 1, kMaxPointsPerThread);

    std::uint64_t keys_total = threads * static_cast<std::uint64_t>(points_per_thread);

    if (remaining_fits && keys_total > remaining64) {
        // Reduce points per thread first.
        while (points_per_thread > 1 && keys_total > remaining64) {
            --points_per_thread;
            keys_total = threads * static_cast<std::uint64_t>(points_per_thread);
        }

        if (keys_total > remaining64) {
            // Adjust grid/block to stay within remaining range.
            std::uint64_t required_threads = remaining64 / points_per_thread;
            if (remaining64 % points_per_thread != 0) {
                ++required_threads;
            }
            if (required_threads == 0) {
                points_per_thread = 1;
                required_threads = remaining64;
            }

            unsigned int warp = 32;
            unsigned int block_threads = config.block.x;
            if (required_threads < block_threads) {
                unsigned int adjusted = static_cast<unsigned int>(std::max<std::uint64_t>(warp, required_threads));
                adjusted = (adjusted / warp) * warp;
                if (adjusted == 0) {
                    adjusted = warp;
                }
                config.block.x = std::min(block_threads, adjusted);
                block_threads = config.block.x;
            }

            if (block_threads == 0) {
                block_threads = warp;
                config.block.x = warp;
            }

            std::uint64_t grid_blocks = required_threads / block_threads;
            if (required_threads % block_threads != 0) {
                ++grid_blocks;
            }
            if (grid_blocks == 0) {
                grid_blocks = 1;
            }
            config.grid.x = static_cast<unsigned int>(grid_blocks);
            threads = static_cast<std::uint64_t>(config.block.x) * config.grid.x;
            keys_total = threads * static_cast<std::uint64_t>(points_per_thread);

            while (keys_total > remaining64 && config.grid.x > 1) {
                --config.grid.x;
                threads = static_cast<std::uint64_t>(config.block.x) * config.grid.x;
                keys_total = threads * static_cast<std::uint64_t>(points_per_thread);
            }

            while (keys_total > remaining64 && points_per_thread > 1) {
                --points_per_thread;
                keys_total = threads * static_cast<std::uint64_t>(points_per_thread);
            }

            if (keys_total > remaining64) {
                keys_total = remaining64;
            }
        }
    }

    config.points_per_thread = points_per_thread;
    config.keys_total = keys_total;
    return config;
}

}  // namespace puzzle71::gpu
