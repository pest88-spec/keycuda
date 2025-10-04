#include "KeyhuntCore/gpu/batch_planner.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace puzzle71::gpu {

std::uint64_t ComputeThreadCount(dim3 grid, dim3 block) {
    auto gx = static_cast<std::uint64_t>(grid.x == 0 ? 1 : grid.x);
    auto bx = static_cast<std::uint64_t>(block.x == 0 ? 1 : block.x);
    return gx * bx;
}

static inline unsigned int EnsureWarpAligned(unsigned int value) {
    constexpr unsigned int kWarp = 32;
    if (value < kWarp) {
        return kWarp;
    }
    if (value > 1024u) {
        value = 1024u;
    }
    unsigned int remainder = value % kWarp;
    if (remainder != 0) {
        value += (kWarp - remainder);
        if (value > 1024u) {
            value = 1024u;
        }
    }
    return value;
}

void ClampBatchConfig(BatchConfig& cfg, std::uint64_t keys_limit) {
    constexpr unsigned int kWarp = 32;

    if (cfg.block.x == 0) {
        cfg.block.x = kWarp;
    }
    cfg.block.x = EnsureWarpAligned(cfg.block.x);

    if (cfg.grid.x == 0) {
        cfg.grid.x = 1;
    }

    if (cfg.points_per_thread <= 0) {
        cfg.points_per_thread = 1;
    }
    if (cfg.points_per_thread > BatchPlanner::kMaxPointsPerThread) {
        cfg.points_per_thread = BatchPlanner::kMaxPointsPerThread;
    }

    std::uint64_t effective_limit = keys_limit;
    if (effective_limit == 0 || effective_limit > kMaxKeysPerBatch) {
        effective_limit = kMaxKeysPerBatch;
    }

    std::uint64_t threads = ComputeThreadCount(cfg.grid, cfg.block);
    if (threads == 0) {
        cfg.grid = dim3(1, 1, 1);
        cfg.block.x = kWarp;
        threads = ComputeThreadCount(cfg.grid, cfg.block);
    }

    std::uint64_t thread_cap = std::min<std::uint64_t>(kMaxThreadsPerBatch, effective_limit);
    if (thread_cap == 0) {
        thread_cap = 1;
    }

    while (threads > thread_cap && cfg.grid.x > 1) {
        cfg.grid.x = std::max<unsigned int>(1u, cfg.grid.x / 2);
        threads = ComputeThreadCount(cfg.grid, cfg.block);
    }

    while (threads > thread_cap && cfg.block.x > kWarp) {
        cfg.block.x = EnsureWarpAligned(std::max<unsigned int>(kWarp, cfg.block.x / 2));
        threads = ComputeThreadCount(cfg.grid, cfg.block);
    }

    if (threads > thread_cap) {
        std::uint64_t allowed_blocks = (thread_cap + cfg.block.x - 1) / cfg.block.x;
        if (allowed_blocks == 0) {
            allowed_blocks = 1;
        }
        cfg.grid.x = static_cast<unsigned int>(std::max<std::uint64_t>(1, allowed_blocks));
        threads = ComputeThreadCount(cfg.grid, cfg.block);
    }

    if (threads == 0) {
        cfg.grid.x = 1;
        cfg.block.x = kWarp;
        threads = ComputeThreadCount(cfg.grid, cfg.block);
    }

    if (threads > effective_limit) {
        std::uint64_t allowed_blocks = (effective_limit + cfg.block.x - 1) / cfg.block.x;
        if (allowed_blocks == 0) {
            allowed_blocks = 1;
        }
        cfg.grid.x = static_cast<unsigned int>(std::max<std::uint64_t>(1, allowed_blocks));
        threads = ComputeThreadCount(cfg.grid, cfg.block);
        while (threads > effective_limit && cfg.grid.x > 1) {
            --cfg.grid.x;
            threads = ComputeThreadCount(cfg.grid, cfg.block);
        }
        while (threads > effective_limit && cfg.block.x > kWarp) {
            cfg.block.x = EnsureWarpAligned(std::max<unsigned int>(kWarp, cfg.block.x / 2));
            threads = ComputeThreadCount(cfg.grid, cfg.block);
        }
        if (threads == 0) {
            cfg.grid.x = 1;
            cfg.block.x = kWarp;
            threads = ComputeThreadCount(cfg.grid, cfg.block);
        }
    }

    std::uint64_t max_points = effective_limit / threads;
    if (max_points == 0) {
        max_points = 1;
    }
    if (max_points > static_cast<std::uint64_t>(BatchPlanner::kMaxPointsPerThread)) {
        max_points = BatchPlanner::kMaxPointsPerThread;
    }
    if (static_cast<std::uint64_t>(cfg.points_per_thread) > max_points) {
        cfg.points_per_thread = static_cast<int>(max_points);
    }
    if (cfg.points_per_thread <= 0) {
        cfg.points_per_thread = 1;
    }

    std::uint64_t keys_total = threads * static_cast<std::uint64_t>(cfg.points_per_thread);
    if (keys_total > effective_limit) {
        cfg.points_per_thread = static_cast<int>(max_points);
        keys_total = threads * static_cast<std::uint64_t>(cfg.points_per_thread);
    }

    cfg.keys_total = keys_total;
}

