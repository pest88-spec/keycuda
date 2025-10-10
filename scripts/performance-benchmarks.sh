#!/usr/bin/env bash
# T052: Performance Benchmarking Utilities
# Comprehensive performance benchmarking for dependency validation framework

set -euo pipefail

# Script configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BENCHMARK_RESULTS_DIR="$PROJECT_ROOT/results/benchmarks"
PERFORMANCE_BASELINE_DIR="$PROJECT_ROOT/baselines/performance"
BENCHMARK_CONFIG_DIR="$PROJECT_ROOT/.benchmark_config"
BENCHMARK_LOGS_DIR="$PROJECT_ROOT/logs/benchmarks"

# Import validation framework
VALIDATION_SCRIPT="$SCRIPT_DIR/validate-dependency-updates.sh"
if [[ -f "$VALIDATION_SCRIPT" ]]; then
    # Performance benchmarks should be independent - no circular sourcing
    true
fi

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Logging functions
log_benchmark() {
    local level="$1"
    local message="$2"
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    echo -e "${timestamp} [PERF_BENCHMARK] ${level} ${message}"
}

log_info() { log_benchmark "INFO" "$1"; }
log_success() { log_benchmark "SUCCESS" "$1"; }
log_warning() { log_benchmark "WARNING" "$1"; }
log_error() { log_benchmark "ERROR" "$1"; }
log_debug() { log_benchmark "DEBUG" "$1"; }

# Progress indicators
show_progress() {
    local current="$1"
    local total="$2"
    local desc="$3"
    local percent=$((current * 100 / total))
    local bar_length=40
    local filled_length=$((percent * bar_length / 100))
    local bar=""

    for ((i=0; i<filled_length; i++)); do bar+="█"; done
    for ((i=filled_length; i<bar_length; i++)); do bar+="░"; done

    printf "\r${BLUE}%s${NC} [%s] %d%% (%d/%d)" "$desc" "$bar" "$percent" "$current" "$total"
    if [[ $current -eq $total ]]; then echo; fi
}

# Initialize benchmarking framework
init_benchmark_framework() {
    log_info "Initializing performance benchmarking framework..."

    # Create directories
    mkdir -p "$BENCHMARK_RESULTS_DIR" "$PERFORMANCE_BASELINE_DIR" "$BENCHMARK_CONFIG_DIR" "$BENCHMARK_LOGS_DIR"

    # Create default benchmark configuration
    if [[ ! -f "$BENCHMARK_CONFIG_DIR/benchmark_config.json" ]]; then
        create_default_benchmark_config
    fi

    log_success "Benchmarking framework initialized"
}

# Create default benchmark configuration
create_default_benchmark_config() {
    cat > "$BENCHMARK_CONFIG_DIR/benchmark_config.json" << 'EOF'
{
  "benchmark_config_version": "1.0",
  "created_timestamp": "",
  "benchmark_types": {
    "throughput": {
      "description": "GPU throughput benchmarking",
      "default_duration": 300,
      "iterations": 3,
      "warmup_time": 30
    },
    "memory": {
      "description": "Memory usage benchmarking",
      "test_sizes": ["small", "medium", "large"],
      "monitor_interval": 5
    },
    "scalability": {
      "description": "Multi-GPU scalability benchmarking",
      "thread_counts": [1, 2, 4, 8],
      "duration_per_test": 120
    },
    "regression": {
      "description": "Performance regression testing",
      "baseline_comparison": true,
      "regression_threshold": 5.0
    }
  },
  "test_scenarios": {
    "small_range": {
      "start": "0000000000000000000000000000000000000000000000000000000000000000",
      "end": "00000000000000000000000000000000000000000000000000000000000000FF",
      "description": "Small range (256 keys) for quick testing"
    },
    "medium_range": {
      "start": "0000000000000000000000000000000000000000000000000000000000000000",
      "end": "000000000000000000000000000000000000000000000000000000000000FFFF",
      "description": "Medium range (65536 keys) for standard testing"
    },
    "large_range": {
      "start": "0000000000000000000000000000000000000000000000000000000000000000",
      "end": "00000000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF",
      "description": "Large range for comprehensive testing"
    }
  },
  "gpu_settings": {
    "default_device": 0,
    "devices_to_test": "auto",
    "memory_limit_mb": 8192
  },
  "output_settings": {
    "save_raw_data": true,
    "generate_charts": true,
    "export_format": ["json", "csv"],
    "chart_types": ["throughput", "memory", "scalability"]
  },
  "regression_settings": {
    "auto_baseline_update": false,
    "alert_on_regression": true,
    "min_samples_for_regression": 3
  }
}
EOF

    # Update timestamp
    jq --arg timestamp "$(date -u +"%Y-%m-%dT%H:%M:%SZ")" '.created_timestamp = $timestamp' "$BENCHMARK_CONFIG_DIR/benchmark_config.json" > "${BENCHMARK_CONFIG_DIR}/benchmark_config.json.tmp" && mv "${BENCHMARK_CONFIG_DIR}/benchmark_config.json.tmp" "$BENCHMARK_CONFIG_DIR/benchmark_config.json"
}

