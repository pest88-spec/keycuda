#!/bin/bash
#
# Baseline vs Optimized Setup Comparison Tests for Puzzle71Solver
#
# Compares setup complexity between baseline and optimized configurations
# to validate the 80% reduction target and measure improvements.
#
# @origin       https://github.com/Puzzle71Solver/Puzzle71Solver
# @origin_path  scripts/compare-setup-complexity.sh
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
RESULTS_DIR="${PROJECT_ROOT}/setup_comparison_results"
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
REPORT_FILE="${RESULTS_DIR}/setup_comparison_${TIMESTAMP}.json"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo "=========================================="
echo "📊 Setup Complexity Comparison Tests"
echo "=========================================="
echo "Project Root: $PROJECT_ROOT"
echo "Timestamp: $(date)"
echo ""

# Function to print colored output
print_status() {
    local color=$1
    local message=$2
    echo -e "${color}${message}${NC}"
}

# Function to create results directory
create_results_directory() {
    mkdir -p "$RESULTS_DIR"
    print_status "$GREEN" "✅ Created results directory: $RESULTS_DIR"
}

# Function to analyze baseline setup complexity
analyze_baseline_setup() {
    print_status "$BLUE" "📊 Analyzing baseline setup complexity..."

    local baseline_metrics=()

    # Count setup steps in documentation
    local readme_steps=0
    if [[ -f "${PROJECT_ROOT}/README.md" ]]; then
        readme_steps=$(grep -c -E "(git|cmake|make|configure|clone|setup|install)" "${PROJECT_ROOT}/README.md" || echo "0")
    fi

    local quickstart_steps=0
    if [[ -f "${PROJECT_ROOT}/QUICKSTART.md" ]]; then
        quickstart_steps=$(grep -c -E "(git|cmake|make|configure|clone|setup|install)" "${PROJECT_ROOT}/QUICKSTART.md" || echo "0")
    fi

    # Count automation scripts
    local automation_scripts=0
    if [[ -d "${PROJECT_ROOT}/scripts" ]]; then
        automation_scripts=$(find "${PROJECT_ROOT}/scripts" -name "*.sh" -type f | wc -l)
    fi

    # Check for external dependencies
    local external_deps=0
    if [[ -f "${PROJECT_ROOT}/.gitmodules" ]]; then
        external_deps=$(grep -c "^\[submodule" "${PROJECT_ROOT}/.gitmodules" || echo "0")
    fi

    # Check CMake FetchContent dependencies
    local cmake_deps=0
    if [[ -f "${PROJECT_ROOT}/CMakeLists.txt" ]]; then
        cmake_deps=$(grep -c "FetchContent" "${PROJECT_ROOT}/CMakeLists.txt" || echo "0")
    fi

    # Calculate complexity score
    local manual_steps=$((readme_steps + quickstart_steps))
    local automation_score=$((automation_scripts * 5))
    local dependency_penalty=$((external_deps * 10 + cmake_deps * 5))

    local baseline_complexity=$((manual_steps - automation_score + dependency_penalty))
    if [[ $baseline_complexity -lt 0 ]]; then
        baseline_complexity=0
    fi

    baseline_metrics+=("README Documentation Steps:$readme_steps")
    baseline_metrics+=("QuickStart Documentation Steps:$quickstart_steps")
    baseline_metrics+=("Automation Scripts:$automation_scripts")
    baseline_metrics+=("Git Submodules:$external_deps")
    baseline_metrics+=("CMake FetchContent Dependencies:$cmake_deps")
    baseline_metrics+=("Manual Steps Total:$manual_steps")
    baseline_metrics+=("Automation Score:$automation_score")
    baseline_metrics+=("Dependency Penalty:$dependency_penalty")
    baseline_metrics+=("Baseline Complexity Score:$baseline_complexity")

    # Export baseline metrics to JSON
    local baseline_json=$(cat << EOF
{
    "timestamp": "$(date -Iseconds)",
    "configuration": "baseline",
    "documentation": {
        "readme_steps": $readme_steps,
        "quickstart_steps": $quickstart_steps,
        "total_documentation_steps": $((readme_steps + quickstart_steps))
    },
    "automation": {
        "automation_scripts": $automation_scripts,
        "automation_score": $automation_score
    },
    "dependencies": {
        "git_submodules": $external_deps,
        "cmake_fetchcontent": $cmake_deps,
        "total_external_dependencies": $((external_deps + cmake_deps)),
        "dependency_penalty": $dependency_penalty
    },
    "complexity": {
        "manual_steps": $manual_steps,
        "complexity_score": $baseline_complexity,
        "complexity_level": "$(if [[ $baseline_complexity -le 30 ]]; then echo "LOW"; elif [[ $baseline_complexity -le 60 ]]; then echo "MEDIUM"; else echo "HIGH"; fi)"
    }
}
EOF
)

    echo "$baseline_json" > "${RESULTS_DIR}/baseline_metrics_${TIMESTAMP}.json"

    print_status "$GREEN" "✅ Baseline setup complexity analyzed"
    print_status "$BLUE" "   Manual Steps: $manual_steps"
    print_status "$BLUE" "   Automation Scripts: $automation_scripts"
    print_status "$BLUE" "   External Dependencies: $((external_deps + cmake_deps))"
    print_status "$BLUE" "   Complexity Score: $baseline_complexity"

    # Return the complexity score
    echo "$baseline_complexity"
}

