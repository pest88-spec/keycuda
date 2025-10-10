#!/bin/bash

# KeySearchException Debug Script
# 用极小范围复现并调试KeySearchException问题

set -e

DEBUG_DIR="debug_keysearch"
LOG_FILE="${DEBUG_DIR}/debug.log"
GPU_MONITOR_LOG="${DEBUG_DIR}/gpu_monitor.log"

mkdir -p "$DEBUG_DIR"

echo "=== KeySearchException Debug Session ==="
echo "Date: $(date)"
echo "GPU: $(nvidia-smi --query-gpu=name --format=csv,noheader,nounits | head -1)"
echo "CUDA: $(nvcc --version | grep release | awk '{print $5}' | sed 's/,//')"
echo ""

# 函数：启动GPU监控
start_gpu_monitoring() {
    echo "Starting GPU monitoring..."
    nvidia-smi dmon -d 1 -s pucvmet -c 300 > "$GPU_MONITOR_LOG" &
    GPU_MONITOR_PID=$!
    echo "GPU Monitor PID: $GPU_MONITOR_PID"
}

# 函数：停止GPU监控
stop_gpu_monitoring() {
    echo "Stopping GPU monitoring..."
    if [[ -n "$GPU_MONITOR_PID" ]]; then
        kill $GPU_MONITOR_PID 2>/dev/null || true
        wait $GPU_MONITOR_PID 2>/dev/null || true
    fi
}

# 函数：添加调试信息到CudaKeySearchDevice
add_debug_logging() {
    echo "Adding debug logging to CudaKeySearchDevice..."

    # 备份原始文件
    cp third_party/BitCrack/CudaKeySearchDevice/CudaKeySearchDevice.cpp \
       third_party/BitCrack/CudaKeySearchDevice/CudaKeySearchDevice.cpp.backup

    # 在构造函数中添加调试日志
    sed -i '/CudaKeySearchDevice::CudaKeySearchDevice(int device, int threads, int pointsPerThread, int blocks)/a\\
    printf("DEBUG: CudaKeySearchDevice constructor - device=%d, threads=%d, pointsPerThread=%d, blocks=%d\\n", device, threads, pointsPerThread, blocks);' \
        third_party/BitCrack/CudaKeySearchDevice/CudaKeySearchDevice.cpp

    # 在init函数中添加调试日志
    sed -i '/void CudaKeySearchDevice::init(const secp256k1::uint256 &start, int compression, const secp256k1::uint256 &stride)/a\\
    printf("DEBUG: CudaKeySearchDevice::init - start=0x%s, compression=%d, stride=0x%s\\n", start.toString().c_str(), compression, stride.toString().c_str());' \
        third_party/BitCrack/CudaKeySearchDevice/CudaKeySearchDevice.cpp

    # 在cudaCall函数中添加调试日志
    sed -i '/void CudaKeySearchDevice::cudaCall(cudaError_t err)/,/printf("DEBUG: CudaKeySearchDevice::cudaCall - err=%d, msg=%s\\n", err, cudaGetErrorString(err));/c\
    void CudaKeySearchDevice::cudaCall(cudaError_t err)\
    {\
        if(err) {\
            std::string errStr = cudaGetErrorString(err);\
            printf("DEBUG: CudaKeySearchDevice::cudaCall - err=%d, msg=%s\\n", err, errStr.c_str());\
            throw KeySearchException(errStr);\
        }\
    }'
}

# 函数：移除调试日志
remove_debug_logging() {
    echo "Removing debug logging..."
    if [[ -f "third_party/BitCrack/CudaKeySearchDevice/CudaKeySearchDevice.cpp.backup" ]]; then
        mv third_party/BitCrack/CudaKeySearchDevice/CudaKeySearchDevice.cpp.backup \
           third_party/BitCrack/CudaKeySearchDevice/CudaKeySearchDevice.cpp
    fi
}

# 函数：运行极小范围测试
run_micro_test() {
    local test_name="$1"
    local keyspace_start="$2"
    local keyspace_end="$3"

    echo "=== Running Micro Test: $test_name ==="
    echo "Keyspace: $keyspace_start : $keyspace_end"
    echo "Expected: Very small range for debugging"
    echo ""

    start_gpu_monitoring

    start_time=$(date +%s.%N)

    # 运行测试并捕获所有输出
    {
        echo "=== Test Execution Start ==="
        echo "Timestamp: $(date)"
        echo "Working Directory: $(pwd)"
        echo "Binary: $(ls -la ./build/Puzzle71Solver)"
        echo ""

        timeout 30s ./build/Puzzle71Solver \
            --keyspace "${keyspace_start}:${keyspace_end}" \
            --target-address "1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU" \
            --operator-id "debug-test" \
            --operator-purpose "keysearch-exception-debug" \
            --telemetry-jsonl "$DEBUG_DIR/telemetry" \
            2>&1

        exit_code=$?
        echo "=== Test Execution End ==="
        echo "Exit Code: $exit_code"
        echo "Timestamp: $(date)"

    } | tee -a "$LOG_FILE"

    end_time=$(date +%s.%N)
    duration=$(echo "$end_time - $start_time" | bc -l)

    stop_gpu_monitoring

    echo "Test completed in ${duration}s"
    echo "Exit Code: $exit_code"
    echo ""

    # 记录结果
    cat >> "${DEBUG_DIR}/${test_name}_result.json" << EOF
{
  "test_name": "$test_name",
  "keyspace_start": "$keyspace_start",
  "keyspace_end": "$keyspace_end",
  "start_time": $start_time,
  "end_time": $end_time,
  "duration": $duration,
  "exit_code": $exit_code,
  "timestamp": "$(date -Iseconds)"
}
EOF
}

# 主调试流程
echo "Starting KeySearchException debugging..."
echo ""

# 检查二进制文件
if [[ ! -f "./build/Puzzle71Solver" ]]; then
    echo "❌ Puzzle71Solver not found. Building first..."
    make -j$(nproc) Puzzle71Solver
fi

echo "✅ Puzzle71Solver binary found"
echo ""

# 添加调试日志
add_debug_logging

# 重新编译以包含调试日志
echo "Recompiling with debug logging..."
make -j$(nproc) Puzzle71Solver

echo ""
echo "=== Running Micro Tests for Debugging ==="
echo ""

# 测试1：极小范围 (10个key)
run_micro_test "micro_10_keys" "0x1" "0xA"

# 测试2：小范围 (100个key)
run_micro_test "small_100_keys" "0x1" "0x64"

# 测试3：中等小范围 (1000个key)
run_micro_test "medium_1k_keys" "0x1" "0x3E8"

# 清理调试日志
remove_debug_logging

echo ""
echo "=== Debug Session Summary ==="
echo "Logs: $LOG_FILE"
echo "GPU Monitor: $GPU_MONITOR_LOG"
echo "Results: ${DEBUG_DIR}/"

# 分析GPU监控数据
if [[ -f "$GPU_MONITOR_LOG" ]]; then
    echo ""
    echo "=== GPU Monitor Analysis ==="
    avg_util=$(tail -n +4 "$GPU_MONITOR_LOG" 2>/dev/null | awk 'NR>1 && $6!="" {sum+=$6; count++} END {if(count>0) print sum/count; else print 0}')
    max_util=$(tail -n +4 "$GPU_MONITOR_LOG" 2>/dev/null | awk 'NR>1 && $6!="" {if($6>max) max=$6} END {print max+0}')
    echo "Average GPU Utilization: ${avg_util}%"
    echo "Maximum GPU Utilization: ${max_util}%"
fi

echo ""
echo "KeySearchException debug session completed at: $(date)"