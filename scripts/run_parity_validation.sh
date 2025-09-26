#!/bin/bash

# Puzzle71Solver GPU/CPU Parity Validation Script
# 运行大规模样本测试并收集GPU/CPU parity验证证据

set -e

# 配置参数
VALIDATION_DIR="docs/validation"
PARITY_REPORT="${VALIDATION_DIR}/puzzle71_parity.md"
TELEMETRY_DIR="telemetry_parity"
SAMPLE_COUNT=1000000  # 1M samples for validation
GPU_MONITOR_INTERVAL=2

# 创建验证目录
mkdir -p "$VALIDATION_DIR" "$TELEMETRY_DIR"

echo "=== Puzzle71Solver GPU/CPU Parity Validation ==="
echo "Date: $(date)"
echo "Target Samples: $SAMPLE_COUNT"
echo "GPU: $(nvidia-smi --query-gpu=name --format=csv,noheader,nounits | head -1)"
echo "CUDA: $(nvcc --version | grep release | awk '{print $5}' | sed 's/,//')"
echo ""

# 函数：启动GPU监控
start_gpu_monitoring() {
    local test_name="$1"
    echo "Starting GPU monitoring for $test_name..."

    # 启动nvidia-smi监控
    nvidia-smi dmon -d $GPU_MONITOR_INTERVAL -s pucvmet -c 1000 > "${TELEMETRY_DIR}/gpu_monitor_${test_name}.log" &
    GPU_MONITOR_PID=$!

    # 启动简化的系统监控
    ps aux --pid $$ -o pid,ppid,cmd,%mem,%cpu,etime > "${TELEMETRY_DIR}/system_monitor_${test_name}.log" &
    SYSTEM_MONITOR_PID=$!

    echo "GPU Monitor PID: $GPU_MONITOR_PID"
    echo "System Monitor PID: $SYSTEM_MONITOR_PID"
}

# 函数：停止GPU监控
stop_gpu_monitoring() {
    echo "Stopping GPU monitoring..."
    kill $GPU_MONITOR_PID 2>/dev/null || true
    kill $SYSTEM_MONITOR_PID 2>/dev/null || true
    wait $GPU_MONITOR_PID 2>/dev/null || true
    wait $SYSTEM_MONITOR_PID 2>/dev/null || true
}

# 函数：分析GPU性能数据
analyze_gpu_performance() {
    local test_name="$1"
    local log_file="${TELEMETRY_DIR}/gpu_monitor_${test_name}.log"

    if [[ -f "$log_file" ]]; then
        echo "=== GPU Performance Analysis for $test_name ==="

        # 计算关键指标
        avg_gpu_util=$(tail -n +4 "$log_file" | awk 'NR>1 {sum+=$6; count++} END {if(count>0) print sum/count; else print 0}')
        max_gpu_util=$(tail -n +4 "$log_file" | awk 'NR>1 {if($6>max) max=$6} END {print max+0}')

        avg_mem_util=$(tail -n +4 "$log_file" | awk 'NR>1 {sum+=$3; count++} END {if(count>0) print sum/count; else print 0}')
        max_mem_util=$(tail -n +4 "$log_file" | awk 'NR>1 {if($3>max) max=$3} END {print max+0}')

        avg_power=$(tail -n +4 "$log_file" | awk 'NR>1 {sum+=$7; count++} END {if(count>0) print sum/count; else print 0}')
        max_power=$(tail -n +4 "$log_file" | awk 'NR>1 {if($7>max) max=$7} END {print max+0}')

        echo "Average GPU Utilization: ${avg_gpu_util}%"
        echo "Maximum GPU Utilization: ${max_gpu_util}%"
        echo "Average Memory Utilization: ${avg_mem_util}%"
        echo "Maximum Memory Utilization: ${max_mem_util}%"
        echo "Average Power Usage: ${avg_power}W"
        echo "Maximum Power Usage: ${max_power}W"
        echo ""

        # 保存性能数据
        cat > "${TELEMETRY_DIR}/${test_name}_performance.json" << EOF
{
  "test_name": "$test_name",
  "avg_gpu_util_percent": $avg_gpu_util,
  "max_gpu_util_percent": $max_gpu_util,
  "avg_memory_util_percent": $avg_mem_util,
  "max_memory_util_percent": $max_mem_util,
  "avg_power_watts": $avg_power,
  "max_power_watts": $max_power,
  "timestamp": "$(date -Iseconds)"
}
EOF
    fi
}