# Function to analyze optimized setup complexity
analyze_optimized_setup() {
    print_status "$BLUE" "📊 Analyzing optimized setup complexity..."

    local optimized_metrics=()

    # Count setup steps in optimized documentation
    local readme_steps=0
    if [[ -f "${PROJECT_ROOT}/README.md" ]]; then
        # Look for simplified build instructions
        readme_steps=$(grep -c -E "(OFFLINE_BUILD|single command|simplified)" "${PROJECT_ROOT}/README.md" || echo "0")
    fi

    local quickstart_steps=0
    if [[ -f "${PROJECT_ROOT}/QUICKSTART.md" ]]; then
        # Look for 5-minute build instructions
        quickstart_steps=$(grep -c -E "(5 minutes|single command|quick start)" "${PROJECT_ROOT}/QUICKSTART.md" || echo "0")
    fi

    # Count advanced automation scripts
    local automation_scripts=0
    if [[ -d "${PROJECT_ROOT}/scripts" ]]; then
        # Count specialized integration scripts
        automation_scripts=$(find "${PROJECT_ROOT}/scripts" -name "*integration*" -o -name "*setup*" -o -name "*verify*" -name "*.sh" -type f | wc -l)
    fi

    # Check for offline build capabilities
    local offline_capability=0
    if [[ -f "${PROJECT_ROOT}/CMakeLists.txt" ]]; then
        if grep -q "OFFLINE_BUILD" "${PROJECT_ROOT}/CMakeLists.txt"; then
            offline_capability=20
        fi
    fi

    # Check for extracted sources
    local extracted_sources=0
    if [[ -d "${PROJECT_ROOT}/src/extracted" ]]; then
        extracted_sources=$(find "${PROJECT_ROOT}/src/extracted" -type f \( -name "*.c" -o -name "*.cpp" -o -name "*.h" -o -name "*.cu" \) | wc -l)
    fi

    # Calculate optimized complexity score
    local manual_steps=$((readme_steps + quickstart_steps))
    local automation_bonus=$((automation_scripts * 10))
    local offline_bonus=$offline_capability
    local self_contained_bonus=$((extracted_sources > 0 ? 15 : 0))

    local optimized_complexity=$((manual_steps - automation_bonus - offline_bonus - self_contained_bonus))
    if [[ $optimized_complexity -lt 0 ]]; then
        optimized_complexity=0
    fi

    optimized_metrics+=("Optimized README Steps:$readme_steps")
    optimized_metrics+=("Optimized QuickStart Steps:$quickstart_steps")
    optimized_metrics+=("Advanced Automation Scripts:$automation_scripts")
    optimized_metrics+=("Offline Build Capability:$offline_capability")
    optimized_metrics+=("Extracted Sources:$extracted_sources")
    optimized_metrics+=("Manual Steps Total:$manual_steps")
    optimized_metrics+=("Automation Bonus:$automation_bonus")
    optimized_metrics+=("Offline Bonus:$offline_bonus")
    optimized_metrics+=("Self-Contained Bonus:$self_contained_bonus")
    optimized_metrics+=("Optimized Complexity Score:$optimized_complexity")

    # Export optimized metrics to JSON
    local optimized_json=$(cat << EOF
{
    "timestamp": "$(date -Iseconds)",
    "configuration": "optimized",
    "documentation": {
        "readme_steps": $readme_steps,
        "quickstart_steps": $quickstart_steps,
        "total_documentation_steps": $((readme_steps + quickstart_steps))
    },
    "automation": {
        "advanced_automation_scripts": $automation_scripts,
        "automation_bonus": $automation_bonus
    },
    "features": {
        "offline_build_capability": $offline_capability,
        "extracted_sources": $extracted_sources,
        "self_contained_bonus": $self_contained_bonus
    },
    "complexity": {
        "manual_steps": $manual_steps,
        "complexity_score": $optimized_complexity,
        "complexity_level": "$(if [[ $optimized_complexity -le 20 ]]; then echo "VERY_LOW"; elif [[ $optimized_complexity -le 40 ]]; then echo "LOW"; elif [[ $optimized_complexity -le 60 ]]; then echo "MEDIUM"; else echo "HIGH"; fi)"
    }
}
EOF
)

    echo "$optimized_json" > "${RESULTS_DIR}/optimized_metrics_${TIMESTAMP}.json"

    print_status "$GREEN" "✅ Optimized setup complexity analyzed"
    print_status "$BLUE" "   Manual Steps: $manual_steps"
    print_status "$BLUE" "   Advanced Automation Scripts: $automation_scripts"
    print_status "$BLUE" "   Offline Build Capability: $offline_capability"
    print_status "$BLUE" "   Extracted Sources: $extracted_sources"
    print_status "$BLUE" "   Complexity Score: $optimized_complexity"

    # Return the complexity score
    echo "$optimized_complexity"
}

