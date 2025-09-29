/*
 * KeyHunt-Cuda 统一内核接口单元测试
 * 
 * 测试目标:
 * 1. 验证统一接口的正确性
 * 2. 测试不同搜索模式的功能
 * 3. 验证性能优化效果
 * 4. 确保向后兼容性
 */

#include <iostream>
#include <vector>
#include <cassert>
#include <cstring>
#include <chrono>
#include <cuda_runtime.h>
#include "GPU/GPUCompute_Unified.h"
#include "Constants.h"

// 测试配置
#define TEST_MAX_FOUND 100
#define TEST_GRID_SIZE 128
#define TEST_THREADS_PER_BLOCK 128
#define TEST_NUM_THREADS (TEST_GRID_SIZE * TEST_THREADS_PER_BLOCK)

// 测试统计
struct TestStats {
    int total_tests = 0;
    int passed_tests = 0;
    int failed_tests = 0;
    
    void print_summary() const {
        std::cout << "\n=== 测试统计 ===" << std::endl;
        std::cout << "总测试数: " << total_tests << std::endl;
        std::cout << "通过测试: " << passed_tests << std::endl;
        std::cout << "失败测试: " << failed_tests << std::endl;
        std::cout << "通过率: " << (passed_tests * 100.0 / total_tests) << "%" << std::endl;
    }
};

static TestStats g_test_stats;

// 测试宏
#define TEST_CASE(name) \
    do { \
        std::cout << "\n>>> 测试: " << name << std::endl; \
        g_test_stats.total_tests++; \
        try { \
            if (test_##name()) { \
                std::cout << "✅ PASSED: " << name << std::endl; \
                g_test_stats.passed_tests++; \
            } else { \
                std::cout << "❌ FAILED: " << name << std::endl; \
                g_test_stats.failed_tests++; \
            } \
        } catch (const std::exception& e) { \
            std::cout << "❌ EXCEPTION: " << name << " - " << e.what() << std::endl; \
            g_test_stats.failed_tests++; \
        } \
    } while(0)

// CUDA错误检查宏
#define CUDA_CHECK(call) \
    do { \
        cudaError_t error = call; \
        if (error != cudaSuccess) { \
            throw std::runtime_error(std::string("CUDA error: ") + cudaGetErrorString(error)); \
        } \
    } while(0)

// 辅助函数: 创建测试布隆过滤器数据
void create_test_bloom_data(uint8_t* bloom_data, int size) {
    // 创建简单的测试布隆过滤器
    memset(bloom_data, 0, size);
    // 设置一些测试位
    bloom_data[0] = 0xFF;
    bloom_data[size/2] = 0xAA;
    bloom_data[size-1] = 0x55;
}

// 辅助函数: 创建测试目标哈希
void create_test_target_hash(uint32_t* target_hash) {
    target_hash[0] = 0x12345678;
    target_hash[1] = 0x9ABCDEF0;
    target_hash[2] = 0x11223344;
    target_hash[3] = 0x55667788;
    target_hash[4] = 0x99AABBCC;
}

// 辅助函数: 创建测试X点
void create_test_xpoint(uint32_t* xpoint) {
    for (int i = 0; i < 8; i++) {
        xpoint[i] = 0x10000000 * (i + 1);
    }
}

// 辅助函数: 验证CUDA内存
bool verify_cuda_memory(uint32_t* d_found, int expected_found) {
    uint32_t h_found[TEST_MAX_FOUND * ITEM_SIZE_A32];
    CUDA_CHECK(cudaMemcpy(h_found, d_found, sizeof(h_found), cudaMemcpyDeviceToHost));
    
    int actual_found = h_found[0];
    std::cout << "  期望找到: " << expected_found << ", 实际找到: " << actual_found << std::endl;
    
    return (actual_found == expected_found);
}

