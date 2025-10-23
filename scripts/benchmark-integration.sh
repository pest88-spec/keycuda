#!/bin/bash

# Integration Benchmarking Script
# Implements T065: Execute full integration benchmarking against 5-minute build target

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BENCHMARK_DIR="$PROJECT_ROOT/benchmarks"
LOG_DIR="$PROJECT_ROOT/logs/benchmarks"
RESULTS_FILE="$LOG_DIR/benchmark_results.json"

# Benchmark configuration
readonly BUILD_TARGET_SECONDS=300  # 5 minutes
readonly TEST_ITERATIONS=3
readonly CLEAN_BUILD=true

# Ensure directories exist
mkdir -p "$BENCHMARK_DIR" "$LOG_DIR"

# Color codes
readonly RED='\033[0;31m'
readonly GREEN='\033[0;32m'
readonly YELLOW='\033[1;33m'
readonly BLUE='\033[0;34m'
readonly NC='\033[0m'

# Logging
log_info() {
    echo -e "${BLUE}[INFO]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1" | tee -a "$LOG_DIR/benchmark.log"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1" | tee -a "$LOG_DIR/benchmark.log"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1" | tee -a "$LOG_DIR/benchmark.log"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1" | tee -a "$LOG_DIR/benchmark.log"
}

# Benchmark results storage
declare -A benchmark_results

# Initialize results
init_results() {
    cat > "$RESULTS_FILE" << EOF
{
    "benchmark_run": {
        "timestamp": "$(date -Iseconds)",
        "target_build_seconds": $BUILD_TARGET_SECONDS,
        "test_iterations": $TEST_ITERATIONS,
        "clean_build": $CLEAN_BUILD,
        "status": "running",
        "overall_success": false
    },
    "build_results": [],
    "performance_metrics": {
        "min_build_time": 0.0,
        "max_build_time": 0.0,
        "avg_build_time": 0.0,
        "build_success_rate": 0.0,
        "target_achieved": false
    },
    "system_metrics": {
        "cpu_cores": $(nproc),
        "memory_gb": $(free -g | awk '/^Mem:/{print $2}'),
        "disk_space_gb": $(df -BG . | awk 'NR==2{print $4}' | sed 's/G//'),
        "cuda_available": $([ -x "$(command -v nvidia-smi)" ] && echo "true" || echo "false")
    }
}
EOF
}

# System metrics collection
collect_system_metrics() {
    local phase="$1"
    local metrics_file="$LOG_DIR/system_metrics_${phase}.json"

    local cpu_usage=$(top -bn1 | grep "Cpu(s)" | awk '{print $2}' | sed 's/%us,//' || echo "0.0")
    local memory_usage=$(free | grep Mem | awk '{printf "%.1f", $3/$2 * 100.0}' || echo "0.0")
    local disk_usage=$(df -h . | awk 'NR==2 {print $5}' | sed 's/%//' || echo "0.0")
    local load_avg=$(uptime | awk -F'load average:' '{print $2}' | awk '{print $1}' | sed 's/,//' || echo "0.0")

    # GPU metrics if available
    local gpu_usage="0.0"
    local gpu_memory="0.0"

    if command -v nvidia-smi &> /dev/null; then
        gpu_usage=$(nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits | head -1 || echo "0.0")
        gpu_memory=$(nvidia-smi --query-gpu=utilization.memory --format=csv,noheader,nounits | head -1 || echo "0.0")
    fi

    cat > "$metrics_file" << EOF
{
    "timestamp": "$(date -Iseconds)",
    "phase": "$phase",
    "system": {
        "cpu_usage_percent": $cpu_usage,
        "memory_usage_percent": $memory_usage,
        "disk_usage_percent": $disk_usage,
        "load_average": $load_avg
    },
    "gpu": {
        "usage_percent": $gpu_usage,
        "memory_usage_percent": $gpu_memory
    }
}
EOF
}

# Clean build environment
clean_build_environment() {
    log_info "Cleaning build environment"
    collect_system_metrics "pre_clean"

    if [[ -d "$PROJECT_ROOT/build" ]]; then
        rm -rf "$PROJECT_ROOT/build"
        log_info "Removed existing build directory"
    fi

    # Clean any temporary files
    find "$PROJECT_ROOT" -name "*.tmp" -delete 2>/dev/null || true
    find "$PROJECT_ROOT" -name "*.o" -delete 2>/dev/null || true

    collect_system_metrics "post_clean"
    log_success "Build environment cleaned"
}