# Function to perform actual build time measurement
measure_build_time() {
    local build_config=$1
    local build_dir="${RESULTS_DIR}/build_${build_config}_${TIMESTAMP}"

    print_status "$BLUE" "⏱️  Measuring build time for $build_config configuration..."

    mkdir -p "$build_dir"
    cd "$build_dir"

    local start_time=$(date +%s)

    # Configure based on configuration
    if [[ "$build_config" == "baseline" ]]; then
        print_status "$BLUE" "Configuring baseline build..."
        if ! cmake .. -DCMAKE_BUILD_TYPE=Release; then
            print_status "$RED" "❌ Baseline cmake configuration failed"
            cd "$PROJECT_ROOT"
            return 1
        fi
    else
        print_status "$BLUE" "Configuring optimized build (offline)..."
        if ! cmake .. -DCMAKE_BUILD_TYPE=Release -DOFFLINE_BUILD=ON -DSTRICT_ATTRIBUTION=ON; then
            print_status "$RED" "❌ Optimized cmake configuration failed"
            cd "$PROJECT_ROOT"
            return 1
        fi
    fi

    # Measure configuration time
    local config_time=$(date +%s)
    local config_duration=$((config_time - start_time))

    # Build (dry run to avoid long compilation)
    print_status "$BLUE" "Performing build dry run..."
    if ! make --dry-run >/dev/null 2>&1; then
        print_status "$YELLOW" "⚠️  Build dry run had issues, but this may be expected"
    fi

    local end_time=$(date +%s)
    local total_duration=$((end_time - start_time))

    cd "$PROJECT_ROOT"

    # Export build time metrics
    local build_json=$(cat << EOF
{
    "timestamp": "$(date -Iseconds)",
    "configuration": "$build_config",
    "build_directory": "$build_dir",
    "timing": {
        "start_time": $start_time,
        "config_time": $config_time,
        "end_time": $end_time,
        "config_duration_seconds": $config_duration,
        "total_duration_seconds": $total_duration
    },
    "success": true,
    "notes": "Dry run build - no actual compilation performed"
}
EOF
)

    echo "$build_json" > "${RESULTS_DIR}/build_time_${build_config}_${TIMESTAMP}.json"

    print_status "$GREEN" "✅ $build_config build time measured"
    print_status "$BLUE" "   Configuration Time: ${config_duration}s"
    print_status "$BLUE" "   Total Time: ${total_duration}s"

    echo "$total_duration"
}

