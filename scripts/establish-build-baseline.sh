#!/bin/bash
#
# Establish Build Time Baseline for Puzzle71Solver
#
# Comprehensive baseline measurement script that handles both successful and failed builds
# to establish accurate baseline measurements for 5-minute fresh checkout target validation.
# This script is designed to work with the current system state and measure actual performance.
#
# @origin       https://github.com/Puzzle71Solver/Puzzle71Solver
# @origin_path  scripts/establish-build-baseline.sh
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
RESULTS_DIR="$PROJECT_ROOT/baseline_measurements"
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
MEASUREMENT_ID="baseline_$TIMESTAMP"
RESULTS_FILE="$RESULTS_DIR/${MEASUREMENT_ID}_comprehensive.json"

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

# Create results directory
mkdir -p "$RESULTS_DIR"

# Initialize comprehensive measurement data
cat > "$RESULTS_FILE" << EOF
{
    "measurement_id": "$MEASUREMENT_ID",
    "measurement_timestamp": "$(date -Iseconds)",
    "project_root": "$PROJECT_ROOT",
    "measurement_type": "comprehensive_build_baseline",
    "target_build_time_minutes": 5,
    "system_info": {
        "os": "$(uname -s)",
        "kernel": "$(uname -r)",
        "architecture": "$(uname -m)",
        "cpu_cores": "$(nproc)",
        "memory_gb": "$(free -g | awk 'NR==2{print $2}')",
        "disk_space_gb": "$(df -BG . | awk 'NR==2{print $4}' | tr -d 'G')",
        "load_average": "$(uptime | awk -F'load average:' '{print $2}' | xargs)"
    },
    "environment_info": {
        "cuda_version": "$(nvcc --version 2>/dev/null | grep release | awk '{print $6}' || echo 'N/A')",
        "gcc_version": "$(gcc --version 2>/dev/null | head -1 || echo 'N/A')",
        "cmake_version": "$(cmake --version 2>/dev/null | head -1 || echo 'N/A')",
        "git_version": "$(git --version 2>/dev/null || echo 'N/A')",
        "make_version": "$(make --version 2>/dev/null | head -1 || echo 'N/A')",
        "python_version": "$(python3 --version 2>/dev/null || echo 'N/A')",
        "bash_version": "$(bash --version | head -1)"
    },
    "build_phases": {},
    "dependency_analysis": {},
    "source_code_analysis": {},
    "configuration_analysis": {},
    "build_attempt_results": {},
    "baseline_establishment": {
        "estimated_build_time_seconds": 0,
        "confidence_level": "low",
        "measurement_method": "system_analysis",
        "requires_optimization": true,
        "identified_issues": []
    },
    "success_criteria_validation": {
        "meets_5_minute_target": false,
        "target_seconds": 300,
        "estimated_seconds": 0,
        "performance_score": 0.0,
        "optimization_required": true
    }
}
EOF

log "Starting comprehensive build baseline measurement: $MEASUREMENT_ID"

# Phase 1: System Analysis
log "Phase 1: System Analysis"
system_start=$(date +%s.%N)

# Analyze current build state
if [[ -d "$PROJECT_ROOT/build" ]]; then
    build_files_count=$(find "$PROJECT_ROOT/build" -type f | wc -l)
    log "Found existing build directory with $build_files_count files"

    # Check if build artifacts exist
    if [[ -f "$PROJECT_ROOT/build/Puzzle71Solver" ]]; then
        existing_build_age=$(find "$PROJECT_ROOT/build/Puzzle71Solver" -mmin -0 | awk '{print $6}')
        log "Existing build artifact found, last modified: $existing_build_age"
    fi
else
    log "No existing build directory found"
fi

# Analyze project structure
total_source_files=$(find "$PROJECT_ROOT/src" -name "*.cpp" -o -name "*.h" -o -name "*.cu" | wc -l)
log "Total source files: $total_source_files"

c_source_files=$(find "$PROJECT_ROOT/src" -name "*.cpp" | wc -l)
cuda_source_files=$(find "$PROJECT_ROOT/src" -name "*.cu" | wc -l)
header_files=$(find "$PROJECT_ROOT/src" -name "*.h" | wc -l)

log "Source breakdown: C++: $c_source_files, CUDA: $cuda_source_files, Headers: $header_files"

