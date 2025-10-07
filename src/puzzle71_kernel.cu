#include "puzzle71_kernel.h"

#include "compare/kernels/hash160_fused.h"
#include "utils/endianness.h"

using puzzle71::gpu::DeviceCandidate;
using puzzle71::gpu::DeviceResultBuffer;

#include <cuda_runtime.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <mutex>
#include <optional>
#include <unordered_map>

#include "CudaKeySearchDevice/CudaDeviceKeys.cuh"
#include "KeyFinderLib/KeySearchTypes.h"
#include "cudaMath/secp256k1.cuh"

namespace {

constexpr std::array<std::uint32_t, 5> kRipemd160Iv = {
    0x67452301u,
    0xefcdab89u,
    0x98badcfeu,
    0x10325476u,
    0xc3d2e1f0u};

std::array<std::uint32_t, 5> PreFinalDigest(const std::array<std::uint32_t, 5>& final_digest) {
    std::array<std::uint32_t, 5> out{};
    for (std::size_t i = 0; i < out.size(); ++i) {
        const auto swapped = puzzle71::utils::ByteSwap32(final_digest[i]);
        out[i] = swapped - kRipemd160Iv[(i + 1) % out.size()];
    }
    return out;
}

}  // namespace

extern __global__ void keyFinderKernel(int points, int compression);
extern __device__ __constant__ unsigned int _INC_X[8];
extern __device__ __constant__ unsigned int _INC_Y[8];
extern __device__ __constant__ unsigned int* _CHAIN[1];

namespace puzzle71::compare {
__device__ __constant__ std::uint32_t kTargetHash160[5];

cudaError_t UploadTargetHash160(const std::array<std::uint32_t, 5>& host_hash) {
    auto pre_final = PreFinalDigest(host_hash);
    return cudaMemcpyToSymbol(kTargetHash160,
                              pre_final.data(),
                              sizeof(std::uint32_t) * pre_final.size());
}
}  // namespace puzzle71::compare