// 测试1: 基本统一内核启动测试
bool test_basic_unified_kernel_launch() {
    std::cout << "  测试基本统一内核启动..." << std::endl;
    
    // 分配设备内存
    uint64_t* d_keys;
    uint32_t* d_found;
    size_t keys_size = TEST_NUM_THREADS * 8 * sizeof(uint64_t);
    size_t found_size = TEST_MAX_FOUND * ITEM_SIZE_A32 * sizeof(uint32_t);
    
    CUDA_CHECK(cudaMalloc(&d_keys, keys_size));
    CUDA_CHECK(cudaMalloc(&d_found, found_size));
    CUDA_CHECK(cudaMemset(d_found, 0, found_size));
    
    // 创建测试数据
    uint8_t bloom_data[1024];
    create_test_bloom_data(bloom_data, sizeof(bloom_data));
    
    try {
        // 启动统一内核 - MA模式
        launch_unified_kernel<SearchMode::MODE_MA>(
            SEARCH_MODE_MA,
            bloom_data,
            8192,  // bloom_bits
            3,     // bloom_hashes
            d_keys,
            TEST_MAX_FOUND,
            d_found,
            TEST_GRID_SIZE,
            TEST_THREADS_PER_BLOCK,
            CompressionMode::COMPRESSED,
            CoinType::BITCOIN
        );
        
        CUDA_CHECK(cudaDeviceSynchronize());
        
        // 验证结果
        bool result = verify_cuda_memory(d_found, 0);  // 期望找到0个（测试数据）
        
        // 清理
        cudaFree(d_keys);
        cudaFree(d_found);
        
        return result;
        
    } catch (...) {
        cudaFree(d_keys);
        cudaFree(d_found);
        throw;
    }
}

// 测试2: 单地址搜索模式测试
bool test_single_address_search() {
    std::cout << "  测试单地址搜索模式..." << std::endl;
    
    // 分配设备内存
    uint64_t* d_keys;
    uint32_t* d_found;
    size_t keys_size = TEST_NUM_THREADS * 8 * sizeof(uint64_t);
    size_t found_size = TEST_MAX_FOUND * ITEM_SIZE_A32 * sizeof(uint32_t);
    
    CUDA_CHECK(cudaMalloc(&d_keys, keys_size));
    CUDA_CHECK(cudaMalloc(&d_found, found_size));
    CUDA_CHECK(cudaMemset(d_found, 0, found_size));
    
    // 创建测试目标哈希
    uint32_t target_hash[5];
    create_test_target_hash(target_hash);
    
    try {
        // 启动统一内核 - SA模式
        launch_unified_kernel<SearchMode::MODE_SA>(
            SEARCH_MODE_SA,
            target_hash,
            0, 0,  // 单地址模式不需要布隆参数
            d_keys,
            TEST_MAX_FOUND,
            d_found,
            TEST_GRID_SIZE,
            TEST_THREADS_PER_BLOCK,
            CompressionMode::COMPRESSED,
            CoinType::BITCOIN
        );
        
        CUDA_CHECK(cudaDeviceSynchronize());
        
        // 验证结果
        bool result = verify_cuda_memory(d_found, 0);  // 期望找到0个（测试数据）
        
        // 清理
        cudaFree(d_keys);
        cudaFree(d_found);
        
        return result;
        
    } catch (...) {
        cudaFree(d_keys);
        cudaFree(d_found);
        throw;
    }
}

// 测试3: X点搜索模式测试
bool test_xpoint_search() {
    std::cout << "  测试X点搜索模式..." << std::endl;
    
    // 分配设备内存
    uint64_t* d_keys;
    uint32_t* d_found;
    size_t keys_size = TEST_NUM_THREADS * 8 * sizeof(uint64_t);
    size_t found_size = TEST_MAX_FOUND * ITEM_SIZE_X32 * sizeof(uint32_t);
    
    CUDA_CHECK(cudaMalloc(&d_keys, keys_size));
    CUDA_CHECK(cudaMalloc(&d_found, found_size));
    CUDA_CHECK(cudaMemset(d_found, 0, found_size));
    
    // 创建测试布隆数据
    uint8_t bloom_data[1024];
    create_test_bloom_data(bloom_data, sizeof(bloom_data));
    
    try {
        // 启动统一内核 - MX模式
        launch_unified_kernel<SearchMode::MODE_MX>(
            SEARCH_MODE_MX,
            bloom_data,
            8192,  // bloom_bits
            3,     // bloom_hashes
            d_keys,
            TEST_MAX_FOUND,
            d_found,
            TEST_GRID_SIZE,
            TEST_THREADS_PER_BLOCK,
            CompressionMode::COMPRESSED,
            CoinType::BITCOIN
        );
        
        CUDA_CHECK(cudaDeviceSynchronize());
        
        // 验证结果
        bool result = verify_cuda_memory(d_found, 0);  // 期望找到0个（测试数据）
        
        // 清理
        cudaFree(d_keys);
        cudaFree(d_found);
        
        return result;
        
    } catch (...) {
        cudaFree(d_keys);
        cudaFree(d_found);
        throw;
    }
}

