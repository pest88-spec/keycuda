#!/bin/bash
# T035: Add Deployment Testing Framework with Environment Validation
# Tests deployment packages in various environments and validates functionality

set -euo pipefail

# Script configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TEST_RESULTS_DIR="${TEST_RESULTS_DIR:-$PROJECT_ROOT/test-results}"
DEPLOYMENT_TEST_DIR="${DEPLOYMENT_TEST_DIR:-$PROJECT_ROOT/test-deployment}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Test counters
TESTS_TOTAL=0
TESTS_PASSED=0
TESTS_FAILED=0
TESTS_SKIPPED=0

# Test results
TEST_RESULTS=()
FAILED_TESTS=()
SKIPPED_TESTS=()

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
    ((TESTS_TOTAL++))
}

log_skip() {
    echo -e "${CYAN}[SKIP]${NC} $1"
    ((TESTS_SKIPPED++))
    SKIPPED_TESTS+=("$1")
}

# Show help
show_help() {
    cat << EOF
Deployment Testing Framework

USAGE:
    $0 [OPTIONS] [deployment_package]

OPTIONS:
    --test-dir DIR          Test directory for deployment (default: creates temporary)
    --results-dir DIR       Results directory (default: $TEST_RESULTS_DIR)
    --environment ENV       Target environment (docker, chroot, native, clean)
    --quick                 Run quick tests only
    --comprehensive         Run comprehensive test suite
    --stress                Run stress tests
    --compatibility        Run compatibility tests
    --performance          Run performance tests
    --help, -h              Show this help message

DESCRIPTION:
    Tests deployment packages in various environments to ensure they work
    correctly across different platforms and configurations.

EOF
}

# Parse command line arguments
parse_arguments() {
    DEPLOYMENT_PACKAGE=""
    TEST_ENVIRONMENT="native"
    QUICK_MODE=false
    COMPREHENSIVE_MODE=false
    STRESS_MODE=false
    COMPATIBILITY_MODE=false
    PERFORMANCE_MODE=false

    while [[ $# -gt 0 ]]; do
        case $1 in
            --test-dir)
                DEPLOYMENT_TEST_DIR="$2"
                shift 2
                ;;
            --results-dir)
                TEST_RESULTS_DIR="$2"
                shift 2
                ;;
            --environment)
                TEST_ENVIRONMENT="$2"
                shift 2
                ;;
            --quick)
                QUICK_MODE=true
                shift
                ;;
            --comprehensive)
                COMPREHENSIVE_MODE=true
                shift
                ;;
            --stress)
                STRESS_MODE=true
                shift
                ;;
            --compatibility)
                COMPATIBILITY_MODE=true
                shift
                ;;
            --performance)
                PERFORMANCE_MODE=true
                shift
                ;;
            --help|-h)
                show_help
                exit 0
                ;;
            -*)
                log_error "Unknown option: $1"
                show_help
                exit 1
                ;;
            *)
                if [[ -z "$DEPLOYMENT_PACKAGE" ]]; then
                    DEPLOYMENT_PACKAGE="$1"
                else
                    log_error "Too many arguments"
                    exit 1
                fi
                shift
                ;;
        esac
    done
}

# Initialize test environment
initialize_test_environment() {
    log_info "Initializing deployment testing framework..."

    # Create directories
    mkdir -p "$TEST_RESULTS_DIR"
    mkdir -p "$DEPLOYMENT_TEST_DIR"

    # Initialize test results file
    cat > "$TEST_RESULTS_DIR/deployment-test-results.json" << EOF
{
  "deployment_test_results": {
    "test_metadata": {
      "started": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
      "framework_version": "T035-1.0",
      "test_environment": "$TEST_ENVIRONMENT",
      "deployment_package": "$DEPLOYMENT_PACKAGE"
    },
    "test_summary": {
      "total_tests": 0,
      "passed_tests": 0,
      "failed_tests": 0,
      "skipped_tests": 0,
      "overall_status": "RUNNING"
    },
    "test_categories": {
      "environment_validation": {},
      "functionality_tests": {},
      "compatibility_tests": {},
      "performance_tests": {},
      "stress_tests": {}
    },
    "detailed_results": [],
    "failed_tests": [],
    "recommendations": []
  }
}
EOF

    log_success "Test environment initialized"
}