# Load benchmark configuration
load_benchmark_config() {
    if [[ ! -f "$BENCHMARK_CONFIG_DIR/benchmark_config.json" ]]; then
        log_error "Benchmark configuration file not found"
        return 1
    fi

    BENCHMARK_CONFIG=$(cat "$BENCHMARK_CONFIG_DIR/benchmark_config.json")
    log_debug "Benchmark configuration loaded"
}

# Run throughput benchmark
run_throughput_benchmark() {
    local benchmark_type="${1:-standard}"
    local duration="${2:-300}"
    local iterations="${3:-3}"
    local test_binary="$PROJECT_ROOT/build/Puzzle71Solver"

    log_info "Running throughput benchmark (type: $benchmark_type, duration: ${duration}s, iterations: $iterations)"

    if [[ ! -x "$test_binary" ]]; then
        log_error "Puzzle71Solver binary not found or not executable"
        return 1
    fi

    # Select test scenario based on benchmark type
    local test_scenario
    case "$benchmark_type" in
        "quick")
            test_scenario="small_range"
            duration=60
            ;;
        "standard")
            test_scenario="medium_range"
            ;;
        "comprehensive")
            test_scenario="large_range"
            duration=600
            ;;
        *)
            test_scenario="medium_range"
            ;;
    esac

    # Get test range from configuration
    local test_range_start
    local test_range_end
    test_range_start=$(echo "$BENCHMARK_CONFIG" | jq -r ".test_scenarios.${test_scenario}.start")
    test_range_end=$(echo "$BENCHMARK_CONFIG" | jq -r ".test_scenarios.${test_scenario}.end")

    local results_dir="$BENCHMARK_RESULTS_DIR/throughput_$(date +%Y%m%d_%H%M%S)"
    mkdir -p "$results_dir"

    local all_results=()
    local total_iterations=$iterations

    log_info "Starting $total_iterations throughput iterations..."

    for ((i=1; i<=iterations; i++)); do
        show_progress "$i" "$total_iterations" "Throughput benchmark"

        local iteration_log="$results_dir/iteration_${i}.log"
        local start_time=$(date +%s)
        local end_time
        local actual_duration

        # Run benchmark with timeout
        timeout "$duration" "$test_binary" \
            --device 0 \
            --range "${test_range_start}:${test_range_end}" \
            --benchmark \
            --threads 1 \
            --checkpoints 1000 \
            --verbose > "$iteration_log" 2>&1 || true

        end_time=$(date +%s)
        actual_duration=$((end_time - start_time))

        # Parse results
        local iteration_result
        if iteration_result=$(parse_benchmark_output "$iteration_log" "$actual_duration" "$i"); then
            all_results+=("$iteration_result")
        else
            log_warning "Failed to parse benchmark output for iteration $i"
        fi
    done

    # Generate throughput benchmark report
    local throughput_report="$results_dir/throughput_report.json"
    generate_throughput_report "$throughput_report" "$benchmark_type" "$test_scenario" "$all_results"

    # Calculate summary statistics
    local avg_throughput=$(printf '%s\n' "${all_results[@]}" | jq -r '.throughput' | awk '{sum+=$1; count++} END {if(count>0) print sum/count; else print 0}')
    local max_throughput=$(printf '%s\n' "${all_results[@]}" | jq -r '.throughput' | sort -n | tail -1 || echo "0")
    local min_throughput=$(printf '%s\n' "${all_results[@]}" | jq -r '.throughput' | sort -n | head -1 || echo "0")

    log_success "Throughput benchmark completed:"
    log_success "  Average throughput: ${avg_throughput} keys/sec"
    log_success "  Max throughput: ${max_throughput} keys/sec"
    log_success "  Min throughput: ${min_throughput} keys/sec"
    log_success "  Report saved to: $throughput_report"

    return 0
}

