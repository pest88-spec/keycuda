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

// Memory access optimization: Use shared memory for data reuse and reduce global memory access
extern __shared__ unsigned int shared_memory[];

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

    // Memory optimization: Enhanced batch processing with shared memory
    const int BATCH_SIZE = 16; // Increased from 8 for better memory utilization
    const int SHARED_X_OFFSET = 0;
    const int SHARED_Y_OFFSET = BATCH_SIZE * 8;
    const int SHARED_DIGEST_OFFSET = SHARED_Y_OFFSET + BATCH_SIZE * 8;
    const int SHARED_WORK_OFFSET = SHARED_DIGEST_OFFSET + BATCH_SIZE * 5;

    // Shared memory layout:
    // - SHARED_X_OFFSET: BATCH_SIZE * 8 unsigned ints for X coordinates
    // - SHARED_Y_OFFSET: BATCH_SIZE * 8 unsigned ints for Y coordinates
    // - SHARED_DIGEST_OFFSET: BATCH_SIZE * 5 unsigned ints for digests
    // - SHARED_WORK_OFFSET: Working area for intermediate computations

    unsigned int* shared_x = &shared_memory[SHARED_X_OFFSET];
    unsigned int* shared_y = &shared_memory[SHARED_Y_OFFSET];
    std::uint32_t* shared_digest = reinterpret_cast<std::uint32_t*>(&shared_memory[SHARED_DIGEST_OFFSET]);
    bool* shared_match = reinterpret_cast<bool*>(&shared_memory[SHARED_WORK_OFFSET]);
    bool* shared_infinity = &shared_match[BATCH_SIZE];

    int batches = (pointsPerThread + BATCH_SIZE - 1) / BATCH_SIZE;

    for (int batch = 0; batch < batches; ++batch) {
        int start_idx = batch * BATCH_SIZE;
        int end_idx = min(start_idx + BATCH_SIZE, pointsPerThread);
        int current_batch_size = end_idx - start_idx;

        // Memory optimization: Coalesced global memory reads with shared memory staging
        // Read X coordinates coalesced - each thread reads consecutive elements
        #pragma unroll
        for (int i = 0; i < current_batch_size; i += 8) { // Process 8 elements per iteration for 256-bit loads
            int base_idx = start_idx + i;
            if (base_idx < pointsPerThread) {
                // Load 8 consecutive X coordinates (8 * 32 = 256 bits) - optimal memory transaction
                unsigned int x_val[8];
                #pragma unroll
                for (int j = 0; j < 8 && (i + j) < current_batch_size; ++j) {
                    readInt(xPtr, base_idx + j, x_val[j]);
                }

                // Store to shared memory for fast access by all threads in warp
                #pragma unroll
                for (int j = 0; j < 8 && (i + j) < current_batch_size; ++j) {
                    // Store in shared memory with proper alignment
                    int shared_offset = (i + j) * 8;
                    #pragma unroll
                    for (int k = 0; k < 8; ++k) {
                        shared_x[shared_offset + k] = x_val[j][k];
                    }
                }
            }
        }

        // Memory optimization: Prefetch Y coordinates for points that will be processed
        __syncthreads(); // Ensure X coordinates are loaded before proceeding

        // Read Y coordinates only for non-infinity points
        #pragma unroll
        for (int i = 0; i < current_batch_size; i += 4) { // Process 4 elements for Y
            int base_idx = start_idx + i;
            if (base_idx < pointsPerThread) {
                // Check if points are infinity before loading Y
                bool is_inf[BATCH_SIZE];
                #pragma unroll
                for (int j = 0; j < 4 && (i + j) < current_batch_size; ++j) {
                    is_inf[j] = isInfinity(&shared_x[(i + j) * 8]);
                    shared_infinity[i + j] = is_inf[j];
                }

                // Load Y coordinates only for non-infinity points
                if (!is_inf[0] || !is_inf[1] || !is_inf[2] || !is_inf[3]) {
                    unsigned int y_val[4][8];
                    #pragma unroll
                    for (int j = 0; j < 4 && (i + j) < current_batch_size; ++j) {
                        if (!is_inf[j]) {
                            readInt(yPtr, base_idx + j, y_val[j]);
                        }
                    }

                    // Store to shared memory
                    #pragma unroll
                    for (int j = 0; j < 4 && (i + j) < current_batch_size; ++j) {
                        if (!is_inf[j]) {
                            int shared_offset = (i + j) * 8;
                            #pragma unroll
                            for (int k = 0; k < 8; ++k) {
                                shared_y[shared_offset + k] = y_val[j][k];
                            }
                        }
                    }
                }
            }
        }

        __syncthreads(); // Ensure all coordinates are loaded before computation

        // Warp-level optimization: use ballot to reduce branch divergence
        unsigned uncompressed_mask = __ballot_sync(0xffffffff, check_uncompressed);
        unsigned compressed_mask = __ballot_sync(0xffffffff, check_compressed);

        // Memory optimization: Enhanced batch HASH160 computation with shared memory
        if (uncompressed_mask || compressed_mask) {
            // Preload Y parities for compressed hash computation (memory coalescing)
            unsigned int y_parities[BATCH_SIZE];
            #pragma unroll
            for (int i = 0; i < current_batch_size; i += 8) {
                int base_idx = start_idx + i;
                if (base_idx < pointsPerThread) {
                    // Read 8 Y parities coalesced
                    #pragma unroll
                    for (int j = 0; j < 8 && (i + j) < current_batch_size; ++j) {
                        if (!shared_infinity[i + j]) {
                            y_parities[i + j] = readIntLSW(yPtr, base_idx + j);
                        }
                    }
                }
            }

            // Optimized hash computation with memory access pattern optimization
            #pragma unroll
            for (int i = 0; i < current_batch_size; ++i) {
                if (shared_infinity[i]) continue;

                // Compute compressed hash first using shared memory data
                if (check_compressed) {
                    puzzle71::compare::Hash160Compressed(
                        &shared_x[i * 8], y_parities[i], &shared_digest[i * 5]);
                    shared_match[i] = puzzle71::compare::HashMatchesTarget(&shared_digest[i * 5]);

                    if (shared_match[i]) {
                        // Memory optimization: Delay full Y coordinate read until match confirmed
                        // Use shared memory if Y already loaded, otherwise read from global
                        unsigned int full_y[8];
                        bool y_in_shared = !shared_infinity[i];

                        if (y_in_shared) {
                            // Y coordinate already in shared memory
                            #pragma unroll
                            for (int k = 0; k < 8; ++k) {
                                full_y[k] = shared_y[(i * 8) + k];
                            }
                        } else {
                            // Fallback to global memory read (should be rare)
                            readInt(yPtr, start_idx + i, full_y);
                        }

                        EmitCandidate(true, start_idx + i, true,
                                    &shared_x[i * 8], full_y, &shared_digest[i * 5]);
                    }
                }

                // Memory optimization: Only compute uncompressed hash if needed and no compressed match
                if (check_uncompressed && !shared_match[i]) {
                    // Use shared memory data for uncompressed hash computation
                    puzzle71::compare::Hash160Uncompressed(
                        &shared_x[i * 8], &shared_y[i * 8], &shared_digest[i * 5]);
                    shared_match[i] = puzzle71::compare::HashMatchesTarget(&shared_digest[i * 5]);

                    if (shared_match[i]) {
                        EmitCandidate(true, start_idx + i, false,
                                    &shared_x[i * 8], &shared_y[i * 8], &shared_digest[i * 5]);
                    }
                }
            }
        }

        // Memory optimization: Enhanced batch elliptic operations with shared memory staging
        unsigned int inverse[8] = {0, 0, 0, 0, 0, 0, 0, 1};
        unsigned int new_x[BATCH_SIZE * 8]; // Temporary storage for new X coordinates
        unsigned int new_y[BATCH_SIZE * 8]; // Temporary storage for new Y coordinates

        // Memory optimization: Use shared memory to avoid repeated global memory reads
        // Combined batch preparation using shared memory data
        #pragma unroll
        for (int i = 0; i < current_batch_size; ++i) {
            if (!shared_infinity[i]) {
                beginBatchAddWithDouble(_INC_X, _INC_Y, &shared_x[i * 8], chain,
                                       0, 0, inverse); // Use shared memory offsets
            }
        }

        // Memory optimization: Single batch inverse for all valid points
        bool has_valid_points = false;
        #pragma unroll
        for (int i = 0; i < current_batch_size; ++i) {
            if (!shared_infinity[i]) {
                has_valid_points = true;
                break;
            }
        }

        if (has_valid_points) {
            doBatchInverse(inverse);
        }

        // Memory optimization: Batch complete operations with coalesced writes
        // First, compute all results in temporary storage
        #pragma unroll
        for (int i = 0; i < current_batch_size; ++i) {
            if (!shared_infinity[i]) {
                completeBatchAddWithDouble(_INC_X, _INC_Y, &shared_x[i * 8], &shared_y[i * 8],
                                         0, 0, chain, inverse, &new_x[i * 8], &new_y[i * 8]);
            } else {
                // Handle infinity points efficiently - use constant data
                #pragma unroll
                for (int k = 0; k < 8; ++k) {
                    new_x[i * 8 + k] = _INC_X[k];
                    new_y[i * 8 + k] = _INC_Y[k];
                }
            }
        }

        // Memory optimization: Coalesced global memory writes
        // Write X coordinates coalesced - better memory bandwidth utilization
        #pragma unroll
        for (int i = 0; i < current_batch_size; i += 4) { // Process 4 elements for optimal write patterns
            int base_idx = start_idx + i;
            if (base_idx < pointsPerThread) {
                #pragma unroll
                for (int j = 0; j < 4 && (i + j) < current_batch_size; ++j) {
                    writeInt(xPtr, base_idx + j, &new_x[(i + j) * 8]);
                }
            }
        }

        // Memory optimization: Write Y coordinates coalesced
        __syncthreads(); // Small synchronization to ensure X writes complete
        #pragma unroll
        for (int i = 0; i < current_batch_size; i += 4) {
            int base_idx = start_idx + i;
            if (base_idx < pointsPerThread) {
                #pragma unroll
                for (int j = 0; j < 4 && (i + j) < current_batch_size; ++j) {
                    writeInt(yPtr, base_idx + j, &new_y[(i + j) * 8]);
                }
            }
        }

        __syncthreads(); // Ensure all writes complete before next batch
    }
}

