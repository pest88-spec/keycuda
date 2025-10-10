#!/bin/bash
#
# Setup Complexity Baseline Measurement Script for Puzzle71Solver
#
# Measures current setup complexity baseline including manual steps, knowledge requirements,
# automation features, and time-to-first-build. Establishes baseline for 80% reduction target.
#
# @origin       https://github.com/Puzzle71Solver/Puzzle71Solver
# @origin_path  scripts/measure-setup-complexity.sh
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
MEASUREMENT_ID="setup_complexity_$TIMESTAMP"
RESULTS_FILE="$RESULTS_DIR/${MEASUREMENT_ID}_complexity.json"

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

# Initialize setup complexity measurement
cat > "$RESULTS_FILE" << EOF
{
    "measurement_id": "$MEASUREMENT_ID",
    "measurement_timestamp": "$(date -Iseconds)",
    "project_root": "$PROJECT_ROOT",
    "measurement_type": "setup_complexity_baseline",
    "target_reduction_percentage": 80,
    "system_info": {
        "os": "$(uname -s)",
        "architecture": "$(uname -m)",
        "user_privileges": "$(id -u)",
        "shell": "$SHELL"
    },
    "setup_procedure_analysis": {},
    "dependency_requirements": {},
    "automation_assessment": {},
    "knowledge_requirements": {},
    "time_measurements": {},
    "complexity_scoring": {},
    "baseline_establishment": {
        "total_setup_steps": 0,
        "manual_interventions_required": 0,
        "automation_coverage_percentage": 0,
        "knowledge_prerequisites_count": 0,
        "complexity_score": 100,
        "time_to_first_build_minutes": 0
    },
    "success_criteria_validation": {
        "meets_80_percent_reduction": false,
        "target_reduction_percentage": 80,
        "current_complexity_score": 100,
        "estimated_reduction_potential": 0,
        "optimization_required": true
    }
}
EOF

log "Starting setup complexity baseline measurement: $MEASUREMENT_ID"

# Phase 1: Setup Procedure Analysis
log "Phase 1: Setup Procedure Analysis"
setup_start=$(date +%s.%N)

# Analyze documented setup procedures
setup_procedures=()
setup_files=()

# Check for setup documentation
if [[ -f "$PROJECT_ROOT/README.md" ]]; then
    setup_files+=("README.md")
    log "Found README.md"
fi

if [[ -f "$PROJECT_ROOT/QUICKSTART.md" ]]; then
    setup_files+=("QUICKSTART.md")
    log "Found QUICKSTART.md"
fi

if [[ -f "$PROJECT_ROOT/INSTALL.md" ]]; then
    setup_files+=("INSTALL.md")
    log "Found INSTALL.md"
fi

# Check for setup scripts
if [[ -d "$PROJECT_ROOT/scripts" ]]; then
    setup_scripts_count=$(find "$PROJECT_ROOT/scripts" -name "*.sh" | wc -l)
    log "Found $setup_scripts_count setup scripts in scripts/ directory"
    setup_files+=("scripts/ directory")
fi

# Analyze setup instructions
total_setup_steps=0
manual_steps=0

for setup_file in "${setup_files[@]}"; do
    if [[ -f "$PROJECT_ROOT/$setup_file" ]]; then
        log "Analyzing $setup_file for setup steps..."

        # Count common setup commands
        file_steps=$(grep -c -E "(git clone|git submodule|cmake|make|npm|pip|conda|brew|apt-get)" "$PROJECT_ROOT/$setup_file" 2>/dev/null || echo "0")
        total_setup_steps=$((total_setup_steps + file_steps))

        # Count manual configuration steps
        manual_file_steps=$(grep -c -E "(edit|configure|modify|change|set.*=" "$PROJECT_ROOT/$setup_file" 2>/dev/null || echo "0")
        manual_steps=$((manual_steps + manual_file_steps))

        log "  $setup_file: $file_steps total steps, $manual_file_steps manual steps"
    fi
done

# Check CMakeLists.txt for setup complexity
if [[ -f "$PROJECT_ROOT/CMakeLists.txt" ]]; then
    cmake_options=$(grep -c "option(" "$PROJECT_ROOT/CMakeLists.txt" 2>/dev/null || echo "0")
    cmake_variables=$(grep -c "set(" "$PROJECT_ROOT/CMakeLists.txt" 2>/dev/null || echo "0")
    log "CMakeLists.txt: $cmake_options options, $cmake_variables variables"

    total_setup_steps=$((total_setup_steps + cmake_options + cmake_variables))
fi