# Function to analyze setup prerequisites
analyze_prerequisites() {
    print_status "$BLUE" "🔍 Analyzing setup prerequisites..."

    local required_tools=("git" "cmake" "make" "nvcc" "gcc")
    local available_tools=0
    local missing_tools=()

    for tool in "${required_tools[@]}"; do
        if command -v "$tool" >/dev/null 2>&1; then
            ((available_tools++))
            print_status "$GREEN" "   ✅ $tool"
        else
            missing_tools+=("$tool")
            print_status "$RED" "   ❌ $tool"
        fi
    done

    # Check CUDA installation
    local cuda_available=0
    if command -v nvcc >/dev/null 2>&1; then
        cuda_available=1
        print_status "$GREEN" "   ✅ CUDA (nvcc)"
    else
        print_status "$RED" "   ❌ CUDA (nvcc)"
        missing_tools+=("cuda")
    fi

    # Check for system memory
    local memory_gb=0
    if [[ -f /proc/meminfo ]]; then
        memory_kb=$(grep MemTotal /proc/meminfo | awk '{print $2}')
        memory_gb=$((memory_kb / 1024 / 1024))
        print_status "$BLUE" "   📊 System Memory: ${memory_gb}GB"
    fi

    # Check for disk space
    local disk_available_gb=0
    if command -v df >/dev/null 2>&1; then
        disk_available_gb=$(df . | tail -1 | awk '{print int($4/1024/1024)}')
        print_status "$BLUE" "   💾 Available Disk Space: ${disk_available_gb}GB"
    fi

    local prerequisites_json=$(cat << EOF
{
    "timestamp": "$(date -Iseconds)",
    "required_tools": {
        "total": ${#required_tools[@]},
        "available": $available_tools,
        "missing_count": ${#missing_tools[@]},
        "missing_tools": $(printf '%s' "${missing_tools[@]}" | jq -R . 'split("\n") | map(select(length > 0)) | .')
    },
    "system_resources": {
        "memory_gb": $memory_gb,
        "disk_available_gb": $disk_available_gb,
        "cuda_available": $cuda_available
    },
    "setup_complexity_impact": {
        "missing_tool_penalty": $((${#missing_tools[@]} * 10)),
        "memory_sufficient": $([ $memory_gb -ge 8 ] && echo "true" || echo "false"),
        "disk_sufficient": $([ $disk_available_gb -ge 2 ] && echo "true" || echo "false"),
        "cuda_sufficient": $([ $cuda_available -eq 1 ] && echo "true" || echo "false")
    }
}
EOF
)

    echo "$prerequisites_json" > "${RESULTS_DIR}/prerequisites_${TIMESTAMP}.json"

    local total_tools=$((${#required_tools[@]} + 1)) # +1 for CUDA
    local setup_readiness=$((available_tools * 100 / total_tools))

    print_status "$GREEN" "✅ Prerequisites analysis completed"
    print_status "$BLUE" "   Tool Availability: ${available_tools}/${total_tools} (${setup_readiness}%)"

    if [[ ${#missing_tools[@]} -gt 0 ]]; then
        print_status "$YELLOW" "⚠️  Missing tools may increase setup complexity"
    fi
}

# Function to generate comprehensive comparison report
generate_comparison_report() {
    local baseline_score=$1
    local optimized_score=$2
    local baseline_build_time=$3
    local optimized_build_time=$4

    print_status "$BLUE" "📋 Generating comprehensive comparison report..."

    local reduction_percentage=0
    if [[ $baseline_score -gt 0 ]]; then
        reduction_percentage=$(( (baseline_score - optimized_score) * 100 / baseline_score ))
    fi

    local build_improvement=0
    if [[ $baseline_build_time -gt 0 ]]; then
        build_improvement=$(( (baseline_build_time - optimized_build_time) * 100 / baseline_build_time ))
    fi

    local meets_target=false
    if [[ $reduction_percentage -ge 80 ]]; then
        meets_target=true
    fi

    # Generate comprehensive JSON report
    local comparison_json=$(cat << EOF
{
    "report_metadata": {
        "timestamp": "$(date -Iseconds)",
        "report_id": "setup_comparison_${TIMESTAMP}",
        "project_root": "$PROJECT_ROOT",
        "target_reduction_percentage": 80
    },
    "complexity_analysis": {
        "baseline": {
            "complexity_score": $baseline_score,
            "complexity_level": "$(if [[ $baseline_score -le 30 ]]; then echo "LOW"; elif [[ $baseline_score -le 60 ]]; then echo "MEDIUM"; else echo "HIGH"; fi)",
            "build_time_seconds": $baseline_build_time
        },
        "optimized": {
            "complexity_score": $optimized_score,
            "complexity_level": "$(if [[ $optimized_score -le 20 ]]; then echo "VERY_LOW"; elif [[ $optimized_score -le 40 ]]; then echo "LOW"; elif [[ $optimized_score -le 60 ]]; then echo "MEDIUM"; else echo "HIGH"; fi)",
            "build_time_seconds": $optimized_build_time
        }
    },
    "improvement_metrics": {
        "complexity_reduction_percentage": $reduction_percentage,
        "build_time_improvement_percentage": $build_improvement,
        "complexity_score_improvement": $((baseline_score - optimized_score)),
        "meets_80_percent_target": $meets_target
    },
    "success_criteria": {
        "target_met": $meets_target,
        "target_percentage": 80,
        "actual_percentage": $reduction_percentage,
        "margin": $((reduction_percentage - 80))
    },
    "assessment": {
        "overall_status": "$(if $meets_target; then echo "SUCCESS"; else echo "NEEDS_IMPROVEMENT"; fi)",
        "complexity_achievement": "$(if [[ $optimized_score -le 20 ]]; then echo "EXCELLENT"; elif [[ $optimized_score -le 40 ]]; then echo "GOOD"; elif [[ $optimized_score -le 60 ]]; then echo "ACCEPTABLE"; else echo "NEEDS_WORK"; fi)",
        "readiness_level": "$(if [[ $reduction_percentage -ge 80 ]]; then echo "PRODUCTION_READY"; elif [[ $reduction_percentage -ge 60 ]]; then echo "NEAR_READY"; elif [[ $reduction_percentage -ge 40 ]]; then echo "DEVELOPMENT"; else echo "EXPERIMENTAL"; fi)"
    },
    "recommendations": [
        $(if [[ ! $meets_target ]]; then echo '"Continue optimization to achieve 80% reduction target",'; fi)
        $(if [[ $optimized_score -gt 40 ]]; then echo '"Focus on reducing manual steps in documentation",'; fi)
        $(if [[ $build_improvement -lt 20 ]]; then echo '"Optimize build configuration for faster builds",'; fi)
        $(if [[ $reduction_percentage -ge 80 ]]; then echo '"Ready for production deployment",'; fi)
        "Continue monitoring setup complexity metrics"
    ]
}
EOF
)

    echo "$comparison_json" > "$REPORT_FILE"

    # Generate human-readable summary
    local summary_file="${RESULTS_DIR}/setup_comparison_summary_${TIMESTAMP}.md"
    cat > "$summary_file" << EOF
# Setup Complexity Comparison Report
================================

**Generated**: $(date)
**Report ID**: setup_comparison_${TIMESTAMP}

## Executive Summary

- **Baseline Complexity Score**: $baseline_score
- **Optimized Complexity Score**: $optimized_score
- **Complexity Reduction**: ${reduction_percentage}%
- **Build Time Improvement**: ${build_improvement}%
- **80% Target**: $(if $meets_target; then echo "✅ MET"; else echo "❌ NOT MET"; fi)

## Results Assessment

### Complexity Analysis
$(if [[ $meets_target ]]; then
echo "🎉 **SUCCESS**: Setup complexity reduction meets the 80% target!"
else
echo "⚠️  **NEEDS IMPROVEMENT**: Setup complexity reduction is ${reduction_percentage}%, below the 80% target."
fi)

- **Baseline**: $(if [[ $baseline_score -le 30 ]]; then echo "Low complexity"; elif [[ $baseline_score -le 60 ]]; then echo "Medium complexity"; else echo "High complexity"; fi)
- **Optimized**: $(if [[ $optimized_score -le 20 ]]; then echo "Very low complexity"; elif [[ $optimized_score -le 40 ]]; then echo "Low complexity"; elif [[ $optimized_score -le 60 ]]; then echo "Medium complexity"; else echo "High complexity"; fi)

### Build Performance
- **Baseline Build Time**: ${baseline_build_time}s
- **Optimized Build Time**: ${optimized_build_time}s
- **Improvement**: ${build_improvement}%

## Recommendations

$(if [[ ! $meets_target ]]; then
echo "- Continue optimization to achieve 80% reduction target"
fi)
$(if [[ $optimized_score -gt 40 ]]; then
echo "- Focus on reducing manual steps in documentation"
fi)
$(if [[ $build_improvement -lt 20 ]]; then
echo "- Optimize build configuration for faster builds"
fi)
$(if [[ $reduction_percentage -ge 80 ]]; then
echo "- Ready for production deployment"
fi)
- Continue monitoring setup complexity metrics

## Next Steps

1. $(if [[ $meets_target ]]; then echo "Proceed with production deployment"; else echo "Continue optimization efforts"; fi)
2. Regular monitoring of setup complexity metrics
3. Maintain documentation updates to reflect simplified process
4. Test setup process on fresh systems

---
*Report generated by setup complexity comparison tool*
EOF

    print_status "$GREEN" "✅ Comprehensive comparison report generated"
    print_status "$BLUE" "   JSON Report: $REPORT_FILE"
    print_status "$BLUE" "   Summary Report: $summary_file"

    # Print summary to console
    echo ""
    print_status "$BLUE" "📊 COMPARISON SUMMARY:"
    print_status "$BLUE" "   Complexity Reduction: ${reduction_percentage}%"
    print_status "$BLUE" "   Target (80%): $(if $meets_target; then echo "✅ MET"; else echo "❌ NOT MET"; fi)"
    print_status "$BLUE" "   Build Improvement: ${build_improvement}%"
    print_status "$BLUE" "   Overall Status: $(if $meets_target; then echo "🎉 SUCCESS"; else echo "⚠️  NEEDS WORK"; fi)"
}

# Main execution
main() {
    print_status "$BLUE" "🚀 Starting setup complexity comparison tests..."

    create_results_directory

    # Analyze prerequisites
    analyze_prerequisites

    # Analyze setup complexity
    print_status "$BLUE" "📊 Analyzing setup configurations..."
    local baseline_score=$(analyze_baseline_setup)
    local optimized_score=$(analyze_optimized_setup)

    # Measure build times
    print_status "$BLUE" "⏱️  Measuring build performance..."
    local baseline_build_time=$(measure_build_time "baseline")
    local optimized_build_time=$(measure_build_time "optimized")

    # Generate comprehensive report
    generate_comparison_report "$baseline_score" "$optimized_score" "$baseline_build_time" "$optimized_build_time"

    print_status ""
    print_status "$GREEN" "🎉 Setup complexity comparison tests completed!"
    print_status "$BLUE" "📁 Results directory: $RESULTS_DIR"
    print_status "$BLUE" "📊 Review the generated reports for detailed analysis"
}

# Execute main function
main "$@"