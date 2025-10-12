#!/bin/bash

# Puzzle71Solver Stability Test Script
# Executes long-term stability testing for production validation
# Supports various test durations with comprehensive monitoring

set -euo pipefail

# Script configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
RESULTS_DIR="$PROJECT_ROOT/benchmarks/stability"
LOG_FILE="$RESULTS_DIR/stability_test_$(date +%Y%m%d_%H%M%S).log"

# Default test duration (24 hours)
DEFAULT_DURATION="24h"
DURATION="${1:-$DEFAULT_DURATION}"

# GPU monitoring interval (seconds)
MONITOR_INTERVAL=30

# Performance degradation threshold (percentage)
DEGRADATION_THRESHOLD=5.0

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Logging function
log() {
    echo -e "${2:-}[$(date '+%Y-%m-%d %H:%M:%S')] $1${NC}" | tee -a "$LOG_FILE"
}

# Print usage information
usage() {
    cat << EOF
Puzzle71Solver Stability Test Script

Usage: $0 [DURATION]

Arguments:
    DURATION    Test duration (default: 24h)
                Formats: 1h, 6h, 12h, 24h, 48h

Examples:
    $0          # Run 24-hour stability test
    $0 6h       # Run 6-hour stability test
    $0 48h      # Run 48-hour stability test

Environment Variables:
    GPU_DEVICE      GPU device ID (default: auto-detect)
    CHECKPOINT_DIR  Checkpoint directory (default: auto-create)
    VERBOSE         Enable verbose output (default: false)

Requirements:
    - CUDA Toolkit 11.8+
    - NVIDIA GPU with sufficient memory
    - Built Puzzle71Solver executable
    - Sufficient disk space for logs and checkpoints
EOF
}

# Parse duration into seconds
parse_duration() {
    local duration="$1"
    local number="${duration%[hH]}"
    local unit="${duration: -1}"

    case "$unit" in
        h|H) echo $((number * 3600)) ;;
        *)
            log "${RED}Error: Invalid duration format '$duration'. Use formats like 1h, 6h, 24h${NC}"
            exit 1
            ;;
    esac
}

# Check system requirements
check_requirements() {
    log "${BLUE}Checking system requirements...${NC}"

    # Check CUDA availability
    if ! command -v nvidia-smi &> /dev/null; then
        log "${RED}Error: nvidia-smi not found. Is NVIDIA driver installed?${NC}"
        exit 1
    fi

    # Check GPU availability
    local gpu_count
    gpu_count=$(nvidia-smi --list-gpus | wc -l)
    if [[ $gpu_count -eq 0 ]]; then
        log "${RED}Error: No NVIDIA GPUs detected${NC}"
        exit 1
    fi

    log "${GREEN}✓ Found $gpu_count NVIDIA GPU(s)${NC}"

    # Check Puzzle71Solver executable
    if [[ ! -f "$BUILD_DIR/Puzzle71Solver" ]]; then
        log "${RED}Error: Puzzle71Solver not found at $BUILD_DIR/Puzzle71Solver${NC}"
        log "${YELLOW}Build the project first: cd build && make -j\$(nproc)${NC}"
        exit 1
    fi

    log "${GREEN}✓ Puzzle71Solver executable found${NC}"

    # Check available disk space (need at least 5GB)
    local available_space
    available_space=$(df "$PROJECT_ROOT" | awk 'NR==2 {print $4}')
    local required_space=$((5 * 1024 * 1024)) # 5GB in KB

    if [[ $available_space -lt $required_space ]]; then
        log "${RED}Error: Insufficient disk space. Need at least 5GB for stability test${NC}"
        exit 1
    fi

    log "${GREEN}✓ Sufficient disk space available${NC}"
}

# Detect optimal GPU device
detect_gpu_device() {
    local gpu_device="${GPU_DEVICE:-}"

    if [[ -n "$gpu_device" ]]; then
        log "${BLUE}Using specified GPU device: $gpu_device${NC}"
        echo "$gpu_device"
        return
    fi

    # Select GPU with most memory
    local best_gpu
    best_gpu=$(nvidia-smi --query-gpu=index,memory.total --format=csv,noheader,nounits |
               sort -k2 -nr | head -1 | awk '{print $1}')

    log "${GREEN}✓ Auto-detected optimal GPU device: $best_gpu${NC}"
    echo "$best_gpu"
}