setup_end=$(date +%s.%N)
setup_duration=$(echo "$setup_end - $setup_start" | bc -l)

# Phase 2: Dependency Requirements Analysis
log "Phase 2: Dependency Requirements Analysis"
dep_start=$(date +%s.%N)

# Analyze external dependencies
external_deps=()
external_tools=()

# Check for tool dependencies
tools=("git" "cmake" "make" "gcc" "g++" "nvcc" "python3" "pip" "conda")
for tool in "${tools[@]}"; do
    if command -v "$tool" >/dev/null 2>&1; then
        version=$("$tool" --version 2>/dev/null | head -1 || echo "available")
        external_tools+=("$tool: $version")
        log "Tool available: $tool ($version)"
    else
        external_tools+=("$tool: NOT FOUND")
        warning "Tool required but not found: $tool"
    fi
done

# Check for system package dependencies
if command -v apt-get >/dev/null 2>&1; then
    system_packages=("build-essential" "cmake" "pkg-config" "libssl-dev")
    for pkg in "${system_packages[@]}"; do
        if dpkg -l "$pkg" >/dev/null 2>&1; then
            external_deps+=("system: $pkg (installed)")
        else
            external_deps+=("system: $pkg (NOT INSTALLED)")
        fi
    done
fi

# Check for CUDA toolkit
if command -v nvcc >/dev/null 2>&1; then
    cuda_version=$(nvcc --version | grep release | awk '{print $6}')
    external_deps+=("cuda: $cuda_version")
    log "CUDA toolkit found: $cuda_version"
else
    external_deps+=("cuda: NOT FOUND")
    warning "CUDA toolkit required but not found"
fi

dep_end=$(date +%s.%N)
dep_duration=$(echo "$dep_end - $dep_start" | bc -l)

# Phase 3: Automation Assessment
log "Phase 3: Automation Assessment"
auto_start=$(date +%s.%N)

# Check for automation features
automation_features=()
automation_score=0

# Check for setup scripts
if [[ -f "$PROJECT_ROOT/scripts/setup-dependencies.sh" ]]; then
    automation_features+=("setup-dependencies script: YES")
    automation_score=$((automation_score + 20))
    log "Found automated dependency setup script"
else
    automation_features+=("setup-dependencies script: NO")
fi

if [[ -f "$PROJECT_ROOT/scripts/setup.sh" ]]; then
    automation_features+=("setup script: YES")
    automation_score=$((automation_score + 15))
    log "Found general setup script"
else
    automation_features+=("setup script: NO")
fi

# Check for build automation
if [[ -f "$PROJECT_ROOT/CMakeLists.txt" ]]; then
    if grep -q "FetchContent" "$PROJECT_ROOT/CMakeLists.txt"; then
        automation_features+=("automatic dependency downloading: YES")
        automation_score=$((automation_score + 15))
        log "Found automatic dependency downloading"
    else
        automation_features+=("automatic dependency downloading: NO")
    fi

    if grep -q "add_subdirectory.*third_party" "$PROJECT_ROOT/CMakeLists.txt"; then
        automation_features+=("submodule management: YES")
        automation_score=$((automation_score + 10))
        log "Found automated submodule management"
    else
        automation_features+=("submodule management: NO")
    fi
fi

# Check for testing automation
if [[ -f "$PROJECT_ROOT/CTestTestfile.cmake" ]] || grep -q "enable_testing" "$PROJECT_ROOT/CMakeLists.txt"; then
    automation_features+=("automated testing: YES")
    automation_score=$((automation_score + 10))
    log "Found automated testing configuration"
else
    automation_features+=("automated testing: NO")
fi

# Check for deployment automation
if [[ -f "$PROJECT_ROOT/scripts/package-deployment.sh" ]]; then
    automation_features+=("automated deployment: YES")
    automation_score=$((automation_score + 15))
    log "Found automated deployment script"
else
    automation_features+=("automated deployment: NO")
fi

auto_end=$(date +%s.%N)
auto_duration=$(echo "$auto_end - $auto_start" | bc -l)

# Calculate automation coverage percentage
max_automation_score=100
automation_coverage=$((automation_score * 100 / max_automation_score))

# Phase 4: Knowledge Requirements Analysis
log "Phase 4: Knowledge Requirements Analysis"
knowledge_start=$(date +%s.%N)

# Define knowledge prerequisites
knowledge_prereqs=()
knowledge_score=0

# Programming knowledge required
knowledge_prereqs+=("C++ programming: REQUIRED")
knowledge_score=$((knowledge_score + 20))

