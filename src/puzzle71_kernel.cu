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
#include <optional>

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
std::optional<puzzle71::kernel::KernelLaunchConfig> g_deterministic_launch;

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

__device__ inline void WriteCandidate(int idx,
                                      bool compressed,
                                      const unsigned int x[8],
                                      const unsigned int y[8],
                                      const std::uint32_t digest[5]) {
    if (g_result_buffer.capacity == 0 || g_result_buffer.candidates == nullptr ||
        g_result_buffer.count == nullptr) {
        return;
    }
    unsigned int slot = atomicAdd(g_result_buffer.count, 1u);
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

    unsigned int inverse[8] = {0, 0, 0, 0, 0, 0, 0, 1};

    for (int i = 0; i < pointsPerThread; ++i) {
        unsigned int x[8];
        readInt(xPtr, i, x);

        if (check_uncompressed) {
            unsigned int y[8];
            readInt(yPtr, i, y);

            std::uint32_t digest[5];
            puzzle71::compare::Hash160Uncompressed(x, y, digest);

            if (puzzle71::compare::HashMatchesTarget(digest)) {
                unsigned int y_full[8];
                copyBigInt(y, y_full);
                WriteCandidate(i, false, x, y_full, digest);
            }
        }

        if (check_compressed) {
            std::uint32_t digest[5];
            unsigned int y_parity = readIntLSW(yPtr, i);
            puzzle71::compare::Hash160Compressed(x, y_parity, digest);

            if (puzzle71::compare::HashMatchesTarget(digest)) {
                unsigned int y[8];
                readInt(yPtr, i, y);
                WriteCandidate(i, true, x, y, digest);
            }
        }

        beginBatchAddWithDouble(_INC_X, _INC_Y, xPtr, chain, i, i, inverse);
    }

    doBatchInverse(inverse);

    for (int i = pointsPerThread - 1; i >= 0; --i) {
        unsigned int newX[8];
        unsigned int newY[8];

        unsigned int x[8];
        readInt(xPtr, i, x);
        bool infinity = isInfinity(x);

        if (!infinity) {
            completeBatchAddWithDouble(_INC_X,
                                       _INC_Y,
                                       xPtr,
                                       yPtr,
                                       i,
                                       i,
                                       chain,
                                       inverse,
                                       newX,
                                       newY);
            writeInt(xPtr, i, newX);
            writeInt(yPtr, i, newY);
        } else {
            copyBigInt(_INC_X, newX);
            copyBigInt(_INC_Y, newY);
            writeInt(xPtr, i, newX);
            writeInt(yPtr, i, newY);
        }
    }
}

__global__ void Puzzle71FusedKernel(int pointsPerThread, int compression) {
    DoPuzzle71Iteration(pointsPerThread, compression);
}

std::atomic<bool> g_register_audit{false};

}  // namespace

namespace puzzle71::kernel {

KernelLaunchConfig ChooseLaunchConfig(std::uint64_t desired_threads) {
    if (g_deterministic_launch) {
        return *g_deterministic_launch;
    }

    KernelLaunchConfig config{};

    // Get GPU device properties for optimal configuration
    cudaDeviceProp device_props;
    cudaError_t err = cudaGetDeviceProperties(&device_props, 0);
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
    if (occ_status != cudaSuccess || block_size <= 0) {
        block_size = std::min(static_cast<int>(device_props.maxThreadsPerBlock), 1024);
    }

    // Calculate optimal grid size to fully utilize all SMs
    unsigned int sm_count = device_props.multiProcessorCount;
    unsigned int max_blocks_per_sm = device_props.maxThreadsPerMultiProcessor / block_size;
    unsigned int optimal_blocks = sm_count * std::max<unsigned int>(max_blocks_per_sm, 1);

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
    g_deterministic_launch = config;
}

void ClearDeterministicLaunchConfig() {
    g_deterministic_launch.reset();
}

bool HasDeterministicLaunchConfig() {
    return g_deterministic_launch.has_value();
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
