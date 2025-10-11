#!/bin/bash

# Setup Complexity Measurement Script
# Measures current setup complexity to establish baseline for 80% reduction target

set -euo pipefail

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
REPORTS_DIR="${PROJECT_ROOT}/reports"
TEMP_DIR="${PROJECT_ROOT}/temp"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Logging
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

# Initialize directories
init_directories() {
    log_info "Initializing setup complexity measurement directories..."

    mkdir -p "$REPORTS_DIR"
    mkdir -p "$TEMP_DIR"

    log_success "Directories initialized"
}

# Count manual setup steps required
count_setup_steps() {
    log_info "Counting current setup steps..."

    local steps=0
    local step_details=()

    # Check for git submodules
    if [[ -f "${PROJECT_ROOT}/.gitmodules" ]]; then
        local submodule_count=$(grep -c "^\[submodule" "${PROJECT_ROOT}/.gitmodules" 2>/dev/null || echo 0)
        if [[ $submodule_count -gt 0 ]]; then
            ((steps++))
            step_details+=("Initialize $submodule_count git submodules")
        fi
    fi

    # Check for external dependencies in CMakeLists.txt
    if [[ -f "${PROJECT_ROOT}/CMakeLists.txt" ]]; then
        local external_deps=$(grep -c "FetchContent\|find_package\|git clone" "${PROJECT_ROOT}/CMakeLists.txt" 2>/dev/null || echo 0)
        if [[ $external_deps -gt 0 ]]; then
            ((steps++))
            step_details+=("Install $external_deps external dependencies")
        fi
    fi

    # Check for system package dependencies
    local system_deps=0
    if command -v apt-get >/dev/null 2>&1; then
        system_deps=$(grep -c "apt-get install\|apt install" "${PROJECT_ROOT}/README.md" 2>/dev/null || echo 0)
    elif command -v yum >/dev/null 2>&1; then
        system_deps=$(grep -c "yum install" "${PROJECT_ROOT}/README.md" 2>/dev/null || echo 0)
    fi

    if [[ $system_deps -gt 0 ]]; then
        ((steps++))
        step_details+=("Install $system_deps system packages")
    fi

    # Check for CUDA toolkit requirement
    if grep -q "CUDA\|nvcc" "${PROJECT_ROOT}/CMakeLists.txt" 2>/dev/null; then
        ((steps++))
        step_details+=("Install CUDA Toolkit")
    fi

    # Check for CMake requirement
    if [[ -f "${PROJECT_ROOT}/CMakeLists.txt" ]]; then
        local cmake_version=$(grep -o "cmake_version [0-9.]*" "${PROJECT_ROOT}/CMakeLists.txt" 2>/dev/null | head -1 | cut -d' ' -f2 || echo "3.22")
        ((steps++))
        step_details+=("Install CMake >= $cmake_version")
    fi

    # Check for build directory creation
    ((steps++))
    step_details+=("Create build directory")

    # Check for CMake configuration
    ((steps++))
    step_details+=("Run CMake configuration")

    # Check for compilation
    ((steps++))
    step_details+=("Compile project")

    # Check for testing/validation
    ((steps++))
    step_details+=("Run tests/validation")

    echo "$steps"

    # Save step details
    printf '%s\n' "${step_details[@]}" > "${TEMP_DIR}/setup_steps.txt"
}

# Measure time-to-first-build
measure_time_to_first_build() {
    log_info "Measuring time-to-first-build..."

    local temp_build_dir="${TEMP_DIR}/fresh_build_test_$(date +%s)"
    local start_time end_time duration

    # Create temporary build directory
    mkdir -p "$temp_build_dir"

    # Record start time
    start_time=$(date +%s)

    # Run CMake configuration (if CMakeLists.txt exists)
    if [[ -f "${PROJECT_ROOT}/CMakeLists.txt" ]]; then
        cd "$temp_build_dir"
        if cmake "$PROJECT_ROOT" >/dev/null 2>&1; then
            # Try to build (limited to avoid long builds)
            if timeout 300 make -j$(nproc) >/dev/null 2>&1; then
                end_time=$(date +%s)
                duration=$((end_time - start_time))
            else
                # If build fails or times out, measure configuration time only
                end_time=$(date +%s)
                duration=$((end_time - start_time))
                log_warning "Build timed out or failed, measuring configuration time only"
            fi
        else
            end_time=$(date +%s)
            duration=$((end_time - start_time))
            log_warning "CMake configuration failed, measuring command execution time only"
        fi
    else
        # No CMakeLists.txt, measure basic setup time
        end_time=$(date +%s)
        duration=$((end_time - start_time))
        log_warning "No CMakeLists.txt found, measuring basic setup time"
    fi

    # Cleanup
    cd "$PROJECT_ROOT"
    rm -rf "$temp_build_dir"

    echo "$duration"
}

