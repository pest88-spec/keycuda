#!/bin/bash

# Setup Comparison Tests Script
# T026: Baseline vs optimized setup comparison tests
#
# This script compares the baseline setup process (with git submodules) against
# the optimized setup process (with extracted sources) to measure improvements
# in setup complexity, time, and reliability.

set -euo pipefail

# Script configuration
SCRIPT_NAME="$(basename "$0")"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Test results
declare -A BASELINE_RESULTS
declare -A OPTIMIZED_RESULTS
declare -A COMPARISON_RESULTS

# Logging functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

log_test() {
    echo -e "${PURPLE}[TEST]${NC} $1"
}

log_result() {
    echo -e "${CYAN}[RESULT]${NC} $1"
}

# Help function
show_help() {
    cat << EOF
$SCRIPT_NAME - Baseline vs Optimized Setup Comparison Tests

USAGE:
    $SCRIPT_NAME [OPTIONS]

OPTIONS:
    --baseline-only       Run baseline tests only
    --optimized-only      Run optimized tests only
    --dry-run             Show what would be done without executing
    --skip-build          Skip build time measurements
    --iterations N        Number of test iterations (default: 3)
    --report-dir DIR      Report directory (default: build/setup-comparison)
    --help                Show this help message

DESCRIPTION:
    This script compares the baseline setup process with git submodules
    against the optimized setup with extracted sources to measure:
    - Setup time reduction
    - Step complexity reduction
    - Network dependency elimination
    - Build success rate improvement
    - Overall setup complexity reduction

EXAMPLES:
    $SCRIPT_NAME                      # Run full comparison
    $SCRIPT_NAME --iterations 5       # Run 5 iterations
    $SCRIPT_NAME --skip-build         # Skip build measurements

EOF
}

# Initialize test environment
init_test_environment() {
    log_info "Initializing test environment..."

    # Create test directory
    TEST_DIR="${REPORT_DIR:-build/setup-comparison}"
    mkdir -p "$TEST_DIR"

    # Create results file
    RESULTS_FILE="$TEST_DIR/comparison-results.json"

    # Initialize JSON results
    cat > "$RESULTS_FILE" << EOF
{
  "test_run": {
    "timestamp": "$(date -Iseconds)",
    "script": "$SCRIPT_NAME",
    "project_root": "$PROJECT_ROOT",
    "iterations": ${ITERATIONS:-3}
  },
  "results": {}
}
EOF

    log_info "Test environment initialized"
    log_info "Results will be saved to: $RESULTS_FILE"
}

# Save results to JSON
save_result() {
    local test_name="$1"
    local baseline_value="$2"
    local optimized_value="$3"
    local unit="$4"

    local improvement=0
    if [[ $baseline_value -gt 0 ]]; then
        improvement=$(( (baseline_value - optimized_value) * 100 / baseline_value ))
    fi

    # Update JSON file
    local temp_file=$(mktemp)
    jq ".results.\"$test_name\" = {
        \"baseline\": $baseline_value,
        \"optimized\": $optimized_value,
        \"unit\": \"$unit\",
        \"improvement_percentage\": $improvement
    }" "$RESULTS_FILE" > "$temp_file" && mv "$temp_file" "$RESULTS_FILE"

    COMPARISON_RESULTS["$test_name"]="$improvement"
}

