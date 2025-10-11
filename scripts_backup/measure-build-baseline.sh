#!/bin/bash
#
# Build Time Baseline Measurement Script for Puzzle71Solver
#
# Measures current build time baseline for fresh checkout with comprehensive
# analysis of build phases, dependency resolution, and compilation metrics.
# Establishes baseline for 5-minute fresh checkout build target validation.
#
# @origin       https://github.com/Puzzle71Solver/Puzzle71Solver
# @origin_path  scripts/measure-build-baseline.sh
# @origin_commit <current_commit>
# @origin_license MIT
# @extracted_date   2025-10-10
# @extracted_by     Puzzle71Solver Team
# @modifications    Created for third-party dependency integration optimization
# @spdx_license_identifier MIT
#

set -euo pipefail

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
RESULTS_DIR="$PROJECT_ROOT/baseline_measurements"
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
MEASUREMENT_ID="baseline_$TIMESTAMP"
RESULTS_FILE="$RESULTS_DIR/${MEASUREMENT_ID}_build_time.json"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Logging function
log() {
    echo -e "${BLUE}[$(date '+%Y-%m-%d %H:%M:%S')]${NC} $1"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1" >&2
}

success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

# Phase timing tracking
declare -A PHASE_START_TIMES
declare -A PHASE_END_TIMES
declare -A PHASE_DURATIONS

# Record phase start
start_phase() {
    local phase="$1"
    log "Starting phase: $phase"
    PHASE_START_TIMES["$phase"]=$(date +%s.%N)
}

# Record phase end
end_phase() {
    local phase="$1"
    local end_time=$(date +%s.%N)
    PHASE_END_TIMES["$phase"]=$end_time

    if [[ -n "${PHASE_START_TIMES[$phase]:-}" ]]; then
        local start_time="${PHASE_START_TIMES[$phase]}"
        local duration=$(echo "$end_time - $start_time" | bc -l)
        PHASE_DURATIONS["$phase"]=$duration
        log "Phase $phase completed in ${duration}s"
    fi
}

# Create results directory
mkdir -p "$RESULTS_DIR"

# Initialize measurement data
cat > "$RESULTS_FILE" << EOF
{
    "measurement_id": "$MEASUREMENT_ID",
    "measurement_timestamp": "$(date -Iseconds)",
    "project_root": "$PROJECT_ROOT",
    "target_build_time_minutes": 5,
    "measurement_type": "fresh_checkout_build_baseline",
    "system_info": {
        "os": "$(uname -s)",
        "kernel": "$(uname -r)",
        "architecture": "$(uname -m)",
        "cpu_cores": "$(nproc)",
        "memory_gb": "$(free -g | awk 'NR==2{print $2}')",
        "disk_space_gb": "$(df -BG . | awk 'NR==2{print $4}' | tr -d 'G')"
    },
    "environment_info": {
        "cuda_version": "$(nvcc --version | grep release | awk '{print $6}' || echo 'N/A')",
        "gcc_version": "$(gcc --version | head -1 || echo 'N/A')",
        "cmake_version": "$(cmake --version | head -1 || echo 'N/A')",
        "git_version": "$(git --version || echo 'N/A')",
        "make_version": "$(make --version | head -1 || echo 'N/A')"
    },
    "phases": {},
    "build_configuration": "Release",
    "build_successful": false,
    "total_build_time_seconds": 0,
    "error_message": "",
    "artifacts": [],
    "success_criteria_validation": {
        "meets_5_minute_target": false,
        "target_seconds": 300,
        "actual_seconds": 0,
        "performance_score": 0.0
    }
}
EOF

# Phase 1: Fresh Checkout Simulation
start_phase "fresh_checkout_simulation"

log "Simulating fresh checkout environment..."
# Clean any existing build artifacts
if [[ -d "$BUILD_DIR" ]]; then
    log "Cleaning existing build directory..."
    rm -rf "$BUILD_DIR"
fi

# Clean any cache files
log "Cleaning cache files..."
find "$PROJECT_ROOT" -name "*.cache" -delete 2>/dev/null || true
find "$PROJECT_ROOT" -name "CMakeCache.txt" -delete 2>/dev/null || true
find "$PROJECT_ROOT" -name "CMakeFiles" -type d -exec rm -rf {} + 2>/dev/null || true

end_phase "fresh_checkout_simulation"

# Phase 2: Dependency Resolution
start_phase "dependency_resolution"

log "Analyzing dependency resolution..."

# Check git submodules
log "Checking git submodules..."
if git submodule status 2>/dev/null | grep -q .; then
    log "Git submodules detected:"
    git submodule status
else
    log "No git submodules found"
fi