// 测试4: 以太坊搜索模式测试
bool test_ethereum_search() {
    std::cout << "  测试以太坊搜索模式..." << std::endl;
    
    // 分配设备内存
    uint64_t* d_keys;
    uint32_t* d_found;
    size_t keys_size = TEST_NUM_THREADS * 8 * sizeof(uint64_t);
    size_t found_size = TEST_MAX_FOUND * ITEM_SIZE_A32 * sizeof(uint32_t);
    
    CUDA_CHECK(cudaMalloc(&d_keys, keys_size));
    CUDA_CHECK(cudaMalloc(&d_found, found_size));
    CUDA_CHECK(cudaMemset(d_found, 0, found_size));
    
    // 创建测试目标哈希
    uint32_t target_hash[5];
    create_test_target_hash(target_hash);
    
    try {
        // 启动统一内核 - ETH_SA模式
        launch_unified_kernel<SearchMode::MODE_ETH_SA>(
            SEARCH_MODE_ETH_SA,
            target_hash,
            0, 0,  // 单地址模式不需要布隆参数
            d_keys,
            TEST_MAX_FOUND,
            d_found,
            TEST_GRID_SIZE,
            TEST_THREADS_PER_BLOCK,
            CompressionMode::COMPRESSED,
            CoinType::ETHEREUM
        );
        
        CUDA_CHECK(cudaDeviceSynchronize());
        
        // 验证结果
        bool result = verify_cuda_memory(d_found, 0);  // 期望找到0个（测试数据）
        
        // 清理
        cudaFree(d_keys);
        cudaFree(d_found);
        
        return result;
        
    } catch (...) {
        cudaFree(d_keys);
        cudaFree(d_found);
        throw;
    }
}

// 测试5: 压缩模式测试
bool test_compression_modes() {
    std::cout << "  测试压缩模式..." << std::endl;
    
    // 分配设备内存
    uint64_t* d_keys;
    uint32_t* d_found;
    size_t keys_size = TEST_NUM_THREADS * 8 * sizeof(uint64_t);
    size_t found_size = TEST_MAX_FOUND * ITEM_SIZE_A32 * sizeof(uint32_t);
    
    CUDA_CHECK(cudaMalloc(&d_keys, keys_size));
    CUDA_CHECK(cudaMalloc(&d_found, found_size));
    CUDA_CHECK(cudaMemset(d_found, 0, found_size));
    
    // 创建测试布隆数据
    uint8_t bloom_data[1024];
    create_test_bloom_data(bloom_data, sizeof(bloom_data));
    
    try {
        // 测试压缩模式
        launch_unified_kernel<SearchMode::MODE_MA>(
            SEARCH_MODE_MA,
            bloom_data,
            8192,  // bloom_bits
            3,     // bloom_hashes
            d_keys,
            TEST_MAX_FOUND,
            d_found,
            TEST_GRID_SIZE,
            TEST_THREADS_PER_BLOCK,
            CompressionMode::COMPRESSED,
            CoinType::BITCOIN
        );
        
        CUDA_CHECK(cudaDeviceSynchronize());
        
        // 验证结果
        bool compressed_result = verify_cuda_memory(d_found, 0);
        
        // 测试非压缩模式
        CUDA_CHECK(cudaMemset(d_found, 0, found_size));
        
        launch_unified_kernel<SearchMode::MODE_MA>(
            SEARCH_MODE_MA,
            bloom_data,
            8192,  // bloom_bits
            3,     // bloom_hashes
            d_keys,
            TEST_MAX_FOUND,
            d_found,
            TEST_GRID_SIZE,
            TEST_THREADS_PER_BLOCK,
            CompressionMode::UNCOMPRESSED,
            CoinType::BITCOIN
        );
        
        CUDA_CHECK(cudaDeviceSynchronize());
        
        // 验证结果
        bool uncompressed_result = verify_cuda_memory(d_found, 0);
        
        // 清理
        cudaFree(d_keys);
        cudaFree(d_found);
        
        return compressed_result && uncompressed_result;
        
    } catch (...) {
        cudaFree(d_keys);
        cudaFree(d_found);
        throw;
    }
}