BatchPlanner::BatchPlanner(int device_id) : device_id_(device_id) {
    auto status = cudaGetDeviceProperties(&props_, device_id_);
    if (status != cudaSuccess) {
        throw std::runtime_error("cudaGetDeviceProperties failed for planner");
    }
}

void BatchPlanner::SetDeterministicLaunchConfig(const puzzle71::kernel::KernelLaunchConfig& config) {
    deterministic_launch_ = config;
}

BatchConfig BatchPlanner::Plan(const shards::ShardWalker& walker,
                               std::uint64_t desired_keys_hint) const {
    BatchConfig config{};

    if (walker.Done()) {
        return config;
    }

    if (deterministic_launch_) {
       
        config.block = deterministic_launch_->block;
        config.grid = deterministic_launch_->grid;
        int points = deterministic_launch_->points_per_thread <= 0
                         ? 1
                         : std::min(deterministic_launch_->points_per_thread, kMaxPointsPerThread);
        config.points_per_thread = points;

        std::uint64_t threads = ComputeThreadCount(config.grid, config.block);
        if (threads == 0) {
            config.block = dim3(32, 1, 1);
            config.grid = dim3(1, 1, 1);
            threads = ComputeThreadCount(config.grid, config.block);
        }

        std::uint64_t batch_size = deterministic_launch_->batch_size;
        if (batch_size == 0) {
            batch_size = threads * static_cast<std::uint64_t>(config.points_per_thread);
        }
        config.keys_total = batch_size;
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

    // Calculate points per thread with empirical limits for secp256k1 kernels
    // VanitySearch/BitCrack optimal: 4-16 points/thread
    constexpr int kMinPointsPerThread = 1;
    constexpr int kMaxPointsPerThread = 16;  // Prevents register spillage
    constexpr int kOptimalPointsPerThread = 8;

    int points_per_thread = static_cast<int>((target_keys + threads - 1) / threads);
    if (points_per_thread <= 0) {
        points_per_thread = 1;
    }

    // Prefer optimal range for best occupancy
    if (points_per_thread > kMaxPointsPerThread) {
        // Too many points/thread - increase thread count instead
        points_per_thread = kOptimalPointsPerThread;
    }
    points_per_thread = std::clamp(points_per_thread, kMinPointsPerThread, kMaxPointsPerThread);

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

    std::uint64_t clamp_limit = remaining_fits
        ? std::min<std::uint64_t>(remaining64, kMaxKeysPerBatch)
        : kMaxKeysPerBatch;
    ClampBatchConfig(config, clamp_limit);
    return config;
}

}  // namespace puzzle71::gpu