# Parse benchmark output
parse_benchmark_output() {
    local log_file="$1"
    local duration="$2"
    local iteration="$3"

    if [[ ! -f "$log_file" ]]; then
        return 1
    fi

    # Extract metrics from log
    local throughput=$(grep -o "throughput: [0-9.]*" "$log_file" | cut -d: -f2 | tail -1 || echo "0")
    local keys_total=$(grep -o "keys total: [0-9]*" "$log_file" | cut -d: -f2 | tail -1 || echo "0")
    local checkpoints_completed=$(grep -o "checkpoints completed: [0-9]*" "$log_file" | cut -d: -f2 | tail -1 || echo "0")
    local gpu_utilization=$(grep -o "GPU utilization: [0-9.]*%" "$log_file" | cut -d: -f2 | tail -1 || echo "0%")
    local memory_usage=$(grep -o "Memory usage: [0-9.]*MB" "$log_file" | cut -d: -f2 | tail -1 || echo "0MB")

    # Clean up values
    throughput=$(echo "$throughput" | sed 's/[^0-9.]//g')
    keys_total=$(echo "$keys_total" | sed 's/[^0-9]//g')
    checkpoints_completed=$(echo "$checkpoints_completed" | sed 's/[^0-9]//g')
    gpu_utilization=$(echo "$gpu_utilization" | sed 's/[^0-9.]//g')
    memory_usage=$(echo "$memory_usage" | sed 's/[^0-9.]//g')

    if [[ -z "$throughput" || "$throughput" == "0" ]]; then
        return 1
    fi

    jq -n \
        --argjson iteration "$iteration" \
        --argjson duration "$duration" \
        --argjson throughput "${throughput:-0}" \
        --argjson keys_total "${keys_total:-0}" \
        --argjson checkpoints_completed "${checkpoints_completed:-0}" \
        --argjson gpu_utilization "${gpu_utilization:-0}" \
        --argjson memory_usage "${memory_usage:-0}" \
        '{
            iteration: $iteration,
            duration: $duration,
            throughput: $throughput,
            keys_total: $keys_total,
            checkpoints_completed: $checkpoints_completed,
            gpu_utilization: $gpu_utilization,
            memory_usage: $memory_usage,
            timestamp: now
        }'
}

# Generate throughput report
generate_throughput_report() {
    local report_file="$1"
    local benchmark_type="$2"
    local test_scenario="$3"
    local results_array="$4"  # JSON array string

    # Convert results to proper JSON array
    local results_json
    results_json=$(printf '%s\n' "${results_array[@]}" | jq -s .)

    # Calculate statistics
    local stats
    stats=$(echo "$results_json" | jq '{
        count: length,
        avg_throughput: map(.throughput) | add / length,
        max_throughput: map(.throughput) | max,
        min_throughput: map(.throughput) | min,
        avg_duration: map(.duration) | add / length,
        total_keys: map(.keys_total) | add,
        avg_gpu_utilization: map(.gpu_utilization) | add / length,
        avg_memory_usage: map(.memory_usage) | add / length
    }')

    cat > "$report_file" << EOF
{
  "benchmark_type": "throughput",
  "benchmark_subtype": "$benchmark_type",
  "test_scenario": "$test_scenario",
  "timestamp": "$(date -u +"%Y-%m-%dT%H:%M:%SZ")",
  "statistics": $stats,
  "results": $results_json
}
EOF
}

