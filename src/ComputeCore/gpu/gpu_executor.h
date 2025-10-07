#pragma once

#include "ComputeCore/gpu/batch_planner.h"
#include "ComputeCore/shards/shard_walker.h"
#include "ComputeCore/adapters/reference/keyfinder_adapter.h"
#include "ComputeCore/gpu/device_buffers.h"
#include "ComputeCore/gpu/device_memory.h"
#include "ComputeCore/gpu/device_results.h"
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
    std::vector<reference_adapter::ComputationResult> candidates;
    double keys_per_sec{0.0};
    std::uint32_t dropped_candidates{0};
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
    void SmartCleanup();
    int device_id_{0};
    bool compressed_{true};
    cudaDeviceProp props_{};

    BatchConfig config_{};
    core::UInt256 batch_start_{};

    DeviceBuffers host_scalars_;
    gpu::DeviceArray<puzzle71::gpu::DeviceCandidate> device_candidates_;
    gpu::DeviceArray<std::uint32_t> device_candidate_count_;
    gpu::DeviceArray<std::uint32_t> device_candidate_overflow_;
    std::vector<puzzle71::gpu::DeviceCandidate> host_candidates_;

    CudaDeviceKeys device_keys_;
    bool verbose_{false};

    // Asynchronous memory transfer optimization
    cudaStream_t compute_stream_;
    cudaStream_t transfer_stream_;
    bool streams_initialized_{false};
    BatchConfig last_config_{};
    bool gpu_initialized_{false};
    core::UInt256 expected_next_scalar_;
    bool has_expected_next_{false};

    void InitializeDeviceKeys(const std::vector<secp256k1::uint256>& scalars,
                              int points_per_thread,
                              dim3 grid,
                              dim3 block);

    void PrepareResultBuffers(std::size_t capacity);

    // Asynchronous memory transfer methods
    void InitializeStreams();
    void CleanupStreams();
    void InitializeDeviceKeysAsync(const std::vector<secp256k1::uint256>& scalars,
                                   int points_per_thread,
                                   dim3 grid,
                                   dim3 block);
};

}  // namespace puzzle71::gpu
