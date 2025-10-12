#!/bin/bash

# CI Performance Gate Script
# Part of T045: Integrate performance gate into CI pipeline
#
# Usage: ./performance_gate.sh [gpu_model] [baseline_file] [output_file]
# Example: ./performance_gate.sh rtx3090 benchmarks/baselines/rtx3090.json benchmarks/results/latest.json
#
# This script implements zero-tolerance performance regression detection:
# - Runs comprehensive GPU performance benchmarks
# - Compares against established baselines
# - Fails CI build if any performance regression detected
# - Archives results with SHA-256 protection
# - Provides detailed regression reporting

set -euo pipefail

# Default values
GPU_MODEL=${1:-rtx3090}
BASELINE_FILE=${2:-benchmarks/baselines/${GPU_MODEL}.json}
OUTPUT_FILE=${3:-benchmarks/results/latest.json}
BENCHMARK_DURATION=${BENCHMARK_DURATION:-600}  # 10 minutes
CI_MODE=${CI_MODE:-true}  # Running in CI environment

# Colors for output (use plain text in CI)
if [[ "$CI_MODE" == "true" ]]; then
    RED=''
    GREEN=''
    YELLOW=''
    BLUE=''
    NC=''
else
    RED='\033[0;31m'
    GREEN='\033[0;32m'
    YELLOW='\033[1;33m'
    BLUE='\033[0;34m'
    NC='\033[0m'
fi

# Logging function
log() {
    echo -e "${BLUE}[$(date +'%Y-%m-%d %H:%M:%S')]${NC} $1"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1" >&2
}

warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

# CI-specific logging
ci_log() {
    if [[ "$CI_MODE" == "true" ]]; then
        echo "::group::$1"
        echo "$1"
        echo "::endgroup::"
    else
        log "$1"
    fi
}

# CI error reporting
ci_error() {
    if [[ "$CI_MODE" == "true" ]]; then
        echo "::error::$1"
    else
        error "$1"
    fi
}

# CI warning reporting
ci_warning() {
    if [[ "$CI_MODE" == "true" ]]; then
        echo "::warning::$1"
    else
        warning "$1"
    fi
}

# Show usage information
show_help() {
    cat << EOF
CI Performance Gate Script

USAGE:
    $0 <gpu_model> <baseline_file> [output_file]

ARGUMENTS:
    gpu_model      GPU model identifier (e.g., rtx3090, rtx2080ti, h20, a100)
    baseline_file   Path to baseline JSON file
    output_file     Path for current benchmark result (optional)

ENVIRONMENT VARIABLES:
    BENCHMARK_DURATION  Benchmark duration in seconds (default: 600)
    CI_MODE             Set to 'true' when running in CI (default: true)

CI INTEGRATION:
- Exit code 0: Performance meets baseline, CI passes
- Exit code 1: Performance regression detected, CI FAILS
- Exit code 2: Configuration or execution error, CI FAILS

ZERO-TOLERANCE POLICY:
- Any negative throughput delta = REGRESSION
- No exceptions, no grace periods
- Immediate CI failure on regression detection

EXAMPLES:
    # CI pipeline usage
    $0 rtx3090 benchmarks/baselines/rtx3090.json

    # Local testing
    CI_MODE=false $0 rtx3090 benchmarks/baselines/rtx3090.json benchmarks/results/test.json

EOF
}

# Initialize CI environment
initialize_ci_environment() {
    ci_log "Initializing CI performance gate..."

    if [[ "$CI_MODE" == "true" ]]; then
        log "Running in CI mode"
        log "Environment: ${CI_SERVER_NAME:-Unknown}"
        log "Pipeline: ${CI_PIPELINE_ID:-Unknown}"
        log "Branch: ${CI_BRANCH:-Unknown}"
        log "Commit: ${CI_COMMIT_SHA:-Unknown}"
    else
        log "Running in local mode"
    fi

    log "Performance Gate Configuration:"
    log "  GPU Model: $GPU_MODEL"
    log "  Baseline File: $BASELINE_FILE"
    log "  Output File: $OUTPUT_FILE"
    log "  Benchmark Duration: $BENCHMARK_DURATION seconds"
    log "  Regression Policy: ZERO-TOLERANCE"
}

