#!/bin/bash

# Puzzle71Solver GPU Performance Benchmark Script
# 测试GPU优化效果并生成详细性能报告

set -e

# 配置参数
BENCHMARK_DIR="benchmarks"
RESULTS_FILE="${BENCHMARK_DIR}/latest.json"
LOG_FILE="${BENCHMARK_DIR}/benchmark.log"
GPU_MONITOR_INTERVAL=1  # 秒

# 测试配置 - 使用正确的Puzzle 71范围（较小值开始，较大值结束）
KEYSPACE_START="0x1"
KEYSPACE_END="0x100000"  # 小范围用于快速测试
TARGET_ADDRESS="1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU"
OPERATOR_ID="benchmark"
OPERATOR_PURPOSE="performance-test"

# 创建基准测试目录
mkdir -p "$BENCHMARK_DIR"

# 清理旧文件
rm -f "$RESULTS_FILE" "$LOG_FILE"

echo "=== Puzzle71Solver GPU Performance Benchmark ==="
echo "Date: $(date)"
echo "GPU: $(nvidia-smi --query-gpu=name --format=csv,noheader,nounits | head -1)"
echo "CUDA: $(nvcc --version | grep release | awk '{print $5}' | sed 's/,//')"
echo ""

# 获取GPU基准信息
echo "=== GPU Information ==="
nvidia-smi --query-gpu=name,memory.total,compute_cap --format=csv,noheader,nounits | head -1 | \
    awk -F',' '{printf "GPU Model: %s\nMemory: %s MB\nCompute Capability: %s\n", $1, $2, $3}'
echo ""

# 函数：运行基准测试
run_benchmark() {
    local test_name="$1"
    local keyspace_start="$2"
    local keyspace_end="$3"
    local expected_duration="$4"

    echo "=== Running: $test_name ==="
    echo "Keyspace: $keyspace_start : $keyspace_end"
    echo "Expected duration: ${expected_duration}s"

    # 启动GPU监控
    nvidia-smi dmon -d $GPU_MONITOR_INTERVAL -s m -c 100 > "${BENCHMARK_DIR}/gpu_monitor_${test_name}.log" &
    GPU_MONITOR_PID=$!

    # 运行测试
    start_time=$(date +%s.%N)

    timeout ${expected_duration}s ./Puzzle71Solver \
        --keyspace "${keyspace_start}:${keyspace_end}" \
        --target-address "$TARGET_ADDRESS" \
        --operator-id "${OPERATOR_ID}" \
        --operator-purpose "${OPERATOR_PURPOSE}" \
        --telemetry-jsonl "telemetry" \
        2>&1 | tee -a "$LOG_FILE"

    exit_code=$?
    end_time=$(date +%s.%N)

    # 停止GPU监控
    kill $GPU_MONITOR_PID 2>/dev/null || true

    # 计算实际运行时间
    duration=$(echo "$end_time - $start_time" | bc -l)

    echo "Actual duration: ${duration}s"
    echo "Exit code: $exit_code"
    echo ""

    # 记录结果
    echo "{
  \"test_name\": \"$test_name\",
  \"keyspace_start\": \"$keyspace_start\",
  \"keyspace_end\": \"$keyspace_end\",
  \"start_time\": $start_time,
  \"end_time\": $end_time,
  \"duration\": $duration,
  \"exit_code\": $exit_code,
  \"timestamp\": \"$(date -Iseconds)\"
}" >> "${BENCHMARK_DIR}/${test_name}.json"
}

# 函数：分析GPU监控日志
analyze_gpu_usage() {
    local test_name="$1"
    local log_file="${BENCHMARK_DIR}/gpu_monitor_${test_name}.log"

    if [[ -f "$log_file" ]]; then
        echo "=== GPU Usage Analysis for $test_name ==="

        # 计算平均GPU利用率
        avg_util=$(tail -n +3 "$log_file" | awk 'NR>1 {sum+=$6; count++} END {if(count>0) print sum/count; else print 0}')

        # 计算最大GPU利用率
        max_util=$(tail -n +3 "$log_file" | awk 'NR>1 {if($6>max) max=$6} END {print max+0}')

        # 计算平均内存使用
        avg_mem=$(tail -n +3 "$log_file" | awk 'NR>1 {sum+=$3; count++} END {if(count>0) print sum/count; else print 0}')

        # 计算最大内存使用
        max_mem=$(tail -n +3 "$log_file" | awk 'NR>1 {if($3>max) max=$3} END {print max+0}')

        echo "Average GPU Utilization: ${avg_util}%"
        echo "Maximum GPU Utilization: ${max_util}%"
        echo "Average Memory Usage: ${avg_mem} MiB"
        echo "Maximum Memory Usage: ${max_mem} MiB"
        echo ""

        # 保存分析结果
        echo "{
  \"test_name\": \"$test_name\",
  \"avg_gpu_util\": $avg_util,
  \"max_gpu_util\": $max_util,
  \"avg_memory_mb\": $avg_mem,
  \"max_memory_mb\": $max_mem
}" >> "${BENCHMARK_DIR}/${test_name}_gpu_stats.json"
    fi
}

# 函数：分析telemetry数据
analyze_telemetry() {
    echo "=== Telemetry Analysis ==="

    if [[ -d "telemetry" ]]; then
        total_keys=0
        total_time=0
        file_count=0

        for telemetry_file in telemetry/*.jsonl; do
            if [[ -f "$telemetry_file" ]]; then
                echo "Analyzing: $telemetry_file"

                # 提取keys_processed并求和
                keys_processed=$(grep -o '"keys_processed":[0-9]*' "$telemetry_file" | cut -d: -f2 | awk '{sum+=$1} END {print sum+0}')

                # 提取时间戳计算运行时间
                first_timestamp=$(head -1 "$telemetry_file" | grep -o '"ts":[0-9.]*' | cut -d: -f2)
                last_timestamp=$(tail -1 "$telemetry_file" | grep -o '"ts":[0-9.]*' | cut -d: -f2)

                if [[ -n "$first_timestamp" && -n "$last_timestamp" ]]; then
                    run_time=$(echo "$last_timestamp - $first_timestamp" | bc -l)
                    total_time=$(echo "$total_time + $run_time" | bc -l)
                fi

                total_keys=$(echo "$total_keys + $keys_processed" | bc -l)
                file_count=$((file_count + 1))

                echo "  Keys processed: $keys_processed"
                if [[ -n "$run_time" ]]; then
                    echo "  Run time: ${run_time}s"
                    if (( $(echo "$run_time > 0" | bc -l) )); then
                        throughput=$(echo "scale=2; $keys_processed / $run_time" | bc -l)
                        echo "  Throughput: ${throughput} keys/sec"
                    fi
                fi
                echo ""
            fi
        done

        if (( $(echo "$total_time > 0" | bc -l) )); then
            avg_throughput=$(echo "scale=2; $total_keys / $total_time" | bc -l)
            echo "=== Overall Performance ==="
            echo "Total keys processed: $total_keys"
            echo "Total time: ${total_time}s"
            echo "Average throughput: ${avg_throughput} keys/sec"

            # 保存总体结果
            echo "{
  \"total_keys\": $total_keys,
  \"total_time\": $total_time,
  \"avg_throughput\": $avg_throughput,
  \"test_files\": $file_count,
  \"timestamp\": \"$(date -Iseconds)\"
}" > "$RESULTS_FILE"
        fi
    else
        echo "No telemetry data found"
    fi
}

# 运行基准测试序列
echo "Starting benchmark tests..."
echo ""

# 测试1：小范围快速测试
run_benchmark "quick_test" "0x1" "0x10000" 10
analyze_gpu_usage "quick_test"

# 测试2：中等范围测试
run_benchmark "medium_test" "0x1" "0x100000" 30
analyze_gpu_usage "medium_test"

# 测试3：大范围测试
run_benchmark "large_test" "0x1" "0x1000000" 60
analyze_gpu_usage "large_test"

# 分析telemetry数据
analyze_telemetry

# 生成最终报告
echo "=== Benchmark Summary ==="
echo "Results saved to: $RESULTS_FILE"
echo "Log file: $LOG_FILE"

if [[ -f "$RESULTS_FILE" ]]; then
    echo ""
    echo "=== Final Performance Metrics ==="
    cat "$RESULTS_FILE"
fi

echo ""
echo "Benchmark completed at: $(date)"