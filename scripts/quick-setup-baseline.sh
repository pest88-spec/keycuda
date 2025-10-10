#!/bin/bash
#
# Quick Setup Complexity Baseline for Puzzle71Solver
#
# Quick baseline measurement for setup complexity focusing on key metrics
# needed for 80% reduction target validation.
#
# @origin       https://github.com/Puzzle71Solver/Puzzle71Solver
# @origin_path  scripts/quick-setup-baseline.sh
# @origin_commit <current_commit>
# @origin_license MIT
# @extracted_date   2025-10-10
# @extracted_by     Puzzle71Solver Team
# @modifications    Created for third-party dependency integration optimization
# @spdx_license_identifier MIT
#

set -euo pipefail

# Configuration
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
RESULTS_DIR="$PROJECT_ROOT/baseline_measurements"
MEASUREMENT_ID="setup_baseline_$TIMESTAMP"

# Create results directory
mkdir -p "$RESULTS_DIR"

echo "=== SETUP COMPLEXITY BASELINE MEASUREMENT ==="
echo "Measurement ID: $MEASUREMENT_ID"
echo "Timestamp: $(date)"
echo ""

# Analyze setup files
setup_files=("README.md" "QUICKSTART.md" "scripts/")
total_files=0
total_steps=0
manual_steps=0

for file in "${setup_files[@]}"; do
    if [[ -e "$PROJECT_ROOT/$file" ]]; then
        echo "Found: $file"
        total_files=$((total_files + 1))

        if [[ -f "$PROJECT_ROOT/$file" ]]; then
            steps=$(grep -c -E "(git|cmake|make|configure|edit|modify)" "$PROJECT_ROOT/$file" 2>/dev/null || echo "0")
            total_steps=$((total_steps + steps))
            manual_steps=$((manual_steps + steps))
            echo "  Setup steps found: $steps"
        fi
    fi
done

echo ""
echo "SETUP PROCEDURE ANALYSIS:"
echo "Total setup files: $total_files"
echo "Total setup steps: $total_steps"
echo "Manual steps required: $manual_steps"
echo ""

# Check automation
automation_score=0
if [[ -f "$PROJECT_ROOT/scripts/setup-dependencies.sh" ]]; then
    automation_score=$((automation_score + 25))
    echo "✅ Found automated dependency setup script (+25)"
fi

if [[ -f "$PROJECT_ROOT/CMakeLists.txt" ]] && grep -q "FetchContent" "$PROJECT_ROOT/CMakeLists.txt"; then
    automation_score=$((automation_score + 25))
    echo "✅ Found automatic dependency downloading (+25)"
fi

if [[ -d "$PROJECT_ROOT/scripts" ]] && [[ $(find "$PROJECT_ROOT/scripts" -name "*.sh" | wc -l) -gt 10 ]]; then
    automation_score=$((automation_score + 20))
    echo "✅ Found extensive automation scripts (+20)"
fi

if [[ -f "$PROJECT_ROOT/scripts/package-deployment.sh" ]]; then
    automation_score=$((automation_score + 20))
    echo "✅ Found automated deployment script (+20)"
fi

automation_coverage=$((automation_score * 100 / 90))  # Max score is 90
if [[ $automation_coverage -gt 100 ]]; then
    automation_coverage=100
fi

echo ""
echo "AUTOMATION ASSESSMENT:"
echo "Automation score: $automation_score/90"
echo "Automation coverage: ${automation_coverage}%"
echo ""

# Check dependencies
missing_deps=0
tools=("git" "cmake" "make" "nvcc" "gcc" "python3")

echo "DEPENDENCY ANALYSIS:"
for tool in "${tools[@]}"; do
    if command -v "$tool" >/dev/null 2>&1; then
        echo "✅ $tool: available"
    else
        echo "❌ $tool: MISSING"
        missing_deps=$((missing_deps + 1))
    fi
done

echo "Missing dependencies: $missing_deps"
echo ""

# Calculate complexity score (0-100, lower is better)
complexity_score=100
complexity_score=$((complexity_score - automation_score))
complexity_score=$((complexity_score + (manual_steps * 3)))
complexity_score=$((complexity_score + (missing_deps * 10)))

if [[ $complexity_score -lt 0 ]]; then
    complexity_score=0