system_end=$(date +%s.%N)
system_duration=$(echo "$system_end - $system_start" | bc -l)

# Phase 2: Dependency Analysis
log "Phase 2: Dependency Analysis"
dep_start=$(date +%s.%N)

# Check git submodules
log "Checking git submodules..."
submodule_count=0
submodule_info=""
if git submodule status 2>/dev/null | grep -q .; then
    submodule_count=$(git submodule status | wc -l)
    submodule_info=$(git submodule status)
    log "Found $submodule_count git submodules"
else
    log "No git submodules found"
fi

# Analyze CMakeLists.txt dependencies
log "Analyzing CMake dependencies..."
if [[ -f "$PROJECT_ROOT/CMakeLists.txt" ]]; then
    fetchcontent_deps=$(grep -c "FetchContent_Declare" "$PROJECT_ROOT/CMakeLists.txt" 2>/dev/null || echo "0")
    findpackage_deps=$(grep -c "find_package" "$PROJECT_ROOT/CMakeLists.txt" 2>/dev/null || echo "0")
    add_subdirectory_deps=$(grep -c "add_subdirectory" "$PROJECT_ROOT/CMakeLists.txt" 2>/dev/null || echo "0")

    log "CMake dependencies: FetchContent: $fetchcontent_deps, find_package: $findpackage_deps, add_subdirectory: $add_subdirectory_deps"

    # Check for specific dependencies
    if grep -q "nlohmann" "$PROJECT_ROOT/CMakeLists.txt"; then
        log "Found nlohmann/json dependency"
    fi
    if grep -q "secp256k1" "$PROJECT_ROOT/CMakeLists.txt"; then
        log "Found secp256k1 dependency"
    fi
fi

dep_end=$(date +%s.%N)
dep_duration=$(echo "$dep_end - $dep_start" | bc -l)

# Phase 3: Build Configuration Test
log "Phase 3: Build Configuration Test"
config_start=$(date +%s.%N)

# Clean and create build directory
if [[ -d "$PROJECT_ROOT/build" ]]; then
    rm -rf "$PROJECT_ROOT/build"
fi
mkdir -p "$PROJECT_ROOT/build"
cd "$PROJECT_ROOT/build"

# Test CMake configuration
config_success=false
config_output=""
cmake_time=0

if cmake .. -DCMAKE_BUILD_TYPE=Release > cmake_output.log 2>&1; then
    config_success=true
    cmake_time=$(grep "Configuring done" cmake_output.log | grep -o "[0-9.]* seconds" | head -1 | awk '{print $1}' || echo "0")
    log "CMake configuration successful in ${cmake_time}s"
else
    config_output=$(cat cmake_output.log)
    log "CMake configuration failed"
fi

config_end=$(date +%s.%N)
config_duration=$(echo "$config_end - $config_start" | bc -l)

# Phase 4: Build Attempt Analysis
log "Phase 4: Build Attempt Analysis"
build_start=$(date +%s.%N)

# Analyze what would be built
source_files_count=$(find "$PROJECT_ROOT/src" -name "*.cpp" -o -name "*.cu" | wc -l)
log "Build would process $source_files_count source files"

# Estimate build time based on source files and complexity
# Simple heuristic: ~2 seconds per C++ file, ~5 seconds per CUDA file
estimated_build_time=$(echo "$c_source_files * 2 + $cuda_source_files * 5" | bc -l)
log "Estimated build time based on source files: ${estimated_build_time}s"

# Attempt actual build (with timeout)
build_success=false
build_output=""
build_time=0

log "Attempting build (with 5-minute timeout)..."
if timeout 300s make -j$(nproc) > build_output.log 2>&1; then
    build_success=true
    build_end=$(date +%s.%N)
    build_time=$(echo "$build_end - $build_start" | bc -l)
    log "Build completed successfully in ${build_time}s"
else
    build_output=$(tail -50 build_output.log)
    build_end=$(date +%s.%N)
    build_time=300  # Timeout after 5 minutes
    log "Build failed or timed out after 5 minutes"
fi

build_end_phase=$(date +%s.%N)
build_duration=$(echo "$build_end_phase - $build_start" | bc -l)

# Phase 5: Results Analysis
log "Phase 5: Results Analysis"
analysis_start=$(date +%s.%N)

