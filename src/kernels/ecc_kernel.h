/**
 * @file ecc_kernel.h
 * @brief ECC点运算CUDA Kernel头文件
 * 
 * P0-C002修复：ECC计算专用kernel接口
 */

#pragma once

#include <cuda_runtime.h>
#include <cstdint>

namespace puzzle71::kernels {

/**
 * @brief ECC点运算Kernel
 *
 * 执行批量ECC点加法，使用Montgomery批量逆元优化
 * 寄存器使用：~30个/线程
 *
 * 注意：xPtr, yPtr, chain指针在kernel内部通过ec::getXPtr()等函数获取
 *
 * @param pointsPerThread 每个线程处理的点数量
 */
__global__ void EccKernel(
    int pointsPerThread
);

/**
 * @brief 启动ECC Kernel
 *
 * @param gridDim Grid维度
 * @param blockDim Block维度
 * @param pointsPerThread 每个线程处理的点数量
 * @param stream CUDA流（nullptr表示默认流）
 * @return cudaError_t 错误码
 */
cudaError_t LaunchEccKernel(
    dim3 gridDim,
    dim3 blockDim,
    int pointsPerThread,
    cudaStream_t stream = nullptr
);

}  // namespace puzzle71::kernels

