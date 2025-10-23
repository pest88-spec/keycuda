#!/bin/bash

# Multiple Build Benchmark Runner
# T029: Run multiple 5-minute build benchmarks for statistical significance

set -euo pipefail

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BENCHMARK_SCRIPT="$SCRIPT_DIR/benchmark-5min-build.sh"
ITERATIONS=3
TARGET_BUILD_MINUTES=5
TARGET_BUILD_SECONDS=$((TARGET_BUILD_MINUTES * 60))
RESULTS_DIR="$PROJECT_ROOT/benchmark-results"

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
RED='\033[0;31m'
NC='\033[0m'

# Results storage
declare -a BUILD_TIMES=()
declare -a BUILD_SUCCESS=()
declare -a REPORT_FILES=()

# Logging
log() {
    echo -e "${BLUE}[BENCHMARK-RUNNER]${NC} $1"
}

success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Create results directory
setup_results() {
    mkdir -p "$RESULTS_DIR"
    local timestamp=$(date '+%Y%m%d_%H%M%S')
    RESULTS_DIR="$RESULTS_DIR/benchmark-$timestamp"
    mkdir -p "$RESULTS_DIR"
    log "Results directory: $RESULTS_DIR"
}

# Run single benchmark iteration
run_benchmark_iteration() {
    local iteration=$1
    local iteration_start=$(date +%s)

    log "Running benchmark iteration $iteration/$ITERATIONS..."

    # Create iteration-specific results
    local iteration_dir="$RESULTS_DIR/iteration-$iteration"
    mkdir -p "$iteration_dir"

    # Run benchmark with timeout safety
    local timeout_duration=$((TARGET_BUILD_SECONDS + 300)) # 5 minutes extra buffer
    local benchmark_result=0

    if timeout "$timeout_duration" "$BENCHMARK_SCRIPT" 2>&1 | tee "$iteration_dir/benchmark.log"; then
        benchmark_result=0
    else
        local exit_code=$?
        if [[ $exit_code -eq 124 ]]; then
            error "Benchmark iteration $iteration timed out after ${timeout_duration} seconds"
            benchmark_result=1
        else
            warning "Benchmark iteration $iteration failed with exit code $exit_code"
            benchmark_result=1
        fi
    fi

    local iteration_end=$(date +%s)
    local iteration_duration=$((iteration_end - iteration_start))

    # Collect results
    local report_file="$PROJECT_ROOT/build-benchmark-results.json"
    if [[ -f "$report_file" ]]; then
        cp "$report_file" "$iteration_dir/report.json"
        REPORT_FILES+=("$iteration_dir/report.json")

        # Extract metrics
        local build_success=$(jq -r '.build_success // false' "$report_file" 2>/dev/null || echo "false")
        local build_time=$(jq -r '.total_build_time_seconds // 0' "$report_file" 2>/dev/null || echo "0")

        BUILD_SUCCESS+=("$build_success")
        BUILD_TIMES+=("$build_time")

        log "Iteration $iteration result: Success=$build_success, Time=${build_time}s, Duration=${iteration_duration}s"
    else
        warning "No report file found for iteration $iteration"
        BUILD_SUCCESS+=("false")
        BUILD_TIMES+=("0")
    fi

    return $benchmark_result
}