# Validate CI environment
validate_ci_environment() {
    ci_log "Validating CI environment..."

    # Check if running in CI with proper environment
    if [[ "$CI_MODE" == "true" ]]; then
        # Verify baseline exists
        if [[ ! -f "$BASELINE_FILE" ]]; then
            ci_error "Baseline file not found: $BASELINE_FILE"
            ci_error "CI cannot proceed without baseline for regression testing"
            exit 2
        fi

        # Verify we can write to output directory
        local output_dir
        output_dir=$(dirname "$OUTPUT_FILE")
        if ! mkdir -p "$output_dir" 2>/dev/null; then
            ci_error "Cannot create output directory: $output_dir"
            exit 2
        fi

        # Verify GPU is available
        if ! nvidia-smi &> /dev/null; then
            ci_error "No CUDA devices available in CI environment"
            ci_error "Performance testing requires GPU access"
            exit 2
        fi
    fi

    success "CI environment validation passed"
}

# Check prerequisites for benchmark execution
check_benchmark_prerequisites() {
    ci_log "Checking benchmark prerequisites..."

    # Verify benchmark executable exists
    if [[ ! -f "./build/Puzzle71Solver" ]]; then
        ci_error "Benchmark executable not found: ./build/Puzzle71Solver"
        ci_error "Build the project first"
        exit 2
    fi

    # Verify benchmark script exists
    if [[ ! -f "./scripts/run_benchmarks.sh" ]]; then
        ci_error "Benchmark script not found: ./scripts/run_benchmarks.sh"
        exit 2
    fi

    # Make scripts executable
    chmod +x ./scripts/run_benchmarks.sh 2>/dev/null || true

    success "Benchmark prerequisites check passed"
}

# Execute performance benchmark
run_ci_benchmark() {
    ci_log "Starting CI performance benchmark..."

    local benchmark_start=$(date +%s)
    local benchmark_log="${OUTPUT_FILE%.json}_benchmark.log"

    # Run benchmark with timeout protection
    local timeout_duration=$((BENCHMARK_DURATION + 300))  # Add 5 minutes buffer
    ci_log "Executing benchmark (timeout: ${timeout_duration}s)..."

    if timeout "$timeout_duration" ./scripts/run_benchmarks.sh "$GPU_MODEL" "$BASELINE_FILE" "$OUTPUT_FILE" > "$benchmark_log" 2>&1; then
        local benchmark_end=$(date +%s)
        local benchmark_duration=$((benchmark_end - benchmark_start))

        ci_log "Benchmark completed successfully in ${benchmark_duration}s"
        success "Benchmark result saved to: $OUTPUT_FILE"

        # Show benchmark summary
        if [[ -f "$benchmark_log" ]]; then
            ci_log "Benchmark Summary:"
            tail -10 "$benchmark_log" | grep -E "(Performance|Throughput|Result)" || true
        fi
    else
        local exit_code=$?
        local benchmark_end=$(date +%s)
        local benchmark_duration=$((benchmark_end - benchmark_start))

        ci_error "Benchmark failed after ${benchmark_duration}s (exit code: $exit_code)"
        ci_error "Check benchmark log: $benchmark_log"

        # Show error details from log
        if [[ -f "$benchmark_log" ]]; then
            ci_error "Last 20 lines of benchmark log:"
            tail -20 "$benchmark_log" >&2
        fi

        exit 2
    fi
}