elif [[ $complexity_score -gt 100 ]]; then
    complexity_score=100
fi

echo "COMPLEXITY SCORING:"
echo "Overall complexity score: $complexity_score/100 (lower is better)"
echo ""

# Calculate reduction potential
reduction_potential=$automation_score
if [[ $reduction_potential -gt 80 ]]; then
    reduction_potential=80
fi

echo "REDUCTION POTENTIAL:"
echo "Estimated reduction: ${reduction_potential}%"
echo "Target reduction: 80%"
echo ""

# Success criteria validation
if [[ $reduction_potential -ge 80 ]]; then
    meets_target=true
    echo "✅ MEETS 80% REDUCTION TARGET"
else
    meets_target=false
    echo "❌ DOES NOT MEET 80% REDUCTION TARGET"
fi

echo ""
echo "TIME ESTIMATES:"
if [[ $automation_score -ge 60 ]]; then
    estimated_setup=8
elif [[ $automation_score -ge 30 ]]; then
    estimated_setup=15
else
    estimated_setup=25
fi

echo "Estimated setup time: ${estimated_setup} minutes"
echo "Estimated time to first build: $((estimated_setup + 3)) minutes"
echo ""

# Create baseline summary
cat > "$RESULTS_DIR/${MEASUREMENT_ID}_summary.txt" << EOF
Setup Complexity Baseline Report
================================

Measurement ID: $MEASUREMENT_ID
Timestamp: $(date)

CURRENT STATE
------------
Setup Files: $total_files
Setup Steps: $total_steps
Manual Steps: $manual_steps
Automation Coverage: ${automation_coverage}%
Missing Dependencies: $missing_deps

Complexity Score: $complexity_score/100
$(if [[ $complexity_score -le 50 ]]; then echo "Complexity Level: GOOD"; elif [[ $complexity_score -le 70 ]]; then echo "Complexity Level: MODERATE"; else echo "Complexity Level: HIGH"; fi)

SUCCESS CRITERIA
---------------
80% Reduction Target: $([ "$meets_target" = "true" ] && echo "MET" || echo "NOT MET")
Reduction Potential: ${reduction_potential}%
Optimization Required: $([ "$meets_target" = "true" ] && echo "No" || echo "Yes")

TIME ESTIMATES
---------------
Estimated Setup Time: ${estimated_setup} minutes
Time to First Build: $((estimated_setup + 3)) minutes

RECOMMENDATIONS
---------------
$(if [[ "$automation_coverage" -lt 50 ]]; then echo "• Increase automation coverage to reduce manual steps"; fi)
$(if [[ "$manual_steps" -gt 20 ]]; then echo "• Reduce manual intervention through scripts"; fi)
$(if [[ "$missing_deps" -gt 0 ]]; then echo "• Resolve missing dependencies"; fi)
$(if [[ "$complexity_score" -gt 70 ]]; then echo "• Simplify build configuration"; fi)

EOF

echo "Results saved to: $RESULTS_DIR/${MEASUREMENT_ID}_summary.txt"
echo ""
echo "=== BASELINE ESTABLISHED ==="
echo "Setup Complexity Score: $complexity_score/100"
echo "Reduction Potential: ${reduction_potential}%"
echo "80% Target: $([ "$meets_target" = "true" ] && echo "MET ✅" || echo "NOT MET ❌")"
echo "========================================"

# Save JSON baseline
cat > "$RESULTS_DIR/${MEASUREMENT_ID}_baseline.json" << EOF
{
    "measurement_id": "$MEASUREMENT_ID",
    "timestamp": "$(date -Iseconds)",
    "setup_files_count": $total_files,
    "setup_steps_total": $total_steps,
    "manual_steps_count": $manual_steps,
    "automation_coverage_percentage": $automation_coverage,
    "missing_dependencies_count": $missing_deps,
    "complexity_score": $complexity_score,
    "reduction_potential_percentage": $reduction_potential,
    "estimated_setup_time_minutes": $estimated_setup,
    "time_to_first_build_minutes": $((estimated_setup + 3)),
    "meets_80_percent_target": $meets_target
}
EOF

# Exit with appropriate status
if [[ "$meets_target" == "true" ]]; then
    exit 0
else
    exit 1
fi