# Create test configuration
create_test_config() {
    local gpu_device="$1"
    local test_dir="$RESULTS_DIR/$(date +%Y%m%d_%H%M%S)"

    mkdir -p "$test_dir"

    # Create checkpoint directory
    local checkpoint_dir="${CHECKPOINT_DIR:-$test_dir/checkpoints}"
    mkdir -p "$checkpoint_dir"

    # Create test configuration file
    cat > "$test_dir/stability_config.json" << EOF
{
    "testStartTime": "$(date -Iseconds)",
    "duration": "$DURATION",
    "gpuDevice": $gpu_device,
    "checkpointDirectory": "$checkpoint_dir",
    "monitoringInterval": $MONITOR_INTERVAL,
    "degradationThreshold": $DEGRADATION_THRESHOLD,
    "puzzle71Config": {
        "keyspace": "0x20000000000000000:0x20000000000001000",
        "targetAddress": "1BY8GQbnueYofwSuFAT3USAhGjPrkxDdW9",
        "operatorId": "stability-test-gpu$gpu_device",
        "operatorPurpose": "24-hour stability validation test"
    }
}
EOF

    echo "$test_dir"
}

# Run initial validation test
run_initial_validation() {
    local gpu_device="$1"
    local test_dir="$2"

    log "${BLUE}Running initial validation test...${NC}"

    # Use Puzzle 40 for quick validation (known answer)
    local validation_start=$(date +%s)

    "$BUILD_DIR/Puzzle71Solver" \
        --keyspace 0xe9ae490000:0xe9ae494000 \
        --target-address 1EeAxcprB2PpCnr34VfZdFrkUWuxyiNEFv \
        --operator-id "stability-validation" \
        --operator-purpose "Pre-test validation" \
        --device "$gpu_device" \
        --super \
        > "$test_dir/validation_output.txt" 2>&1 || {
        log "${RED}Error: Initial validation test failed${NC}"
        log "${RED}Check $test_dir/validation_output.txt for details${NC}"
        exit 1
    }

    local validation_end=$(date +%s)
    local validation_duration=$((validation_end - validation_start))

    # Check if validation found the expected result
    if grep -q "Found match" "$test_dir/validation_output.txt"; then
        log "${GREEN}✓ Initial validation passed (${validation_duration}s)${NC}"
    else
        log "${RED}Error: Initial validation did not find expected match${NC}"
        exit 1
    fi
}

# Start stability test
start_stability_test() {
    local gpu_device="$1"
    local test_dir="$2"
    local checkpoint_dir="$3"

    log "${BLUE}Starting stability test for $DURATION on GPU $gpu_device...${NC}"

    # Start Puzzle71Solver in background
    nohup "$BUILD_DIR/Puzzle71Solver" \
        --keyspace 0x20000000000000000:0x20000000000001000 \
        --target-address 1BY8GQbnueYofwSuFAT3USAhGjPrkxDdW9 \
        --operator-id "stability-test-gpu$gpu_device" \
        --operator-purpose "24-hour stability validation test" \
        --device "$gpu_device" \
        --enable-checkpoint \
        > "$test_dir/solver_output.txt" 2>&1 &

    local solver_pid=$!
    echo "$solver_pid" > "$test_dir/solver.pid"

    log "${GREEN}✓ Stability test started (PID: $solver_pid)${NC}"
    echo "$solver_pid"
}