namespace {

__device__ DeviceResultBuffer g_result_buffer;
std::mutex g_deterministic_mutex;
std::unordered_map<int, puzzle71::kernel::KernelLaunchConfig> g_deterministic_by_device;
std::optional<puzzle71::kernel::KernelLaunchConfig> g_global_deterministic;

__device__ inline void FinalizeDigest(const std::uint32_t in[5], std::uint32_t out[5]) {
    const std::uint32_t iv[5] = {
        0x67452301u,
        0xefcdab89u,
        0x98badcfeu,
        0x10325476u,
        0xc3d2e1f0u};
    for (int i = 0; i < 5; ++i) {
        const std::uint32_t value = in[i] + iv[(i + 1) % 5];
        out[i] = puzzle71::utils::ByteSwap32(value);
    }
}

__device__ inline void EmitCandidate(bool has_candidate,
                                     int idx,
                                     bool compressed,
                                     const unsigned int x[8],
                                     const unsigned int y[8],
                                     const std::uint32_t digest[5]) {
    if (g_result_buffer.capacity == 0 || g_result_buffer.candidates == nullptr ||
        g_result_buffer.count == nullptr) {
        return;
    }
    const unsigned full_mask = 0xffffffffu;
    unsigned active = __ballot_sync(full_mask, has_candidate);
    if (active == 0u) {
        return;
    }

    const int lane = threadIdx.x & 31;
    const int leader = __ffs(active) - 1;
    const unsigned int matches = __popc(active);

    std::uint32_t base_index = 0;
    if (lane == leader) {
        base_index = atomicAdd(g_result_buffer.count, matches);
        if (g_result_buffer.dropped != nullptr) {
            std::uint32_t overflow = 0;
            if (base_index >= g_result_buffer.capacity) {
                overflow = matches;
            } else if (base_index + matches > g_result_buffer.capacity) {
                overflow = (base_index + matches) - g_result_buffer.capacity;
            }
            if (overflow > 0) {
                atomicAdd(g_result_buffer.dropped, overflow);
            }
        }
    }

    base_index = __shfl_sync(active, base_index, leader);
    if (!has_candidate) {
        return;
    }

    unsigned lane_offset = __popc(active & ((1u << lane) - 1));
    std::uint32_t slot = base_index + lane_offset;
    if (slot >= g_result_buffer.capacity) {
        return;
    }

    DeviceCandidate& out = g_result_buffer.candidates[slot];
    out.block = static_cast<std::uint32_t>(blockIdx.x);
    out.thread = static_cast<std::uint32_t>(threadIdx.x);
    out.idx = static_cast<std::uint32_t>(idx);
    out.compressed = compressed ? 1u : 0u;
    for (int i = 0; i < 8; ++i) {
        out.x[i] = x[i];
        out.y[i] = y[i];
    }
    FinalizeDigest(digest, out.digest);
}

__device__ void DoPuzzle71Iteration(int pointsPerThread, int compression) {
    unsigned int *chain = _CHAIN[0];
    unsigned int *xPtr = ec::getXPtr();
    unsigned int *yPtr = ec::getYPtr();

    const bool check_uncompressed =
        (compression == PointCompressionType::UNCOMPRESSED) ||
        (compression == PointCompressionType::BOTH);
    const bool check_compressed =
        (compression == PointCompressionType::COMPRESSED) ||
        (compression == PointCompressionType::BOTH);

    // Optimization: Process 8 points at once for better vectorization
    const int BATCH_SIZE = 8;
    int batches = (pointsPerThread + BATCH_SIZE - 1) / BATCH_SIZE;

    for (int batch = 0; batch < batches; ++batch) {
        int start_idx = batch * BATCH_SIZE;
        int end_idx = min(start_idx + BATCH_SIZE, pointsPerThread);
        int current_batch_size = end_idx - start_idx;

        // Pre-allocate working arrays for batch processing
        unsigned int batch_x[BATCH_SIZE][8];
        unsigned int batch_y[BATCH_SIZE][8];
        std::uint32_t batch_digest[BATCH_SIZE][5];
        bool batch_match[BATCH_SIZE];
        bool batch_infinity[BATCH_SIZE];

        // Batch read all points for better memory coalescing
        for (int i = 0; i < current_batch_size; ++i) {
            readInt(xPtr, start_idx + i, batch_x[i]);
            readInt(yPtr, start_idx + i, batch_y[i]);
            batch_infinity[i] = isInfinity(batch_x[i]);
        }

        // Warp-level optimization: use ballot to reduce branch divergence
        unsigned uncompressed_mask = __ballot_sync(0xffffffff, check_uncompressed);
        unsigned compressed_mask = __ballot_sync(0xffffffff, check_compressed);

        // Optimized batch HASH160 computation - reduce branch divergence
        if (uncompressed_mask || compressed_mask) {
            for (int i = 0; i < current_batch_size; ++i) {
                if (batch_infinity[i]) continue;

                // Compute compressed hash first (cheaper if only checking compressed)
                if (check_compressed) {
                    unsigned int y_parity = readIntLSW(yPtr, start_idx + i);
                    puzzle71::compare::Hash160Compressed(
                        batch_x[i], y_parity, batch_digest[i]);
                    batch_match[i] = puzzle71::compare::HashMatchesTarget(batch_digest[i]);

                    if (batch_match[i]) {
                        // Read full Y coordinate only if we have a match
                        readInt(yPtr, start_idx + i, batch_y[i]);
                        EmitCandidate(true, start_idx + i, true,
                                    batch_x[i], batch_y[i], batch_digest[i]);
                    }
                }

                // Only compute uncompressed hash if needed and no compressed match found
                if (check_uncompressed && !batch_match[i]) {
                    puzzle71::compare::Hash160Uncompressed(
                        batch_x[i], batch_y[i], batch_digest[i]);
                    batch_match[i] = puzzle71::compare::HashMatchesTarget(batch_digest[i]);

                    if (batch_match[i]) {
                        EmitCandidate(true, start_idx + i, false,
                                    batch_x[i], batch_y[i], batch_digest[i]);
                    }
                }
            }
        }

        // Optimized batch elliptic operations - reduce redundant computations
        unsigned int inverse[8] = {0, 0, 0, 0, 0, 0, 0, 1};

        // Combined batch preparation - avoid repeated X coordinate reads
        for (int i = 0; i < current_batch_size; ++i) {
            if (!batch_infinity[i]) {
                beginBatchAddWithDouble(_INC_X, _INC_Y, xPtr, chain,
                                       start_idx + i, start_idx + i, inverse);
            }
        }

        // Single batch inverse for all points - only if we have valid points
        bool has_valid_points = false;
        for (int i = 0; i < current_batch_size; ++i) {
            if (!batch_infinity[i]) {
                has_valid_points = true;
                break;
            }
        }

        if (has_valid_points) {
            doBatchInverse(inverse);
        }

        // Batch complete elliptic curve operations - optimized memory access
        for (int i = 0; i < current_batch_size; ++i) {
            if (!batch_infinity[i]) {
                unsigned int newX[8], newY[8];
                completeBatchAddWithDouble(_INC_X, _INC_Y, xPtr, yPtr,
                                         start_idx + i, start_idx + i, chain,
                                         inverse, newX, newY);
                writeInt(xPtr, start_idx + i, newX);
                writeInt(yPtr, start_idx + i, newY);
            } else {
                // Handle infinity points efficiently - direct constant assignment
                writeInt(xPtr, start_idx + i, _INC_X);
                writeInt(yPtr, start_idx + i, _INC_Y);
            }
        }
    }
}

// Phase A optimization: Add launch_bounds to optimize block size
// Fixed: Removed minBlocksPerSM parameter (was causing nvlink regcount errors)
// Previous issue: __launch_bounds__(256, 6) limited max regcount to 40
// but SHA256/RIPEMD160 functions need 51-99 registers
// Solution: Let compiler auto-optimize register allocation within 256 threads/block
__global__ void __launch_bounds__(256) Puzzle71FusedKernel(int pointsPerThread, int compression) {
    DoPuzzle71Iteration(pointsPerThread, compression);
}

std::atomic<bool> g_register_audit{false};

}  // namespace

