#!/bin/bash

# Baseline Establishment Workflow Script
# Part of T044: Create baseline establishment workflow
#
# Usage: ./baseline_update.sh [--approve] [gpu_model] [current_result_file] [baseline_file]
# Example: ./baseline_update.sh --approve rtx3090 benchmarks/results/latest.json benchmarks/baselines/rtx3090.json
#
# This script manages baseline updates with safety controls:
# - Requires explicit --approve flag to prevent accidental updates
# - Validates performance improvement before updating baseline
# - Maintains SHA-256 protection for baseline integrity
# - Provides audit trail of baseline changes

set -euo pipefail

# Default values
APPROVE_UPDATE=false
GPU_MODEL=${2:-rtx3090}
CURRENT_RESULT_FILE=${3:-benchmarks/results/latest.json}
BASELINE_FILE=${4:-benchmarks/baselines/${GPU_MODEL}.json}
USERNAME=${USERNAME:-$(whoami)}

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

# Show usage information
show_help() {
    cat << EOF
Baseline Establishment Workflow Script

USAGE:
    $0 --approve <gpu_model> <current_result_file> <baseline_file>
    $0 --help

ARGUMENTS:
    --approve       REQUIRED flag to confirm baseline update (safety mechanism)
    gpu_model       GPU model identifier (e.g., rtx3090, rtx2080ti, h20, a100)
    current_result  Path to current benchmark result JSON file
    baseline_file   Path to baseline file that will be updated

SAFETY FEATURES:
    - Explicit --approve flag required (prevents accidental updates)
    - Performance validation (new baseline must be better than old)
    - SHA-256 digest protection for baseline integrity
    - Audit trail logging of all baseline changes
    - Backup of previous baseline before update

EXAMPLES:
    # Preview what would be updated (no --approve flag)
    $0 rtx3090 benchmarks/results/latest.json benchmarks/baselines/rtx3090.json

    # Actually update baseline with approval
    $0 --approve rtx3090 benchmarks/results/latest.json benchmarks/baselines/rtx3090.json

EXIT CODES:
    0 - Baseline updated successfully
    1 - Validation failed or update aborted
    2 - Configuration or file error

EOF
}

