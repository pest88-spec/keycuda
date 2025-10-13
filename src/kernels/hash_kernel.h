/**
 * @file hash_kernel.h
 * @brief Hash计算和地址比对CUDA Kernel头文件
 * 
 * P0-C002修复：Hash计算专用kernel接口
 */

#pragma once

#include <cuda_runtime.h>
#include <cstdint>

#include "compute/gpu/device_results.h"

namespace puzzle71::kernels {

/**
 * @brief Hash计算和地址比对Kernel
 *
 * 执行SHA256+RIPEMD160计算并比对目标地址
 * 寄存器使用：~40个/线程
 *
 * 注意：xPtr, yPtr指针在kernel内部通过ec::getXPtr()等函数获取
 *
 * @param pointsPerThread 每个线程处理的点数量
 * @param compression 压缩类型（UNCOMPRESSED/COMPRESSED/BOTH）
 */
__global__ void HashKernel(
    int pointsPerThread,
    int compression
);

/**
 * @brief 启动Hash Kernel
 *
 * @param gridDim Grid维度
 * @param blockDim Block维度
 * @param pointsPerThread 每个线程处理的点数量
 * @param compression 压缩类型
 * @param stream CUDA流（nullptr表示默认流）
 * @return cudaError_t 错误码
 */
cudaError_t LaunchHashKernel(
    dim3 gridDim,
    dim3 blockDim,
    int pointsPerThread,
    int compression,
    cudaStream_t stream = nullptr
);

/**
 * @brief 设置结果缓冲区
 * 
 * 必须在启动HashKernel之前调用
 * 
 * @param buffer 结果缓冲区
 * @return cudaError_t 错误码
 */
cudaError_t SetResultBuffer(const gpu::DeviceResultBuffer& buffer);

}  // namespace puzzle71::kernels