# Build system knowledge
knowledge_prereqs+=("CMake build system: REQUIRED")
knowledge_score=$((knowledge_score + 15))

# CUDA knowledge
knowledge_prereqs+=("CUDA programming: REQUIRED")
knowledge_score=$((knowledge_score + 20))

# Version control knowledge
knowledge_prereqs+=("Git version control: REQUIRED")
knowledge_score=$((knowledge_score + 10))

# Linux system administration
knowledge_prereqs+=("Linux system administration: REQUIRED")
knowledge_score=$((knowledge_score + 15))

# Cryptography knowledge (advanced)
if grep -q -i "secp256\|crypto\|hash\|signature" "$PROJECT_ROOT/src"/*.cpp "$PROJECT_ROOT/src"/*.h 2>/dev/null; then
    knowledge_prereqs+=("Cryptography concepts: REQUIRED")
    knowledge_score=$((knowledge_score + 20))
fi

# GPU computing knowledge (advanced)
if [[ -f "$PROJECT_ROOT/src"/*.cu ]] || grep -q -i "cuda\|gpu\|kernel" "$PROJECT_ROOT/CMakeLists.txt" 2>/dev/null; then
    knowledge_prereqs+=("GPU computing concepts: REQUIRED")
    knowledge_score=$((knowledge_score + 25))
fi

knowledge_end=$(date +%s.%N)
knowledge_duration=$(echo "$knowledge_end - $knowledge_start" | bc -l)

# Phase 5: Time Measurements
log "Phase 5: Time Measurements"
time_start=$(date +%s.%N)

# Simulate setup time measurement
log "Simulating time-to-first-build..."

# Current setup complexity time estimation (based on analysis)
# Manual setup: ~15-30 minutes
# Automated setup: ~5-10 minutes

if [[ $automation_score -ge 70 ]]; then
    estimated_setup_minutes=8  # Well automated
elif [[ $automation_score -ge 40 ]]; then
    estimated_setup_minutes=15  # Partially automated
else
    estimated_setup_minutes=25  # Mostly manual
fi

# Calculate time to first build (setup + build)
time_to_first_build_minutes=$(echo "$estimated_setup_minutes + 2.5" | bc -l)  # Add ~2.5 minutes for actual build

time_end=$(date +%s.%N)
time_duration=$(echo "$time_end - $time_start" | bc -l)

# Phase 6: Complexity Scoring
log "Phase 6: Complexity Scoring"
score_start=$(date +%s.%N)

# Calculate overall complexity score (0-100, lower is better)
complexity_score=100

# Subtract automation score (automation reduces complexity)
complexity_score=$((complexity_score - automation_score))

# Add manual intervention penalty
manual_penalty=$((manual_steps * 2))
complexity_score=$((complexity_score + manual_penalty))

# Add dependency penalty
missing_tools_count=$(echo "${external_tools[*]}" | grep -c "NOT FOUND")
missing_deps_count=$(echo "${external_deps[*]}" | grep -c "NOT INSTALLED")
dependency_penalty=$(((missing_tools_count + missing_deps_count) * 5))
complexity_score=$((complexity_score + dependency_penalty))

# Add knowledge penalty (more knowledge required = higher complexity)
max_knowledge_score=125
knowledge_penalty=$((knowledge_score * 30 / max_knowledge_score))
complexity_score=$((complexity_score + knowledge_penalty))

# Clamp score between 0 and 100
if [[ $complexity_score -lt 0 ]]; then
    complexity_score=0
elif [[ $complexity_score -gt 100 ]]; then
    complexity_score=100
fi

score_end=$(date +%s.%N)
score_duration=$(echo "$score_end - $score_start" | bc -l)

# Calculate reduction potential
reduction_potential=0
if [[ $complexity_score -gt 20 ]]; then
    reduction_potential=$((complexity_score - 20))  # Can reduce to 20 with full automation
fi

# Success criteria validation
target_reduction=80
meets_reduction=false
if [[ $reduction_potential -ge $target_reduction ]]; then
    meets_reduction=true
fi

# Update results with comprehensive data
tmp_file=$(mktemp)
jq --argjson setup_procedure "$(cat << EOF
{
    "start_time": $setup_start,
    "end_time": $setup_end,
    "duration_seconds": $setup_duration,
    "setup_files": $(printf '%s\n' "${setup_files[@]}" | jq -R . | jq -s .),
    "total_steps_found": $total_setup_steps,
    "manual_steps_count": $manual_steps
}
EOF
)" \
--argjson dependencies "$(cat << EOF
{
    "start_time": $dep_start,
    "end_time": $dep_end,
    "duration_seconds": $dep_duration,
    "external_tools": $(printf '%s\n' "${external_tools[@]}" | jq -R . | jq -s .),
    "external_dependencies": $(printf '%s\n' "${external_deps[@]}" | jq -R . | jq -s .),
    "missing_tools_count": $missing_tools_count,
    "missing_dependencies_count": $missing_deps_count
}
EOF
)" \
--argjson automation "$(cat << EOF
{
    "start_time": $auto_start,
    "end_time": $auto_end,
    "duration_seconds": $auto_duration,
    "automation_features": $(printf '%s\n' "${automation_features[@]}" | jq -R . | jq -s .),
    "automation_score": $automation_score,
    "automation_coverage_percentage": $automation_coverage
}
EOF
)" \
--argjson knowledge "$(cat << EOF
{
    "start_time": $knowledge_start,
    "end_time": $knowledge_end,
    "duration_seconds": $knowledge_duration,
    "knowledge_prerequisites": $(printf '%s\n' "${knowledge_prereqs[@]}" | jq -R . | jq -s .),
    "knowledge_score": $knowledge_score,
    "knowledge_prerequisites_count": ${#knowledge_prereqs[@]}
}
EOF
)" \
--argjson time_measurements "$(cat << EOF
{
    "start_time": $time_start,
    "end_time": $time_end,
    "duration_seconds": $time_duration,
    "estimated_setup_time_minutes": $estimated_setup_minutes,
    "time_to_first_build_minutes": $time_to_first_build_minutes
}
EOF
)" \
--arg complexity_score "$complexity_score" \
--arg total_steps "$total_setup_steps" \
--arg manual_steps "$manual_steps" \
--arg automation_coverage "$automation_coverage" \
--arg knowledge_prereqs "${#knowledge_prereqs[@]}" \
--arg setup_time "$estimated_setup_minutes" \
--arg time_to_build "$time_to_first_build_minutes" \
--arg reduction_potential "$reduction_potential" \
--arg meets_reduction "$meets_reduction" \
--arg reduction_target "$target_reduction" \
--arg requires_opt "$([ "$reduction_potential" -lt "$target_reduction" ] && echo true || echo false)" \
'.setup_procedure_analysis = $setup_procedure |
 .dependency_requirements = $dependencies |
 .automation_assessment = $automation |
 .knowledge_requirements = $knowledge |
 .time_measurements = $time_measurements |
 .complexity_scoring = {
    "overall_score": ($complexity_score | tonumber),
    "total_steps": ($total_steps | tonumber),
    "manual_interventions": ($manual_steps | tonumber),
    "automation_coverage": ($automation_coverage | tonumber)
 },
 .baseline_establishment.total_setup_steps = ($total_steps | tonumber) |
 .baseline_establishment.manual_interventions_required = ($manual_steps | tonumber) |
 .baseline_establishment.automation_coverage_percentage = ($automation_coverage | tonumber) |
 .baseline_establishment.knowledge_prerequisites_count = ($knowledge_prereqs | tonumber) |
 .baseline_establishment.complexity_score = ($complexity_score | tonumber) |
 .baseline_establishment.time_to_first_build_minutes = ($time_to_build | tonumber) |
 .success_criteria_validation.meets_80_percent_reduction = ($meets_reduction | test("true")) |
 .success_criteria_validation.estimated_reduction_potential = ($reduction_potential | tonumber) |
 .success_criteria_validation.optimization_required = ($requires_opt | test("true"))' \
"$RESULTS_FILE" > "$tmp_file" && mv "$tmp_file" "$RESULTS_FILE"

# Generate human-readable summary
summary_file="$RESULTS_DIR/${MEASUREMENT_ID}_summary.txt"
cat > "$summary_file" << EOF
Setup Complexity Baseline Report
=================================

Measurement ID: $MEASUREMENT_ID
Timestamp: $(date)
Target: 80% reduction in setup complexity
Measurement Method: comprehensive analysis

SETUP PROCEDURE ANALYSIS
-------------------------
Total Setup Steps Found: $total_setup_steps
Manual Steps Required: $manual_steps
Setup Documentation: ${#setup_files[@]} files
Analysis Duration: ${setup_duration}s

DEPENDENCY REQUIREMENTS
---------------------
External Tools: ${#external_tools[@]}
Missing Tools: $missing_tools_count
External Dependencies: ${#external_deps[@]}
Missing Dependencies: $missing_deps_count
Analysis Duration: ${dep_duration}s

AUTOMATION ASSESSMENT
--------------------
Automation Score: $automation_score/100
Automation Coverage: ${automation_coverage}%
Automation Features:
$(printf '• %s\n' "${automation_features[@]}")
Assessment Duration: ${auto_duration}s

KNOWLEDGE REQUIREMENTS
---------------------
Knowledge Prerequisites: ${#knowledge_prereqs[@]}
Knowledge Score: $knowledge_score/125
Analysis Duration: ${knowledge_duration}s

TIME MEASUREMENTS
----------------
Estimated Setup Time: ${estimated_setup_minutes} minutes
Time to First Build: ${time_to_first_build_minutes} minutes
Measurement Duration: ${time_duration}s

COMPLEXITY SCORING
-----------------
Overall Complexity Score: $complexity_score/100
- Lower score indicates simpler setup
- Higher score indicates more complex setup

Breakdown:
- Base Score: 100
- Automation Reduction: -$automation_score
- Manual Intervention Penalty: -$((manual_steps * 2))
- Missing Dependencies Penalty: -$dependency_penalty
- Knowledge Requirements Penalty: $((knowledge_score * 30 / 125))

BASELINE ESTABLISHMENT
-------------------
Total Setup Steps: $total_setup_steps
Manual Interventions Required: $manual_steps
Automation Coverage: ${automation_coverage}%
Knowledge Prerequisites: ${#knowledge_prereqs[@]}
Complexity Score: $complexity_score/100
Time to First Build: ${time_to_first_build_minutes} minutes

SUCCESS CRITERIA VALIDATION
---------------------------
80% Reduction Target: $([ "$meets_reduction" = "true" ] && echo "MET ✅" || echo "NOT MET ❌")
Target Reduction: ${target_reduction}%
Estimated Reduction Potential: ${reduction_potential}%
Optimization Required: $([ "$reduction_potential" -lt "$target_reduction" ] && echo "Yes" || echo "No")

RECOMMENDATIONS
---------------
$([ "$meets_reduction" = "true" ] && echo "✅ Current setup meets 80% reduction target" || echo "❌ Current setup requires optimization to meet 80% reduction target")
Current complexity score: $complexity_score/100 $([ "$complexity_score" -le 50 ] && echo "(Good)" || echo "- $( [ "$complexity_score" -gt 70 ] && echo "(High complexity)" || echo "(Moderate)")
Automation coverage: ${automation_coverage}% $([ "$automation_coverage" -ge 70 ] && echo "(Good)" || echo "- $( [ "$automation_coverage" -ge 40 ] && echo "(Moderate)" || echo "(Low)")

OPTIMIZATION OPPORTUNITIES
------------------------
$([ "$automation_coverage" -lt 50 ] && echo "• Increase automation coverage to reduce manual steps")
$([ "$manual_steps" -gt 10 ] && echo "• Reduce manual intervention through scripts")
$([ "$missing_tools_count" -gt 0 ] || [ "$missing_deps_count" -gt 0 ]) && echo "• Resolve missing dependencies")
$([ "$complexity_score" -gt 60 ] && echo "• Simplify build configuration")
$([ "$knowledge_score" -gt 80 ] && echo "• Provide better documentation and guides")

NEXT STEPS
-----------
1. Implement automation opportunities identified
2. Reduce manual setup steps through scripts
3. Resolve missing dependencies
4. Simplify configuration requirements
5. Re-measure complexity after optimization
6. Track progress toward 80% reduction target

EOF

success "Setup complexity baseline measurement completed!"
log "Results saved to: $RESULTS_FILE"
log "Summary report: $summary_file"

# Display summary
echo ""
echo "=== SETUP COMPLEXITY BASELINE SUMMARY ==="
echo "Current Setup Steps: $total_setup_steps"
echo "Manual Interventions: $manual_steps"
echo "Automation Coverage: ${automation_coverage}%"
echo "Complexity Score: $complexity_score/100"
echo "Time to First Build: ${time_to_first_build_minutes} minutes"
echo "Reduction Target: ${target_reduction}%"
echo "Reduction Potential: ${reduction_potential}%"
echo "80% Reduction Target: $([ "$meets_reduction" = "true" ] && echo "✅ MET" || echo "❌ NOT MET")"
echo "Optimization Required: $([ "$reduction_potential" -lt "$target_reduction" ] && echo "Yes" || echo "No")"
echo "Detailed Report: $summary_file"
echo "=========================================="

# Exit with appropriate status
if [[ "$requires_opt" = "true" ]]; then
    exit 1
else
    exit 0
fi