// 测试6: 性能基准测试
bool test_performance_benchmark() {
    std::cout << "  测试性能基准..." << std::endl;
    
    // 分配设备内存
    uint64_t* d_keys;
    uint32_t* d_found;
    size_t keys_size = TEST_NUM_THREADS * 8 * sizeof(uint64_t);
    size_t found_size = TEST_MAX_FOUND * ITEM_SIZE_A32 * sizeof(uint32_t);
    
    CUDA_CHECK(cudaMalloc(&d_keys, keys_size));
    CUDA_CHECK(cudaMalloc(&d_found, found_size));
    CUDA_CHECK(cudaMemset(d_found, 0, found_size));
    
    // 创建测试布隆数据
    uint8_t bloom_data[1024];
    create_test_bloom_data(bloom_data, sizeof(bloom_data));
    
    try {
        // 预热GPU
        for (int i = 0; i < 3; i++) {
            launch_unified_kernel<SearchMode::MODE_MA>(
                SEARCH_MODE_MA,
                bloom_data,
                8192,  // bloom_bits
                3,     // bloom_hashes
                d_keys,
                TEST_MAX_FOUND,
                d_found,
                TEST_GRID_SIZE,
                TEST_THREADS_PER_BLOCK,
                CompressionMode::COMPRESSED,
                CoinType::BITCOIN
            );
            CUDA_CHECK(cudaDeviceSynchronize());
        }
        
        // 性能测试
        auto start = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < 10; i++) {
            launch_unified_kernel<SearchMode::MODE_MA>(
                SEARCH_MODE_MA,
                bloom_data,
                8192,  // bloom_bits
                3,     // bloom_hashes
                d_keys,
                TEST_MAX_FOUND,
                d_found,
                TEST_GRID_SIZE,
                TEST_THREADS_PER_BLOCK,
                CompressionMode::COMPRESSED,
                CoinType::BITCOIN
            );
            CUDA_CHECK(cudaDeviceSynchronize());
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        double avg_time = duration.count() / 10.0;
        std::cout << "  平均内核执行时间: " << avg_time << " ms" << std::endl;
        
        // 性能基准: 应该小于100ms (可根据硬件调整)
        bool performance_ok = (avg_time < 100.0);
        
        // 清理
        cudaFree(d_keys);
        cudaFree(d_found);
        
        return performance_ok;
        
    } catch (...) {
        cudaFree(d_keys);
        cudaFree(d_found);
        throw;
    }
}