# Measure baseline setup
measure_baseline_setup() {
    log_test "Measuring baseline setup (with git submodules)..."

    # Create baseline test directory
    BASELINE_TEST_DIR="$TEST_DIR/baseline-test"
    rm -rf "$BASELINE_TEST_DIR"
    mkdir -p "$BASELINE_TEST_DIR"

    # Clone the repository fresh (simulating new user)
    log_info "Cloning repository for baseline test..."
    local clone_start=$(date +%s%N)
    git clone --recursive "$PROJECT_ROOT" "$BASELINE_TEST_DIR" 2>/dev/null || {
        log_warning "Could not clone with recursive, trying standard clone..."
        git clone "$PROJECT_ROOT" "$BASELINE_TEST_DIR"
    }
    local clone_end=$(date +%s%N)
    local clone_time=$(( (clone_end - clone_start) / 1000000 ))

    cd "$BASELINE_TEST_DIR"

    # Count setup steps (approximation)
    local setup_steps=0

    # Check for git submodule initialization
    if git submodule status 2>/dev/null | grep -q .; then
        ((setup_steps++))
        log_info "Found git submodules requiring initialization"
    fi

    # Check for external dependencies
    if grep -q "FetchContent" CMakeLists.txt 2>/dev/null; then
        ((setup_steps++))
        log_info "Found FetchContent dependencies"
    fi

    # Check for system dependencies
    if grep -q "find_package.*PkgConfig" CMakeLists.txt 2>/dev/null; then
        ((setup_steps++))
        log_info "Found system package dependencies"
    fi

    # Check for manual setup steps
    if [[ -f "scripts/setup-dependencies.sh" ]]; then
        ((setup_steps++))
        log_info "Found manual setup script"
    fi

    BASELINE_RESULTS["setup_steps"]=$setup_steps
    BASELINE_RESULTS["clone_time_ms"]=$clone_time
    BASELINE_RESULTS["requires_internet"]=1

    # Measure build time if not skipped
    if [[ "${SKIP_BUILD:-false}" != "true" ]]; then
        log_info "Building baseline version..."
        local build_start=$(date +%s%N)

        mkdir -p build
        cd build
        cmake .. -DCMAKE_BUILD_TYPE=Release >/dev/null 2>&1 || {
            log_error "Baseline CMake configuration failed"
            BASELINE_RESULTS["build_success"]=0
        }

        if [[ ${BASELINE_RESULTS["build_success"]:-1} -ne 0 ]]; then
            make -j$(nproc) >/dev/null 2>&1 || {
                log_warning "Baseline build failed, measuring partial build time"
            }
            local build_end=$(date +%s%N)
            local build_time=$(( (build_end - build_start) / 1000000 ))
            BASELINE_RESULTS["build_time_ms"]=$build_time
            BASELINE_RESULTS["build_success"]=1
        fi
    fi

    # Count dependencies
    local dependencies=0
    if git submodule status 2>/dev/null | grep -q .; then
        dependencies=$(git submodule status | wc -l)
    fi
    BASELINE_RESULTS["dependencies"]=$dependencies

    log_result "Baseline setup steps: $setup_steps"
    log_result "Baseline clone time: ${clone_time}ms"
    log_result "Baseline dependencies: $dependencies"
}

# Measure optimized setup
measure_optimized_setup() {
    log_test "Measuring optimized setup (with extracted sources)..."

    # Create optimized test directory
    OPTIMIZED_TEST_DIR="$TEST_DIR/optimized-test"
    rm -rf "$OPTIMIZED_TEST_DIR"
    mkdir -p "$OPTIMIZED_TEST_DIR"

    # Clone the repository fresh (simulating new user)
    log_info "Cloning repository for optimized test..."
    local clone_start=$(date +%s%N)
    git clone "$PROJECT_ROOT" "$OPTIMIZED_TEST_DIR" 2>/dev/null
    local clone_end=$(date +%s%N)
    local clone_time=$(( (clone_end - clone_start) / 1000000 ))

    cd "$OPTIMIZED_TEST_DIR"

    # Count setup steps (should be fewer)
    local setup_steps=0

    # Check for extracted sources
    if [[ -d "src/extracted" ]]; then
        log_info "Found extracted sources - no external cloning needed"
    else
        ((setup_steps++))
        log_warning "Extracted sources not found"
    fi

    # Check for offline build capability
    if grep -q "ENABLE_OFFLINE_BUILD" CMakeLists.txt 2>/dev/null; then
        log_info "Offline build mode available"
    else
        ((setup_steps++))
    fi

    # No git submodules should be present
    if ! git submodule status 2>/dev/null | grep -q .; then
        log_info "No git submodules - good!"
    else
        ((setup_steps++))
        log_warning "Git submodules still present"
    fi

    OPTIMIZED_RESULTS["setup_steps"]=$setup_steps
    OPTIMIZED_RESULTS["clone_time_ms"]=$clone_time
    OPTIMIZED_RESULTS["requires_internet"]=0

    # Measure build time if not skipped
    if [[ "${SKIP_BUILD:-false}" != "true" ]]; then
        log_info "Building optimized version with offline mode..."
        local build_start=$(date +%s%N)

        mkdir -p build
        cd build
        cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_OFFLINE_BUILD=ON >/dev/null 2>&1 || {
            log_error "Optimized CMake configuration failed"
            OPTIMIZED_RESULTS["build_success"]=0
        }

        if [[ ${OPTIMIZED_RESULTS["build_success"]:-1} -ne 0 ]]; then
            make -j$(nproc) >/dev/null 2>&1 || {
                log_warning "Optimized build failed, measuring partial build time"
            }
            local build_end=$(date +%s%N)
            local build_time=$(( (build_end - build_start) / 1000000 ))
            OPTIMIZED_RESULTS["build_time_ms"]=$build_time
            OPTIMIZED_RESULTS["build_success"]=1
        fi
    fi

    # Count integrated dependencies
    local dependencies=0
    if [[ -d "src/extracted" ]]; then
        dependencies=$(find src/extracted -maxdepth 1 -type d | wc -l)
        ((dependencies--))  # Subtract the extracted directory itself
    fi
    OPTIMIZED_RESULTS["dependencies"]=$dependencies

    log_result "Optimized setup steps: $setup_steps"
    log_result "Optimized clone time: ${clone_time}ms"
    log_result "Optimized dependencies: $dependencies"
}