# Run memory benchmark
run_memory_benchmark() {
    log_info "Running memory benchmark..."

    local test_binary="$PROJECT_ROOT/build/Puzzle71Solver"
    if [[ ! -x "$test_binary" ]]; then
        log_error "Puzzle71Solver binary not found"
        return 1
    fi

    local results_dir="$BENCHMARK_RESULTS_DIR/memory_$(date +%Y%m%d_%H%M%S)"
    mkdir -p "$results_dir"

    local test_sizes=("small" "medium" "large")
    local total_tests=${#test_sizes[@]}
    local test_num=0

    for size in "${test_sizes[@]}"; do
        ((test_num++))
        show_progress "$test_num" "$total_tests" "Memory benchmark ($size)"

        # Determine test duration based on size
        local test_duration
        case "$size" in
            "small") test_duration=60 ;;
            "medium") test_duration=180 ;;
            "large") test_duration=300 ;;
            *) test_duration=180 ;;
        esac

        # Get test range
        local test_range_start
        local test_range_end
        test_range_start=$(echo "$BENCHMARK_CONFIG" | jq -r ".test_scenarios.${size}_range.start // .test_scenarios.medium_range.start")
        test_range_end=$(echo "$BENCHMARK_CONFIG" | jq -r ".test_scenarios.${size}_range.end // .test_scenarios.medium_range.end")

        # Run memory benchmark with monitoring
        run_memory_test_with_monitoring "$test_binary" "$size" "$test_duration" "$test_range_start" "$test_range_end" "$results_dir"
    done

    # Generate memory benchmark report
    local memory_report="$results_dir/memory_report.json"
    generate_memory_report "$memory_report" "$results_dir"

    log_success "Memory benchmark completed"
    log_success "Report saved to: $memory_report"

    return 0
}

# Run memory test with monitoring
run_memory_test_with_monitoring() {
    local test_binary="$1"
    local size="$2"
    local duration="$3"
    local range_start="$4"
    local range_end="$5"
    local results_dir="$6"

    local monitor_log="$results_dir/memory_monitor_${size}.log"
    local test_log="$results_dir/memory_test_${size}.log"

    # Start memory monitoring in background
    (
        while true; do
            local timestamp=$(date +%s)
            local gpu_memory=$(nvidia-smi --query-gpu=memory.used,memory.total --format=csv,noheader,nounits 2>/dev/null | head -1)
            local system_memory=$(free -m | awk 'NR==2{printf "%.1f", $3*100/$2}')
            echo "${timestamp},${gpu_memory},${system_memory}%" >> "$monitor_log"
            sleep 5
        done
    ) &
    local monitor_pid=$!

    # Run the test
    timeout "$duration" "$test_binary" \
        --device 0 \
        --range "${range_start}:${range_end}" \
        --threads 1 \
        --checkpoints 1000 \
        > "$test_log" 2>&1 || true

    # Stop monitoring
    kill $monitor_pid 2>/dev/null || true
    wait $monitor_pid 2>/dev/null || true
}

# Generate memory report
generate_memory_report() {
    local report_file="$1"
    local results_dir="$2"

    local memory_results="[]"

    # Process memory monitor logs
    for monitor_log in "$results_dir"/memory_monitor_*.log; do
        if [[ -f "$monitor_log" ]]; then
            local size=$(basename "$monitor_log" .log | sed 's/memory_monitor_//')
            local memory_data
            memory_data=$(process_memory_log "$monitor_log" "$size")
            memory_results=$(echo "$memory_results" | jq --argjson data "$memory_data" '. += [$data]')
        fi
    done

    cat > "$report_file" << EOF
{
  "benchmark_type": "memory",
  "timestamp": "$(date -u +"%Y-%m-%dT%H:%M:%SZ")",
  "results": $memory_results
}
EOF
}