# Check CMake dependencies
log "Checking CMake dependencies..."
if [[ -f "$PROJECT_ROOT/CMakeLists.txt" ]]; then
    # Count FetchContent dependencies
    fetchcontent_count=$(grep -c "FetchContent_Declare" "$PROJECT_ROOT/CMakeLists.txt" 2>/dev/null || echo "0")
    log "FetchContent dependencies: $fetchcontent_count"

    # Count find_package dependencies
    findpackage_count=$(grep -c "find_package" "$PROJECT_ROOT/CMakeLists.txt" 2>/dev/null || echo "0")
    log "find_package dependencies: $findpackage_count"
fi

end_phase "dependency_resolution"

# Phase 3: Build Configuration
start_phase "build_configuration"

log "Configuring build environment..."

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Run CMake configuration
log "Running CMake configuration..."
if cmake .. -DCMAKE_BUILD_TYPE=Release -DOFFLINE_BUILD=ON > cmake_output.log 2>&1; then
    success "CMake configuration successful"
else
    error "CMake configuration failed"
    tail -20 cmake_output.log
    exit 1
fi

end_phase "build_configuration"

# Phase 4: Compilation
start_phase "compilation"

log "Starting compilation phase..."

# Record compile start time
compile_start=$(date +%s.%N)

# Run compilation with timing
if make -j$(nproc) > compile_output.log 2>&1; then
    success "Compilation successful"
else
    error "Compilation failed"
    tail -20 compile_output.log
    exit 1
fi

# Record compile end time
compile_end=$(date +%s.%N)
compile_duration=$(echo "$compile_end - $compile_start" | bc -l)

end_phase "compilation"

# Phase 5: Linking
start_phase "linking"

log "Linking phase..."
# Note: Linking is typically part of compilation in make, but we'll track it separately if possible
# For this baseline, we'll use a subset of compilation time as linking time
linking_duration=$(echo "$compile_duration * 0.2" | bc -l)
log "Linking estimated at ${linking_duration}s"

end_phase "linking"

# Phase 6: Artifact Collection
start_phase "artifact_collection"

log "Collecting build artifacts..."

# Collect binary artifacts
artifacts=()
if [[ -f "$BUILD_DIR/Puzzle71Solver" ]]; then
    artifacts+=("$BUILD_DIR/Puzzle71Solver")
    log "Main executable found: Puzzle71Solver"
fi