// Phase A optimization: Remove fixed launch bounds to enable dynamic block sizing
// Previous: __launch_bounds__(256) limited block size to 256 threads
// Issue: Fixed bounds prevented optimal block sizes for different GPU architectures
// Solution: Remove launch bounds to allow dynamic block sizing (256-1024 threads)
// This enables architecture-specific optimization:
// - Hopper (sm_90): 512-1024 threads/block for maximum occupancy
// - Ada Lovelace (sm_89): 384-768 threads/block for balanced performance
// - Ampere (sm_80-86): 256-512 threads/block for optimal utilization
// - Turing (sm_75): 256-384 threads/block for conservative operation
__global__ void Puzzle71FusedKernel(int pointsPerThread, int compression) {
    DoPuzzle71Iteration(pointsPerThread, compression);
}

// Memory optimization: Calculate shared memory requirements for enhanced kernel
size_t CalculateRequiredSharedMemory(int block_size) {
    // Enhanced batch size for memory optimization
    const int BATCH_SIZE = 16;

    // Shared memory layout requirements:
    // - X coordinates: BATCH_SIZE * 8 unsigned ints
    // - Y coordinates: BATCH_SIZE * 8 unsigned ints
    // - Digests: BATCH_SIZE * 5 unsigned ints
    // - Working area: BATCH_SIZE booleans (matches) + BATCH_SIZE booleans (infinity)

    size_t shared_x_bytes = BATCH_SIZE * 8 * sizeof(unsigned int);
    size_t shared_y_bytes = BATCH_SIZE * 8 * sizeof(unsigned int);
    size_t shared_digest_bytes = BATCH_SIZE * 5 * sizeof(unsigned int);
    size_t shared_work_bytes = 2 * BATCH_SIZE * sizeof(bool); // matches + infinity

    // Total shared memory requirement
    size_t total_shared = shared_x_bytes + shared_y_bytes + shared_digest_bytes + shared_work_bytes;

    // Add padding for memory alignment (32-byte alignment for optimal performance)
    const size_t alignment = 32;
    total_shared = ((total_shared + alignment - 1) / alignment) * alignment;

    return total_shared;
}