# Process memory log
process_memory_log() {
    local log_file="$1"
    local size="$2"

    if [[ ! -f "$log_file" ]]; then
        jq -n --arg size "$size" '{size: $size, error: "log file not found"}'
        return 1
    fi

    # Calculate memory statistics
    local avg_gpu_used
    local max_gpu_used
    local avg_system_usage
    local max_system_usage

    # Parse GPU memory usage
    avg_gpu_used=$(awk -F, 'NR>1{gsub(/[^0-9]/,"",$2); sum+=$2; count++} END{if(count>0) print sum/count; else print 0}' "$log_file")
    max_gpu_used=$(awk -F, 'NR>1{gsub(/[^0-9]/,"",$2); if($2>max) max=$2} END{print max+0}' "$log_file")

    # Parse system memory usage
    avg_system_usage=$(awk -F, 'NR>1{gsub(/[%]/,"",$3); sum+=$3; count++} END{if(count>0) print sum/count; else print 0}' "$log_file")
    max_system_usage=$(awk -F, 'NR>1{gsub(/[%]/,"",$3); if($3>max) max=$3} END{print max+0}' "$log_file")

    jq -n \
        --arg size "$size" \
        --argjson avg_gpu_used "${avg_gpu_used:-0}" \
        --argjson max_gpu_used "${max_gpu_used:-0}" \
        --argjson avg_system_usage "${avg_system_usage:-0}" \
        --argjson max_system_usage "${max_system_usage:-0}" \
        '{
            size: $size,
            avg_gpu_memory_mb: $avg_gpu_used,
            max_gpu_memory_mb: $max_gpu_used,
            avg_system_memory_percent: $avg_system_usage,
            max_system_memory_percent: $max_system_usage
        }'
}

# Run scalability benchmark
run_scalability_benchmark() {
    log_info "Running scalability benchmark..."

    local test_binary="$PROJECT_ROOT/build/Puzzle71Solver"
    if [[ ! -x "$test_binary" ]]; then
        log_error "Puzzle71Solver binary not found"
        return 1
    fi

    local results_dir="$BENCHMARK_RESULTS_DIR/scalability_$(date +%Y%m%d_%H%M%S)"
    mkdir -p "$results_dir"

    # Get thread counts from configuration
    local thread_counts
    thread_counts=$(echo "$BENCHMARK_CONFIG" | jq -r '.benchmark_types.scalability.thread_counts[]')
    local duration_per_test
    duration_per_test=$(echo "$BENCHMARK_CONFIG" | jq -r '.benchmark_types.scalability.duration_per_test')

    local total_tests=$(echo "$thread_counts" | wc -l)
    local test_num=0

    # Get test range
    local test_range_start
    local test_range_end
    test_range_start=$(echo "$BENCHMARK_CONFIG" | jq -r '.test_scenarios.medium_range.start')
    test_range_end=$(echo "$BENCHMARK_CONFIG" | jq -r '.test_scenarios.medium_range.end')

    while IFS= read -r thread_count; do
        ((test_num++))
        show_progress "$test_num" "$total_tests" "Scalability benchmark ($thread_count threads)"

        local test_log="$results_dir/scalability_${thread_count}_threads.log"
        local start_time=$(date +%s)

        # Run scalability test
        timeout "$duration_per_test" "$test_binary" \
            --device 0 \
            --range "${test_range_start}:${test_range_end}" \
            --threads "$thread_count" \
            --checkpoints 1000 \
            --benchmark \
            > "$test_log" 2>&1 || true

        local end_time=$(date +%s)
        local actual_duration=$((end_time - start_time))

        # Parse results
        local result
        if result=$(parse_benchmark_output "$test_log" "$actual_duration" "$thread_count"); then
            echo "$result" > "$results_dir/scalability_result_${thread_count}.json"
        else
            log_warning "Failed to parse scalability test results for $thread_count threads"
        fi

    done <<< "$thread_counts"

    # Generate scalability report
    local scalability_report="$results_dir/scalability_report.json"
    generate_scalability_report "$scalability_report" "$results_dir"

    log_success "Scalability benchmark completed"
    log_success "Report saved to: $scalability_report"

    return 0
}