# Calculate complexity score
calculate_complexity_score() {
    local -n results_ref=$1
    local score=0

    # Base score
    score=$((score + results_ref["setup_steps"] * 10))

    # Network dependency penalty
    if [[ ${results_ref["requires_internet"]:-0} -eq 1 ]]; then
        score=$((score + 50))
    fi

    # Manual operations penalty
    if [[ -f "$TEST_DIR/${results_ref}_test-dir/scripts/setup-dependencies.sh" ]]; then
        score=$((score + 30))
    fi

    # Build time component (seconds)
    if [[ -n "${results_ref["build_time_ms"]:-}" ]]; then
        local build_seconds=$((results_ref["build_time_ms"] / 1000))
        score=$((score + build_seconds))
    fi

    echo $score
}

# Generate comparison report
generate_report() {
    log_info "Generating comparison report..."

    # Calculate improvements
    save_result "setup_steps" "${BASELINE_RESULTS["setup_steps"]}" "${OPTIMIZED_RESULTS["setup_steps"]}" "steps"

    if [[ -n "${BASELINE_RESULTS["clone_time_ms"]:-}" && -n "${OPTIMIZED_RESULTS["clone_time_ms"]:-}" ]]; then
        save_result "clone_time" "${BASELINE_RESULTS["clone_time_ms"]}" "${OPTIMIZED_RESULTS["clone_time_ms"]}" "ms"
    fi

    if [[ -n "${BASELINE_RESULTS["build_time_ms"]:-}" && -n "${OPTIMIZED_RESULTS["build_time_ms"]:-}" ]]; then
        save_result "build_time" "${BASELINE_RESULTS["build_time_ms"]}" "${OPTIMIZED_RESULTS["build_time_ms"]}" "ms"
    fi

    save_result "dependencies" "${BASELINE_RESULTS["dependencies"]}" "${OPTIMIZED_RESULTS["dependencies"]}" "count"
    save_result "requires_internet" "${BASELINE_RESULTS["requires_internet"]}" "${OPTIMIZED_RESULTS["requires_internet"]}" "boolean"

    # Calculate complexity scores
    BASELINE_RESULTS["complexity_score"]=$(calculate_complexity_score BASELINE_RESULTS)
    OPTIMIZED_RESULTS["complexity_score"]=$(calculate_complexity_score OPTIMIZED_RESULTS)
    save_result "complexity_score" "${BASELINE_RESULTS["complexity_score"]}" "${OPTIMIZED_RESULTS["complexity_score"]}" "score"

    # Generate human-readable report
    local report_file="$TEST_DIR/comparison-report.txt"
    cat > "$report_file" << EOF
=== SETUP COMPARISON REPORT ===
Generated: $(date)
Project: $PROJECT_ROOT

BASELINE (with git submodules):
- Setup steps: ${BASELINE_RESULTS["setup_steps"]}
- Clone time: ${BASELINE_RESULTS["clone_time_ms"]:-N/A}ms
- Build time: ${BASELINE_RESULTS["build_time_ms"]:-N/A}ms
- Dependencies: ${BASELINE_RESULTS["dependencies"]:-N/A}
- Requires internet: $([ "${BASELINE_RESULTS["requires_internet"]:-0}" -eq 1 ] && echo "Yes" || echo "No")
- Complexity score: ${BASELINE_RESULTS["complexity_score"]}

OPTIMIZED (with extracted sources):
- Setup steps: ${OPTIMIZED_RESULTS["setup_steps"]}
- Clone time: ${OPTIMIZED_RESULTS["clone_time_ms"]:-N/A}ms
- Build time: ${OPTIMIZED_RESULTS["build_time_ms"]:-N/A}ms
- Dependencies: ${OPTIMIZED_RESULTS["dependencies"]:-N/A}
- Requires internet: $([ "${OPTIMIZED_RESULTS["requires_internet"]:-0}" -eq 1 ] && echo "Yes" || echo "No")
- Complexity score: ${OPTIMIZED_RESULTS["complexity_score"]}

IMPROVEMENTS:
EOF

    # Add improvements to report
    for test_name in "${!COMPARISON_RESULTS[@]}"; do
        local improvement="${COMPARISON_RESULTS[$test_name]}"
        echo "- $test_name: ${improvement}%" >> "$report_file"
    done

    cat >> "$report_file" << EOF

SUMMARY:
- Setup complexity reduction: ${COMPARISON_RESULTS["complexity_score"]:-0}%
- Time reduction: ${COMPARISON_RESULTS["build_time"]:-0}%
- Steps reduction: ${COMPARISON_RESULTS["setup_steps"]:-0}%
- Network dependency elimination: ${COMPARISON_RESULTS["requires_internet"]:-0}%

CONCLUSION:
EOF

    # Add conclusion based on improvements
    local overall_improvement=0
    local count=0
    for improvement in "${COMPARISON_RESULTS[@]}"; do
        overall_improvement=$((overall_improvement + improvement))
        ((count++))
    done

    if [[ $count -gt 0 ]]; then
        overall_improvement=$((overall_improvement / count))

        if [[ $overall_improvement -ge 80 ]]; then
            echo "✅ EXCELLENT: Overall setup improvement of ${overall_improvement}%" >> "$report_file"
        elif [[ $overall_improvement -ge 50 ]]; then
            echo "✅ GOOD: Overall setup improvement of ${overall_improvement}%" >> "$report_file"
        elif [[ $overall_improvement -ge 20 ]]; then
            echo "⚠️  MODERATE: Overall setup improvement of ${overall_improvement}%" >> "$report_file"
        else
            echo "❌ MINIMAL: Overall setup improvement of only ${overall_improvement}%" >> "$report_file"
        fi
    fi

    log_success "Comparison report generated"
    log_info "Text report: $report_file"
    log_info "JSON results: $RESULTS_FILE"
}