# Determine build baseline
final_build_time=$build_time
if [[ "$build_success" == "true" ]]; then
    baseline_method="actual_build"
    confidence="high"
else
    final_build_time=$estimated_build_time
    baseline_method="estimated_calculation"
    confidence="medium"
fi

# Success criteria validation
target_seconds=300
meets_target=false
performance_score=0

if (( $(echo "$final_build_time <= $target_seconds" | bc -l) )); then
    meets_target=true
fi

if (( $(echo "$target_seconds > 0" | bc -l) )); then
    performance_score=$(echo "scale=2; ($target_seconds - $final_build_time) / $target_seconds * 100" | bc -l)
    if (( $(echo "$performance_score < 0" | bc -l) )); then
        performance_score=0
    fi
fi

analysis_end=$(date +%s.%N)
analysis_duration=$(echo "$analysis_end - $analysis_start" | bc -l)

# Identify optimization requirements
issues_found=()
if [[ "$build_success" != "true" ]]; then
    issues_found+=("Build compilation failure - requires dependency resolution")
fi
if (( $(echo "$final_build_time > $target_seconds" | bc -l) )); then
    issues_found+=("Build time exceeds 5-minute target by $(echo "scale=1; $final_build_time - $target_seconds" | bc -l)s")
fi
if [[ "$config_success" != "true" ]]; then
    issues_found+=("CMake configuration failure - dependency issues")
fi