# Generate scalability report
generate_scalability_report() {
    local report_file="$1"
    local results_dir="$2"

    local scalability_results="[]"

    # Process scalability results
    for result_file in "$results_dir"/scalability_result_*.json; do
        if [[ -f "$result_file" ]]; then
            local result_data
            result_data=$(cat "$result_file")
            scalability_results=$(echo "$scalability_results" | jq --argjson data "$result_data" '. += [$data]')
        fi
    done

    # Calculate scalability metrics
    local scalability_metrics
    scalability_metrics=$(echo "$scalability_results" | jq '{
        thread_counts: map(.iteration),
        throughputs: map(.throughput),
        efficiency: map(.throughput / (.iteration * .[0].throughput)) | .,
        speedup: map(.throughput / .[0].throughput)
    }')

    cat > "$report_file" << EOF
{
  "benchmark_type": "scalability",
  "timestamp": "$(date -u +"%Y-%m-%dT%H:%M:%SZ")",
  "results": $scalability_results,
  "metrics": $scalability_metrics
}
EOF
}

# Run performance regression test
run_performance_regression_test() {
    log_info "Running performance regression test..."

    local baseline_file="$PERFORMANCE_BASELINE_DIR/performance_baseline.json"
    local current_results_dir="$BENCHMARK_RESULTS_DIR/regression_$(date +%Y%m%d_%H%M%S)"
    mkdir -p "$current_results_dir"

    if [[ ! -f "$baseline_file" ]]; then
        log_warning "No performance baseline found, creating new baseline"
        run_throughput_benchmark "standard" 300 3
        mv "$BENCHMARK_RESULTS_DIR"/throughput_*/throughput_report.json "$baseline_file"
        log_success "New performance baseline created"
        return 0
    fi

    # Run current performance test
    log_info "Running current performance test..."
    run_throughput_benchmark "standard" 300 3

    # Find the latest throughput report
    local latest_report
    latest_report=$(find "$BENCHMARK_RESULTS_DIR" -name "throughput_report.json" -type f | sort -r | head -1)

    if [[ ! -f "$latest_report" ]]; then
        log_error "No throughput report found"
        return 1
    fi

    # Compare with baseline
    local regression_result
    regression_result=$(compare_with_baseline "$baseline_file" "$latest_report")

    # Generate regression report
    local regression_report="$current_results_dir/regression_report.json"
    cat > "$regression_report" << EOF
{
  "test_type": "performance_regression",
  "timestamp": "$(date -u +"%Y-%m-%dT%H:%M:%SZ")",
  "baseline_file": "$baseline_file",
  "current_report": "$latest_report",
  "comparison": $regression_result,
  "regression_detected": $(echo "$regression_result" | jq '.regression_detected')
}
EOF

    # Check for regression
    local regression_detected
    regression_detected=$(echo "$regression_result" | jq -r '.regression_detected')

    if [[ "$regression_detected" == "true" ]]; then
        log_error "Performance regression detected!"
        echo "$regression_result" | jq -r '.regression_details[]' | while read -r detail; do
            log_error "  $detail"
        done
        return 1
    else
        log_success "No performance regression detected"
        return 0
    fi
}

