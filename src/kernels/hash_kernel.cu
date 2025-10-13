/**
 * @file hash_kernel.cu
 * @brief Hash计算和地址比对专用CUDA Kernel（优化寄存器使用）
 * 
 * P0-C002修复：将Hash计算从puzzle71_kernel.cu分离
 * 目标：寄存器使用 ≤40个/线程
 * 
 * 参考：
 * - NVIDIA CUDA Samples: SHA256/RIPEMD160优化
 * - 铁笼协议v5.0: ZERO-TOLERANCE-PERFORMANCE原则
 */

#include "hash_kernel.h"
#include "compare/kernels/hash160_fused.h"
#include "utils/endianness.h"
#include "CudaKeySearchDevice/CudaDeviceKeys.cuh"
#include "KeyFinderLib/KeySearchTypes.h"
#include "compute/gpu/device_buffers.h"
#include "compute/gpu/device_results.h"
#include "cudaMath/secp256k1.cuh"

#include <cuda_runtime.h>

using puzzle71::gpu::DeviceCandidate;
using puzzle71::gpu::DeviceResultBuffer;

namespace {

// 全局结果缓冲区
__device__ DeviceResultBuffer g_result_buffer;

/**
 * @brief 完成Hash160摘要（添加IV并字节交换）
 */
__device__ inline void FinalizeDigest(const std::uint32_t in[5], std::uint32_t out[5]) {
    const std::uint32_t iv[5] = {
        0x67452301u,
        0xefcdab89u,
        0x98badcfeu,
        0x10325476u,
        0xc3d2e1f0u
    };
    
    for (int i = 0; i < 5; ++i) {
        const std::uint32_t value = in[i] + iv[(i + 1) % 5];
        out[i] = puzzle71::utils::ByteSwap32(value);
    }
}

/**
 * @brief 发射候选结果（使用warp级优化）
 * 
 * 使用__ballot_sync和__popc减少atomic操作次数
 */
__device__ inline void EmitCandidate(
    bool has_candidate,
    int idx,
    bool compressed,
    const unsigned int x[8],
    const unsigned int y[8],
    const std::uint32_t digest[5]
) {
    if (g_result_buffer.capacity == 0 || 
        g_result_buffer.candidates == nullptr ||
        g_result_buffer.count == nullptr) {
        return;
    }
    
    const unsigned full_mask = 0xffffffffu;
    unsigned active = __ballot_sync(full_mask, has_candidate);
    
    if (active == 0u) {
        return;  // 整个warp都没有候选
    }
    
    const int lane = threadIdx.x & 31;
    const int leader = __ffs(active) - 1;
    const unsigned int matches = __popc(active);
    
    std::uint32_t base_index = 0;
    
    // 只有leader线程执行atomic操作
    if (lane == leader) {
        base_index = atomicAdd(g_result_buffer.count, matches);
    }
    
    // 广播base_index到整个warp
    base_index = __shfl_sync(full_mask, base_index, leader);
    
    if (!has_candidate) {
        return;
    }
    
    // 计算当前线程在warp中的相对位置
    const unsigned int mask_before = (1u << lane) - 1u;
    const unsigned int offset = __popc(active & mask_before);
    const std::uint32_t slot = base_index + offset;
    
    if (slot >= g_result_buffer.capacity) {
        return;  // 缓冲区已满
    }
    
    // 写入结果
    DeviceCandidate& out = g_result_buffer.candidates[slot];
    out.idx = static_cast<std::uint32_t>(idx);
    out.compressed = compressed ? 1u : 0u;
    
    for (int i = 0; i < 8; ++i) {
        out.x[i] = x[i];
        out.y[i] = y[i];
    }
    
    FinalizeDigest(digest, out.digest);
}

}  // namespace

namespace puzzle71::kernels {

/**
 * @brief Hash计算和地址比对Kernel（寄存器优化版）
 * 
 * 功能：
 * 1. 读取ECC计算后的公钥坐标
 * 2. 计算Hash160（SHA256 + RIPEMD160）
 * 3. 比对目标地址
 * 4. 发射匹配的候选
 * 
 * 寄存器使用分析：
 * - x[8], y[8]: 16个寄存器
 * - digest[5]: 5个寄存器
 * - SHA256状态: ~10个寄存器（优化后）
 * - RIPEMD160状态: ~10个寄存器（优化后）
 * - 其他变量: ~5个寄存器
 * 总计: ~40个寄存器 ✅
 * 
 * @param pointsPerThread 每个线程处理的点数量
 * @param compression 压缩类型（UNCOMPRESSED/COMPRESSED/BOTH）
 * @param xPtr X坐标数组指针
 * @param yPtr Y坐标数组指针
 */
__global__ void __launch_bounds__(256) HashKernel(
    int pointsPerThread,
    int compression
) {
    // 在device代码中获取指针
    unsigned int* xPtr = ec::getXPtr();
    unsigned int* yPtr = ec::getYPtr();

    const bool check_uncompressed =
        (compression == PointCompressionType::UNCOMPRESSED) ||
        (compression == PointCompressionType::BOTH);
    const bool check_compressed =
        (compression == PointCompressionType::COMPRESSED) ||
        (compression == PointCompressionType::BOTH);

    for (int i = 0; i < pointsPerThread; ++i) {
        unsigned int x[8];
        readInt(xPtr, i, x);
        
        // 检查未压缩地址
        if (check_uncompressed) {
            unsigned int y[8];
            std::uint32_t digest[5]{};
            
            readInt(yPtr, i, y);
            puzzle71::compare::Hash160Uncompressed(x, y, digest);
            
            bool match = puzzle71::compare::HashMatchesTarget(digest);
            EmitCandidate(match, i, false, x, y, digest);
        }
        
        // 检查压缩地址
        if (check_compressed) {
            std::uint32_t digest[5]{};
            unsigned int y_parity = readIntLSW(yPtr, i);
            
            puzzle71::compare::Hash160Compressed(x, y_parity, digest);
            
            bool match = puzzle71::compare::HashMatchesTarget(digest);
            
            // 只有匹配时才读取完整的Y坐标
            unsigned int y_full[8]{};
            if (match) {
                readInt(yPtr, i, y_full);
            }
            
            EmitCandidate(match, i, true, x, y_full, digest);
        }
    }
}

/**
 * @brief 启动Hash Kernel的辅助函数
 * 
 * @param gridDim Grid维度
 * @param blockDim Block维度
 * @param pointsPerThread 每个线程处理的点数量
 * @param compression 压缩类型
 * @param xPtr X坐标数组指针
 * @param yPtr Y坐标数组指针
 * @param stream CUDA流（可选）
 * @return cudaError_t 错误码
 */
cudaError_t LaunchHashKernel(
    dim3 gridDim,
    dim3 blockDim,
    int pointsPerThread,
    int compression,
    cudaStream_t stream
) {
    if (stream == nullptr) {
        HashKernel<<<gridDim, blockDim>>>(pointsPerThread, compression);
    } else {
        HashKernel<<<gridDim, blockDim, 0, stream>>>(pointsPerThread, compression);
    }

    return cudaGetLastError();
}

/**
 * @brief 设置结果缓冲区
 * 
 * @param buffer 结果缓冲区指针
 * @return cudaError_t 错误码
 */
cudaError_t SetResultBuffer(const DeviceResultBuffer& buffer) {
    return cudaMemcpyToSymbol(
        g_result_buffer,
        &buffer,
        sizeof(DeviceResultBuffer)
    );
}

}  // namespace puzzle71::kernels