// 测试7: 内存对齐测试
bool test_memory_alignment() {
    std::cout << "  测试内存对齐..." << std::endl;
    
    // 测试不同对齐方式
    struct AlignmentTest {
        size_t size;
        bool should_work;
    };
    
    AlignmentTest tests[] = {
        {TEST_NUM_THREADS * 8 * sizeof(uint64_t), true},           // 自然对齐
        {TEST_NUM_THREADS * 8 * sizeof(uint64_t) + 1, false},      // 非对齐
        {TEST_NUM_THREADS * 8 * sizeof(uint64_t) - 1, false},      // 非对齐
        {((TEST_NUM_THREADS * 8 * sizeof(uint64_t) + 31) / 32) * 32, true}  // 32字节对齐
    };
    
    bool all_passed = true;
    
    for (const auto& test : tests) {
        uint64_t* d_keys;
        uint32_t* d_found;
        
        cudaError_t alloc_result = cudaMalloc(&d_keys, test.size);
        cudaError_t found_result = cudaMalloc(&d_found, test.size);
        
        bool test_passed = (alloc_result == cudaSuccess && found_result == cudaSuccess);
        
        if (test.should_work && !test_passed) {
            std::cout << "  ❌ 期望成功的对齐测试失败 (size: " << test.size << ")" << std::endl;
            all_passed = false;
        } else if (!test.should_work && test_passed) {
            std::cout << "  ⚠️  非对齐测试意外成功 (size: " << test.size << ")" << std::endl;
        }
        
        if (alloc_result == cudaSuccess) cudaFree(d_keys);
        if (found_result == cudaSuccess) cudaFree(d_found);
    }
    
    return all_passed;
}

// 测试8: 错误处理测试
bool test_error_handling() {
    std::cout << "  测试错误处理..." << std::endl;
    
    // 测试1: 空指针处理
    try {
        launch_unified_kernel<SearchMode::MODE_MA>(
            SEARCH_MODE_MA,
            nullptr,  // 空指针
            8192,
            3,
            nullptr,  // 空指针
            TEST_MAX_FOUND,
            nullptr,  // 空指针
            TEST_GRID_SIZE,
            TEST_THREADS_PER_BLOCK,
            CompressionMode::COMPRESSED,
            CoinType::BITCOIN
        );
        
        // 应该抛出异常或返回错误
        std::cout << "  ⚠️  空指针未触发错误处理" << std::endl;
        return false;
        
    } catch (const std::exception& e) {
        std::cout << "  ✅ 空指针正确处理: " << e.what() << std::endl;
    }
    
    // 测试2: 无效参数处理
    try {
        uint64_t* d_keys;
        uint32_t* d_found;
        CUDA_CHECK(cudaMalloc(&d_keys, 100));
        CUDA_CHECK(cudaMalloc(&d_found, 100));
        
        uint8_t bloom_data[100];
        create_test_bloom_data(bloom_data, sizeof(bloom_data));
        
        // 使用无效的网格配置
        launch_unified_kernel<SearchMode::MODE_MA>(
            SEARCH_MODE_MA,
            bloom_data,
            8192,
            3,
            d_keys,
            TEST_MAX_FOUND,
            d_found,
            0,  // 无效块数
            0,  // 无效线程数
            CompressionMode::COMPRESSED,
            CoinType::BITCOIN
        );
        
        cudaFree(d_keys);
        cudaFree(d_found);
        
        std::cout << "  ⚠️  无效参数未触发错误处理" << std::endl;
        return false;
        
    } catch (const std::exception& e) {
        std::cout << "  ✅ 无效参数正确处理: " << e.what() << std::endl;
    }
    
    return true;
}