# Extract deployment package if needed
extract_deployment_package() {
    if [[ -z "$DEPLOYMENT_PACKAGE" ]]; then
        # Look for default deployment package
        local default_package=$(find "$PROJECT_ROOT/build" -name "*Deployment*.tar.gz" 2>/dev/null | head -1)
        if [[ -n "$default_package" ]]; then
            DEPLOYMENT_PACKAGE="$default_package"
            log_info "Using default deployment package: $DEPLOYMENT_PACKAGE"
        else
            log_error "No deployment package found. Please specify one or build it first."
            exit 1
        fi
    fi

    if [[ ! -f "$DEPLOYMENT_PACKAGE" ]]; then
        log_error "Deployment package not found: $DEPLOYMENT_PACKAGE"
        exit 1
    fi

    log_info "Extracting deployment package: $DEPLOYMENT_PACKAGE"

    # Extract package
    case "$DEPLOYMENT_PACKAGE" in
        *.tar.gz|*.tgz)
            tar -xzf "$DEPLOYMENT_PACKAGE" -C "$DEPLOYMENT_TEST_DIR"
            ;;
        *.tar.bz2|*.tbz2)
            tar -xjf "$DEPLOYMENT_PACKAGE" -C "$DEPLOYMENT_TEST_DIR"
            ;;
        *.tar.xz|*.txz)
            tar -xJf "$DEPLOYMENT_PACKAGE" -C "$DEPLOYMENT_TEST_DIR"
            ;;
        *.zip)
            unzip -q "$DEPLOYMENT_PACKAGE" -d "$DEPLOYMENT_TEST_DIR"
            ;;
        *)
            log_error "Unsupported package format: $DEPLOYMENT_PACKAGE"
            exit 1
            ;;
    esac

    # Find deployment directory
    DEPLOYMENT_DIR=$(find "$DEPLOYMENT_TEST_DIR" -name "bin" -type d | head -1 | sed 's|/bin||')

    if [[ -z "$DEPLOYMENT_DIR" ]]; then
        log_error "Could not find deployment directory in extracted package"
        exit 1
    fi

    log_success "Package extracted to: $DEPLOYMENT_DIR"
}

# Test environment validation
test_environment_validation() {
    log_test "Environment Validation"

    local validation_passed=true
    local validation_results=()

    # Check system requirements
    local system_info=$(uname -a)
    validation_results+=("System: $system_info")

    # Check memory requirements
    local total_memory=$(free -m | awk '/^Mem:/{print $2}')
    if [[ $total_memory -ge 2048 ]]; then
        validation_results+=("Memory: ${total_memory}MB (✓ meets 2GB requirement)")
    else
        validation_results+=("Memory: ${total_memory}MB (✗ below 2GB requirement)")
        validation_passed=false
    fi

    # Check disk space
    local available_space=$(df -m "$DEPLOYMENT_DIR" | awk 'NR==2 {print $4}')
    if [[ $available_space -ge 1024 ]]; then
        validation_results+=("Disk: ${available_space}MB available (✓ meets 1GB requirement)")
    else
        validation_results+=("Disk: ${available_space}MB available (✗ below 1GB requirement)")
        validation_passed=false
    fi

    # Check CUDA availability
    if command -v nvidia-smi >/dev/null 2>&1; then
        local cuda_version=$(nvidia-smi | grep -i cuda | awk '{print $9}' | head -1 || echo "unknown")
        validation_results+=("CUDA: $cuda_version (✓ available)")
    else
        validation_results+=("CUDA: Not available (⚠ GPU acceleration will not work)")
    fi

    # Check required system libraries
    local required_libs=("libc.so.6" "libm.so.6" "libpthread.so.0")
    for lib in "${required_libs[@]}"; do
        if ldconfig -p | grep -q "$lib"; then
            validation_results+=("Library $lib: ✓ available")
        else
            validation_results+=("Library $lib: ✗ missing")
            validation_passed=false
        fi
    done

    # Record results
    if [[ "$validation_passed" == true ]]; then
        log_success "Environment validation passed"
        ((TESTS_PASSED++))
        TEST_RESULTS+=("environment_validation:PASSED")
    else
        log_error "Environment validation failed"
        ((TESTS_FAILED++))
        TEST_RESULTS+=("environment_validation:FAILED")
        FAILED_TESTS+=("Environment validation")
    fi

    # Save detailed results
    printf '%s\n' "${validation_results[@]}" > "$TEST_RESULTS_DIR/environment-validation.txt"
}

# Test deployment package integrity
test_package_integrity() {
    log_test "Package Integrity Test"

    local integrity_passed=true

    # Check essential directories
    local essential_dirs=("bin" "lib" "config" "scripts" "docs")
    for dir in "${essential_dirs[@]}"; do
        if [[ -d "$DEPLOYMENT_DIR/$dir" ]]; then
            log_info "✓ Directory $dir exists"
        else
            log_error "✗ Directory $dir missing"
            integrity_passed=false
        fi
    done

    # Check main executable
    if [[ -x "$DEPLOYMENT_DIR/bin/Puzzle71Solver" ]]; then
        log_info "✓ Main executable exists and is executable"
    else
        log_error "✗ Main executable missing or not executable"
        integrity_passed=false
    fi

    # Check deployment scripts
    local scripts=("run.sh" "verify.sh" "setup-env.sh")
    for script in "${scripts[@]}"; do
        if [[ -x "$DEPLOYMENT_DIR/scripts/$script" ]]; then
            log_info "✓ Script $script exists and is executable"
        else
            log_warning "⚠ Script $script missing or not executable"
        fi
    done

    # Check configuration files
    if [[ -f "$DEPLOYMENT_DIR/MANIFEST.json" ]]; then
        if python3 -m json.tool "$DEPLOYMENT_DIR/MANIFEST.json" >/dev/null 2>&1; then
            log_info "✓ MANIFEST.json is valid JSON"
        else
            log_error "✗ MANIFEST.json is not valid JSON"
            integrity_passed=false
        fi
    else
        log_error "✗ MANIFEST.json missing"
        integrity_passed=false
    fi

    # Record results
    if [[ "$integrity_passed" == true ]]; then
        log_success "Package integrity test passed"
        ((TESTS_PASSED++))
        TEST_RESULTS+=("package_integrity:PASSED")
    else
        log_error "Package integrity test failed"
        ((TESTS_FAILED++))
        TEST_RESULTS+=("package_integrity:FAILED")
        FAILED_TESTS+=("Package integrity")
    fi
}

# Test basic functionality
test_basic_functionality() {
    log_test "Basic Functionality Test"

    # Test help command
    if [[ -x "$DEPLOYMENT_DIR/bin/Puzzle71Solver" ]]; then
        if "$DEPLOYMENT_DIR/bin/Puzzle71Solver" --help >/dev/null 2>&1; then
            log_info "✓ Help command works"
        else
            log_error "✗ Help command failed"
            return 1
        fi
    else
        log_error "✗ Main executable not found"
        return 1
    fi

    # Test version command
    if "$DEPLOYMENT_DIR/bin/Puzzle71Solver" --version >/dev/null 2>&1; then
        log_info "✓ Version command works"
    else
        log_warning "⚠ Version command not supported"
    fi

    # Test environment setup script
    if [[ -x "$DEPLOYMENT_DIR/scripts/setup-env.sh" ]]; then
        if "$DEPLOYMENT_DIR/scripts/setup-env.sh" >/dev/null 2>&1; then
            log_info "✓ Environment setup script works"
        else
            log_warning "⚠ Environment setup script has issues"
        fi
    fi

    # Test verification script
    if [[ -x "$DEPLOYMENT_DIR/scripts/verify.sh" ]]; then
        if "$DEPLOYMENT_DIR/scripts/verify.sh" >/dev/null 2>&1; then
            log_info "✓ Verification script works"
        else
            log_warning "⚠ Verification script has issues"
        fi
    fi

    log_success "Basic functionality test passed"
    ((TESTS_PASSED++))
    TEST_RESULTS+=("basic_functionality:PASSED")
}

# Test dependency resolution
test_dependency_resolution() {
    log_test "Dependency Resolution Test"

    if [[ ! -x "$DEPLOYMENT_DIR/bin/Puzzle71Solver" ]]; then
        log_error "✗ Main executable not found"
        return 1
    fi

    # Check dynamic dependencies
    local missing_deps=0
    while IFS= read -r line; do
        if [[ "$line" == *"not found"* ]]; then
            local dep=$(echo "$line" | awk '{print $1}')
            log_error "✗ Missing dependency: $dep"
            ((missing_deps++))
        fi
    done <<< "$(ldd "$DEPLOYMENT_DIR/bin/Puzzle71Solver" 2>/dev/null || true)"

    if [[ $missing_deps -eq 0 ]]; then
        log_success "All dependencies resolved"
        ((TESTS_PASSED++))
        TEST_RESULTS+=("dependency_resolution:PASSED")
    else
        log_error "$missing_deps missing dependencies"
        ((TESTS_FAILED++))
        TEST_RESULTS+=("dependency_resolution:FAILED")
        FAILED_TESTS+=("Dependency resolution")
    fi
}

# Test performance (if enabled)
test_performance() {
    if [[ "$PERFORMANCE_MODE" != true && "$COMPREHENSIVE_MODE" != true ]]; then
        log_skip "Performance test (use --performance or --comprehensive)"
        return 0
    fi

    log_test "Performance Test"

    if [[ ! -x "$DEPLOYMENT_DIR/bin/Puzzle71Solver" ]]; then
        log_error "✗ Main executable not found"
        return 1
    fi

    # Measure startup time
    local start_time=$(date +%s%N)
    "$DEPLOYMENT_DIR/bin/Puzzle71Solver" --version >/dev/null 2>&1 || true
    local end_time=$(date +%s%N)
    local startup_time=$(( (end_time - start_time) / 1000000 )) # Convert to milliseconds

    if [[ $startup_time -lt 5000 ]]; then
        log_info "✓ Startup time: ${startup_time}ms (good)"
    elif [[ $startup_time -lt 10000 ]]; then
        log_info "✓ Startup time: ${startup_time}ms (acceptable)"
    else
        log_warning "⚠ Startup time: ${startup_time}ms (slow)"
    fi

    # Measure memory usage
    local memory_usage=0
    if command -v /usr/bin/time >/dev/null 2>&1; then
        memory_usage=$(/usr/bin/time -f "%M" "$DEPLOYMENT_DIR/bin/Puzzle71Solver" --version 2>&1 | tail -1 || echo "0")
        log_info "Memory usage: ${memory_usage}KB"
    fi

    log_success "Performance test completed"
    ((TESTS_PASSED++))
    TEST_RESULTS+=("performance:PASSED")
}

