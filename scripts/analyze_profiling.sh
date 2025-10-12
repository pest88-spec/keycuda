#!/bin/bash

# Nsight Compute Profiling Automation Script
# Part of T042: Create Nsight Compute profiling automation script
#
# Usage: ./analyze_profiling.sh <gpu_device> <kernel_name> [executable] [output_dir]
# Example: ./analyze_profiling.sh 0 eccScalarMulKernel ./build/Puzzle71Solver ./profiling
#
# This script automates Nsight Compute profiling for CUDA kernels with:
# - Custom profiling sections (SpeedOfLight, MemoryWorkloadAnalysis, Occupancy)
# - Automatic metric extraction and threshold validation
# - CI integration support with exit code 1 for metric failures
# - Report archiving with timestamped filenames

set -euo pipefail

# Default values
GPU_DEVICE=${1:-0}
KERNEL_NAME=${2:-eccScalarMulKernel}
EXECUTABLE=${3:-./build/Puzzle71Solver}
OUTPUT_DIR=${4:-./benchmarks/profiling}
SECTION_SET="SpeedOfLight,MemoryWorkloadAnalysis,Occupancy"

# Validation thresholds (per specification)
COALESCING_THRESHOLD=90.0
BANK_CONFLICTS_THRESHOLD=5.0
OCCUPANCY_THRESHOLD=50.0

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

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

# Check dependencies
check_dependencies() {
    log "Checking dependencies..."

    # Check if Nsight Compute is available
    if ! command -v ncu &> /dev/null; then
        error "Nsight Compute (ncu) not found in PATH"
        error "Please install Nsight Compute: https://developer.nvidia.com/nsight-compute"
        exit 1
    fi

    # Check if executable exists
    if [[ ! -f "$EXECUTABLE" ]]; then
        error "Executable not found: $EXECUTABLE"
        error "Please build the project first"
        exit 1
    fi

    # Check if GPU device is available
    if ! nvidia-smi --id=$GPU_DEVICE --query-gpu=name --format=csv,noheader,nounits &> /dev/null; then
        error "GPU device $GPU_DEVICE not available"
        error "Available devices:"
        nvidia-smi --query-gpu=index,name --format=csv,noheader,nounits | head -5
        exit 1
    fi

    success "Dependencies check passed"
}

# Create output directory
setup_output_directory() {
    local timestamp=$(date +"%Y%m%d_%H%M%S")
    local profile_dir="${OUTPUT_DIR}/${KERNEL_NAME}_${GPU_DEVICE}_${timestamp}"

    mkdir -p "$profile_dir"
    echo "$profile_dir"
}

# Run Nsight Compute profiling
run_profiling() {
    local output_dir=$1
    local report_file="${output_dir}/${KERNEL_NAME}_${GPU_DEVICE}.ncu-rep"

    log "Starting Nsight Compute profiling..."
    log "GPU Device: $GPU_DEVICE"
    log "Kernel: $KERNEL_NAME"
    log "Executable: $EXECUTABLE"
    log "Output: $report_file"

    # Build NCU command
    local ncu_cmd="ncu"
    ncu_cmd+=" --target-processes all"
    ncu_cmd+=" --kernel-name ${KERNEL_NAME}"
    ncu_cmd+=" --section-set ${SECTION_SET}"
    ncu_cmd+=" --export ${report_file}"
    ncu_cmd+=" --force-overwrite"
    ncu_cmd+=" --replay-mode application"
    ncu_cmd+=" --clock-control none"
    ncu_cmd+=" --profile-from-start off"
    ncu_cmd+=" --profile-child-processes"
    ncu_cmd+=" --kernel-name-base function"

    # Add kernel filtering
    ncu_cmd+=" --kernel-name ${KERNEL_NAME}"

    # Add device specification
    ncu_cmd+=" --device ${GPU_DEVICE}"

    log "Executing: $ncu_cmd -- \"$EXECUTABLE\""

    # Run profiling
    if $ncu_cmd -- "$EXECUTABLE" > "${output_dir}/profiling.log" 2>&1; then
        success "Profiling completed successfully"
        return 0
    else
        error "Profiling failed"
        error "Check log file: ${output_dir}/profiling.log"
        return 1
    fi
}

