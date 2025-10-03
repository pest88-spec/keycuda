#pragma once

#include "KeyhuntCore/gpu/batch_planner.h"
#include "KeyhuntCore/shards/shard_walker.h"
#include "KeyhuntCore/adapters/bitcrack/keyfinder_adapter.h"
#include "KeyhuntCore/gpu/device_buffers.h"
#include "KeyhuntCore/gpu/device_memory.h"
#include "KeyhuntCore/gpu/device_results.h"
#include "CudaKeySearchDevice/CudaDeviceKeys.h"
#include "KeyFinderLib/KeySearchTypes.h"

#include <cuda_runtime.h>

#include <array>
#include <cstdint>
#include <vector>

namespace puzzle71::gpu {

struct StepResult {
    core::UInt256 next_scalar;
    std::uint64_t processed_keys{0};
    std::uint64_t elapsed_us{0};
    std::vector<bitcrack_adapter::KeySearchResult> candidates;
    double keys_per_sec{0.0};
};

class GpuExecutor {
public:
    GpuExecutor(int device_id,
                bool compressed,
                const std::array<std::uint32_t, 5>& target_hash160,
                bool verbose);
    ~GpuExecutor();
    GpuExecutor(const GpuExecutor&) = delete;
    GpuExecutor& operator=(const GpuExecutor&) = delete;
    GpuExecutor(GpuExecutor&&) noexcept = default;
    GpuExecutor& operator=(GpuExecutor&&) noexcept = default;

    void PrepareBatch(const BatchConfig& config,
                      const core::UInt256& start_scalar);

    StepResult Execute();

private:
    int device_id_{0};
    bool compressed_{true};
    cudaDeviceProp props_{};

    BatchConfig config_{};
    core::UInt256 batch_start_{};

    DeviceBuffers host_scalars_;
    gpu::DeviceArray<puzzle71::gpu::DeviceCandidate> device_candidates_;
    gpu::DeviceArray<std::uint32_t> device_candidate_count_;
    std::vector<puzzle71::gpu::DeviceCandidate> host_candidates_;

    CudaDeviceKeys device_keys_;
    bool verbose_{false};

    void InitializeDeviceKeys(const std::vector<secp256k1::uint256>& scalars,
                              int points_per_thread,
                              dim3 grid,
                              dim3 block);

    void PrepareResultBuffers(std::size_t capacity);
};

}  // namespace puzzle71::gpu