// Memory optimization: Enhanced kernel launch with shared memory configuration
void LaunchPuzzle71FusedKernel(int pointsPerThread, int compression,
                               const dim3& grid_size, const dim3& block_size,
                               cudaStream_t stream = 0) {
    size_t shared_memory_size = CalculateRequiredSharedMemory(block_size.x);

    std::cout << "[KERNEL] Launching with shared memory: " << shared_memory_size
              << " bytes, grid=" << grid_size.x << ", block=" << block_size.x << std::endl;

    Puzzle71FusedKernel<<<grid_size, block_size, shared_memory_size, stream>>>(pointsPerThread, compression);

    // Check for launch errors
    cudaError_t launch_error = cudaGetLastError();
    if (launch_error != cudaSuccess) {
        std::cerr << "[KERNEL] Launch error: " << cudaGetErrorString(launch_error) << std::endl;
        std::cerr << "[KERNEL] Grid: " << grid_size.x << ", Block: " << block_size.x
                  << ", Shared: " << shared_memory_size << " bytes" << std::endl;
    }
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

    // Phase A optimization: Enhanced block size optimization with expanded range
    // Target: maximize blocks/SM for high occupancy with flexible block sizing (128-1024)
    // Hopper (sm_90): 512-1024 threads/block for 2-4 blocks/SM maximum throughput
    // Ada Lovelace (sm_89): 384-896 threads/block for balanced performance
    // Ampere (sm_80-86): 256-768 threads/block for optimal utilization
    // Turing (sm_75): 128-512 threads/block for flexible operation
    // Pascal (sm_60-61): 128-384 threads/block for conservative operation
    if (device_props.major >= 9) {
        // Hopper: H20, H100 - use 896 threads for optimal occupancy (2048/896≈2.3 blocks/SM)
        // Maximum thread counts for Hopper's instruction-level parallelism
        block_size = 896;
    } else if (device_props.major >= 8) {
        if (device_props.minor >= 9) {
            // Ada Lovelace (RTX 40xx): 640 threads for optimal balance (128 threads/warps * 5 warps)
            block_size = 640;
        } else {
            // Ampere (A100, RTX 30xx): 512 threads for excellent occupancy (2048/512=4 blocks/SM)
            block_size = 512;
        }
    } else if (device_props.major >= 7) {
        // Turing: 384 threads for improved occupancy over conservative 256
        block_size = 384;
    } else if (device_props.major >= 6) {
        // Pascal: Support with smaller block sizes for memory-bound workloads
        block_size = 256;
    } else if (occ_status != cudaSuccess || block_size <= 0) {
        // Fallback for unknown architectures
        block_size = std::min(static_cast<int>(device_props.maxThreadsPerBlock), 512);
    }

    // Phase A optimization: Remove fixed lower bound to enable dynamic block sizing
    // Previous: Fixed lower bound of 256 prevented small block optimization
    // Issue: Conservative minimum limited flexibility for certain workloads
    // Solution: Allow 128-1024 range for optimal block sizing across workloads
    // This enables workload-specific optimization:
    // - Small workloads: 128-256 threads/block for better resource utilization
    // - Medium workloads: 256-512 threads/block for balanced performance
    // - Large workloads: 512-1024 threads/block for maximum throughput
    block_size = std::clamp(block_size, 128, 1024);

    // Calculate optimal grid size to fully utilize all SMs
    unsigned int sm_count = device_props.multiProcessorCount;
    unsigned int max_blocks_per_sm = device_props.maxThreadsPerMultiProcessor / block_size;

    // Phase A optimization: Enhanced adaptive grid sizing with flexible block support
    // Target optimal blocks/SM based on architecture and expanded block size range (128-1024)
    // Hopper (sm_90): 2-4 blocks/SM with large blocks (896-1024 threads) for maximum throughput
    // Ada Lovelace (sm_89): 3-6 blocks/SM with medium-large blocks (640-896 threads)
    // Ampere (sm_80-86): 4-8 blocks/SM with medium blocks (512-768 threads) for balance
    // Turing (sm_75): 4-10 blocks/SM with flexible blocks (384-512 threads) for efficiency
    // Pascal (sm_60-61): 6-12 blocks/SM with smaller blocks (128-384 threads) for compatibility
    unsigned int target_blocks_per_sm;
    if (device_props.major >= 9) {
        // Hopper: Fewer blocks with maximum thread count for instruction-level parallelism
        target_blocks_per_sm = std::min<unsigned int>(max_blocks_per_sm,
            block_size >= 896 ? 3 : 4);
    } else if (device_props.major >= 8) {
        if (device_props.minor >= 9) {
            // Ada Lovelace: Balanced approach with medium-large blocks
            target_blocks_per_sm = std::min<unsigned int>(max_blocks_per_sm,
                block_size >= 640 ? 4 : 6);
        } else {
            // Ampere: More blocks with medium size for excellent occupancy
            target_blocks_per_sm = std::min<unsigned int>(max_blocks_per_sm,
                block_size >= 512 ? 6 : 8);
        }
    } else if (device_props.major >= 7) {
        // Turing: Flexible block sizing for different workload characteristics
        target_blocks_per_sm = std::min<unsigned int>(max_blocks_per_sm,
            block_size >= 384 ? 6 : 10);
    } else if (device_props.major >= 6) {
        // Pascal: Smaller blocks, more blocks per SM for compatibility
        target_blocks_per_sm = std::min<unsigned int>(max_blocks_per_sm, 12);
    } else {
        // Older architectures: Very conservative with maximum flexibility
        target_blocks_per_sm = std::min<unsigned int>(max_blocks_per_sm, 16);
    }
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
