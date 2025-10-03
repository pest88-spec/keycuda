#include "KeyhuntCore/gpu/gpu_executor.h"
#include "KeyhuntCore/gpu/batch_planner.h"
#include "KeyhuntCore/shards/shard_walker.h"
#include "KeyhuntCore/adapters/bitcrack/conversions.h"
#include "KeyhuntCore/adapters/bitcrack/keyfinder_adapter.h"
#include "puzzle71_kernel.h"

#include <iostream>

int main() {
    std::array<std::uint32_t, 5> target_hash{0x751e76e8, 0x199196d4, 0x54941c45, 0xd1b3a323, 0xf1433bd6};
    puzzle71::gpu::GpuExecutor executor(0, true, target_hash);

    puzzle71::gpu::BatchConfig cfg;
    cfg.block = dim3(64,1,1);
    cfg.grid = dim3(1,1,1);
    cfg.points_per_thread = 8;
    cfg.keys_total = static_cast<std::uint64_t>(cfg.block.x) * cfg.grid.x * cfg.points_per_thread;

    puzzle71::core::UInt256 start = puzzle71::core::UInt256::Zero();

    executor.PrepareBatch(cfg, start);
    auto result = executor.Execute();

    std::cout << "processed_keys=" << result.processed_keys << " candidates=" << result.candidates.size() << "\n";

    for (const auto& cand : result.candidates) {
        std::cout << "candidate key=" << cand.private_key.ToHex() << "\n";
    }

    return 0;
}
