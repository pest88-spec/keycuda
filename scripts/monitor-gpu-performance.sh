#!/bin/bash

# GPU Performance Monitoring Script for Puzzle71Solver
# Monitors GPU utilization, memory usage, and performance metrics

# Configuration
MONITOR_INTERVAL=0.5  # seconds
LOG_FILE="gpu_performance_monitor.log"
OUTPUT_CSV="gpu_metrics.csv"

# Initialize CSV output
echo "timestamp,gpu_utilization,memory_used_mb,memory_total_mb,memory_utilization,temperature,power_draw_w,keys_per_sec" > $OUTPUT_CSV

# Function to get GPU metrics
get_gpu_metrics() {
    local nvidia_output=$(nvidia-smi --query-gpu=utilization.gpu,memory.used,memory.total,temperature.gpu,power.draw --format=csv,noheader,nounits)
    echo "$nvidia_output"
}

# Function to get keys/sec from latest benchmark
get_keys_per_sec() {
    if [ -f "benchmarks/latest.json" ]; then
        local keys_per_sec=$(python3 -c "
import json
import sys
try:
    with open('benchmarks/latest.json', 'r') as f:
        data = json.load(f)
    if 'stats' in data and 'mean' in data['stats']:
        print(f\"{data['stats']['mean']:.2f}\")
    else:
        print('0')
except:
    print('0')
" 2>/dev/null)
        echo "$keys_per_sec"
    else
        echo "0"
    fi
}

# Start monitoring
echo "Starting GPU Performance Monitoring..."
echo "Timestamp, GPU Util%, Memory Used MB, Memory Total MB, Memory Util%, Temp C, Power W, Keys/sec" | tee $LOG_FILE

monitor_duration=${1:-60}  # Default 60 seconds
end_time=$(($(date +%s) + monitor_duration))

while [ $(date +%s) -lt $end_time ]; do
    timestamp=$(date '+%Y-%m-%d %H:%M:%S.%3N')

    # Get GPU metrics
    gpu_metrics=$(get_gpu_metrics)
    gpu_util=$(echo $gpu_metrics | cut -d',' -f1)
    memory_used=$(echo $gpu_metrics | cut -d',' -f2)
    memory_total=$(echo $gpu_metrics | cut -d',' -f3)
    temperature=$(echo $gpu_metrics | cut -d',' -f4)
    power_draw=$(echo $gpu_metrics | cut -d',' -f5)

    # Calculate memory utilization
    memory_util=$((memory_used * 100 / memory_total))

    # Get keys per sec
    keys_per_sec=$(get_keys_per_sec)

    # Log to file and console
    log_entry="$timestamp, $gpu_util%, $memory_used MB, $memory_total MB, $memory_util%, $temperature°C, ${power_draw}W, ${keys_per_sec} keys/s"
    echo "$log_entry" | tee -a $LOG_FILE

    # Append to CSV for analysis
    echo "$timestamp,$gpu_util,$memory_used,$memory_total,$memory_util,$temperature,$power_draw,$keys_per_sec" >> $OUTPUT_CSV

    # Alert if GPU utilization is low
    if [ "$gpu_util" -lt 50 ] && [ "$memory_used" -gt 100 ]; then
        echo "[WARNING] Low GPU utilization ($gpu_util%) with significant memory usage ($memory_used MB)" | tee -a $LOG_FILE
    fi

    # Alert if memory usage is too high
    if [ "$memory_util" -gt 90 ]; then
        echo "[WARNING] High memory utilization ($memory_util%)" | tee -a $LOG_FILE
    fi

    sleep $MONITOR_INTERVAL
done

echo "Monitoring completed. Results saved to $LOG_FILE and $OUTPUT_CSV"

# Generate summary report
echo ""
echo "=== Performance Summary ==="
echo "Average GPU Utilization: $(awk -F',' '{sum+=$2; count++} END {if(count>0) printf "%.1f%%", sum/count}' $OUTPUT_CSV)"
echo "Peak Memory Usage: $(awk -F',' '{if($3>max) max=$3} END {printf "%d MB", max}' $OUTPUT_CSV)"
echo "Average Memory Utilization: $(awk -F',' '{sum+=$5; count++} END {if(count>0) printf "%.1f%%", sum/count}' $OUTPUT_CSV)"