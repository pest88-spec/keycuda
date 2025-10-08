#!/usr/bin/env python3
"""
Test script to verify batch configuration and GPU memory usage
"""

import subprocess
import json
import time
import os

def run_test_with_memory_monitor():
    """Run a simple test while monitoring GPU memory"""

    print("=== 批次配置和显存使用测试 ===")

    # Start monitoring GPU memory in background
    print("启动GPU监控...")
    monitor_cmd = [
        'nvidia-smi',
        '--query-gpu=memory.used,memory.total,utilization.gpu,power.draw',
        '--format=csv,noheader,nounits',
        '-l', '1'  # Every 1 second
    ]

    # Run benchmark with verbose output
    print("运行基准测试...")
    benchmark_cmd = ['../scripts/run-benchmarks.sh']

    # Get initial GPU state
    initial_result = subprocess.run(['nvidia-smi', '--query-gpu=memory.used,memory.total,utilization.gpu', '--format=csv,noheader,nounits'],
                                   capture_output=True, text=True)
    initial_memory = initial_result.stdout.strip().split(', ')[0] if initial_result.returncode == 0 else "0"

    print(f"初始显存使用: {initial_memory} MB")

    # Run the benchmark
    start_time = time.time()
    process = subprocess.Popen(benchmark_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

    # Monitor memory during execution
    peak_memory = int(initial_memory)
    samples_checked = 0

    while process.poll() is None and samples_checked < 30:  # Monitor for max 30 seconds
        try:
            result = subprocess.run(['nvidia-smi', '--query-gpu=memory.used,utilization.gpu', '--format=csv,noheader,nounits'],
                                  capture_output=True, text=True, timeout=2)
            if result.returncode == 0:
                mem, util = result.stdout.strip().split(', ')
                current_memory = int(mem)
                current_util = int(util)
                peak_memory = max(peak_memory, current_memory)

                print(f"当前显存: {current_memory} MB, GPU利用率: {current_util}%")

                # If we see significant memory usage, that's good
                if current_memory > 100:
                    print(f"✓ 检测到显著显存使用: {current_memory} MB")

                # If we see GPU utilization, that's even better
                if current_util > 0:
                    print(f"✓ 检测到GPU利用率: {current_util}%")

        except subprocess.TimeoutExpired:
            pass

        time.sleep(0.5)
        samples_checked += 1

    # Get final output
    stdout, stderr = process.communicate()

    print(f"\n=== 测试结果 ===")
    print(f"峰值显存使用: {peak_memory} MB")
    print(f"测试时间: {time.time() - start_time:.2f} 秒")
    print(f"监控样本数: {samples_checked}")

    # Parse benchmark results if available
    if "Benchmark results written to" in stdout:
        try:
            with open('../benchmarks/latest.json', 'r') as f:
                benchmark_data = json.load(f)
                keys_per_sec = benchmark_data['stats']['mean']
                print(f"性能: {keys_per_sec/1_000_000:.2f} Mkeys/s")

                if peak_memory > 1:
                    memory_efficiency = keys_per_sec / peak_memory
                    print(f"显存效率: {memory_efficiency:.0f} keys/s per MB")
                else:
                    print("⚠ 显存使用过低，批次配置可能未生效")
        except Exception as e:
            print(f"无法读取基准测试结果: {e}")

    # Analysis
    print(f"\n=== 分析 ===")
    if peak_memory < 10:
        print("❌ 显存使用极低 - 批次配置有问题")
        print("   可能原因:")
        print("   - GetProgressiveBatchConfig函数未被调用")
        print("   - 内存分配失败")
        print("   - 使用了默认的小批次配置")
    elif peak_memory < 100:
        print("⚠ 显存使用较低 - 批次大小可能不够大")
    else:
        print("✓ 显存使用正常")

    return peak_memory

def check_batch_config_code():
    """Check if batch configuration changes are actually applied"""
    print("\n=== 检查批次配置代码 ===")

    # Check if the aggressive config is in the code
    with open('../src/ComputeCore/gpu/gpu_executor.cpp', 'r') as f:
        content = f.read()

    if '16384' in content and '512' in content:
        print("✓ 找到超激进批次配置 (grid=16384, ppt=512)")
    else:
        print("⚠ 未找到超激进批次配置")

    if 'GPU-UTIL' in content:
        print("✓ 找到GPU利用率调试日志")
    else:
        print("⚠ 未找到GPU利用率调试日志")

if __name__ == "__main__":
    print("批次配置和GPU利用率测试")
    print("=" * 50)

    check_batch_config_code()
    peak_memory = run_test_with_memory_monitor()

    print(f"\n=== 总结 ===")
    print(f"峰值显存使用: {peak_memory} MB")
    if peak_memory < 100:
        print("建议: 检查批次配置是否正确应用")
        print("      可能需要强制使用更大的批次大小")