# Monitor system resources
monitor_system() {
    local gpu_device="$1"
    local test_dir="$2"
    local duration_seconds="$3"
    local solver_pid="$4"

    log "${BLUE}Starting system monitoring for ${DURATION}...${NC}"

    local start_time=$(date +%s)
    local end_time=$((start_time + duration_seconds))
    local sample_count=0

    # Create monitoring data file
    local monitor_file="$test_dir/system_monitor.csv"
    cat > "$monitor_file" << EOF
timestamp,epoch_time,gpu_temp,gpu_util,gpu_power,gpu_memory_used,gpu_memory_total,cpu_load,system_memory,throughput_estimate
EOF

    while [[ $(date +%s) -lt $end_time ]]; do
        local current_time=$(date +%s)
        local timestamp=$(date -Iseconds)

        # Check if solver process is still running
        if ! kill -0 "$solver_pid" 2>/dev/null; then
            log "${RED}Error: Solver process (PID: $solver_pid) has stopped unexpectedly${NC}"
            return 1
        fi

        # Collect GPU metrics
        local gpu_info
        gpu_info=$(nvidia-smi --query-gpu=temperature.gpu,utilization.gpu,power.draw,memory.used,memory.total \
                              --format=csv,noheader,nounits --id="$gpu_device")

        local gpu_temp=$(echo "$gpu_info" | awk -F',' '{print $1}')
        local gpu_util=$(echo "$gpu_info" | awk -F',' '{print $2}')
        local gpu_power=$(echo "$gpu_info" | awk -F',' '{print $3}')
        local gpu_mem_used=$(echo "$gpu_info" | awk -F',' '{print $4}')
        local gpu_mem_total=$(echo "$gpu_info" | awk -F',' '{print $5}')

        # Collect system metrics
        local cpu_load=$(uptime | awk -F'load average:' '{print $2}' | awk '{print $1}' | tr -d ',')
        local system_memory=$(free -m | awk 'NR==2{printf "%.1f", $3*100/$2}')

        # Estimate throughput from solver output (simplified)
        local throughput="N/A"
        if [[ -f "$test_dir/solver_output.txt" ]]; then
            throughput=$(tail -100 "$test_dir/solver_output.txt" | grep -o '[0-9.]\+ Mkeys/s' | tail -1 | cut -d' ' -f1 || echo "N/A")
        fi

        # Write monitoring data
        echo "$timestamp,$current_time,$gpu_temp,$gpu_util,$gpu_power,$gpu_mem_used,$gpu_mem_total,$cpu_load,$system_memory,$throughput" >> "$monitor_file"

        sample_count=$((sample_count + 1))

        # Check for alerts
        if [[ ${gpu_temp%.*} -gt 85 ]]; then
            log "${YELLOW}⚠️ High GPU temperature: ${gpu_temp}°C${NC}"
        fi

        if [[ ${gpu_util%.*} -lt 50 ]]; then
            log "${YELLOW}⚠️ Low GPU utilization: ${gpu_util}%${NC}"
        fi

        # Progress update every 10 samples (5 minutes)
        if [[ $((sample_count % 10)) -eq 0 ]]; then
            local elapsed=$((current_time - start_time))
            local remaining=$((end_time - current_time))
            local progress=$((elapsed * 100 / duration_seconds))

            log "${BLUE}Progress: ${progress}% (${elapsed}s elapsed, ${remaining}s remaining)${NC}"
            log "${BLUE}  GPU: ${gpu_temp}°C, ${gpu_util}% util, ${throughput} throughput${NC}"
        fi

        sleep $MONITOR_INTERVAL
    done

    log "${GREEN}✓ Monitoring completed ($sample_count samples collected)${NC}"
    return 0
}

# Analyze stability test results
analyze_results() {
    local test_dir="$1"
    local monitor_file="$test_dir/system_monitor.csv"

    log "${BLUE}Analyzing stability test results...${NC}"

    # Create analysis script
    python3 << EOF
import json
import pandas as pd
import numpy as np
from datetime import datetime

# Read monitoring data
df = pd.read_csv('$monitor_file')

if len(df) == 0:
    print("ERROR: No monitoring data collected")
    exit(1)

# Calculate statistics
results = {
    "analysisTime": datetime.now().isoformat(),
    "testDuration": {
        "samples": len(df),
        "durationHours": len(df) * $MONITOR_INTERVAL / 3600
    },
    "gpuMetrics": {
        "temperature": {
            "mean": float(df['gpu_temp'].mean()),
            "max": float(df['gpu_temp'].max()),
            "min": float(df['gpu_temp'].min())
        },
        "utilization": {
            "mean": float(df['gpu_util'].mean()),
            "max": float(df['gpu_util'].max()),
            "min": float(df['gpu_util'].min())
        },
        "power": {
            "mean": float(df['gpu_power'].mean()),
            "max": float(df['gpu_power'].max()),
            "min": float(df['gpu_power'].min())
        },
        "memory": {
            "usedMean": float(df['gpu_memory_used'].mean()),
            "usedMax": float(df['gpu_memory_used'].max()),
            "total": int(df['gpu_memory_total'].iloc[0])
        }
    },
    "systemMetrics": {
        "cpuLoad": {
            "mean": float(df['cpu_load'].mean()),
            "max": float(df['cpu_load'].max())
        },
        "systemMemory": {
            "mean": float(df['system_memory'].mean()),
            "max": float(df['system_memory'].max())
        }
    }
}

# Performance degradation analysis
throughput_values = df['throughput_estimate'][df['throughput_estimate'] != 'N/A'].astype(float)
if len(throughput_values) > 10:
    # Split into first and last quarters
    split_point = len(throughput_values) // 4
    first_quarter = throughput_values[:split_point]
    last_quarter = throughput_values[-split_point:]

    if len(first_quarter) > 0 and len(last_quarter) > 0:
        first_mean = first_quarter.mean()
        last_mean = last_quarter.mean()
        degradation = ((first_mean - last_mean) / first_mean) * 100

        results["performanceDegradation"] = {
            "firstQuarterMean": float(first_mean),
            "lastQuarterMean": float(last_mean),
            "degradationPercent": float(degradation),
            "threshold": $DEGRADATION_THRESHOLD,
            "passed": degradation <= $DEGRADATION_THRESHOLD
        }

# Check for anomalies
results["anomalies"] = {
    "highTemperatureEvents": int((df['gpu_temp'] > 85).sum()),
    "lowUtilizationEvents": int((df['gpu_util'] < 50).sum()),
    "dataGaps": int((df['epoch_time'].diff() > $MONITOR_INTERVAL * 2).sum())
}

# Overall assessment
results["overallStatus"] = "PASSED"
failure_reasons = []

if results.get("performanceDegradation", {}).get("passed", True) == False:
    failure_reasons.append("Performance degradation exceeded threshold")
    results["overallStatus"] = "FAILED"

if results["anomalies"]["highTemperatureEvents"] > len(df) * 0.01:  # >1% of samples
    failure_reasons.append("Excessive high temperature events")
    results["overallStatus"] = "FAILED"

if results["anomalies"]["lowUtilizationEvents"] > len(df) * 0.05:  # >5% of samples
    failure_reasons.append("Excessive low utilization events")
    results["overallStatus"] = "FAILED"

if results["gpuMetrics"]["temperature"]["max"] > 90:
    failure_reasons.append("Maximum temperature exceeded 90°C")
    results["overallStatus"] = "FAILED"

results["failureReasons"] = failure_reasons

# Save analysis results
with open('$test_dir/stability_analysis.json', 'w') as f:
    json.dump(results, f, indent=2)

print(f"Analysis completed: {results['overallStatus']}")
if failure_reasons:
    print(f"Failure reasons: {', '.join(failure_reasons)}")
EOF

    # Check analysis results
    local analysis_file="$test_dir/stability_analysis.json"
    if [[ -f "$analysis_file" ]]; then
        local overall_status
        overall_status=$(python3 -c "
import json
with open('$analysis_file', 'r') as f:
    data = json.load(f)
print(data['overallStatus'])
")

        if [[ "$overall_status" == "PASSED" ]]; then
            log "${GREEN}✅ Stability test PASSED${NC}"

            # Show key metrics
            python3 << EOF
import json
with open('$analysis_file', 'r') as f:
    data = json.load(f)

print(f"Test Duration: {data['testDuration']['durationHours']:.1f} hours ({data['testDuration']['samples']} samples)")
print(f"GPU Temperature: {data['gpuMetrics']['temperature']['mean']:.1f}°C avg (max: {data['gpuMetrics']['temperature']['max']:.1f}°C)")
print(f"GPU Utilization: {data['gpuMetrics']['utilization']['mean']:.1f}% avg (min: {data['gpuMetrics']['utilization']['min']:.1f}%)")
print(f"System Memory: {data['systemMetrics']['systemMemory']['mean']:.1f}% avg")

if 'performanceDegradation' in data:
    deg = data['performanceDegradation']
    print(f"Performance Degradation: {deg['degradationPercent']:.2f}% (threshold: {deg['threshold']}%)")

print(f"Anomalies: {data['anomalies']['highTemperatureEvents']} high temp, {data['anomalies']['lowUtilizationEvents']} low util events")
EOF
        else
            log "${RED}❌ Stability test FAILED${NC}"

            # Show failure reasons
            python3 << EOF
import json
with open('$analysis_file', 'r') as f:
    data = json.load(f)

print(f"Failure Reasons: {', '.join(data['failureReasons'])}")
print(f"GPU Temperature: {data['gpuMetrics']['temperature']['mean']:.1f}°C avg (max: {data['gpuMetrics']['temperature']['max']:.1f}°C)")
print(f"GPU Utilization: {data['gpuMetrics']['utilization']['mean']:.1f}% avg (min: {data['gpuMetrics']['utilization']['min']:.1f}%)")
EOF
            return 1
        fi
    else
        log "${RED}Error: Analysis results not found${NC}"
        return 1
    fi

    return 0
}

# Cleanup function
cleanup() {
    local test_dir="$1"
    local solver_pid="$2"

    log "${BLUE}Cleaning up...${NC}"

    # Stop solver process
    if kill -0 "$solver_pid" 2>/dev/null; then
        log "${BLUE}Stopping solver process (PID: $solver_pid)...${NC}"
        kill "$solver_pid"
        sleep 5

        # Force kill if still running
        if kill -0 "$solver_pid" 2>/dev/null; then
            kill -9 "$solver_pid"
        fi
    fi

    # Create summary report
    if [[ -f "$test_dir/stability_analysis.json" ]]; then
        cp "$test_dir/stability_analysis.json" "$RESULTS_DIR/latest_stability_test.json"
        log "${GREEN}✓ Summary report saved to $RESULTS_DIR/latest_stability_test.json${NC}"
    fi

    log "${GREEN}✓ Cleanup completed${NC}"
}

# Main execution
main() {
    local duration_seconds
    duration_seconds=$(parse_duration "$DURATION")

    log "${BLUE}Puzzle71Solver Stability Test${NC}"
    log "${BLUE}Duration: $DURATION (${duration_seconds} seconds)${NC}"
    log "${BLUE}Monitor Interval: ${MONITOR_INTERVAL} seconds${NC}"
    log "${BLUE}Degradation Threshold: ${DEGRADATION_THRESHOLD}%${NC}"

    # Setup
    mkdir -p "$RESULTS_DIR"
    check_requirements

    local gpu_device
    gpu_device=$(detect_gpu_device)

    local test_dir
    test_dir=$(create_test_config "$gpu_device")

    log "${GREEN}✓ Test directory: $test_dir${NC}"

    # Run initial validation
    run_initial_validation "$gpu_device" "$test_dir"

    # Start stability test
    local solver_pid
    solver_pid=$(start_stability_test "$gpu_device" "$test_dir" "$test_dir/checkpoints")

    # Trap for cleanup
    trap 'cleanup "$test_dir" "$solver_pid"' EXIT

    # Monitor system
    if monitor_system "$gpu_device" "$test_dir" "$duration_seconds" "$solver_pid"; then
        # Analyze results
        if analyze_results "$test_dir"; then
            log "${GREEN}🎉 Stability test completed successfully!${NC}"
            log "${GREEN}   Test results available in: $test_dir${NC}"
            exit 0
        else
            log "${RED}❌ Stability test failed${NC}"
            log "${RED}   Check analysis: $test_dir/stability_analysis.json${NC}"
            exit 1
        fi
    else
        log "${RED}❌ Monitoring failed${NC}"
        exit 1
    fi
}

# Parse command line arguments
case "${1:-}" in
    -h|--help)
        usage
        exit 0
        ;;
    *)
        main "$@"
        ;;
esac