# Extract metrics from NCU report
extract_metrics() {
    local report_file=$1

    log "Extracting metrics from profiling report..."

    # Extract key metrics using ncu-query
    local coalescing=$(ncu-query --query "metric(name:__global_load_efficiency_pct)" --format csv,noheader "$report_file" 2>/dev/null | head -1 | tr -d '%' || echo "0")
    local bank_conflicts=$(ncu-query --query "metric(name:__shared_bank_conflicts_per_request)" --format csv,noheader "$report_file" 2>/dev/null | head -1 || echo "0")
    local occupancy=$(ncu-query --query "metric(name:__sm_occupancy_pct)" --format csv,noheader "$report_file" 2>/dev/null | head -1 | tr -d '%' || echo "0")

    # Clean up and validate values
    coalescing=$(echo "$coalescing" | sed 's/[^0-9.]//g')
    bank_conflicts=$(echo "$bank_conflicts" | sed 's/[^0-9.]//g')
    occupancy=$(echo "$occupancy" | sed 's/[^0-9.]//g')

    # Set defaults if extraction failed
    [[ -z "$coalescing" || "$coalescing" == "0" ]] && coalescing="0.0"
    [[ -z "$bank_conflicts" || "$bank_conflicts" == "0" ]] && bank_conflicts="0.0"
    [[ -z "$occupancy" || "$occupancy" == "0" ]] && occupancy="0.0"

    echo "$coalescing,$bank_conflicts,$occupancy"
}

# Validate metrics against thresholds
validate_metrics() {
    local coalescing=$1
    local bank_conflicts=$2
    local occupancy=$3
    local validation_failed=0

    log "Validating metrics against thresholds..."
    log "Coalescing Efficiency: ${coalescing}% (threshold: ${COALESCING_THRESHOLD}%)"
    log "Shared Memory Bank Conflicts: ${bank_conflicts}% (threshold: ${BANK_CONFLICTS_THRESHOLD}%)"
    log "Occupancy: ${occupancy}% (threshold: ${OCCUPANCY_THRESHOLD}%)"

    # Validate coalescing efficiency
    if (( $(echo "$coalescing < $COALESCING_THRESHOLD" | bc -l) )); then
        error "Coalescing efficiency below threshold: ${coalescing}% < ${COALESCING_THRESHOLD}%"
        validation_failed=1
    else
        success "Coalescing efficiency passes threshold: ${coalescing}%"
    fi

    # Validate bank conflicts
    if (( $(echo "$bank_conflicts > $BANK_CONFLICTS_THRESHOLD" | bc -l) )); then
        error "Bank conflicts above threshold: ${bank_conflicts}% > ${BANK_CONFLICTS_THRESHOLD}%"
        validation_failed=1
    else
        success "Bank conflicts within threshold: ${bank_conflicts}%"
    fi

    # Validate occupancy
    if (( $(echo "$occupancy < $OCCUPANCY_THRESHOLD" | bc -l) )); then
        error "Occupancy below threshold: ${occupancy}% < ${OCCUPANCY_THRESHOLD}%"
        validation_failed=1
    else
        success "Occupancy passes threshold: ${occupancy}%"
    fi

    return $validation_failed
}