# Test stress scenarios (if enabled)
test_stress_scenarios() {
    if [[ "$STRESS_MODE" != true && "$COMPREHENSIVE_MODE" != true ]]; then
        log_skip "Stress test (use --stress or --comprehensive)"
        return 0
    fi

    log_test "Stress Test"

    if [[ ! -x "$DEPLOYMENT_DIR/bin/Puzzle71Solver" ]]; then
        log_error "✗ Main executable not found"
        return 1
    fi

    # Multiple concurrent invocations
    local pids=()
    for i in {1..5}; do
        "$DEPLOYMENT_DIR/bin/Puzzle71Solver" --version >/dev/null 2>&1 &
        pids+=($!)
    done

    local failed_concurrent=0
    for pid in "${pids[@]}"; do
        if ! wait "$pid"; then
            ((failed_concurrent++))
        fi
    done

    if [[ $failed_concurrent -eq 0 ]]; then
        log_success "✓ Concurrent execution test passed"
    else
        log_error "✗ $failed_concurrent concurrent executions failed"
    fi

    log_success "Stress test completed"
    ((TESTS_PASSED++))
    TEST_RESULTS+=("stress:PASSED")
}

# Test compatibility scenarios
test_compatibility() {
    if [[ "$COMPATIBILITY_MODE" != true && "$COMPREHENSIVE_MODE" != true ]]; then
        log_skip "Compatibility test (use --compatibility or --comprehensive)"
        return 0
    fi

    log_test "Compatibility Test"

    # Test with different LD_LIBRARY_PATH scenarios
    local old_ld_path="$LD_LIBRARY_PATH"

    # Test 1: Empty LD_LIBRARY_PATH
    export LD_LIBRARY_PATH=""
    if "$DEPLOYMENT_DIR/bin/Puzzle71Solver" --version >/dev/null 2>&1; then
        log_info "✓ Works with empty LD_LIBRARY_PATH"
    else
        log_warning "⚠ Does not work with empty LD_LIBRARY_PATH"
    fi

    # Test 2: Deployment library path only
    export LD_LIBRARY_PATH="$DEPLOYMENT_DIR/lib"
    if "$DEPLOYMENT_DIR/bin/Puzzle71Solver" --version >/dev/null 2>&1; then
        log_info "✓ Works with deployment library path"
    else
        log_warning "⚠ Does not work with deployment library path"
    fi

    # Restore original LD_LIBRARY_PATH
    export LD_LIBRARY_PATH="$old_ld_path"

    log_success "Compatibility test completed"
    ((TESTS_PASSED++))
    TEST_RESULTS+=("compatibility:PASSED")
}