# Perform single build benchmark
run_build_benchmark() {
    local iteration="$1"
    log_info "Running build benchmark iteration $iteration/$TEST_ITERATIONS"

    collect_system_metrics "pre_build_$iteration"

    local start_time=$(date +%s)
    local start_time_ns=$(date +%s.%N)

    # Create build directory
    mkdir -p "$PROJECT_ROOT/build"
    cd "$PROJECT_ROOT/build"

    # Configure build
    log_info "Configuring build (iteration $iteration)"
    local configure_start=$(date +%s.%N)
    if ! cmake .. -DCMAKE_BUILD_TYPE=Release > "$LOG_DIR/cmake_$iteration.log" 2>&1; then
        log_error "CMake configuration failed (iteration $iteration)"
        return 1
    fi
    local configure_end=$(date +%s.%N)
    local configure_time=$(echo "$configure_end - $configure_start" | bc -l)

    # Build project
    log_info "Building project (iteration $iteration)"
    local build_start=$(date +%s.%N)
    local build_result=0

    if timeout $((BUILD_TARGET_SECONDS + 60)) make -j$(nproc) > "$LOG_DIR/make_$iteration.log" 2>&1; then
        build_result=0
    else
        local exit_code=$?
        if [[ $exit_code -eq 124 ]]; then
            log_error "Build timed out (iteration $iteration)"
            build_result=2  # Timeout
        else
            log_error "Build failed (iteration $iteration)"
            build_result=1  # Failed
        fi
    fi

    local build_end=$(date +%s.%N)
    local build_time=$(echo "$build_end - $build_start" | bc -l)
    local total_time=$(echo "$build_end - $start_time_ns" | bc -l)

    collect_system_metrics "post_build_$iteration"

    # Verify build
    local binary_size=0
    local verification_passed=false

    if [[ $build_result -eq 0 ]]; then
        if [[ -f "$PROJECT_ROOT/build/Puzzle71Solver" ]]; then
            binary_size=$(stat -c%s "$PROJECT_ROOT/build/Puzzle71Solver" 2>/dev/null || echo "0")

            # Test binary execution
            if timeout 10 "$PROJECT_ROOT/build/Puzzle71Solver" --version > "$LOG_DIR/version_$iteration.log" 2>&1; then
                verification_passed=true
                log_success "Build verification passed (iteration $iteration)"
            else
                log_warning "Build verification failed - binary doesn't execute properly (iteration $iteration)"
            fi
        else
            log_warning "Build binary not found (iteration $iteration)"
        fi
    fi

    # Store results
    local success=false
    if [[ $build_result -eq 0 && $verification_passed == true ]]; then
        success=true
    fi

    local target_achieved=false
    if [[ $success == true && $(echo "$total_time <= $BUILD_TARGET_SECONDS" | bc -l) -eq 1 ]]; then
        target_achieved=true
    fi

    benchmark_results["iteration_$iteration"]=$(cat << EOF
{
    "iteration": $iteration,
    "start_time": "$(date -d@$start_time -Iseconds)",
    "success": $success,
    "target_achieved": $target_achieved,
    "configure_time_seconds": $configure_time,
    "build_time_seconds": $build_time,
    "total_time_seconds": $total_time,
    "binary_size_bytes": $binary_size,
    "verification_passed": $verification_passed,
    "error_code": $build_result
}
EOF
)

    if [[ $success == true ]]; then
        if [[ $target_achieved == true ]]; then
            log_success "Build $iteration completed successfully in ${total_time}s (${total_time}s <= ${BUILD_TARGET_SECONDS}s target)"
        else
            log_warning "Build $iteration completed successfully but exceeded target: ${total_time}s > ${BUILD_TARGET_SECONDS}s"
        fi
    else
        log_error "Build $iteration failed (error code: $build_result)"
    fi

    return $build_result
}