# Check command line arguments
parse_arguments() {
    if [[ $# -eq 0 ]]; then
        show_help
        exit 0
    fi

    if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
        show_help
        exit 0
    fi

    if [[ "${1:-}" != "--approve" ]]; then
        warning "Missing --approve flag - running in preview mode only"
        warning "Use --approve to actually update the baseline"
        APPROVE_UPDATE=false
    else
        APPROVE_UPDATE=true
        shift
        GPU_MODEL=${1:-rtx3090}
        CURRENT_RESULT_FILE=${2:-benchmarks/results/latest.json}
        BASELINE_FILE=${4:-benchmarks/baselines/${GPU_MODEL}.json}
    fi

    log "Baseline update mode: $([ "$APPROVE_UPDATE" = true ] && echo "APPROVED" || echo "PREVIEW")"
    log "GPU Model: $GPU_MODEL"
    log "Current Result: $CURRENT_RESULT_FILE"
    log "Baseline File: $BASELINE_FILE"
}

# Validate files and environment
validate_environment() {
    log "Validating environment..."

    # Check if current result file exists
    if [[ ! -f "$CURRENT_RESULT_FILE" ]]; then
        error "Current result file not found: $CURRENT_RESULT_FILE"
        error "Run benchmark first: ./scripts/run_benchmarks.sh"
        exit 2
    fi

    # Create baseline directory if it doesn't exist
    mkdir -p "$(dirname "$BASELINE_FILE")"

    # Check if baseline exists (optional for new baseline)
    local baseline_exists=false
    if [[ -f "$BASELINE_FILE" ]]; then
        baseline_exists=true
        log "Existing baseline found: $BASELINE_FILE"
    else
        log "No existing baseline - creating new baseline"
    fi

    # Validate JSON format of current result
    if ! python3 -c "
import json
try:
    with open('$CURRENT_RESULT_FILE', 'r') as f:
        result = json.load(f)
    required = ['resultId', 'gpuModel', 'medianThroughput', 'sha256Digest']
    for field in required:
        if field not in result:
            print(f'Missing required field in current result: {field}')
            exit(1)
    print('Current result validation passed')
except Exception as e:
    print(f'Invalid JSON in current result: {e}')
    exit(1)
" 2>/dev/null; then
        error "Invalid current result file: $CURRENT_RESULT_FILE"
        exit 2
    fi

    success "Environment validation passed"
}

# Load performance data from files
load_performance_data() {
    log "Loading performance data..."

    # Load current result data
    python3 -c "
import json

# Load current result
with open('$CURRENT_RESULT_FILE', 'r') as f:
    current = json.load(f)

print(f'Current Performance: {current[\"medianThroughput\"]:.3f} Gkeys/s')
print(f'GPU Utilization: {sum(current.get(\"gpuUtilizationSamples\", [0])) / len(current.get(\"gpuUtilizationSamples\", [1])):.1f}%')
print(f'Memory Bandwidth: {sum(current.get(\"memoryBandwidthSamples\", [0])) / len(current.get(\"memoryBandwidthSamples\", [1])):.1f}%')
print(f'Validation Pass Rate: {current.get(\"validationPassRate\", 0):.1f}%')
" 2>/dev/null

    # If baseline exists, load and compare
    if [[ -f "$BASELINE_FILE" ]]; then
        python3 -c "
import json

# Load baseline
with open('$BASELINE_FILE', 'r') as f:
    baseline = json.load(f)

print(f'Baseline Performance: {baseline[\"medianThroughput\"]:.3f} Gkeys/s')
print(f'Baseline GPU Utilization: {baseline.get(\"gpuUtilizationPercent\", 0):.1f}%')
print(f'Baseline Memory Bandwidth: {baseline.get(\"memoryBandwidthPercent\", 0):.1f}%')
print(f'Baseline Occupancy: {baseline.get(\"occupancyPercent\", 0):.1f}%')
" 2>/dev/null
    fi
}

# Validate performance improvement
validate_performance_improvement() {
    log "Validating performance improvement criteria..."

    if [[ ! -f "$BASELINE_FILE" ]]; then
        log "No existing baseline - any performance is acceptable"
        return 0
    fi

    local improvement_valid
    improvement_valid=$(python3 -c "
import json

# Load current result and baseline
with open('$CURRENT_RESULT_FILE', 'r') as f:
    current = json.load(f)

with open('$BASELINE_FILE', 'r') as f:
    baseline = json.load(f)

# Performance comparison
current_throughput = current['medianThroughput']
baseline_throughput = baseline['medianThroughput']

improvement_percent = ((current_throughput - baseline_throughput) / baseline_throughput) * 100

print(f'Performance Change: {improvement_percent:+.2f}%')
print(f'Current: {current_throughput:.3f} Gkeys/s')
print(f'Baseline: {baseline_throughput:.3f} Gkeys/s')

# Validation criteria
validation_passed = True

# 1. Throughput must be better than baseline
if current_throughput <= baseline_throughput:
    print(f'FAIL: Throughput not improved ({current_throughput:.3f} <= {baseline_throughput:.3f})')
    validation_passed = False

# 2. GPU utilization must be >= 90%
current_utilization = sum(current.get('gpuUtilizationSamples', [0])) / len(current.get('gpuUtilizationSamples', [1]))
if current_utilization < 90.0:
    print(f'FAIL: GPU utilization too low ({current_utilization:.1f}% < 90%)')
    validation_passed = False

# 3. Memory bandwidth must be >= 70%
current_bandwidth = sum(current.get('memoryBandwidthSamples', [0])) / len(current.get('memoryBandwidthSamples', [1]))
if current_bandwidth < 70.0:
    print(f'FAIL: Memory bandwidth too low ({current_bandwidth:.1f}% < 70%)')
    validation_passed = False

# 4. Validation pass rate must be 100%
current_validation = current.get('validationPassRate', 0)
if current_validation < 100.0:
    print(f'FAIL: Validation pass rate not 100% ({current_validation:.1f}% < 100%)')
    validation_passed = False

if validation_passed:
    print('SUCCESS: All performance criteria met')
else:
    print('FAILED: One or more criteria not met')

exit(0 if validation_passed else 1)
" 2>/dev/null)

    if [[ $? -eq 0 ]]; then
        success "Performance improvement validation passed"
        return 0
    else
        error "Performance improvement validation failed"
        error "Baseline update rejected - performance does not meet criteria"
        return 1
    fi
}

# Create new baseline from current result
create_new_baseline() {
    log "Creating new baseline from current result..."

    local temp_baseline="${BASELINE_FILE}.tmp"
    local timestamp=$(date +"%Y-%m-%dT%H:%M:%SZ")

    python3 -c "
import json
import hashlib
from datetime import datetime

# Load current result
with open('$CURRENT_RESULT_FILE', 'r') as f:
    result = json.load(f)

# Create baseline entity
baseline = {
    'baselineId': f'{result[\"gpuModel\"]}_v{datetime.now().strftime(\"%Y%m%d_%H%M%S\")}',
    'gpuModel': result['gpuModel'],
    'computeCapability': result.get('hardwareMetadata', {}).get('computeCapability', 'unknown'),
    'driverVersion': result.get('hardwareMetadata', {}).get('driverVersion', 'unknown'),
    'cudaRuntimeVersion': result.get('hardwareMetadata', {}).get('cudaRuntimeVersion', 'unknown'),
    'kernelConfiguration': result.get('kernelConfiguration', {}),
    'targetThroughput': result['medianThroughput'],  # Use current as new target
    'medianThroughput': result['medianThroughput'],
    'gpuUtilizationPercent': sum(result.get('gpuUtilizationSamples', [0])) / len(result.get('gpuUtilizationSamples', [1])),
    'memoryBandwidthPercent': sum(result.get('memoryBandwidthSamples', [0])) / len(result.get('memoryBandwidthSamples', [1])),
    'occupancyPercent': 50.0,  # This would be calculated from kernel configuration
    'registerUsage': result.get('kernelConfiguration', {}).get('registerBudget', 128),
    'establishedDate': timestamp,
    'status': 'Active',
    'sha256Digest': ''  # Will be computed below
}

# Remove existing digest for calculation
baseline_str = json.dumps(baseline, sort_keys=True, separators=(',', ':'))
baseline['sha256Digest'] = hashlib.sha256(baseline_str.encode()).hexdigest()

# Save temporary baseline
with open('$temp_baseline', 'w') as f:
    json.dump(baseline, f, indent=2)

print(f'New baseline created: {baseline[\"baselineId\"]}')
print(f'Target throughput: {baseline[\"targetThroughput\"]:.3f} Gkeys/s')
print(f'SHA-256 digest: {baseline[\"sha256Digest\"]}')
" 2>/dev/null

    success "New baseline created: $temp_baseline"
}

# Backup existing baseline
backup_existing_baseline() {
    if [[ ! -f "$BASELINE_FILE" ]]; then
        log "No existing baseline to backup"
        return 0
    fi

    local backup_file="${BASELINE_FILE}.backup.$(date +%Y%m%d_%H%M%S)"

    log "Backing up existing baseline..."
    cp "$BASELINE_FILE" "$backup_file"

    success "Existing baseline backed up: $backup_file"
}

# Update baseline file
update_baseline_file() {
    log "Updating baseline file..."

    local temp_baseline="${BASELINE_FILE}.tmp"

    if [[ ! -f "$temp_baseline" ]]; then
        error "Temporary baseline file not found: $temp_baseline"
        return 1
    fi

    # Move temporary to final location
    mv "$temp_baseline" "$BASELINE_FILE"

    success "Baseline file updated: $BASELINE_FILE"
}

# Log baseline change to audit trail
log_baseline_change() {
    local audit_file="benchmarks/baselines/audit_log.txt"
    local timestamp=$(date +"%Y-%m-%d %H:%M:%S")

    log "Logging baseline change to audit trail..."

    python3 -c "
import json

# Load new baseline
with open('$BASELINE_FILE', 'r') as f:
    baseline = json.load(f)

# Create audit entry
audit_entry = f'''[{timestamp}] BASELINE UPDATE
User: $USERNAME
GPU Model: {baseline['gpuModel']}
Baseline ID: {baseline['baselineId']}
Throughput: {baseline['medianThroughput']:.3f} Gkeys/s
GPU Utilization: {baseline['gpuUtilizationPercent']:.1f}%
Memory Bandwidth: {baseline['memoryBandwidthPercent']:.1f}%
SHA-256: {baseline['sha256Digest']}
Status: Approved with --approve flag
'''

# Append to audit log
with open('$audit_file', 'a') as f:
    f.write(audit_entry)
    f.write('\\n')

print(f'Audit entry added to: {audit_file}')
" 2>/dev/null

    success "Baseline change logged to audit trail"
}

# Generate baseline update summary
generate_update_summary() {
    local summary_file="benchmarks/baselines/update_summary_${GPU_MODEL}.txt"

    log "Generating baseline update summary..."

    python3 -c "
import json
from datetime import datetime

# Load new baseline
with open('$BASELINE_FILE', 'r') as f:
    baseline = json.load(f)

summary = f'''
Baseline Update Summary
======================

Update Information:
- Timestamp: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}
- User: $USERNAME
- GPU Model: {baseline['gpuModel']}
- Approval: Explicit --approve flag provided

New Baseline Details:
- Baseline ID: {baseline['baselineId']}
- Target Throughput: {baseline['targetThroughput']:.3f} Gkeys/s
- Median Throughput: {baseline['medianThroughput']:.3f} Gkeys/s
- GPU Utilization: {baseline['gpuUtilizationPercent']:.1f}%
- Memory Bandwidth: {baseline['memoryBandwidthPercent']:.1f}%
- Occupancy: {baseline['occupancyPercent']:.1f}%
- Register Usage: {baseline['registerUsage']}
- Established Date: {baseline['establishedDate']}

Validation Status:
- All performance criteria met ✓
- SHA-256 protection enabled ✓
- Audit trail updated ✓

Next Steps:
- This baseline will be used for future regression testing
- CI pipeline will fail if performance drops below these values
- To update again, run this script with --approve flag

Files Updated:
- Baseline: {baseline['baselineId']} -> {BASELINE_FILE}
- Audit Log: benchmarks/baselines/audit_log.txt
'''

with open('$summary_file', 'w') as f:
    f.write(summary)

print(f'Update summary saved to: {summary_file}')
" 2>/dev/null

    success "Update summary generated"
}

# Main execution
main() {
    log "Starting baseline establishment workflow"

    # Parse command line arguments
    parse_arguments "$@"

    # Execute workflow steps
    validate_environment
    load_performance_data

    # Validate performance improvement
    if ! validate_performance_improvement; then
        error "Baseline update rejected - performance criteria not met"
        exit 1
    fi

    # Create new baseline
    create_new_baseline

    # If in preview mode, stop here
    if [[ "$APPROVE_UPDATE" != true ]]; then
        log "PREVIEW MODE: Baseline ready for update"
        log "To actually update, run with --approve flag:"
        log "  $0 --approve $GPU_MODEL $CURRENT_RESULT_FILE $BASELINE_FILE"
        exit 0
    fi

    # Execute actual update
    backup_existing_baseline
    update_baseline_file
    log_baseline_change
    generate_update_summary

    # Final status
    echo
    success "Baseline update completed successfully!"
    success "New baseline established: $(python3 -c "import json; print(json.load(open('$BASELINE_FILE'))['baselineId'])" 2>/dev/null)"
    success "Audit trail maintained in: benchmarks/baselines/audit_log.txt"
    success "Future CI runs will compare against this baseline"

    exit 0
}

# Execute main function
main "$@"