# Find library artifacts
for lib in "$BUILD_DIR"/*.so "$BUILD_DIR"/*.a; do
    if [[ -f "$lib" ]]; then
        artifacts+=("$lib")
        log "Library artifact found: $(basename "$lib")"
    fi
done

# Calculate total artifact size
total_size=0
for artifact in "${artifacts[@]}"; do
    if [[ -f "$artifact" ]]; then
        size=$(stat -c%s "$artifact" 2>/dev/null || echo "0")
        total_size=$((total_size + size))
    fi
done

log "Total artifact size: $((total_size / 1024 / 1024)) MB"

end_phase "artifact_collection"

# Phase 7: Post-Build Validation
start_phase "post_build_validation"

log "Running post-build validation..."

# Test executable if it exists
if [[ -f "$BUILD_DIR/Puzzle71Solver" ]]; then
    log "Testing executable..."
    if timeout 10s "$BUILD_DIR/Puzzle71Solver" --version > /dev/null 2>&1; then
        success "Executable validation passed"
    else
        warning "Executable validation failed or timeout"
    fi
fi

end_phase "post_build_validation"

# Calculate total build time
total_build_time=0
for phase in "${!PHASE_DURATIONS[@]}"; do
    if [[ -n "${PHASE_DURATIONS[$phase]:-}" ]]; then
        total_build_time=$(echo "$total_build_time + ${PHASE_DURATIONS[$phase]}" | bc -l)
    fi
done

# Success criteria validation
target_seconds=300
meets_target=false
if (( $(echo "$total_build_time <= $target_seconds" | bc -l) )); then
    meets_target=true
fi

performance_score=0
if (( $(echo "$target_seconds > 0" | bc -l) )); then
    performance_score=$(echo "scale=2; ($target_seconds - $total_build_time) / $target_seconds * 100" | bc -l)
    if (( $(echo "$performance_score < 0" | bc -l) )); then
        performance_score=0
    fi
fi

# Update results with final data
tmp_file=$(mktemp)
jq --arg total_time "$total_build_time" \
   --arg target_time "$target_seconds" \
   --arg meets_target "$meets_target" \
   --arg score "$performance_score" \
   --arg success "true" \
   --argjson artifacts "$(printf '%s\n' "${artifacts[@]}" | jq -R . | jq -s .)" \
   --argjson phases "$(cat << EOF
{
EOF
for phase in "${!PHASE_DURATIONS[@]}"; do
    if [[ -n "${PHASE_DURATIONS[$phase]:-}" ]]; then
        cat << EOF
    "$phase": {
        "start_time": ${PHASE_START_TIMES[$phase]},
        "end_time": ${PHASE_END_TIMES[$phase]},
        "duration_seconds": ${PHASE_DURATIONS[$phase]}
    },
EOF
    fi
done
cat << EOF
    "compilation_details": {
        "start_time": $compile_start,
        "end_time": $compile_end,
        "duration_seconds": $compile_duration
    }
}
EOF
)" \
'.total_build_time_seconds = ($total_time | tonumber) |
 .success_criteria_validation.meets_5_minute_target = ($meets_target | test("true")) |
 .success_criteria_validation.actual_seconds = ($total_time | tonumber) |
 .success_criteria_validation.performance_score = ($score | tonumber) |
 .build_successful = ($success | test("true")) |
 .artifacts = $artifacts |
 .phases = $phases' \
"$RESULTS_FILE" > "$tmp_file" && mv "$tmp_file" "$RESULTS_FILE"

# Generate summary report
log "Generating baseline measurement report..."

summary_file="$RESULTS_DIR/${MEASUREMENT_ID}_summary.txt"
cat > "$summary_file" << EOF
Build Time Baseline Measurement Report
======================================

Measurement ID: $MEASUREMENT_ID
Timestamp: $(date)
Target: 5-minute fresh checkout build

SYSTEM INFORMATION
-----------------
OS: $(uname -s) $(uname -r)
Architecture: $(uname -m)
CPU Cores: $(nproc)
Memory: $(free -h | awk 'NR==2{print $2}')B
Disk Space: $(df -h . | awk 'NR==2{print $4}')

BUILD ENVIRONMENT
------------------
CUDA Version: $(nvcc --version | grep release | awk '{print $6}' || echo 'N/A')
GCC Version: $(gcc --version | head -1 || echo 'N/A')
CMake Version: $(cmake --version | head -1 || echo 'N/A')
Git Version: $(git --version || echo 'N/A')

PHASE BREAKDOWN
---------------
EOF

for phase in "${!PHASE_DURATIONS[@]}"; do
    if [[ -n "${PHASE_DURATIONS[$phase]:-}" ]]; then
        printf "%-25s: %8.2f seconds\n" "$phase" "${PHASE_DURATIONS[$phase]}" >> "$summary_file"
    fi
done

cat >> "$summary_file" << EOF

TOTAL BUILD TIME: ${total_build_time} seconds
TARGET TIME:     ${target_seconds} seconds
RESULT:          $([ "$meets_target" = "true" ] && echo "PASSES ✅" || echo "FAILS ❌")
PERFORMANCE:     ${performance_score}% score

SUCCESS CRITERIA VALIDATION
---------------------------
5-minute target:      $([ "$meets_target" = "true" ] && echo "MET" || echo "NOT MET")
Performance score:     ${performance_score}/100
Build status:           SUCCESSFUL

ARTIFACTS
---------
EOF

for artifact in "${artifacts[@]}"; do
    if [[ -f "$artifact" ]]; then
        size=$(stat -c%s "$artifact" 2>/dev/null || echo "0")
        printf "%-30s: %8d bytes\n" "$(basename "$artifact")" "$size" >> "$summary_file"
    fi
done

cat >> "$summary_file" << EOF

RECOMMENDATIONS
---------------
$([ "$meets_target" = "true" ] && echo "✅ Build time meets 5-minute target baseline" || echo "❌ Build time exceeds 5-minute target - optimization needed")
Performance score: ${performance_score}% $([ "$(echo "$performance_score >= 80" | bc -l)" = "1" ] && echo "- Good baseline" || echo "- Improvement opportunity")
Next steps: Use this baseline to measure optimization progress

EOF

success "Build time baseline measurement completed!"
log "Results saved to: $RESULTS_FILE"
log "Summary report: $summary_file"

# Display summary
echo ""
echo "=== BUILD TIME BASELINE SUMMARY ==="
echo "Total Build Time: ${total_build_time} seconds"
echo "Target: ${target_seconds} seconds"
echo "Result: $([ "$meets_target" = "true" ] && echo "✅ PASSES 5-minute target" || echo "❌ FAILS 5-minute target")"
echo "Performance Score: ${performance_score}%"
echo "Artifacts Created: ${#artifacts[@]}"
echo "Detailed Report: $summary_file"
echo "================================"

# Exit with success if target met, failure otherwise
if [[ "$meets_target" = "true" ]]; then
    exit 0
else
    exit 1
fi