# Run benchmark iterations
run_benchmark_iterations() {
    log_info "Starting $TEST_ITERATIONS build benchmark iterations"
    log_info "Target: ${BUILD_TARGET_SECONDS}s (5 minutes) per build"

    local successful_builds=0
    local target_achieved_builds=0

    for ((i=1; i<=TEST_ITERATIONS; i++)); do
        clean_build_environment

        if run_build_benchmark $i; then
            ((successful_builds++))
            if [[ $(echo "${benchmark_results[iteration_$i]}" | jq -r '.target_achieved') == "true" ]]; then
                ((target_achieved_builds++))
            fi
        fi

        # Small delay between iterations
        if [[ $i -lt $TEST_ITERATIONS ]]; then
            sleep 5
        fi
    done

    log_info "Benchmark iterations completed: $successful_builds/$TEST_ITERATIONS successful"
    log_info "Target achieved in $target_achieved_builds/$TEST_ITERATIONS builds"

    # Update results
    local temp_file=$(mktemp)
    jq --arg successful_builds "$successful_builds" \
       --arg target_achieved_builds "$target_achieved_builds" \
       --arg total_iterations "$TEST_ITERATIONS" \
       '
       .benchmark_run.status = "completed" |
       .performance_metrics.build_success_rate = ($successful_builds | tonumber) / ($total_iterations | tonumber) * 100 |
       .performance_metrics.target_achieved = ($target_achieved_builds | tonumber) > 0
       ' "$RESULTS_FILE" > "$temp_file"
    mv "$temp_file" "$RESULTS_FILE"

    return 0
}

# Analyze benchmark results
analyze_results() {
    log_info "Analyzing benchmark results"

    local total_build_time=0.0
    local min_time=999999.0
    local max_time=0.0
    local successful_builds=0

    # Extract build times from results
    for ((i=1; i<=TEST_ITERATIONS; i++)); do
        if [[ -n "${benchmark_results[iteration_$i]}" ]]; then
            local build_time=$(echo "${benchmark_results[iteration_$i]}" | jq -r '.total_time_seconds')
            local success=$(echo "${benchmark_results[iteration_$i]}" | jq -r '.success')

            if [[ "$success" == "true" ]]; then
                ((successful_builds++))
                total_build_time=$(echo "$total_build_time + $build_time" | bc -l)

                if (( $(echo "$build_time < $min_time" | bc -l) )); then
                    min_time=$build_time
                fi

                if (( $(echo "$build_time > $max_time" | bc -l) )); then
                    max_time=$build_time
                fi
            fi
        fi
    done

    # Calculate averages
    local avg_time=0.0
    if [[ $successful_builds -gt 0 ]]; then
        avg_time=$(echo "scale=2; $total_build_time / $successful_builds" | bc -l)
    fi

    # Update results with performance metrics
    local temp_file=$(mktemp)
    jq --arg min_time "$min_time" \
       --arg max_time "$max_time" \
       --arg avg_time "$avg_time" \
       '
       .performance_metrics.min_build_time = ($min_time | tonumber) |
       .performance_metrics.max_build_time = ($max_time | tonumber) |
       .performance_metrics.avg_build_time = ($avg_time | tonumber) |
       .build_results = [
           $(for ((i=1; i<=TEST_ITERATIONS; i++)); do
               if [[ -n "${benchmark_results[iteration_$i]}" ]]; then
                   echo "${benchmark_results[iteration_$i]}"
                   if [[ $i -lt $TEST_ITERATIONS ]]; then
                       echo ","
                   fi
               fi
           done)
       ]
       ' "$RESULTS_FILE" > "$temp_file"
    mv "$temp_file" "$RESULTS_FILE"

    log_info "Performance analysis:"
    log_info "  Minimum build time: ${min_time}s"
    log_info "  Maximum build time: ${max_time}s"
    log_info "  Average build time: ${avg_time}s"
    log_info "  Successful builds: $successful_builds/$TEST_ITERATIONS"
}