# Analyze all benchmark results
analyze_results() {
    log "Analyzing benchmark results..."

    if [[ ${#BUILD_TIMES[@]} -eq 0 ]]; then
        error "No benchmark results to analyze"
        return 1
    fi

    # Calculate statistics
    local successful_builds=0
    local total_time=0
    local min_time=999999
    local max_time=0
    local target_achievements=0

    for i in "${!BUILD_TIMES[@]}"; do
        local time=${BUILD_TIMES[$i]}
        local success=${BUILD_SUCCESS[$i]}

        if [[ "$success" == "true" ]]; then
            successful_builds=$((successful_builds + 1))
            total_time=$((total_time + time))

            if [[ $time -lt $min_time ]]; then
                min_time=$time
            fi

            if [[ $time -gt $max_time ]]; then
                max_time=$time
            fi

            if [[ $time -le $TARGET_BUILD_SECONDS ]]; then
                target_achievements=$((target_achievements + 1))
            fi
        fi
    done

    # Calculate averages and percentages
    local success_rate=0
    local target_achievement_rate=0
    local avg_time=0

    if [[ $ITERATIONS -gt 0 ]]; then
        success_rate=$(echo "scale=2; $successful_builds * 100 / $ITERATIONS" | bc -l)
        target_achievement_rate=$(echo "scale=2; $target_achievements * 100 / $ITERATIONS" | bc -l)
    fi

    if [[ $successful_builds -gt 0 ]]; then
        avg_time=$(echo "scale=2; $total_time / $successful_builds" | bc -l)
    fi

    # Generate comprehensive report
    local summary_report="$RESULTS_DIR/benchmark-summary.json"
    cat > "$summary_report" << EOF
{
  "summary_metadata": {
    "generated": "$(date -Iseconds)",
    "total_iterations": $ITERATIONS,
    "successful_builds": $successful_builds,
    "target_build_seconds": $TARGET_BUILD_SECONDS
  },
  "performance_metrics": {
    "success_rate_percent": $success_rate,
    "target_achievement_rate_percent": $target_achievement_rate,
    "average_build_time_seconds": $avg_time,
    "minimum_build_time_seconds": $min_time,
    "maximum_build_time_seconds": $max_time,
    "total_build_time_seconds": $total_time
  },
  "individual_results": [
EOF

    # Add individual results
    for i in "${!BUILD_TIMES[@]}"; do
        local time=${BUILD_TIMES[$i]}
        local success=${BUILD_SUCCESS[$i]}
        local target_met="false"

        if [[ "$success" == "true" && $time -le $TARGET_BUILD_SECONDS ]]; then
            target_met="true"
        fi

        if [[ $i -gt 0 ]]; then
            echo "," >> "$summary_report"
        fi
        echo "    {\"iteration\": $((i + 1)), \"success\": $success, \"time_seconds\": $time, \"target_met\": $target_met}" >> "$summary_report"
    done

    cat >> "$summary_report" << EOF

  ],
  "conclusion": {
    "meets_target": $([ $target_achievement_rate -ge 80 ] && echo "true" || echo "false"),
    "recommendation": "$(
        if [[ $target_achievement_rate -ge 80 ]]; then
            echo "Build performance consistently meets target - ready for production deployment"
        elif [[ $target_achievement_rate -ge 50 ]]; then
            echo "Build performance partially meets target - consider optimization for consistency"
        else
            echo "Build performance does not meet target - significant optimization required"
        fi
    )"
  }
}
EOF

    # Display results
    display_results "$success_rate" "$target_achievement_rate" "$avg_time" "$min_time" "$max_time" "$summary_report"

    # Return appropriate exit code
    if [[ $target_achievement_rate -ge 80 ]]; then
        return 0
    else
        return 1
    fi
}

# Display formatted results
display_results() {
    local success_rate=$1
    local target_achievement_rate=$2
    local avg_time=$3
    local min_time=$4
    local max_time=$5
    local report_file=$6

    echo
    echo "=== Build Benchmark Results Summary ==="
    echo
    echo "Total Iterations: $ITERATIONS"
    echo "Successful Builds: $(echo "$success_rate" | cut -d. -f1)%"
    echo "Target Achievement Rate: $(echo "$target_achievement_rate" | cut -d. -f1)%"
    echo
    echo "Build Time Statistics:"
    echo "  Average: ${avg_time}s"
    echo "  Minimum: ${min_time}s"
    echo "  Maximum: ${max_time}s"
    echo "  Target: ${TARGET_BUILD_SECONDS}s"
    echo
    echo "Detailed Reports:"
    for report in "${REPORT_FILES[@]}"; do
        echo "  - $report"
    done
    echo "  Summary: $report_file"
    echo

    if [[ $(echo "$target_achievement_rate >= 80" | bc -l) -eq 1 ]]; then
        success -e "${GREEN}✅ BENCHMARK PASSED: Build performance meets 5-minute target${NC}"
    else
        warning -e "${YELLOW}⚠️  BENCHMARK PARTIAL: Build performance needs improvement${NC}"
    fi
}

# Check dependencies
check_dependencies() {
    for cmd in bc jq timeout; do
        if ! command -v "$cmd" &> /dev/null; then
            error "Missing required dependency: $cmd"
            exit 1
        fi
    done

    if [[ ! -x "$BENCHMARK_SCRIPT" ]]; then
        error "Benchmark script not found or not executable: $BENCHMARK_SCRIPT"
        exit 1
    fi
}

# Main execution
main() {
    log "Starting multiple build benchmark runner..."
    log "Target: ${TARGET_BUILD_MINUTES} minutes per build"
    log "Iterations: $ITERATIONS"

    check_dependencies
    setup_results

    local successful_iterations=0

    # Run benchmark iterations
    for ((i=1; i<=ITERATIONS; i++)); do
        if run_benchmark_iteration "$i"; then
            successful_iterations=$((successful_iterations + 1))
        fi

        # Small delay between iterations
        if [[ $i -lt $ITERATIONS ]]; then
            log "Waiting 10 seconds before next iteration..."
            sleep 10
        fi
    done

    log "Completed $successful_iterations/$ITERATIONS iterations successfully"

    # Analyze and report
    if analyze_results; then
        success "Multiple benchmark runs completed successfully"
        return 0
    else
        warning "Multiple benchmark runs completed with performance issues"
        return 1
    fi
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -i|--iterations)
            ITERATIONS="$2"
            shift 2
            ;;
        -t|--target)
            TARGET_BUILD_MINUTES="$2"
            TARGET_BUILD_SECONDS=$((TARGET_BUILD_MINUTES * 60))
            shift 2
            ;;
        -h|--help)
            echo "Usage: $0 [options]"
            echo "Options:"
            echo "  -i, --iterations N    Number of benchmark iterations (default: 3)"
            echo "  -t, --target N        Target build time in minutes (default: 5)"
            echo "  -h, --help           Show this help message"
            exit 0
            ;;
        *)
            error "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Run if executed directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi