#!/bin/bash

# Build Benchmark with Validation
# Implements T065 with error handling and build issue documentation

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
LOG_DIR="$PROJECT_ROOT/logs/benchmarks"
RESULTS_FILE="$LOG_DIR/build_benchmark_results.json"

# Benchmark configuration
readonly BUILD_TARGET_SECONDS=300  # 5 minutes
readonly CLEAN_BUILD=true

# Ensure directories exist
mkdir -p "$LOG_DIR"

# Color codes
readonly RED='\033[0;31m'
readonly GREEN='\033[0;32m'
readonly YELLOW='\033[1;33m'
readonly BLUE='\033[0;34m'
readonly NC='\033[0m'

# Logging
log_info() {
    echo -e "${BLUE}[INFO]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1" | tee -a "$LOG_DIR/benchmark.log"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1" | tee -a "$LOG_DIR/benchmark.log"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1" | tee -a "$LOG_DIR/benchmark.log"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1" | tee -a "$LOG_DIR/benchmark.log"
}

# Initialize results
init_results() {
    cat > "$RESULTS_FILE" << EOF
{
    "benchmark_run": {
        "timestamp": "$(date -Iseconds)",
        "target_build_seconds": $BUILD_TARGET_SECONDS,
        "clean_build": $CLEAN_BUILD,
        "status": "running"
    },
    "build_results": {
        "cmake_success": false,
        "make_success": false,
        "build_time_seconds": 0,
        "build_errors": [],
        "build_warnings": [],
        "compilation_issues": []
    },
    "system_metrics": {
        "cpu_cores": $(nproc),
        "memory_gb": $(free -g | awk '/^Mem:/{print $2}'),
        "disk_space_gb": $(df -BG . | awk 'NR==2{print $4}' | sed 's/G//'),
        "cuda_available": $([ -x "$(command -v nvidia-smi)" ] && echo "true" || echo "false")
    }
}
EOF
}

# Clean build environment
clean_build_environment() {
    log_info "Cleaning build environment"

    if [[ -d "$PROJECT_ROOT/build" ]]; then
        rm -rf "$PROJECT_ROOT/build"
        log_info "Removed existing build directory"
    fi

    log_success "Build environment cleaned"
}

# Run CMake configuration
run_cmake() {
    log_info "Running CMake configuration"
    local cmake_start=$(date +%s.%N)

    mkdir -p "$PROJECT_ROOT/build"
    cd "$PROJECT_ROOT/build"

    if cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_DEPLOYMENT=OFF > "$LOG_DIR/cmake.log" 2>&1; then
        local cmake_end=$(date +%s.%N)
        local cmake_time=$(echo "$cmake_end - $cmake_start" | bc -l)

        log_success "CMake configuration completed in ${cmake_time}s"

        # Update results
        local temp_file=$(mktemp)
        jq --arg cmake_time "$cmake_time" '
        .build_results.cmake_success = true |
        .build_results.cmake_time_seconds = ($cmake_time | tonumber)
        ' "$RESULTS_FILE" > "$temp_file"
        mv "$temp_file" "$RESULTS_FILE"

        return 0
    else
        log_error "CMake configuration failed"

        # Extract errors from log
        local errors=$(grep -i "error:" "$LOG_DIR/cmake.log" | head -10 | jq -R . | jq -s .)
        local warnings=$(grep -i "warning:" "$LOG_DIR/cmake.log" | head -5 | jq -R . | jq -s .)

        # Update results with errors
        local temp_file=$(mktemp)
        jq --argjson errors "$errors" \
           --argjson warnings "$warnings" \
           '
        .build_results.cmake_success = false |
        .build_results.build_errors = $errors |
        .build_results.build_warnings = $warnings
        ' "$RESULTS_FILE" > "$temp_file"
        mv "$temp_file" "$RESULTS_FILE"

        return 1
    fi
}

# Run build
run_build() {
    log_info "Running build"
    local build_start=$(date +%s.%N)

    cd "$PROJECT_ROOT/build"

    # Try to build with timeout
    if timeout $((BUILD_TARGET_SECONDS + 60)) make -j$(nproc) > "$LOG_DIR/make.log" 2>&1; then
        local build_end=$(date +%s.%N)
        local build_time=$(echo "$build_end - $build_start" | bc -l)

        log_success "Build completed successfully in ${build_time}s"

        # Check if binary was created
        local binary_size=0
        if [[ -f "$PROJECT_ROOT/build/Puzzle71Solver" ]]; then
            binary_size=$(stat -c%s "$PROJECT_ROOT/build/Puzzle71Solver" 2>/dev/null || echo "0")
            log_success "Binary created: $(echo "$binary_size" | numfmt --to=iec)B"
        fi

        # Update results
        local temp_file=$(mktemp)
        jq --arg build_time "$build_time" \
           --arg binary_size "$binary_size" \
           '
        .build_results.make_success = true |
        .build_results.build_time_seconds = ($build_time | tonumber) |
        .build_results.binary_size_bytes = ($binary_size | tonumber)
        ' "$RESULTS_FILE" > "$temp_file"
        mv "$temp_file" "$RESULTS_FILE"

        return 0
    else
        local exit_code=$?
        local build_end=$(date +%s.%N)
        local build_time=$(echo "$build_end - $build_start" | bc -l)

        if [[ $exit_code -eq 124 ]]; then
            log_error "Build timed out after ${build_time}s"
        else
            log_error "Build failed after ${build_time}s (exit code: $exit_code)"
        fi

        # Extract compilation errors
        local errors=$(grep -E "error:|Error:" "$LOG_DIR/make.log" | head -20 | jq -R . | jq -s .)
        local warnings=$(grep -E "warning:|Warning:" "$LOG_DIR/make.log" | head -10 | jq -R . | jq -s .)

        # Categorize issues
        local missing_includes=$(grep -E "has not been declared|not a member of" "$LOG_DIR/make.log" | head -10 | jq -R . | jq -s .)
        local cuda_errors=$(grep -E "CUDA|nvcc|device" "$LOG_DIR/make.log" | grep -E "error:" | head -5 | jq -R . | jq -s .)
        local linking_errors=$(grep -E "undefined reference|cannot find|linker" "$LOG_DIR/make.log" | head -5 | jq -R . | jq -s .)

        # Update results with detailed errors
        local temp_file=$(mktemp)
        jq --arg build_time "$build_time" \
           --argjson errors "$errors" \
           --argjson warnings "$warnings" \
           --argjson missing_includes "$missing_includes" \
           --argjson cuda_errors "$cuda_errors" \
           --argjson linking_errors "$linking_errors" \
           '
        .build_results.make_success = false |
        .build_results.build_time_seconds = ($build_time | tonumber) |
        .build_results.build_errors = $errors |
        .build_results.build_warnings = $warnings |
        .build_results.compilation_issues = {
            "missing_includes": $missing_includes,
            "cuda_errors": $cuda_errors,
            "linking_errors": $linking_errors
        }
        ' "$RESULTS_FILE" > "$temp_file"
        mv "$temp_file" "$RESULTS_FILE"

        return 1
    fi
}

# Analyze build issues
analyze_build_issues() {
    log_info "Analyzing build issues"

    local cmake_success=$(jq -r '.build_results.cmake_success' "$RESULTS_FILE")
    local make_success=$(jq -r '.build_results.make_success' "$RESULTS_FILE")

    if [[ "$cmake_success" == "true" && "$make_success" == "true" ]]; then
        log_success "✅ BUILD SUCCESSFUL - All targets completed"
        return 0
    fi

    # Analyze specific issues
    if [[ "$cmake_success" == "false" ]]; then
        log_warning "CMake configuration issues detected:"
        local error_count=$(jq -r '.build_results.build_errors | length' "$RESULTS_FILE")
        log_warning "  - $error_count CMake errors found"

        if [[ $error_count -gt 0 ]]; then
            log_info "Most common CMake errors:"
            jq -r '.build_results.build_errors[0:3] | .[]' "$RESULTS_FILE" | while read -r error; do
                log_warning "  • $error"
            done
        fi
    fi

    if [[ "$make_success" == "false" ]]; then
        log_warning "Build compilation issues detected:"
        local error_count=$(jq -r '.build_results.build_errors | length' "$RESULTS_FILE")
        local warning_count=$(jq -r '.build_results.build_warnings | length' "$RESULTS_FILE")
        log_warning "  - $error_count compilation errors found"
        log_warning "  - $warning_count warnings found"

        # Check for specific issue types
        local missing_count=$(jq -r '.build_results.compilation_issues.missing_includes | length' "$RESULTS_FILE")
        local cuda_count=$(jq -r '.build_results.compilation_issues.cuda_errors | length' "$RESULTS_FILE")
        local linking_count=$(jq -r '.build_results.compilation_issues.linking_errors | length' "$RESULTS_FILE")

        if [[ $missing_count -gt 0 ]]; then
            log_warning "  - Missing includes ($missing_count instances)"
            log_info "    Common missing headers: <vector>, <iostream>, <chrono>"
        fi

        if [[ $cuda_count -gt 0 ]]; then
            log_warning "  - CUDA compilation errors ($cuda_count instances)"
            log_info "    Check CUDA device variable initialization"
        fi

        if [[ $linking_count -gt 0 ]]; then
            log_warning "  - Linking errors ($linking_count instances)"
            log_info "    Check target definitions and library linking"
        fi
    fi

    return 1
}

# Generate benchmark report
generate_report() {
    log_info "Generating benchmark report"

    local report_file="$LOG_DIR/build_benchmark_report.html"
    local cmake_success=$(jq -r '.build_results.cmake_success' "$RESULTS_FILE")
    local make_success=$(jq -r '.build_results.make_success' "$RESULTS_FILE")
    local build_time=$(jq -r '.build_results.build_time_seconds' "$RESULTS_FILE")

    local overall_status="failure"
    local status_text="❌ BUILD FAILED"
    local status_class="failure"

    if [[ "$cmake_success" == "true" && "$make_success" == "true" ]]; then
        if [[ $(echo "$build_time <= $BUILD_TARGET_SECONDS" | bc -l) -eq 1 ]]; then
            overall_status="success"
            status_text="✅ BUILD SUCCESSFUL"
            status_class="success"
        else
            overall_status="slow"
            status_text="⚠️ BUILD SUCCESSFUL BUT SLOW"
            status_class="warning"
        fi
    fi

    cat > "$report_file" << EOF
<!DOCTYPE html>
<html>
<head>
    <title>Integration Build Benchmark Report</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; }
        .header { background: #2c3e50; color: white; padding: 20px; border-radius: 5px; }
        .success { color: #27ae60; font-weight: bold; }
        .failure { color: #e74c3c; font-weight: bold; }
        .warning { color: #f39c12; font-weight: bold; }
        .metric-card { background: #f8f9fa; padding: 15px; margin: 10px 0; border-radius: 5px; border-left: 4px solid #3498db; }
        .error-section { background: #f8d7da; padding: 15px; margin: 10px 0; border-radius: 5px; border-left: 4px solid #e74c3c; }
        .warning-section { background: #fff3cd; padding: 15px; margin: 10px 0; border-radius: 5px; border-left: 4px solid #f39c12; }
        pre { background: #f8f9fa; padding: 10px; border-radius: 3px; overflow-x: auto; }
    </style>
</head>
<body>
    <div class="header">
        <h1>🔧 Integration Build Benchmark Report</h1>
        <p>Generated: $(date)</p>
        <p>Target Build Time: ${BUILD_TARGET_SECONDS}s (5 minutes)</p>
    </div>

    <div class="metric-card">
        <h2>📊 Build Results</h2>
        <div style="display: flex; flex-wrap: wrap;">
            <div class="metric-card">
                <h3>CMake Status</h3>
                <p class="$([[ "$cmake_success" == "true" ]] && echo success || echo failure)">$cmake_success</p>
            </div>
            <div class="metric-card">
                <h3>Build Status</h3>
                <p class="$([[ "$make_success" == "true" ]] && echo success || echo failure)">$make_success</p>
            </div>
            <div class="metric-card">
                <h3>Build Time</h3>
                <p style="font-size: 24px;">${build_time}s</p>
            </div>
            <div class="metric-card">
                <h3>Overall Result</h3>
                <p class="$status_class" style="font-size: 24px;">$status_text</p>
            </div>
        </div>
    </div>

    <h2>📋 Detailed Analysis</h2>

    <h3>System Information</h3>
    <div class="metric-card">
        <p><strong>CPU Cores:</strong> $(jq -r '.system_metrics.cpu_cores' "$RESULTS_FILE")</p>
        <p><strong>Memory:</strong> $(jq -r '.system_metrics.memory_gb' "$RESULTS_FILE")GB</p>
        <p><strong>Disk Space:</strong> $(jq -r '.system_metrics.disk_space_gb' "$RESULTS_FILE")GB</p>
        <p><strong>CUDA Available:</strong> $(jq -r '.system_metrics.cuda_available' "$RESULTS_FILE")</p>
    </div>

    <h3>Build Issues</h3>
EOF

    # Add build errors if any
    local error_count=$(jq -r '.build_results.build_errors | length' "$RESULTS_FILE")
    if [[ $error_count -gt 0 ]]; then
        cat >> "$report_file" << EOF
    <div class="error-section">
        <h4>Build Errors ($error_count)</h4>
        <pre>$(jq -r '.build_results.build_errors | join("\n")' "$RESULTS_FILE" | head -20)</pre>
    </div>
EOF
    fi

    # Add warnings if any
    local warning_count=$(jq -r '.build_results.build_warnings | length' "$RESULTS_FILE")
    if [[ $warning_count -gt 0 ]]; then
        cat >> "$report_file" << EOF
    <div class="warning-section">
        <h4>Build Warnings ($warning_count)</h4>
        <pre>$(jq -r '.build_results.build_warnings | join("\n")' "$RESULTS_FILE" | head -10)</pre>
    </div>
EOF
    fi

    # Add compilation issues analysis
    local missing_count=$(jq -r '.build_results.compilation_issues.missing_includes | length' "$RESULTS_FILE")
    local cuda_count=$(jq -r '.build_results.compilation_issues.cuda_errors | length' "$RESULTS_FILE")
    local linking_count=$(jq -r '.build_results.compilation_issues.linking_errors | length' "$RESULTS_FILE")

    if [[ $missing_count -gt 0 || $cuda_count -gt 0 || $linking_count -gt 0 ]]; then
        cat >> "$report_file" << EOF
    <h3>Compilation Issue Categories</h3>
    <div class="metric-card">
EOF

        if [[ $missing_count -gt 0 ]]; then
            cat >> "$report_file" << EOF
        <p><strong>Missing Includes:</strong> $missing_count instances</p>
        <pre>$(jq -r '.build_results.compilation_issues.missing_includes | join("\n")' "$RESULTS_FILE" | head -5)</pre>
EOF
        fi

        if [[ $cuda_count -gt 0 ]]; then
            cat >> "$report_file" << EOF
        <p><strong>CUDA Errors:</strong> $cuda_count instances</p>
        <pre>$(jq -r '.build_results.compilation_issues.cuda_errors | join("\n")' "$RESULTS_FILE" | head -5)</pre>
EOF
        fi

        if [[ $linking_count -gt 0 ]]; then
            cat >> "$report_file" << EOF
        <p><strong>Linking Errors:</strong> $linking_count instances</p>
        <pre>$(jq -r '.build_results.compilation_issues.linking_errors | join("\n")' "$RESULTS_FILE" | head -5)</pre>
EOF
        fi

        cat >> "$report_file" << EOF
    </div>
EOF
    fi

    cat >> "$report_file" << EOF
    <h3>Recommendations</h3>
    <div class="metric-card">
EOF

    if [[ "$cmake_success" == "false" ]]; then
        cat >> "$report_file" << EOF
        <p><strong>CMake Issues:</strong></p>
        <ul>
            <li>Check target definitions exist before installation commands</li>
            <li>Verify all dependencies are available or properly optional</li>
            <li>Review CMake configuration logs for missing packages</li>
        </ul>
EOF
    fi

    if [[ "$make_success" == "false" ]]; then
        cat >> "$report_file" << EOF
        <p><strong>Compilation Issues:</strong></p>
        <ul>
            <li>Add missing headers: \#include &lt;vector&gt;, \#include &lt;iostream&gt;, \#include &lt;chrono&gt;</li>
            <li>Fix CUDA device variable initialization issues</li>
            <li>Resolve undefined reference errors in linking</li>
            <li>Review compiler warnings for potential issues</li>
        </ul>
EOF
    fi

    cat >> "$report_file" << EOF
    </div>

    <h3>Log Files</h3>
    <div class="metric-card">
        <p><strong>CMake Log:</strong> <a href="cmake.log">cmake.log</a></p>
        <p><strong>Build Log:</strong> <a href="make.log">make.log</a></p>
        <p><strong>Benchmark Log:</strong> <a href="benchmark.log">benchmark.log</a></p>
    </div>

</body>
</html>
EOF

    log_success "Build benchmark report generated: $report_file"
}

# Main execution
main() {
    local command="${1:-run}"

    case "$command" in
        "run")
            log_info "Starting integration build benchmark"
            log_info "Target: ${BUILD_TARGET_SECONDS}s (5 minutes) build time"

            init_results
            clean_build_environment

            local cmake_success=false
            local make_success=false

            if run_cmake; then
                cmake_success=true
                if run_build; then
                    make_success=true
                fi
            fi

            analyze_build_issues
            generate_report

            # Final status
            if [[ $cmake_success == true && $make_success == true ]]; then
                local build_time=$(jq -r '.build_results.build_time_seconds' "$RESULTS_FILE")
                if [[ $(echo "$build_time <= $BUILD_TARGET_SECONDS" | bc -l) -eq 1 ]]; then
                    log_success "✅ BENCHMARK TARGET ACHIEVED! Build completed in ${build_time}s"
                    return 0
                else
                    log_warning "⚠️ BENCHMARK COMPLETED BUT EXCEEDED TARGET: ${build_time}s > ${BUILD_TARGET_SECONDS}s"
                    return 0
                fi
            else
                log_error "❌ BENCHMARK FAILED! Build did not complete successfully"
                return 1
            fi
            ;;
        "clean")
            log_info "Cleaning benchmark artifacts"
            rm -rf "$PROJECT_ROOT/build" "$LOG_DIR"
            log_success "Benchmark cleanup completed"
            ;;
        "report")
            if [[ -f "$RESULTS_FILE" ]]; then
                generate_report
                echo "Report available: $LOG_DIR/build_benchmark_report.html"
            else
                log_error "No benchmark results found. Run benchmark first."
            fi
            ;;
        "help"|*)
            echo "Usage: $0 {run|clean|report|help}"
            echo ""
            echo "Commands:"
            echo "  run    - Run build benchmark with validation"
            echo "  clean  - Clean build artifacts and logs"
            echo "  report - Generate HTML report from existing results"
            echo "  help   - Show this help message"
            exit 0
            ;;
    esac
}

# Execute main function
main "$@"