# Generate benchmark report
generate_report() {
    log_info "Generating benchmark report"

    local report_file="$LOG_DIR/benchmark_report.html"
    local min_time=$(jq -r '.performance_metrics.min_build_time' "$RESULTS_FILE")
    local max_time=$(jq -r '.performance_metrics.max_build_time' "$RESULTS_FILE")
    local avg_time=$(jq -r '.performance_metrics.avg_build_time' "$RESULTS_FILE")
    local success_rate=$(jq -r '.performance_metrics.build_success_rate' "$RESULTS_FILE")
    local target_achieved=$(jq -r '.performance_metrics.target_achieved' "$RESULTS_FILE")

    local status_class="success"
    local status_text="✅ TARGET ACHIEVED"
    local status_message="Average build time ${avg_time}s meets 5-minute target"

    if [[ $(echo "$avg_time > $BUILD_TARGET_SECONDS" | bc -l) -eq 1 ]]; then
        status_class="failure"
        status_text="❌ TARGET MISSED"
        status_message="Average build time ${avg_time}s exceeds 5-minute target"
    fi

    cat > "$report_file" << EOF
<!DOCTYPE html>
<html>
<head>
    <title>Integration Benchmark Report</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; }
        .header { background: #2c3e50; color: white; padding: 20px; border-radius: 5px; }
        .success { color: #27ae60; font-weight: bold; }
        .failure { color: #e74c3c; font-weight: bold; }
        .warning { color: #f39c12; font-weight: bold; }
        .metric-card { background: #f8f9fa; padding: 15px; margin: 10px 0; border-radius: 5px; border-left: 4px solid #3498db; }
        .chart-container { width: 45%; display: inline-block; margin: 20px; }
        table { width: 100%; border-collapse: collapse; margin: 20px 0; }
        th, td { padding: 10px; border: 1px solid #ddd; text-align: left; }
        th { background-color: #3498db; color: white; }
        .pass { background-color: #d4edda; }
        .fail { background-color: #f8d7da; }
        .target-line { stroke: #e74c3c; stroke-width: 2; stroke-dasharray: 5,5; }
    </style>
</head>
<body>
    <div class="header">
        <h1>🚀 Integration Benchmark Report</h1>
        <p>Generated: $(date)</p>
        <p>Target Build Time: ${BUILD_TARGET_SECONDS}s (5 minutes)</p>
    </div>

    <div class="metric-card">
        <h2>📊 Benchmark Summary</h2>
        <div style="display: flex; flex-wrap: wrap;">
            <div class="metric-card">
                <h3>Test Iterations</h3>
                <p style="font-size: 24px;">$TEST_ITERATIONS</p>
            </div>
            <div class="metric-card">
                <h3>Success Rate</h3>
                <p style="font-size: 24px;">${success_rate}%</p>
            </div>
            <div class="metric-card">
                <h3>Average Build Time</h3>
                <p style="font-size: 24px;">${avg_time}s</p>
            </div>
            <div class="metric-card">
                <h3>Result</h3>
                <p class="$status_class" style="font-size: 24px;">$status_text</p>
            </div>
        </div>
        <p><strong>$status_message</strong></p>
    </div>

    <div class="chart-container">
        <canvas id="buildTimeChart"></canvas>
    </div>
    <div class="chart-container">
        <canvas id="performanceChart"></canvas>
    </div>

    <h2>📋 Detailed Results</h2>
    <table>
        <thead>
            <tr>
                <th>Iteration</th>
                <th>Configure Time</th>
                <th>Build Time</th>
                <th>Total Time</th>
                <th>Success</th>
                <th>Target Achieved</th>
                <th>Binary Size</th>
            </tr>
        </thead>
        <tbody>
EOF

    # Add detailed results from JSON
    jq -r '.build_results[] | @tsv' "$RESULTS_FILE" | while IFS=$'\t' read -r iteration start_time success target_achieved configure_time build_time total_time binary_size verification_passed error_code; do
        local success_class="pass"
        local success_text="✅ Yes"
        local target_class="pass"
        local target_text="✅ Yes"

        if [[ "$success" == "false" ]]; then
            success_class="fail"
            success_text="❌ No"
        fi

        if [[ "$target_achieved" == "false" ]]; then
            target_class="fail"
            target_text="❌ No"
        fi

        cat >> "$report_file" << EOF
            <tr>
                <td>$iteration</td>
                <td>${configure_time}s</td>
                <td>${build_time}s</td>
                <td>${total_time}s</td>
                <td class="$success_class">$success_text</td>
                <td class="$target_class">$target_text</td>
                <td>$(echo "$binary_size" | numfmt --to=iec)B</td>
            </tr>
EOF
    done

    cat >> "$report_file" << EOF
        </tbody>
    </table>

    <script>
        // Build time chart
        const buildTimeCtx = document.getElementById('buildTimeChart').getContext('2d');
        new Chart(buildTimeCtx, {
            type: 'line',
            data: {
                labels: [$(jq -r '.build_results | map(.iteration) | join(",")' "$RESULTS_FILE")],
                datasets: [{
                    label: 'Build Time (seconds)',
                    data: [$(jq -r '.build_results | map(.total_time_seconds) | join(",")' "$RESULTS_FILE")],
                    borderColor: '#3498db',
                    backgroundColor: 'rgba(52, 152, 219, 0.1)',
                    tension: 0.4,
                    fill: true
                }]
            },
            options: {
                responsive: true,
                plugins: {
                    title: {
                        display: true,
                        text: 'Build Time per Iteration'
                    },
                    annotation: {
                        annotations: {
                            line1: {
                                type: 'line',
                                yMin: $BUILD_TARGET_SECONDS,
                                yMax: $BUILD_TARGET_SECONDS,
                                borderColor: '#e74c3c',
                                borderWidth: 2,
                                borderDash: [5, 5],
                                label: {
                                    content: 'Target (300s)',
                                    enabled: true,
                                    position: 'end'
                                }
                            }
                        }
                    }
                },
                scales: {
                    y: {
                        beginAtZero: true,
                        title: {
                            display: true,
                            text: 'Time (seconds)'
                        }
                    }
                }
            }
        });

        // Performance distribution chart
        const performanceCtx = document.getElementById('performanceChart').getContext('2d');
        new Chart(performanceCtx, {
            type: 'bar',
            data: {
                labels: ['Minimum', 'Average', 'Maximum', 'Target'],
                datasets: [{
                    label: 'Time (seconds)',
                    data: [$min_time, $avg_time, $max_time, $BUILD_TARGET_SECONDS],
                    backgroundColor: ['#27ae60', '#3498db', '#f39c12', '#e74c3c']
                }]
            },
            options: {
                responsive: true,
                plugins: {
                    title: {
                        display: true,
                        text: 'Performance Distribution'
                    }
                },
                scales: {
                    y: {
                        beginAtZero: true,
                        title: {
                            display: true,
                            text: 'Time (seconds)'
                        }
                    }
                }
            }
        });
    </script>
</body>
</html>
EOF

    log_success "Benchmark report generated: $report_file"
}

# Main execution
main() {
    local command="${1:-run}"

    case "$command" in
        "run")
            log_info "Starting integration benchmarking"
            log_info "Target: ${BUILD_TARGET_SECONDS}s (5 minutes) build time"
            log_info "Iterations: $TEST_ITERATIONS"
            log_info "Clean build: $CLEAN_BUILD"

            init_results
            collect_system_metrics "benchmark_start"

            run_benchmark_iterations
            analyze_results
            generate_report

            collect_system_metrics "benchmark_complete"

            # Check if target achieved
            local avg_time=$(jq -r '.performance_metrics.avg_build_time' "$RESULTS_FILE")
            local success_rate=$(jq -r '.performance_metrics.build_success_rate' "$RESULTS_FILE")

            log_info "Benchmark completed"
            log_info "Average build time: ${avg_time}s (target: ${BUILD_TARGET_SECONDS}s)"
            log_info "Success rate: ${success_rate}%"

            if [[ $(echo "$avg_time <= $BUILD_TARGET_SECONDS" | bc -l) -eq 1 && $(echo "$success_rate >= 80" | bc -l) -eq 1 ]]; then
                log_success "✅ BENCHMARK TARGET ACHIEVED!"
                return 0
            else
                log_error "❌ BENCHMARK TARGET MISSED!"
                return 1
            fi
            ;;
        "quick")
            # Quick single build test
            log_info "Running quick build test"
            clean_build_environment
            run_build_benchmark 1
            ;;
        "clean")
            log_info "Cleaning benchmark artifacts"
            rm -rf "$BENCHMARK_DIR" "$LOG_DIR"
            log_success "Benchmark cleanup completed"
            ;;
        "report")
            if [[ -f "$RESULTS_FILE" ]]; then
                generate_report
                echo "Report available: $LOG_DIR/benchmark_report.html"
            else
                log_error "No benchmark results found. Run benchmark first."
            fi
            ;;
        "help"|*)
            echo "Usage: $0 {run|quick|clean|report|help}"
            echo ""
            echo "Commands:"
            echo "  run    - Run full benchmark with multiple iterations"
            echo "  quick  - Run single build test"
            echo "  clean  - Clean benchmark artifacts and logs"
            echo "  report - Generate HTML report from existing results"
            echo "  help   - Show this help message"
            exit 0
            ;;
    esac
}

# Execute main function
main "$@"