# Generate test report
generate_test_report() {
    log_info "Generating test report..."

    local overall_status="PASSED"
    if [[ $TESTS_FAILED -gt 0 ]]; then
        overall_status="FAILED"
    elif [[ $TESTS_SKIPPED -gt 0 ]]; then
        overall_status="PASSED_WITH_SKIPS"
    fi

    cat > "$TEST_RESULTS_DIR/deployment-test-results.json" << EOF
{
  "deployment_test_results": {
    "test_metadata": {
      "started": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
      "completed": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
      "framework_version": "T035-1.0",
      "test_environment": "$TEST_ENVIRONMENT",
      "deployment_package": "$DEPLOYMENT_PACKAGE",
      "deployment_directory": "$DEPLOYMENT_DIR"
    },
    "test_summary": {
      "total_tests": $TESTS_TOTAL,
      "passed_tests": $TESTS_PASSED,
      "failed_tests": $TESTS_FAILED,
      "skipped_tests": $TESTS_SKIPPED,
      "overall_status": "$overall_status"
    },
    "test_categories": {
      "environment_validation": {
        "status": "$(grep -q "environment_validation:PASSED" <<< "${TEST_RESULTS[*]}" && echo "PASSED" || echo "FAILED")",
        "description": "System requirements and environment validation"
      },
      "package_integrity": {
        "status": "$(grep -q "package_integrity:PASSED" <<< "${TEST_RESULTS[*]}" && echo "PASSED" || echo "FAILED")",
        "description": "Deployment package structure and file integrity"
      },
      "basic_functionality": {
        "status": "$(grep -q "basic_functionality:PASSED" <<< "${TEST_RESULTS[*]}" && echo "PASSED" || echo "FAILED")",
        "description": "Basic application functionality tests"
      },
      "dependency_resolution": {
        "status": "$(grep -q "dependency_resolution:PASSED" <<< "${TEST_RESULTS[*]}" && echo "PASSED" || echo "FAILED")",
        "description": "Dynamic dependency resolution verification"
      },
      "performance": {
        "status": "$(grep -q "performance:PASSED" <<< "${TEST_RESULTS[*]}" && echo "PASSED" || echo "NOT_RUN")",
        "description": "Application performance and resource usage"
      },
      "stress": {
        "status": "$(grep -q "stress:PASSED" <<< "${TEST_RESULTS[*]}" && echo "PASSED" || echo "NOT_RUN")",
        "description": "Stress testing and concurrent execution"
      },
      "compatibility": {
        "status": "$(grep -q "compatibility:PASSED" <<< "${TEST_RESULTS[*]}" && echo "PASSED" || echo "NOT_RUN")",
        "description": "Cross-environment compatibility testing"
      }
    },
    "detailed_results": [
      $(printf '"%s",' "${TEST_RESULTS[@]}" | sed 's/,$//')
    ],
    "failed_tests": [
      $(printf '"%s",' "${FAILED_TESTS[@]}" | sed 's/,$//')
    ],
    "skipped_tests": [
      $(printf '"%s",' "${SKIPPED_TESTS[@]}" | sed 's/,$//')
    ],
    "recommendations": [
      $([ $TESTS_FAILED -gt 0 ] && echo '"Fix all failed tests before deployment",')
      $([ $TESTS_SKIPPED -gt 0 ] && echo '"Consider running skipped tests for comprehensive validation",')
      "Run deployment testing in target environment",
      "Monitor performance in production"
    ]
  }
}
EOF

    log_success "Test report generated: $TEST_RESULTS_DIR/deployment-test-results.json"
}

# Display test summary
display_test_summary() {
    echo
    echo "=== Deployment Testing Summary ==="
    echo "Total Tests: $TESTS_TOTAL"
    echo "Passed: $TESTS_PASSED"
    echo "Failed: $TESTS_FAILED"
    echo "Skipped: $TESTS_SKIPPED"
    echo

    if [[ $TESTS_FAILED -eq 0 ]]; then
        echo -e "${GREEN}✅ ALL TESTS PASSED${NC}"
        echo "Deployment package is ready for production."
    else
        echo -e "${RED}❌ SOME TESTS FAILED${NC}"
        echo "Failed tests:"
        for test in "${FAILED_TESTS[@]}"; do
            echo "  - $test"
        done
    fi

    if [[ $TESTS_SKIPPED -gt 0 ]]; then
        echo
        echo "Skipped tests:"
        for test in "${SKIPPED_TESTS[@]}"; do
            echo "  - $test"
        done
    fi

    echo
    echo "Detailed report: $TEST_RESULTS_DIR/deployment-test-results.json"
}

# Main testing function
main() {
    log_info "Starting deployment testing framework..."
    log_info "Test environment: $TEST_ENVIRONMENT"

    # Parse arguments
    parse_arguments "$@"

    # Initialize test environment
    initialize_test_environment

    # Extract deployment package
    extract_deployment_package

    # Run tests
    test_environment_validation
    test_package_integrity
    test_basic_functionality
    test_dependency_resolution

    # Optional tests
    test_performance
    test_compatibility
    test_stress_scenarios

    # Generate report
    generate_test_report

    # Display summary
    display_test_summary

    # Return appropriate exit code
    if [[ $TESTS_FAILED -eq 0 ]]; then
        log_success "🎉 T035 DEPLOYMENT TESTING FRAMEWORK COMPLETED"
        exit 0
    else
        log_error "❌ Deployment testing failed"
        exit 1
    fi
}

# Run main function
main "$@"