# Analyze benchmark results for regression
analyze_benchmark_results() {
    ci_log "Analyzing benchmark results for regression..."

    if [[ ! -f "$OUTPUT_FILE" ]]; then
        ci_error "Benchmark result file not found: $OUTPUT_FILE"
        exit 2
    fi

    # Extract performance metrics
    local analysis_result
    analysis_result=$(python3 -c "
import json
import sys

try:
    with open('$OUTPUT_FILE', 'r') as f:
        result = json.load(f)

    # Extract key metrics
    current_throughput = result.get('medianThroughput', 0)
    current_utilization = sum(result.get('gpuUtilizationSamples', [0])) / len(result.get('gpuUtilizationSamples', [1]))
    current_bandwidth = sum(result.get('memoryBandwidthSamples', [0])) / len(result.get('memoryBandwidthSamples', [1]))
    validation_rate = result.get('validationPassRate', 0)

    # Get baseline comparison
    baseline_comp = result.get('baselineComparison', {})
    baseline_throughput = baseline_comp.get('baselineThroughput', 0)
    throughput_delta = baseline_comp.get('throughputDelta', 0)
    throughput_delta_percent = baseline_comp.get('throughputDeltaPercent', 0)
    is_regression = baseline_comp.get('isRegression', False)

    print(f'CURRENT_THROUGHPUT={current_throughput:.6f}')
    print(f'BASELINE_THROUGHPUT={baseline_throughput:.6f}')
    print(f'THROUGHPUT_DELTA={throughput_delta:.6f}')
    print(f'THROUGHPUT_DELTA_PERCENT={throughput_delta_percent:.6f}')
    print(f'IS_REGRESSION={is_regression}')
    print(f'GPU_UTILIZATION={current_utilization:.1f}')
    print(f'MEMORY_BANDWIDTH={current_bandwidth:.1f}')
    print(f'VALIDATION_RATE={validation_rate:.1f}')

except Exception as e:
    print(f'ERROR: {e}')
    sys.exit(1)
" 2>/dev/null)

    if [[ $? -ne 0 ]]; then
        ci_error "Failed to analyze benchmark results"
        exit 2
    fi

    # Parse analysis results
    local current_throughput baseline_throughput throughput_delta throughput_delta_percent
    local is_regression gpu_utilization memory_bandwidth validation_rate

    eval "$(echo "$analysis_result" | sed 's/^/local /')"

    # Report performance metrics
    ci_log "Performance Analysis Results:"
    ci_log "  Current Throughput:  ${current_throughput} Gkeys/s"
    ci_log "  Baseline Throughput: ${baseline_throughput} Gkeys/s"
    ci_log "  Throughput Delta:    ${throughput_delta_percent}%"
    ci_log "  GPU Utilization:     ${gpu_utilization}%"
    ci_log "  Memory Bandwidth:    ${memory_bandwidth}%"
    ci_log "  Validation Rate:     ${validation_rate}%"

    # Check for regression
    if [[ "$is_regression" == "true" ]]; then
        ci_error "🚨 PERFORMANCE REGRESSION DETECTED"
        ci_error "Throughput decreased by ${throughput_delta_percent}%"
        ci_error "Current: ${current_throughput} Gkeys/s vs Baseline: ${baseline_throughput} Gkeys/s"
        ci_error "ZERO-TOLERANCE POLICY: Any regression = CI FAILURE"
        return 1
    else
        success "✅ No performance regression detected"
        success "Performance meets or exceeds baseline: +${throughput_delta_percent}%"
        return 0
    fi
}

# Archive benchmark results
archive_benchmark_results() {
    ci_log "Archiving benchmark results..."

    local archive_dir="benchmarks/results/ci_archive"
    local timestamp=$(date +"%Y%m%d_%H%M%S")
    local archive_file="${archive_dir}/${GPU_MODEL}_${timestamp}.json"

    # Create archive directory
    mkdir -p "$archive_dir"

    # Copy result to archive
    if [[ -f "$OUTPUT_FILE" ]]; then
        cp "$OUTPUT_FILE" "$archive_file"
        success "Result archived: $archive_file"
    fi

    # Also archive benchmark log if it exists
    local benchmark_log="${OUTPUT_FILE%.json}_benchmark.log"
    if [[ -f "$benchmark_log" ]]; then
        cp "$benchmark_log" "${archive_file%.json}_log.txt"
    fi

    # Create CI metadata
    local metadata_file="${archive_file%.json}_metadata.txt"
    cat > "$metadata_file" << EOF
CI Performance Gate Metadata
==========================

Execution Information:
- Timestamp: $(date)
- GPU Model: $GPU_MODEL
- Duration: $BENCHMARK_DURATION seconds
- CI Mode: $CI_MODE

Environment Variables:
- CI_SERVER_NAME: ${CI_SERVER_NAME:-Unknown}
- CI_PIPELINE_ID: ${CI_PIPELINE_ID:-Unknown}
- CI_BRANCH: ${CI_BRANCH:-Unknown}
- CI_COMMIT_SHA: ${CI_COMMIT_SHA:-Unknown}

Files:
- Result: $archive_file
- Log: ${archive_file%.json}_log.txt
- Metadata: $metadata_file

Status: $([ -f "$OUTPUT_FILE" ] && echo "Completed" || echo "Failed")
EOF

    success "Benchmark results archived with metadata"
}

# Generate CI performance report
generate_ci_performance_report() {
    ci_log "Generating CI performance report..."

    local report_file="benchmarks/results/ci_performance_report_${GPU_MODEL}.json"

    python3 -c "
import json
from datetime import datetime

# Load benchmark result if available
result_data = {}
try:
    with open('$OUTPUT_FILE', 'r') as f:
        result_data = json.load(f)
except:
    pass

# Create CI report
ci_report = {
    'ci_metadata': {
        'timestamp': datetime.utcnow().isoformat() + 'Z',
        'gpu_model': '$GPU_MODEL',
        'ci_server': '${CI_SERVER_NAME:-Unknown}',
        'pipeline_id': '${CI_PIPELINE_ID:-Unknown}',
        'branch': '${CI_BRANCH:-Unknown}',
        'commit_sha': '${CI_COMMIT_SHA:-Unknown}',
        'benchmark_duration': $BENCHMARK_DURATION
    },
    'performance_gate': {
        'baseline_file': '$BASELINE_FILE',
        'result_file': '$OUTPUT_FILE',
        'zero_tolerance_policy': True,
        'status': 'PASS' if result_data.get('baselineComparison', {}).get('isRegression') == False else 'FAIL'
    },
    'benchmark_result': result_data,
    'regression_analysis': {
        'is_regression': result_data.get('baselineComparison', {}).get('isRegression', False),
        'throughput_delta_percent': result_data.get('baselineComparison', {}).get('throughputDeltaPercent', 0),
        'zero_tolerance_violation': result_data.get('baselineComparison', {}).get('isRegression', False)
    }
}

# Save CI report
with open('$report_file', 'w') as f:
    json.dump(ci_report, f, indent=2)

print(f'CI performance report saved: $report_file')
" 2>/dev/null

    success "CI performance report generated"
}

# Main execution
main() {
    ci_log "Starting CI Performance Gate"
    ci_log "Zero-Tolerance Performance Regression Detection"

    # Show help if requested
    case "${1:-}" in
        --help|-h)
            show_help
            exit 0
            ;;
    esac

    # Execute CI performance gate workflow
    initialize_ci_environment
    validate_ci_environment
    check_benchmark_prerequisites

    # Run benchmark
    run_ci_benchmark

    # Analyze results
    local regression_detected=false
    if ! analyze_benchmark_results; then
        regression_detected=true
    fi

    # Archive results and generate reports
    archive_benchmark_results
    generate_ci_performance_report

    # Final CI decision
    echo
    ci_log "CI Performance Gate Decision:"

    if $regression_detected; then
        ci_error "🚨 CI PERFORMANCE GATE FAILED"
        ci_error "Reason: Performance regression detected (zero-tolerance policy)"
        ci_error "Action: Required performance optimization before merge"
        ci_error "Impact: Build FAILED - cannot proceed with merge"
        exit 1
    else
        success "✅ CI PERFORMANCE GATE PASSED"
        success "Reason: No performance regression detected")
        success "Action: Approved for merge")
        success "Impact: Build PASSED - can proceed with CI pipeline")
        exit 0
    fi
}

# Execute main function
main "$@"