# Compare with baseline
compare_with_baseline() {
    local baseline_file="$1"
    local current_file="$2"

    local baseline_throughput
    local current_throughput
    local regression_threshold
    regression_threshold=$(echo "$BENCHMARK_CONFIG" | jq -r '.benchmark_types.regression.regression_threshold // 5.0')

    baseline_throughput=$(jq -r '.statistics.avg_throughput' "$baseline_file")
    current_throughput=$(jq -r '.statistics.avg_throughput' "$current_file")

    if [[ "$baseline_throughput" == "null" || "$current_throughput" == "null" ]]; then
        jq -n '{
            regression_detected: false,
            regression_details: ["Unable to compare - missing data"]
        }'
        return 0
    fi

    local regression_percentage
    regression_percentage=$(awk "BEGIN {printf \"%.2f\", ((${baseline_throughput} - ${current_throughput}) / ${baseline_throughput}) * 100}")

    local regression_detected="false"
    local regression_details=()

    if (( $(awk "BEGIN {print ($regression_percentage > $regression_threshold)}") )); then
        regression_detected="true"
        regression_details+=("Performance regression: ${regression_percentage}% (threshold: ${regression_threshold}%)")
    fi

    if [[ ${current_throughput%.*} -lt ${baseline_throughput%.*} ]]; then
        regression_details+=("Current throughput (${current_throughput}) lower than baseline (${baseline_throughput})")
    fi

    jq -n \
        --argjson baseline "$baseline_throughput" \
        --argjson current "$current_throughput" \
        --argjson regression "$regression_percentage" \
        --arg detected "$regression_detected" \
        --argjson details "$(printf '%s\n' "${regression_details[@]}" | jq -R . | jq -s .)" \
        '{
            baseline_throughput: $baseline,
            current_throughput: $current,
            regression_percentage: $regression,
            regression_detected: ($detected == "true"),
            regression_details: $details
        }'
}

# Update performance baseline
update_performance_baseline() {
    log_info "Updating performance baseline..."

    # Run comprehensive benchmark
    run_throughput_benchmark "comprehensive" 600 5

    # Find the latest comprehensive report
    local latest_report
    latest_report=$(find "$BENCHMARK_RESULTS_DIR" -name "throughput_report.json" -type f | sort -r | head -1)

    if [[ -f "$latest_report" ]]; then
        # Create backup of old baseline
        if [[ -f "$PERFORMANCE_BASELINE_DIR/performance_baseline.json" ]]; then
            local backup_file="$PERFORMANCE_BASELINE_DIR/performance_baseline_backup_$(date +%Y%m%d_%H%M%S).json"
            cp "$PERFORMANCE_BASELINE_DIR/performance_baseline.json" "$backup_file"
            log_info "Old baseline backed up to: $backup_file"
        fi

        # Update baseline
        cp "$latest_report" "$PERFORMANCE_BASELINE_DIR/performance_baseline.json"
        log_success "Performance baseline updated"
    else
        log_error "No benchmark report found to use as baseline"
        return 1
    fi

    return 0
}

# Generate performance charts (placeholder)
generate_performance_charts() {
    local results_dir="$1"

    log_info "Generating performance charts..."

    # This would require matplotlib or similar plotting library
    # For now, we'll create a simple text-based summary
    local chart_summary="$results_dir/chart_summary.txt"

    cat > "$chart_summary" << EOF
Performance Chart Summary
========================

Charts would be generated here using matplotlib or similar tools:

1. Throughput over time
2. Memory usage patterns
3. Scalability curves
4. Performance regression trends

To enable chart generation, install:
- python3-matplotlib
- python3-numpy
- python3-pandas

Chart generation is currently a placeholder feature.
EOF

    log_info "Chart summary saved to: $chart_summary"
}

# Export benchmark results to CSV
export_to_csv() {
    local results_dir="$1"

    log_info "Exporting benchmark results to CSV..."

    local csv_file="$results_dir/benchmark_results.csv"

    # Create CSV header
    echo "timestamp,benchmark_type,test_scenario,iteration,duration,throughput,keys_total,gpu_utilization,memory_usage" > "$csv_file"

    # Process all JSON result files
    find "$results_dir" -name "*_report.json" -type f | while read -r report_file; do
        local benchmark_type
        benchmark_type=$(jq -r '.benchmark_type' "$report_file")

        if [[ "$benchmark_type" == "throughput" ]]; then
            jq -r '.results[] | [.timestamp, .benchmark_type, .test_scenario, .iteration, .duration, .throughput, .keys_total, .gpu_utilization, .memory_usage] | @csv' "$report_file" | sed 's/"//g' >> "$csv_file"
        fi
    done

    log_success "Results exported to CSV: $csv_file"
}