# Generate summary report
generate_summary() {
    local output_dir=$1
    local coalescing=$2
    local bank_conflicts=$3
    local occupancy=$4
    local report_file="${output_dir}/${KERNEL_NAME}_${GPU_DEVICE}.ncu-rep"

    local summary_file="${output_dir}/profiling_summary.txt"

    log "Generating summary report..."

    cat > "$summary_file" << EOF
Nsight Compute Profiling Summary
==================================

Profile Information:
- GPU Device: $GPU_DEVICE ($(nvidia-smi --id=$GPU_DEVICE --query-gpu=name --format=csv,noheader,nounits))
- Kernel: $KERNEL_NAME
- Executable: $EXECUTABLE
- Timestamp: $(date)
- Report File: $report_file

Performance Metrics:
- Coalescing Efficiency: ${coalescing}% (threshold: ${COALESCING_THRESHOLD}%)
- Shared Memory Bank Conflicts: ${bank_conflicts}% (threshold: ${BANK_CONFLICTS_THRESHOLD}%)
- Occupancy: ${occupancy}% (threshold: ${OCCUPANCY_THRESHOLD}%)

Threshold Validation:
EOF

    if validate_metrics "$coalescing" "$bank_conflicts" "$occupancy"; then
        echo "- Result: PASS - All metrics meet thresholds" >> "$summary_file"
        echo "- Status: Ready for production deployment" >> "$summary_file"
    else
        echo "- Result: FAIL - One or more metrics below thresholds" >> "$summary_file"
        echo "- Status: Requires optimization before deployment" >> "$summary_file"
    fi

    cat >> "$summary_file" << EOF

Recommendations:
EOF

    # Add recommendations based on metrics
    if (( $(echo "$coalescing < $COALESCING_THRESHOLD" | bc -l) )); then
        echo "- Improve memory access patterns for better coalescing" >> "$summary_file"
    fi

    if (( $(echo "$bank_conflicts > $BANK_CONFLICTS_THRESHOLD" | bc -l) )); then
        echo "- Reduce shared memory bank conflicts through padding or reorganization" >> "$summary_file"
    fi

    if (( $(echo "$occupancy < $OCCUPANCY_THRESHOLD" | bc -l) )); then
        echo "- Optimize register usage or block size for better occupancy" >> "$summary_file"
    fi

    success "Summary report generated: $summary_file"
}

# Main execution
main() {
    log "Starting Nsight Compute profiling automation"
    log "Arguments: GPU=$GPU_DEVICE, Kernel=$KERNEL_NAME, Executable=$EXECUTABLE"

    # Check dependencies
    check_dependencies

    # Setup output directory
    local output_dir
    output_dir=$(setup_output_directory)
    log "Output directory: $output_dir"

    # Run profiling
    local report_file="${output_dir}/${KERNEL_NAME}_${GPU_DEVICE}.ncu-rep"
    if ! run_profiling "$output_dir"; then
        error "Profiling execution failed"
        exit 1
    fi

    # Extract metrics
    local metrics
    metrics=$(extract_metrics "$report_file")

    # Parse metrics
    IFS=',' read -r coalescing bank_conflicts occupancy <<< "$metrics"

    # Validate metrics
    local validation_passed=true
    if ! validate_metrics "$coalescing" "$bank_conflicts" "$occupancy"; then
        validation_passed=false
    fi

    # Generate summary
    generate_summary "$output_dir" "$coalescing" "$bank_conflicts" "$occupancy"

    # Print final results
    echo
    log "Profiling completed!"
    echo "Results: Coalescing=${coalescing}%, Conflicts=${bank_conflicts}%, Occupancy=${occupancy}%"
    echo "Report: $report_file"
    echo "Summary: ${output_dir}/profiling_summary.txt"

    # Return appropriate exit code for CI integration
    if $validation_passed; then
        success "All metrics meet thresholds - exiting with code 0"
        exit 0
    else
        error "One or more metrics below thresholds - exiting with code 1 (for CI integration)"
        exit 1
    fi
}

# Help function
show_help() {
    cat << EOF
Nsight Compute Profiling Automation Script

USAGE:
    $0 <gpu_device> <kernel_name> [executable] [output_dir]

ARGUMENTS:
    gpu_device    GPU device ID (default: 0)
    kernel_name   CUDA kernel name to profile (default: eccScalarMulKernel)
    executable    Path to executable (default: ./build/Puzzle71Solver)
    output_dir    Output directory for reports (default: ./benchmarks/profiling)

EXAMPLES:
    # Profile eccScalarMulKernel on device 0
    $0 0 eccScalarMulKernel

    # Profile with custom executable
    $0 0 eccScalarMulKernel ./build/Puzzle71Solver

    # Specify output directory
    $0 0 eccScalarMulKernel ./build/Puzzle71Solver ./my_profiling

THRESHOLDS:
    - Coalescing Efficiency: ≥${COALESCING_THRESHOLD}%
    - Bank Conflicts: ≤${BANK_CONFLICTS_THRESHOLD}%
    - Occupancy: ≥${OCCUPANCY_THRESHOLD}%

EXIT CODES:
    0 - All metrics meet thresholds
    1 - One or more metrics below thresholds (for CI integration)

EOF
}

# Parse command line arguments
case "${1:-}" in
    -h|--help)
        show_help
        exit 0
        ;;
    *)
        main "$@"
        ;;
esac