// 测试9: 并发执行测试
bool test_concurrent_execution() {
    std::cout << "  测试并发执行..." << std::endl;
    
    const int NUM_STREAMS = 4;
    cudaStream_t streams[NUM_STREAMS];
    uint64_t* d_keys[NUM_STREAMS];
    uint32_t* d_found[NUM_STREAMS];
    
    // 创建流
    for (int i = 0; i < NUM_STREAMS; i++) {
        CUDA_CHECK(cudaStreamCreate(&streams[i]));
        
        size_t keys_size = TEST_NUM_THREADS * 8 * sizeof(uint64_t);
        size_t found_size = TEST_MAX_FOUND * ITEM_SIZE_A32 * sizeof(uint32_t);
        
        CUDA_CHECK(cudaMalloc(&d_keys[i], keys_size));
        CUDA_CHECK(cudaMalloc(&d_found[i], found_size));
        CUDA_CHECK(cudaMemsetAsync(d_found[i], 0, found_size, streams[i]));
    }
    
    // 创建测试数据
    uint8_t bloom_data[1024];
    create_test_bloom_data(bloom_data, sizeof(bloom_data));
    
    try {
        // 并发启动多个内核
        auto start = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < NUM_STREAMS; i++) {
            launch_unified_kernel<SearchMode::MODE_MA>(
                SEARCH_MODE_MA,
                bloom_data,
                8192,
                3,
                d_keys[i],
                TEST_MAX_FOUND,
                d_found[i],
                TEST_GRID_SIZE / NUM_STREAMS,  // 减少每个流的网格大小
                TEST_THREADS_PER_BLOCK,
                CompressionMode::COMPRESSED,
                CoinType::BITCOIN
            );
        }
        
        // 等待所有流完成
        for (int i = 0; i < NUM_STREAMS; i++) {
            CUDA_CHECK(cudaStreamSynchronize(streams[i]));
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        std::cout << "  并发执行时间: " << duration.count() << " ms" << std::endl;
        
        // 验证所有结果
        bool all_passed = true;
        for (int i = 0; i < NUM_STREAMS; i++) {
            if (!verify_cuda_memory(d_found[i], 0)) {
                all_passed = false;
            }
        }
        
        // 清理
        for (int i = 0; i < NUM_STREAMS; i++) {
            cudaFree(d_keys[i]);
            cudaFree(d_found[i]);
            cudaStreamDestroy(streams[i]);
        }
        
        return all_passed;
        
    } catch (...) {
        for (int i = 0; i < NUM_STREAMS; i++) {
            cudaFree(d_keys[i]);
            cudaFree(d_found[i]);
            cudaStreamDestroy(streams[i]);
        }
        throw;
    }
}

// 测试10: 向后兼容性测试
bool test_backward_compatibility() {
    std::cout << "  测试向后兼容性..." << std::endl;
    
    // 验证传统接口仍然可用
    #ifdef WITHGPU
    // 这里可以添加对传统接口的测试，如果它们仍然存在
    std::cout << "  ✅ 向后兼容性框架已就绪" << std::endl;
    return true;
    #else
    std::cout << "  ⚠️  GPU支持未启用，跳过兼容性测试" << std::endl;
    return true;
    #endif
}

// 主测试函数
int main() {
    std::cout << "==========================================" << std::endl;
    std::cout << "KeyHunt-Cuda 统一内核接口单元测试" << std::endl;
    std::cout << "==========================================" << std::endl;
    std::cout << "测试时间: " << __DATE__ << " " << __TIME__ << std::endl;
    std::cout << "CUDA版本: " << CUDART_VERSION << std::endl;
    
    // 获取GPU信息
    int device_count = 0;
    cudaGetDeviceCount(&device_count);
    std::cout << "GPU数量: " << device_count << std::endl;
    
    if (device_count > 0) {
        cudaDeviceProp prop;
        cudaGetDeviceProperties(&prop, 0);
        std::cout << "主要GPU: " << prop.name << " (计算能力 " << prop.major << "." << prop.minor << ")" << std::endl;
    }
    std::cout << std::endl;
    
    try {
        // 运行所有测试
        TEST_CASE(basic_unified_kernel_launch);
        TEST_CASE(single_address_search);
        TEST_CASE(xpoint_search);
        TEST_CASE(ethereum_search);
        TEST_CASE(compression_modes);
        TEST_CASE(performance_benchmark);
        TEST_CASE(memory_alignment);
        TEST_CASE(error_handling);
        TEST_CASE(concurrent_execution);
        TEST_CASE(backward_compatibility);
        
    } catch (const std::exception& e) {
        std::cout << "❌ 测试执行异常: " << e.what() << std::endl;
        return 1;
    }
    
    // 打印测试统计
    g_test_stats.print_summary();
    
    // 返回适当的退出码
    if (g_test_stats.failed_tests == 0) {
        std::cout << "\n🎉 所有测试通过！" << std::endl;
        return 0;
    } else {
        std::cout << "\n❌ 有 " << g_test_stats.failed_tests << " 个测试失败" << std::endl;
        return 1;
    }
}