namespace puzzle71::kernel {

KernelLaunchConfig ChooseLaunchConfig(std::uint64_t desired_threads) {
    int device_id = 0;
    if (cudaGetDevice(&device_id) != cudaSuccess) {
        device_id = 0;
    }

    {
        std::lock_guard<std::mutex> lock(g_deterministic_mutex);
        auto it = g_deterministic_by_device.find(device_id);
        if (it != g_deterministic_by_device.end()) {
            return it->second;
        }
        if (g_global_deterministic) {
            return *g_global_deterministic;
        }
    }

    KernelLaunchConfig config{};

    // Get GPU device properties for optimal configuration
    cudaDeviceProp device_props;
    cudaError_t err = cudaGetDeviceProperties(&device_props, device_id);
    if (err != cudaSuccess) {
        // Fallback to conservative defaults if device query fails
        device_props.multiProcessorCount = 28;
        device_props.maxThreadsPerBlock = 1024;
        device_props.maxThreadsPerMultiProcessor = 2048;
        device_props.maxGridSize[0] = 2147483647;
    }

    // Use occupancy API to get optimal block size
    int min_grid = 0;
    int block_size = 0;
    cudaError_t occ_status = cudaOccupancyMaxPotentialBlockSize(&min_grid, &block_size, Puzzle71FusedKernel, 0, 0);

    // Optimize block size based on GPU architecture
    // Target: maximize blocks/SM for high occupancy
    // Hopper (sm_90): 192-256 threads/block for 8-10 blocks/SM
    // Ampere/Ada (sm_80-89): 256-384 threads/block
    // Turing (sm_75): 256-512 threads/block
    if (device_props.major >= 9) {
        // Hopper: H20, H100 - use 256 for 8 blocks/SM (2048/256=8)
        block_size = 256;
    } else if (device_props.major >= 8) {
        // Ampere/Ada: A100, RTX 30xx/40xx
        block_size = 256;
    } else if (occ_status != cudaSuccess || block_size <= 0) {
        block_size = std::min(static_cast<int>(device_props.maxThreadsPerBlock), 1024);
    }

    // Clamp block size to proven range for reference kernels
    block_size = std::clamp(block_size, 128, 512);

    // Calculate optimal grid size to fully utilize all SMs
    unsigned int sm_count = device_props.multiProcessorCount;
    unsigned int max_blocks_per_sm = device_props.maxThreadsPerMultiProcessor / block_size;

    // Phase A optimization: Aggressive grid sizing for Hopper/Ampere
    // Target high block count for maximum occupancy
    // Hopper (sm_90): 16 blocks/SM = 1248 blocks on H20 (78 SMs)
    // Ampere/Ada (sm_80-89): 12 blocks/SM
    // Older arch (sm_75): 8 blocks/SM
    unsigned int target_blocks_per_sm = std::min<unsigned int>(
        max_blocks_per_sm,
        device_props.major >= 9 ? 16 : (device_props.major >= 8 ? 12 : 8)
    );
    unsigned int optimal_blocks = sm_count * target_blocks_per_sm;

    // Don't exceed device limits but maximize utilization
    std::uint64_t blocks = std::min<std::uint64_t>(optimal_blocks,
                                                  static_cast<std::uint64_t>(device_props.maxGridSize[0]));

    // Adjust blocks based on desired threads
    if (desired_threads > 0) {
        std::uint64_t needed_blocks = (desired_threads + block_size - 1) / block_size;
        blocks = std::min(blocks, needed_blocks);
    }

    // Ensure at least minimum grid from occupancy analysis
    blocks = std::max<std::uint64_t>(blocks, static_cast<std::uint64_t>(min_grid > 0 ? min_grid : 1));

    config.block = dim3(static_cast<unsigned int>(block_size), 1, 1);
    config.grid = dim3(static_cast<unsigned int>(blocks), 1, 1);
    config.batch_size = static_cast<std::uint64_t>(config.block.x) * config.grid.x;
    config.points_per_thread = 1;

    return config;
}

void SetDeterministicLaunchConfig(const KernelLaunchConfig& config) {
    int device_id = 0;
    if (cudaGetDevice(&device_id) != cudaSuccess) {
        device_id = 0;
    }
    std::lock_guard<std::mutex> lock(g_deterministic_mutex);
    g_deterministic_by_device[device_id] = config;
    g_global_deterministic = config;
}

void ClearDeterministicLaunchConfig() {
    int device_id = 0;
    if (cudaGetDevice(&device_id) != cudaSuccess) {
        device_id = 0;
    }
    std::lock_guard<std::mutex> lock(g_deterministic_mutex);
    g_deterministic_by_device.erase(device_id);
    if (g_deterministic_by_device.empty()) {
        g_global_deterministic.reset();
    }
}

bool HasDeterministicLaunchConfig() {
    std::lock_guard<std::mutex> lock(g_deterministic_mutex);
    return !g_deterministic_by_device.empty() || g_global_deterministic.has_value();
}

cudaError_t LaunchFusedKernel(dim3 grid,
                              dim3 block,
                              int points_per_thread,
                              int compression) {
    if (g_register_audit.load(std::memory_order_relaxed)) {
        cudaFuncAttributes attrs{};
        if (cudaFuncGetAttributes(&attrs, Puzzle71FusedKernel) == cudaSuccess) {
            std::fprintf(stderr,
                         "[register_audit] fused_kernel regs=%d shared=%zu bytes\n",
                         attrs.numRegs,
                         static_cast<std::size_t>(attrs.sharedSizeBytes));
        }
    }

    Puzzle71FusedKernel<<<grid, block>>>(points_per_thread, compression);
    return cudaGetLastError();
}

cudaError_t SetResultBuffer(const puzzle71::gpu::DeviceResultBuffer& buffer) {
    return cudaMemcpyToSymbol(g_result_buffer, &buffer, sizeof(buffer));
}

void EnableRegisterAudit(bool enabled) {
    g_register_audit.store(enabled, std::memory_order_relaxed);
}

bool IsRegisterAuditEnabled() {
    return g_register_audit.load(std::memory_order_relaxed);
}

}  // namespace puzzle71::kernel