# Assess knowledge prerequisites
assess_knowledge_prerequisites() {
    log_info "Assessing knowledge prerequisites..."

    local knowledge_score=0
    local max_score=0
    local knowledge_details=()

    # Check for README complexity
    if [[ -f "${PROJECT_ROOT}/README.md" ]]; then
        local readme_lines=$(wc -l < "${PROJECT_ROOT}/README.md")
        local readme_sections=$(grep -c "^#" "${PROJECT_ROOT}/README.md" 2>/dev/null || echo 0)

        ((max_score += 2))
        if [[ $readme_lines -lt 100 ]]; then
            ((knowledge_score++))
            knowledge_details+=("README is concise ($readme_lines lines)")
        else
            knowledge_details+=("README is lengthy ($readme_lines lines)")
        fi

        if [[ $readme_sections -lt 10 ]]; then
            ((knowledge_score++))
            knowledge_details+=("README has reasonable structure ($readme_sections sections)")
        else
            knowledge_details+=("README has complex structure ($readme_sections sections)")
        fi
    fi

    # Check for documentation complexity
    local doc_dirs=$(find "$PROJECT_ROOT" -name "doc*" -type d 2>/dev/null | wc -l)
    local doc_files=$(find "$PROJECT_ROOT" -name "*.md" -type f 2>/dev/null | wc -l)

    ((max_score += 2))
    if [[ $doc_dirs -le 2 ]]; then
        ((knowledge_score++))
        knowledge_details+=("Limited documentation directories ($doc_dirs)")
    else
        knowledge_details+=("Many documentation directories ($doc_dirs)")
    fi

    if [[ $doc_files -le 5 ]]; then
        ((knowledge_score++))
        knowledge_details+=("Reasonable documentation files ($doc_files)")
    else
        knowledge_details+=("Extensive documentation required ($doc_files files)")
    fi

    # Check for tool requirements
    local tools=0
    if [[ -f "${PROJECT_ROOT}/CMakeLists.txt" ]]; then
        ((tools++))
    fi
    if grep -q "CUDA\|nvcc" "${PROJECT_ROOT}/CMakeLists.txt" 2>/dev/null; then
        ((tools++))
    fi
    if [[ -f "${PROJECT_ROOT}/.gitmodules" ]]; then
        ((tools++))
    fi

    ((max_score += 1))
    if [[ $tools -le 2 ]]; then
        ((knowledge_score++))
        knowledge_details+=("Limited tool requirements ($tools specialized tools)")
    else
        knowledge_details+=("Multiple tool requirements ($tools specialized tools)")
    fi

    # Calculate complexity score (inverse of simplicity score)
    local complexity_score=$((max_score - knowledge_score))
    echo "$complexity_score"

    # Save knowledge details
    printf '%s\n' "${knowledge_details[@]}" > "${TEMP_DIR}/knowledge_prerequisites.txt"
}

# Generate setup complexity report
generate_complexity_report() {
    log_info "Generating setup complexity report..."

    local setup_steps count_setup_steps
    local time_to_build measure_time_to_first_build
    local knowledge_score assess_knowledge_prerequisites
    local overall_complexity

    # Get measurements
    setup_steps=$(count_setup_steps)
    time_to_build=$(measure_time_to_first_build)
    knowledge_score=$(assess_knowledge_prerequisites)

    # Calculate overall complexity score (0-100, higher is more complex)
    local step_complexity=$((setup_steps * 10))  # Each step = 10 points
    local time_complexity=$((time_to_build / 6))   # Each minute = ~1.7 points
    local knowledge_complexity=$((knowledge_score * 15))  # Knowledge complexity

    overall_complexity=$((step_complexity + time_complexity + knowledge_complexity))

    # Ensure score is within 0-100 range
    if [[ $overall_complexity -gt 100 ]]; then
        overall_complexity=100
    fi

    # Generate report
    local report_file="${REPORTS_DIR}/setup-complexity-baseline-$(date +%Y%m%d_%H%M%S).json"

    cat > "$report_file" << EOF
{
    "setup_complexity_baseline": {
        "timestamp": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
        "measurements": {
            "setup_steps": {
                "count": $setup_steps,
                "details": $(cat "${TEMP_DIR}/setup_steps.txt" | jq -R . | jq -s . 2>/dev/null || echo '["No steps recorded"]')
            },
            "time_to_first_build": {
                "seconds": $time_to_build,
                "minutes": $(echo "scale=1; $time_to_build / 60" | bc -l 2>/dev/null || echo "0")
            },
            "knowledge_prerequisites": {
                "complexity_score": $knowledge_score,
                "details": $(cat "${TEMP_DIR}/knowledge_prerequisites.txt" | jq -R . | jq -s . 2>/dev/null || echo '["No knowledge prerequisites recorded"]')
            }
        },
        "overall_complexity": {
            "score": $overall_complexity,
            "max_score": 100,
            "interpretation": "$(
                if [[ $overall_complexity -lt 30 ]]; then
                    echo "Low complexity"
                elif [[ $overall_complexity -lt 60 ]]; then
                    echo "Medium complexity"
                else
                    echo "High complexity"
                fi
            )"
        },
        "reduction_target": {
            "target_reduction_percent": 80,
            "target_score": $((overall_complexity * 20 / 100)),
            "current_vs_target": {
                "current": $overall_complexity,
                "target": $((overall_complexity * 20 / 100)),
                "reduction_needed": $((overall_complexity - (overall_complexity * 20 / 100)))
            }
        }
    }
}