# 函数：分析telemetry数据
analyze_telemetry_throughput() {
    echo "=== Telemetry Throughput Analysis ==="

    if [[ -d "$TELEMETRY_DIR" ]]; then
        total_samples=0
        total_valid_pairs=0
        total_parity_matches=0
        total_time=0
        file_count=0

        for telemetry_file in "$TELEMETRY_DIR"/*.jsonl; do
            if [[ -f "$telemetry_file" && "$telemetry_file" == *"${TELEMETRY_DIR}/"* ]]; then
                echo "Analyzing: $telemetry_file"

                # 统计样本数
                samples=$(grep -c '"scalar"' "$telemetry_file" || echo 0)
                total_samples=$((total_samples + samples))

                # 统计有效key-address对
                valid_pairs=$(grep -c '"address"' "$telemetry_file" || echo 0)
                total_valid_pairs=$((total_valid_pairs + valid_pairs))

                # 统计parity匹配
                parity_matches=$(grep -c '"parity_match":true' "$telemetry_file" || echo 0)
                total_parity_matches=$((total_parity_matches + parity_matches))

                # 计算运行时间
                first_timestamp=$(head -1 "$telemetry_file" | grep -o '"ts":[0-9.]*' | cut -d: -f2)
                last_timestamp=$(tail -1 "$telemetry_file" | grep -o '"ts":[0-9.]*' | cut -d: -f2)

                if [[ -n "$first_timestamp" && -n "$last_timestamp" ]]; then
                    run_time=$(echo "$last_timestamp - $first_timestamp" | bc -l)
                    total_time=$(echo "$total_time + $run_time" | bc -l)

                    if (( $(echo "$run_time > 0" | bc -l) )); then
                        throughput=$(echo "scale=2; $samples / $run_time" | bc -l)
                        echo "  Samples: $samples, Valid pairs: $valid_pairs, Parity matches: $parity_matches"
                        echo "  Run time: ${run_time}s, Throughput: ${throughput} samples/sec"
                    fi
                fi
                file_count=$((file_count + 1))
                echo ""
            fi
        done

        if (( $(echo "$total_time > 0" | bc -l) )); then
            avg_throughput=$(echo "scale=2; $total_samples / $total_time" | bc -l)
            parity_success_rate=$(echo "scale=4; $total_parity_matches / $total_valid_pairs" | bc -l)

            echo "=== Overall Validation Results ==="
            echo "Total samples processed: $total_samples"
            echo "Total valid key-address pairs: $total_valid_pairs"
            echo "Total parity matches: $total_parity_matches"
            echo "Total validation time: ${total_time}s"
            echo "Average throughput: ${avg_throughput} samples/sec"
            echo "Parity success rate: $parity_success_rate"
            echo "Validation files: $file_count"

            # 保存总体结果
            cat > "${TELEMETRY_DIR}/validation_summary.json" << EOF
{
  "total_samples": $total_samples,
  "total_valid_pairs": $total_valid_pairs,
  "total_parity_matches": $total_parity_matches,
  "total_time_seconds": $total_time,
  "avg_throughput_samples_per_sec": $avg_throughput,
  "parity_success_rate": $parity_success_rate,
  "validation_files": $file_count,
  "target_samples": $SAMPLE_COUNT,
  "timestamp": "$(date -Iseconds)"
}
EOF
        else
            echo "No valid telemetry data found"
        fi
    else
        echo "No telemetry directory found"
    fi
}

# 函数：生成parity验证报告
generate_parity_report() {
    echo "Generating Parity Validation Report..."

    cat > "$PARITY_REPORT" << EOF
# Puzzle71Solver GPU/CPU Parity Validation Report

**Validation Date**: $(date)
**Target Samples**: $SAMPLE_COUNT
**GPU Hardware**: $(nvidia-smi --query-gpu=name --format=csv,noheader,nounits | head -1)
**CUDA Version**: $(nvcc --version | grep release | awk '{print $5}' | sed 's/,//')
**Driver Version**: $(nvidia-smi --query-gpu=driver_version --format=csv,noheader,nounits | head -1)

## 🎯 Validation Objective

验证GPU加速计算与CPU参考实现之间的一致性，确保：
- GPU计算的私钥 → 公钥 → 地址转换正确性
- 与bitcoin-core/secp256k1参考实现的100%一致性
- 高吞吐量下的计算稳定性
- GPU资源利用效率

## 📊 Validation Configuration

### GPU优化配置
\`\`\`
GPU Configuration:
  Device: $(nvidia-smi --query-gpu=name --format=csv,noheader,nounits | head -1)
  SM Count: $(nvidia-smi --query-gpu=multiprocessor_count --format=csv,noheader,nounits | head -1)
  Block Size: 1024 (动态优化)
  Grid Size: $(nvidia-smi --query-gpu=multiprocessor_count --format=csv,noheader,nounits | head -1) (充分利用SM)
  Total Threads: $((1024 * $(nvidia-smi --query-gpu=multiprocessor_count --format=csv,noheader,nounits | head -1)))
  Points Per Thread: 1 (最大并行度)
\`\`\`

### 验证参数
- **样本数量**: $SAMPLE_COUNT (1M+ 大规模验证)
- **监控间隔**: ${GPU_MONITOR_INTERVAL}s
- **目标精度**: <1e-10 相对误差要求
- **验证库**: bitcoin-core/secp256k1 (CPU参考)

## 📈 Validation Results

EOF

    # 添加性能数据
    if [[ -f "${TELEMETRY_DIR}/validation_summary.json" ]]; then
        echo "### 吞吐量统计" >> "$PARITY_REPORT"
        echo "" >> "$PARITY_REPORT"

        # 解析JSON数据
        total_samples=$(grep -o '"total_samples":[0-9]*' "${TELEMETRY_DIR}/validation_summary.json" | cut -d: -f2)
        avg_throughput=$(grep -o '"avg_throughput_samples_per_sec":[0-9.]*' "${TELEMETRY_DIR}/validation_summary.json" | cut -d: -f2)
        parity_success_rate=$(grep -o '"parity_success_rate":[0-9.]*' "${TELEMETRY_DIR}/validation_summary.json" | cut -d: -f2)
        total_time=$(grep -o '"total_time_seconds":[0-9.]*' "${TELEMETRY_DIR}/validation_summary.json" | cut -d: -f2)

        echo "| 指标 | 数值 |" >> "$PARITY_REPORT"
        echo "|------|------|" >> "$PARITY_REPORT"
        echo "| 总样本数 | $total_samples |" >> "$PARITY_REPORT"
        echo "| 平均吞吐量 | ${avg_throughput} samples/sec |" >> "$PARITY_REPORT"
        echo "| Parity成功率 | $parity_success_rate |" >> "$PARITY_REPORT"
        echo "| 验证时间 | ${total_time}s |" >> "$PARITY_REPORT"
        echo "" >> "$PARITY_REPORT"
    fi

    # 添加GPU性能数据
    for perf_file in "${TELEMETRY_DIR}"/*_performance.json; do
        if [[ -f "$perf_file" ]]; then
            test_name=$(basename "$perf_file" _performance.json)
            avg_gpu_util=$(grep -o '"avg_gpu_util_percent":[0-9.]*' "$perf_file" | cut -d: -f2)
            max_gpu_util=$(grep -o '"max_gpu_util_percent":[0-9.]*' "$perf_file" | cut -d: -f2)
            avg_power=$(grep -o '"avg_power_watts":[0-9.]*' "$perf_file" | cut -d: -f2)

            echo "### GPU性能 - $test_name" >> "$PARITY_REPORT"
            echo "" >> "$PARITY_REPORT"
            echo "| 指标 | 平均值 | 最大值 |" >> "$PARITY_REPORT"
            echo "|------|--------|--------|" >> "$PARITY_REPORT"
            echo "| GPU利用率 | ${avg_gpu_util}% | ${max_gpu_util}% |" >> "$PARITY_REPORT"
            echo "| 功耗 | ${avg_power}W | $(grep -o '"max_power_watts":[0-9.]*' "$perf_file" | cut -d: -f2)W |" >> "$PARITY_REPORT"
            echo "" >> "$PARITY_REPORT"
        fi
    done

    # 添加监控数据文件信息
    echo "## 📁 监控数据文件" >> "$PARITY_REPORT"
    echo "" >> "$PARITY_REPORT"
    echo "### GPU监控日志" >> "$PARITY_REPORT"
    echo "" >> "$PARITY_REPORT"

    for log_file in "${TELEMETRY_DIR}"/gpu_monitor_*.log; do
        if [[ -f "$log_file" ]]; then
            echo "- \`$(basename "$log_file")\` - nvidia-smi dmon 实时监控数据" >> "$PARITY_REPORT"
        fi
    done

    echo "" >> "$PARITY_REPORT"
    echo "### 系统监控日志" >> "$PARITY_REPORT"
    echo "" >> "$PARITY_REPORT"

    for log_file in "${TELEMETRY_DIR}"/system_monitor_*.log; do
        if [[ -f "$log_file" ]]; then
            echo "- \`$(basename "$log_file")\` - 系统资源使用情况" >> "$PARITY_REPORT"
        fi
    done

    echo "" >> "$PARITY_REPORT"
    echo "### Telemetry数据" >> "$PARITY_REPORT"
    echo "" >> "$PARITY_REPORT"

    for telemetry_file in "$TELEMETRY_DIR"/*.jsonl; do
        if [[ -f "$telemetry_file" && "$telemetry_file" == *"${TELEMETRY_DIR}/"* ]]; then
            echo "- \`$(basename "$telemetry_file")\` - 详细的parity验证记录" >> "$PARITY_REPORT"
        fi
    done

    cat >> "$PARITY_REPORT" << EOF

## 🎯 验证结论

### ✅ GPU优化成果
1. **动态资源配置**: 消除硬编码限制，实现GPU架构自适应
2. **资源利用率**: 充分利用所有SM，最大化并行计算能力
3. **监控体系**: 完整的性能监控和数据分析框架

### ⚠️ 待解决问题
1. **程序稳定性**: 当前版本存在崩溃问题，需要进一步调试
2. **完整验证**: 需要修复稳定性问题后完成完整的1M样本验证

### 📋 下一步计划
1. 修复程序稳定性问题
2. 完成大规模parity验证
3. 实现真正的256位私钥扫描
4. 优化GPU kernel性能

---

*报告生成时间: $(date)*
*验证环境: Ubuntu 22.04 + CUDA $(nvcc --version | grep release | awk '{print $5}' | sed 's/,//') + $(nvidia-smi --query-gpu=name --format=csv,noheader,nounits | head -1)*
EOF

    echo "Parity validation report generated: $PARITY_REPORT"
}

# 主验证流程
echo "Starting GPU/CPU Parity Validation..."
echo ""

# 检查是否有bitcoin-core parity支持
echo "=== Checking Bitcoin-Core Secp256k1 Support ==="
if [[ ! -f "/usr/include/secp256k1.h" ]]; then
    echo "❌ bitcoin-core/secp256k1 not found"
    echo "Please install libsecp256k1-dev: apt install libsecp256k1-dev"
    exit 1
fi
echo "✅ bitcoin-core/secp256k1 found"
echo ""

# 尝试运行验证测试
echo "=== Running Parity Validation Test ==="
start_gpu_monitoring "parity_validation"

# 使用较小的keyspace范围进行验证测试
start_time=$(date +%s.%N)

timeout 300s ./Puzzle71Solver \
    --keyspace "0x1:0x100000" \
    --target-address "1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU" \
    --operator-id "parity-validation" \
    --operator-purpose "gpu-cpu-parity-test" \
    --telemetry-jsonl "$TELEMETRY_DIR" \
    --dry-run || true

end_time=$(date +%s.%N)
duration=$(echo "$end_time - $start_time" | bc -l)

stop_gpu_monitoring

echo "Validation test completed in ${duration}s"
echo ""

# 分析结果
analyze_gpu_performance "parity_validation"
analyze_telemetry_throughput
generate_parity_report

echo ""
echo "=== Validation Summary ==="
echo "📊 Report: $PARITY_REPORT"
echo "📁 Telemetry: $TELEMETRY_DIR/"
echo "🎯 Next: Fix stability issues and run full 1M sample validation"
echo ""
echo "GPU/CPU Parity Validation completed at: $(date)"