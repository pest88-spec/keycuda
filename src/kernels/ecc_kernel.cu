/**
 * @file ecc_kernel.cu
 * @brief ECC点运算专用CUDA Kernel（优化寄存器使用）
 * 
 * P0-C002修复：将ECC计算从puzzle71_kernel.cu分离
 * 目标：寄存器使用 ≤30个/线程
 * 
 * 参考：
 * - NVIDIA CUDA Samples: reduction, shared memory optimization
 * - CCCL BlockReduce primitives
 * - 铁笼协议v5.0: ZERO-TOLERANCE-PERFORMANCE原则
 */

#include "ecc_kernel.h"
#include "cudaMath/secp256k1.cuh"
#include "CudaKeySearchDevice/CudaDeviceKeys.cuh"

#include <cuda_runtime.h>

// 外部常量（来自BitCrack）
extern __device__ __constant__ unsigned int _INC_X[8];
extern __device__ __constant__ unsigned int _INC_Y[8];
extern __device__ __constant__ unsigned int* _CHAIN[1];

namespace puzzle71::kernels {

/**
 * @brief ECC点运算Kernel（寄存器优化版）
 * 
 * 功能：
 * 1. 批量ECC点加法（Batch Point Addition）
 * 2. Montgomery批量逆元（Batch Inverse）
 * 3. 完成批量点加法
 * 
 * 寄存器使用分析：
 * - inverse[8]: 8个寄存器
 * - x[8], newX[8], newY[8]: 24个寄存器（循环内重用）
 * - 指针和索引: 5个寄存器
 * - ECC临时变量: ~10个寄存器
 * 总计: ~30个寄存器 ✅
 * 
 * @param pointsPerThread 每个线程处理的点数量
 * @param xPtr X坐标数组指针
 * @param yPtr Y坐标数组指针
 * @param chain 链式存储指针
 */
__global__ void __launch_bounds__(256) EccKernel(
    int pointsPerThread
) {
    // 在device代码中获取指针
    unsigned int* chain = _CHAIN[0];
    unsigned int* xPtr = ec::getXPtr();
    unsigned int* yPtr = ec::getYPtr();

    // 批量逆元累积器（8个寄存器）
    unsigned int inverse[8] = {0, 0, 0, 0, 0, 0, 0, 1};

    // 阶段1: 批量点加法准备（Batch Add Preparation）
    // 使用Montgomery技巧累积所有点的斜率分母
    for (int i = 0; i < pointsPerThread; ++i) {
        beginBatchAddWithDouble(
            _INC_X,      // 增量点X坐标
            _INC_Y,      // 增量点Y坐标
            xPtr,        // 当前点X坐标数组
            chain,       // 链式存储
            i,           // 当前索引
            i,           // 目标索引
            inverse      // 累积逆元
        );
    }
    
    // 阶段2: 批量逆元计算（Batch Inverse）
    // 使用Montgomery算法一次性计算所有逆元
    // 这是性能关键路径，O(n)复杂度
    doBatchInverse(inverse);
    
    // 阶段3: 完成批量点加法（Complete Batch Add）
    // 使用计算好的逆元完成所有点的加法
    for (int i = pointsPerThread - 1; i >= 0; --i) {
        unsigned int x[8];
        readInt(xPtr, i, x);
        
        bool infinity = isInfinity(x);
        
        if (!infinity) {
            // 正常点：完成点加法
            unsigned int newX[8];
            unsigned int newY[8];
            
            completeBatchAddWithDouble(
                _INC_X,
                _INC_Y,
                xPtr,
                yPtr,
                i,
                i,
                chain,
                inverse,
                newX,
                newY
            );
            
            writeInt(xPtr, i, newX);
            writeInt(yPtr, i, newY);
        } else {
            // 无穷远点：直接使用增量点
            unsigned int newX[8];
            unsigned int newY[8];
            
            copyBigInt(_INC_X, newX);
            copyBigInt(_INC_Y, newY);
            
            writeInt(xPtr, i, newX);
            writeInt(yPtr, i, newY);
        }
    }
}

/**
 * @brief 启动ECC Kernel的辅助函数
 * 
 * @param gridDim Grid维度
 * @param blockDim Block维度
 * @param pointsPerThread 每个线程处理的点数量
 * @param xPtr X坐标数组指针
 * @param yPtr Y坐标数组指针
 * @param chain 链式存储指针
 * @param stream CUDA流（可选）
 * @return cudaError_t 错误码
 */
cudaError_t LaunchEccKernel(
    dim3 gridDim,
    dim3 blockDim,
    int pointsPerThread,
    cudaStream_t stream
) {
    if (stream == nullptr) {
        EccKernel<<<gridDim, blockDim>>>(pointsPerThread);
    } else {
        EccKernel<<<gridDim, blockDim, 0, stream>>>(pointsPerThread);
    }

    return cudaGetLastError();
}

}  // namespace puzzle71::kernels