# Print usage information
print_usage() {
    cat << EOF
T052: Performance Benchmarking Utilities

USAGE:
    $0 <command> [options]

COMMANDS:
    throughput [type]          Run throughput benchmark
    memory                    Run memory benchmark
    scalability               Run scalability benchmark
    regression                Run performance regression test
    baseline                  Update performance baseline
    export <directory>        Export results to CSV
    charts <directory>        Generate performance charts
    help                      Show this help message

BENCHMARK TYPES:
    quick                     Quick test (60s, small range)
    standard                  Standard test (300s, medium range) [default]
    comprehensive             Comprehensive test (600s, large range)

EXAMPLES:
    $0 throughput             # Run standard throughput benchmark
    $0 throughput quick       # Run quick throughput benchmark
    $0 memory                 # Run memory benchmark
    $0 scalability            # Run scalability benchmark
    $0 regression             # Run performance regression test
    $0 baseline               # Update performance baseline
    $0 export results/        # Export results to CSV

OPTIONS:
    --duration <seconds>      Custom benchmark duration
    --iterations <count>      Number of benchmark iterations
    --device <id>             GPU device to use
    --verbose                 Enable verbose output
    --help, -h                Show this help message

EOF
}

# Main script execution
main() {
    local command=""
    local benchmark_type="standard"
    local duration=""
    local iterations=""
    local device=""
    local verbose=false

    # Parse arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            --duration)
                duration="$2"
                shift 2
                ;;
            --iterations)
                iterations="$2"
                shift 2
                ;;
            --device)
                device="$2"
                shift 2
                ;;
            --verbose)
                verbose=true
                shift
                ;;
            --help|-h)
                print_usage
                exit 0
                ;;
            throughput|memory|scalability|regression|baseline|export|charts|help)
                command="$1"
                shift
                # Check for benchmark type
                if [[ $# -gt 0 && ! "$1" =~ ^-- && "$command" == "throughput" ]]; then
                    if [[ "quick standard comprehensive" =~ "$1" ]]; then
                        benchmark_type="$1"
                        shift
                    fi
                fi
                ;;
            *)
                log_error "Unknown argument: $1"
                print_usage
                exit 1
                ;;
        esac
    done

    # Initialize benchmark framework
    init_benchmark_framework
    load_benchmark_config

    # Execute command
    case "$command" in
        throughput)
            local test_duration="${duration:-$(echo "$BENCHMARK_CONFIG" | jq -r ".benchmark_types.throughput.default_duration")}"
            local test_iterations="${iterations:-$(echo "$BENCHMARK_CONFIG" | jq -r ".benchmark_types.throughput.iterations")}"
            run_throughput_benchmark "$benchmark_type" "$test_duration" "$test_iterations"
            ;;
        memory)
            run_memory_benchmark
            ;;
        scalability)
            run_scalability_benchmark
            ;;
        regression)
            run_performance_regression_test
            ;;
        baseline)
            update_performance_baseline
            ;;
        export)
            if [[ -z "$duration" ]]; then
                log_error "export command requires directory path"
                print_usage
                exit 1
            fi
            export_to_csv "$duration"
            ;;
        charts)
            if [[ -z "$duration" ]]; then
                log_error "charts command requires directory path"
                print_usage
                exit 1
            fi
            generate_performance_charts "$duration"
            ;;
        help|"")
            print_usage
            ;;
        *)
            log_error "Unknown command: $command"
            print_usage
            exit 1
            ;;
    esac
}

# Execute main function with all arguments
main "$@"