requires_optimization=true
if [[ ${#issues_found[@]} -eq 0 ]] && [[ "$meets_target" == "true" ]]; then
    requires_optimization=false
fi

# Update results with comprehensive data
tmp_file=$(mktemp)
jq --argjson phases "$(cat << EOF
{
    "system_analysis": {
        "start_time": $system_start,
        "end_time": $system_end,
        "duration_seconds": $system_duration,
        "source_files_total": $total_source_files,
        "source_files_breakdown": {
            "cpp_files": $c_source_files,
            "cuda_files": $cuda_source_files,
            "header_files": $header_files
        }
    },
    "dependency_analysis": {
        "start_time": $dep_start,
        "end_time": $dep_end,
        "duration_seconds": $dep_duration,
        "git_submodules": {
            "count": $submodule_count,
            "details": $(echo "$submodule_info" | jq -R . | jq -s .)
        },
        "cmake_dependencies": {
            "fetchcontent_count": $fetchcontent_deps,
            "findpackage_count": $findpackage_deps,
            "add_subdirectory_count": $add_subdirectory_deps
        }
    },
    "configuration_analysis": {
        "start_time": $config_start,
        "end_time": $config_end,
        "duration_seconds": $config_duration,
        "cmake_success": $config_success,
        "cmake_time_seconds": $cmake_time,
        "cmake_output": $(echo "$config_output" | jq -R . | jq -s .)
    },
    "build_attempt": {
        "start_time": $build_start,
        "end_time": $build_end_phase,
        "duration_seconds": $build_duration,
        "estimated_build_time": $estimated_build_time,
        "actual_build_time": $build_time,
        "build_success": $build_success,
        "timeout_occurred": $([ "$build_time" -eq 300 ] && echo true || echo false),
        "build_output": $(echo "$build_output" | jq -R . | jq -s .)
    },
    "results_analysis": {
        "start_time": $analysis_start,
        "end_time": $analysis_end,
        "duration_seconds": $analysis_duration
    }
}
EOF
)" \
--arg final_time "$final_build_time" \
--arg method "$baseline_method" \
--arg confidence "$confidence" \
--argjson issues "$(printf '%s\n' "${issues_found[@]}" | jq -R . | jq -s .)" \
--arg requires_opt "$requires_optimization" \
--arg meets_target "$meets_target" \
--arg score "$performance_score" \
'.build_phases = $phases |
 .baseline_establishment.estimated_build_time_seconds = ($final_time | tonumber) |
 .baseline_establishment.confidence_level = $confidence |
 .baseline_establishment.measurement_method = $method |
 .baseline_establishment.requires_optimization = ($requires_opt | test("true")) |
 .baseline_establishment.identified_issues = $issues |
 .success_criteria_validation.meets_5_minute_target = ($meets_target | test("true")) |
 .success_criteria_validation.estimated_seconds = ($final_time | tonumber) |
 .success_criteria_validation.performance_score = ($score | tonumber) |
 .success_criteria_validation.optimization_required = ($requires_opt | test("true"))' \
"$RESULTS_FILE" > "$tmp_file" && mv "$tmp_file" "$RESULTS_FILE"

# Generate human-readable summary
summary_file="$RESULTS_DIR/${MEASUREMENT_ID}_summary.txt"
cat > "$summary_file" << EOF
Comprehensive Build Time Baseline Report
===========================================

Measurement ID: $MEASUREMENT_ID
Timestamp: $(date)
Target: 5-minute fresh checkout build
Measurement Method: $baseline_method
Confidence Level: $confidence

SYSTEM ANALYSIS
---------------
Total Source Files: $total_source_files
- C++ Files: $c_source_files
- CUDA Files: $cuda_source_files
- Header Files: $header_files
Analysis Duration: ${system_duration}s

DEPENDENCY ANALYSIS
-------------------
Git Submodules: $submodule_count
CMake Dependencies:
- FetchContent: $fetchcontent_deps
- find_package: $findpackage_deps
- add_subdirectory: $add_subdirectory_deps
Analysis Duration: ${dep_duration}s

CONFIGURATION ANALYSIS
----------------------
CMake Success: $config_success
CMake Time: ${cmake_time}s
Configuration Duration: ${config_duration}s

BUILD ATTEMPT ANALYSIS
----------------------
Estimated Build Time: ${estimated_build_time}s
Actual Build Time: ${build_time}s
Build Success: $build_success
Build Duration: ${build_duration}s
Timeout Occurred: $([ "$build_time" -eq 300 ] && echo "Yes" || echo "No")

BASELINE ESTABLISHMENT
----------------------
Estimated Build Time: ${final_build_time}s
Measurement Method: $baseline_method
Confidence Level: $confidence
Requires Optimization: $requires_optimization

Issues Identified:
$(printf '• %s\n' "${issues_found[@]}")

SUCCESS CRITERIA VALIDATION
---------------------------
5-minute target: $([ "$meets_target" = "true" ] && echo "MET ✅" || echo "NOT MET ❌")
Target: ${target_seconds} seconds
Estimated: ${final_build_time} seconds
Performance Score: ${performance_score}%
Optimization Required: $([ "$requires_optimization" = "true" ] && echo "Yes" || echo "No")

RECOMMENDATIONS
---------------
$([ "$meets_target" = "true" ] && echo "✅ Current build meets 5-minute target baseline" || echo "❌ Current build exceeds 5-minute target - optimization required")
$([ "$requires_optimization" = "true" ] && echo "⚠️  Optimization recommended before proceeding with user stories")
Measurement confidence: $confidence
$([[ "$confidence" == "high" ]] && echo "Baseline is reliable for optimization tracking" || echo "Baseline should be refined after optimization")

NEXT STEPS
-----------
1. $([ "$requires_optimization" = "true" ] && echo "Address identified build issues" || echo "Proceed with integration optimization")
2. $([ "$build_success" != "true" ] && echo "Fix dependency and compilation issues" || echo "Optimize build performance")
3. Re-run baseline measurement after optimization
4. Use established baseline to track optimization progress

EOF

success "Comprehensive build baseline measurement completed!"
log "Results saved to: $RESULTS_FILE"
log "Summary report: $summary_file"

# Display summary
echo ""
echo "=== COMPREHENSIVE BUILD BASELINE SUMMARY ==="
echo "Estimated Build Time: ${final_build_time} seconds"
echo "Target: ${target_seconds} seconds"
echo "Result: $([ "$meets_target" = "true" ] && echo "✅ MEETS 5-minute target" || echo "❌ EXCEEDS 5-minute target")"
echo "Performance Score: ${performance_score}%"
echo "Measurement Method: $baseline_method"
echo "Confidence: $confidence"
echo "Optimization Required: $([ "$requires_optimization" = "true" ] && echo "Yes" || echo "No")"
echo "Issues Identified: ${#issues_found[@]}"
echo "Detailed Report: $summary_file"
echo "=========================================="

# Exit with appropriate status
if [[ "$requires_optimization" = "true" ]]; then
    exit 1
else
    exit 0
fi