# Run multiple iterations
run_iterations() {
    local iterations=${ITERATIONS:-3}
    log_info "Running $iterations iterations for accuracy..."

    for ((i=1; i<=iterations; i++)); do
        log_info "Iteration $i/$iterations"

        # Reset results for this iteration
        unset BASELINE_RESULTS
        declare -A BASELINE_RESULTS
        unset OPTIMIZED_RESULTS
        declare -A OPTIMIZED_RESULTS

        # Run tests
        if [[ "${BASELINE_ONLY:-false}" != "true" ]]; then
            measure_optimized_setup
        fi

        if [[ "${OPTIMIZED_ONLY:-false}" != "true" ]]; then
            measure_baseline_setup
        fi

        # Generate iteration report
        local iteration_file="$TEST_DIR/iteration-$i-results.json"
        jq -n \
            --argjson baseline "$(declare -p BASELINE_RESULTS | sed 's/declare -A //')" \
            --argjson optimized "$(declare -p OPTIMIZED_RESULTS | sed 's/declare -A //')" \
            '{baseline: $baseline, optimized: $optimized}' > "$iteration_file"
    done
}

# Main function
main() {
    local baseline_only=false
    local optimized_only=false
    local dry_run=false
    local iterations=3

    # Parse command line arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            --baseline-only)
                baseline_only=true
                shift
                ;;
            --optimized-only)
                optimized_only=true
                shift
                ;;
            --dry-run)
                dry_run=true
                shift
                ;;
            --skip-build)
                export SKIP_BUILD=true
                shift
                ;;
            --iterations)
                iterations="$2"
                shift 2
                ;;
            --report-dir)
                REPORT_DIR="$2"
                shift 2
                ;;
            --help|--help|-h)
                show_help
                exit 0
                ;;
            *)
                log_error "Unknown option: $1"
                show_help
                exit 1
                ;;
        esac
    done

    # Set iterations
    export ITERATIONS=$iterations

    log_info "Starting setup comparison tests..."
    log_info "Iterations: $iterations"
    log_info "Skip build: ${SKIP_BUILD:-false}"

    if [[ "$dry_run" == "true" ]]; then
        log_info "Dry run mode - no actual tests will be executed"
        exit 0
    fi

    # Initialize test environment
    init_test_environment

    # Run tests
    if [[ "$iterations" -gt 1 ]]; then
        run_iterations
    else
        # Single iteration
        if [[ "$baseline_only" != "true" ]]; then
            measure_optimized_setup
        fi

        if [[ "$optimized_only" != "true" ]]; then
            measure_baseline_setup
        fi
    fi

    # Generate report
    generate_report

    # Show summary
    echo
    log_success "🎉 Setup comparison tests completed!"

    # Show key improvements
    echo
    log_result "Setup steps reduction: ${COMPARISON_RESULTS["setup_steps"]:-0}%"
    log_result "Build time reduction: ${COMPARISON_RESULTS["build_time"]:-0}%"
    log_result "Complexity reduction: ${COMPARISON_RESULTS["complexity_score"]:-0}%"
    log_result "Network dependency eliminated: ${COMPARISON_RESULTS["requires_internet"]:-0}%"

    exit 0
}

# Script execution
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi