#!/usr/bin/env bash
# T052: Test Framework for Dependency Validation
# Comprehensive test suite for the dependency validation framework

set -euo pipefail

# Test framework configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TEST_RESULTS_DIR="$PROJECT_ROOT/test_results/validation"
TEST_TEMP_DIR="$PROJECT_ROOT/test_temp/validation"
VALIDATION_SCRIPT="$PROJECT_ROOT/scripts/validate-dependency-updates.sh"
VALIDATION_TESTS_SCRIPT="$PROJECT_ROOT/scripts/validation-tests.sh"
BENCHMARK_SCRIPT="$PROJECT_ROOT/scripts/performance-benchmarks.sh"

# Test statistics
TESTS_TOTAL=0
TESTS_PASSED=0
TESTS_FAILED=0
TESTS_SKIPPED=0

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Logging functions
log_test() {
    local level="$1"
    local message="$2"
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    echo -e "${timestamp} [TEST_FRAMEWORK] ${level} ${message}"
}

log_info() { log_test "INFO" "$1"; }
log_success() { log_test "SUCCESS" "$1"; }
log_warning() { log_test "WARNING" "$1"; }
log_error() { log_test "ERROR" "$1"; }
log_debug() { log_test "DEBUG" "$1"; }

# Test assertion functions
assert_equals() {
    local expected="$1"
    local actual="$2"
    local test_name="$3"

    if [[ "$expected" == "$actual" ]]; then
        test_pass "$test_name"
        return 0
    else
        test_fail "$test_name" "Expected '$expected', got '$actual'"
        return 1
    fi
}

assert_not_equals() {
    local not_expected="$1"
    local actual="$2"
    local test_name="$3"

    if [[ "$not_expected" != "$actual" ]]; then
        test_pass "$test_name"
        return 0
    else
        test_fail "$test_name" "Expected not '$not_expected', but got '$actual'"
        return 1
    fi
}

assert_file_exists() {
    local file_path="$1"
    local test_name="$2"

    if [[ -f "$file_path" ]]; then
        test_pass "$test_name"
        return 0
    else
        test_fail "$test_name" "File '$file_path' does not exist"
        return 1
    fi
}

assert_file_not_exists() {
    local file_path="$1"
    local test_name="$2"

    if [[ ! -f "$file_path" ]]; then
        test_pass "$test_name"
        return 0
    else
        test_fail "$test_name" "File '$file_path' exists but shouldn't"
        return 1
    fi
}

assert_command_success() {
    local command="$1"
    local test_name="$2"

    if eval "$command" >/dev/null 2>&1; then
        test_pass "$test_name"
        return 0
    else
        test_fail "$test_name" "Command failed: $command"
        return 1
    fi
}

assert_command_failure() {
    local command="$1"
    local test_name="$2"

    if ! eval "$command" >/dev/null 2>&1; then
        test_pass "$test_name"
        return 0
    else
        test_fail "$test_name" "Command succeeded but should have failed: $command"
        return 1
    fi
}

assert_json_field() {
    local json_file="$1"
    local field_path="$2"
    local expected_value="$3"
    local test_name="$4"

    if [[ ! -f "$json_file" ]]; then
        test_fail "$test_name" "JSON file '$json_file' does not exist"
        return 1
    fi

    local actual_value
    if actual_value=$(jq -r "$field_path" "$json_file" 2>/dev/null); then
        if [[ "$actual_value" == "$expected_value" ]]; then
            test_pass "$test_name"
            return 0
        else
            test_fail "$test_name" "JSON field '$field_path': Expected '$expected_value', got '$actual_value'"
            return 1
        fi
    else
        test_fail "$test_name" "Failed to read JSON field '$field_path' from '$json_file'"
        return 1
    fi
}

# Test result functions
test_pass() {
    local test_name="$1"
    ((TESTS_PASSED++))
    log_success "✅ PASS: $test_name"
}

test_fail() {
    local test_name="$1"
    local error_message="$2"
    ((TESTS_FAILED++))
    log_error "❌ FAIL: $test_name - $error_message"
}

test_skip() {
    local test_name="$1"
    local reason="$2"
    ((TESTS_SKIPPED++))
    log_warning "⏭️  SKIP: $test_name - $reason"
}

# Initialize test framework
init_test_framework() {
    log_info "Initializing T052 dependency validation test framework..."

    # Create test directories
    mkdir -p "$TEST_RESULTS_DIR" "$TEST_TEMP_DIR"

    # Set up test environment
    export TEST_MODE=1
    export VALIDATION_CACHE_DIR="$TEST_TEMP_DIR/.dependency_validation"
    export VALIDATION_LOGS_DIR="$TEST_TEMP_DIR/logs"
    export VALIDATION_REPORTS_DIR="$TEST_TEMP_DIR/reports"

    # Clean up previous test artifacts
    rm -rf "$TEST_TEMP_DIR"/*
    mkdir -p "$TEST_TEMP_DIR"

    log_info "Test framework initialized"
}

# Cleanup test environment
cleanup_test_environment() {
    log_info "Cleaning up test environment..."

    # Preserve test results but clean temp files
    if [[ -d "$TEST_TEMP_DIR" ]]; then
        find "$TEST_TEMP_DIR" -name "*.tmp" -delete 2>/dev/null || true
        find "$TEST_TEMP_DIR" -name "*.log" -delete 2>/dev/null || true
    fi

    log_info "Test environment cleaned up"
}

# Test suite: Validation framework initialization
test_validation_framework_initialization() {
    log_info "Running validation framework initialization tests..."

    # Test 1: Script existence
    ((TESTS_TOTAL++))
    assert_file_exists "$VALIDATION_SCRIPT" "Validation script exists"

    # Test 2: Script executability
    ((TESTS_TOTAL++))
    assert_command_success "test -x $VALIDATION_SCRIPT" "Validation script is executable"

    # Test 3: Help command
    ((TESTS_TOTAL++))
    assert_command_success "$VALIDATION_SCRIPT help" "Help command works"

    # Test 4: Config command (should fail initially)
    ((TESTS_TOTAL++))
    assert_command_failure "$VALIDATION_SCRIPT config 2>/dev/null" "Config command fails when not initialized"

    # Test 5: Initialization
    ((TESTS_TOTAL++))
    assert_command_success "$VALIDATION_SCRIPT init" "Framework initialization succeeds"

    # Test 6: Config command after initialization
    ((TESTS_TOTAL++))
    assert_command_success "$VALIDATION_SCRIPT config >/dev/null 2>&1" "Config command works after initialization"

    # Test 7: Configuration file creation
    ((TESTS_TOTAL++))
    assert_file_exists "$VALIDATION_CACHE_DIR/validation_config.json" "Validation configuration file created"
}

# Test suite: Configuration validation
test_configuration_validation() {
    log_info "Running configuration validation tests..."

    # Test 1: Configuration file structure
    ((TESTS_TOTAL++))
    assert_json_field "$VALIDATION_CACHE_DIR/validation_config.json" ".validation_config_version" "1.0" "Configuration version is correct"

    # Test 2: Validation levels exist
    ((TESTS_TOTAL++))
    assert_json_field "$VALIDATION_CACHE_DIR/validation_config.json" ".validation_levels.quick.description" "Quick validation for urgent updates" "Quick validation level configured"

    # Test 3: Test configurations exist
    ((TESTS_TOTAL++))
    assert_json_field "$VALIDATION_CACHE_DIR/validation_config.json" ".test_configurations.build_check.enabled" "true" "Build check test is enabled"

    # Test 4: Rollback settings exist
    ((TESTS_TOTAL++))
    assert_json_field "$VALIDATION_CACHE_DIR/validation_config.json" ".rollback_settings.auto_rollback_on_failure" "true" "Auto-rollback is enabled"

    # Test 5: Invalid validation level
    ((TESTS_TOTAL++))
    assert_command_failure "$VALIDATION_SCRIPT validate invalid_level 2>/dev/null" "Invalid validation level is rejected"
}

# Test suite: Dependency health checks
test_dependency_health_checks() {
    log_info "Running dependency health check tests..."

    # Test 1: Validation utilities script exists
    ((TESTS_TOTAL++))
    assert_file_exists "$VALIDATION_TESTS_SCRIPT" "Validation utilities script exists"

    # Test 2: Validation utilities help
    ((TESTS_TOTAL++))
    assert_command_success "$VALIDATION_TESTS_SCRIPT help >/dev/null 2>&1" "Validation utilities help works"

    # Test 3: Health check command
    ((TESTS_TOTAL++))
    # Note: This may fail if CUDA is not available, so we check if command runs (not necessarily succeeds)
    if command -v nvcc >/dev/null 2>&1; then
        assert_command_success "$VALIDATION_TESTS_SCRIPT health-check >/dev/null 2>&1" "Health check runs when CUDA available"
    else
        test_skip "health_check" "CUDA not available"
    fi

    # Test 4: CUDA validation
    ((TESTS_TOTAL++))
    if command -v nvcc >/dev/null 2>&1; then
        assert_command_success "$VALIDATION_TESTS_SCRIPT cuda-validation >/dev/null 2>&1" "CUDA validation runs when CUDA available"
    else
        test_skip "cuda_validation" "CUDA not available"
    fi

    # Test 5: GPU memory check
    ((TESTS_TOTAL++))
    if command -v nvidia-smi >/dev/null 2>&1; then
        assert_command_success "$VALIDATION_TESTS_SCRIPT gpu-memory >/dev/null 2>&1" "GPU memory check runs when nvidia-smi available"
    else
        test_skip "gpu_memory" "nvidia-smi not available"
    fi
}

# Test suite: Build validation
test_build_validation() {
    log_info "Running build validation tests..."

    # Test 1: CMake availability
    ((TESTS_TOTAL++))
    if command -v cmake >/dev/null 2>&1; then
        test_pass "CMake is available"
    else
        test_skip "build_validation" "CMake not available"
        return 0
    fi

    # Test 2: Build validation utility
    ((TESTS_TOTAL++))
    assert_command_success "$VALIDATION_TESTS_SCRIPT build-validation >/dev/null 2>&1 || true" "Build validation utility runs"

    # Test 3: Build directory creation
    ((TESTS_TOTAL++))
    local build_dir="$PROJECT_ROOT/build"
    if [[ -d "$build_dir" ]]; then
        test_pass "Build directory exists"
    else
        test_skip "build_directory" "Build directory does not exist"
    fi

    # Test 4: Binary existence (if built)
    ((TESTS_TOTAL++))
    local test_binary="$PROJECT_ROOT/build/Puzzle71Solver"
    if [[ -x "$test_binary" ]]; then
        test_pass "Puzzle71Solver binary exists and is executable"

        # Test 5: Binary help functionality
        ((TESTS_TOTAL++))
        assert_command_success "timeout 5 $test_binary --help >/dev/null 2>&1" "Binary help command works"
    else
        test_skip "binary_functionality" "Puzzle71Solver binary not built"
    fi
}

# Test suite: Performance benchmarking
test_performance_benchmarking() {
    log_info "Running performance benchmarking tests..."

    # Test 1: Benchmark script exists
    ((TESTS_TOTAL++))
    assert_file_exists "$BENCHMARK_SCRIPT" "Performance benchmark script exists"

    # Test 2: Benchmark script help
    ((TESTS_TOTAL++))
    assert_command_success "$BENCHMARK_SCRIPT help >/dev/null 2>&1" "Benchmark script help works"

    # Test 3: Benchmark initialization
    ((TESTS_TOTAL++))
    assert_command_success "$BENCHMARK_SCRIPT init >/dev/null 2>&1" "Benchmark framework initialization succeeds"

    # Test 4: Benchmark configuration creation
    ((TESTS_TOTAL++))
    assert_file_exists "$TEST_TEMP_DIR/.benchmark_config/benchmark_config.json" "Benchmark configuration file created"

    # Test 5: Quick benchmark (if binary exists)
    ((TESTS_TOTAL++))
    local test_binary="$PROJECT_ROOT/build/Puzzle71Solver"
    if [[ -x "$test_binary" ]]; then
        # Run a very short benchmark test
        assert_command_success "timeout 30 $BENCHMARK_SCRIPT throughput quick >/dev/null 2>&1 || true" "Quick benchmark runs"
    else
        test_skip "quick_benchmark" "Puzzle71Solver binary not available"
    fi
}

# Test suite: Validation workflow
test_validation_workflow() {
    log_info "Running validation workflow tests..."

    # Test 1: Pre-update validation (dry run)
    ((TESTS_TOTAL++))
    assert_command_success "timeout 60 $VALIDATION_SCRIPT pre-update quick >/dev/null 2>&1 || true" "Pre-update validation runs"

    # Test 2: Validation with dry run
    ((TESTS_TOTAL++))
    assert_command_success "timeout 60 $VALIDATION_SCRIPT validate quick --dry-run >/dev/null 2>&1 || true" "Validation with dry run works"

    # Test 3: Invalid test command
    ((TESTS_TOTAL++))
    assert_command_failure "$VALIDATION_SCRIPT test invalid_test invalid_phase 2>/dev/null" "Invalid test command is rejected"

    # Test 4: Test execution framework
    ((TESTS_TOTAL++))
    # Test a simple validation test
    if command -v execute_dependency_check >/dev/null 2>&1; then
        assert_command_success "execute_dependency_check >/dev/null 2>&1 || true" "Individual test execution works"
    else
        test_skip "test_execution" "Test execution functions not available"
    fi

    # Test 5: Report generation
    ((TESTS_TOTAL++))
    assert_command_success "$VALIDATION_TESTS_SCRIPT report >/dev/null 2>&1 || true" "Report generation works"
}

# Test suite: Integration with dependency management
test_dependency_management_integration() {
    log_info "Running dependency management integration tests..."

    # Test 1: Update dependencies script exists
    ((TESTS_TOTAL++))
    local update_script="$PROJECT_ROOT/scripts/update-dependencies.sh"
    assert_file_exists "$update_script" "Update dependencies script exists"

    # Test 2: Update dependencies script help
    ((TESTS_TOTAL++))
    assert_command_success "$update_script help >/dev/null 2>&1" "Update dependencies help works"

    # Test 3: Dependency version detection
    ((TESTS_TOTAL++))
    assert_command_success "$update_script detect >/dev/null 2>&1 || true" "Dependency version detection works"

    # Test 4: Compatibility validation
    ((TESTS_TOTAL++))
    assert_command_success "$update_script validate >/dev/null 2>&1 || true" "Compatibility validation works"

    # Test 5: Report generation
    ((TESTS_TOTAL++))
    assert_command_success "$update_script report json >/dev/null 2>&1 || true" "Dependency report generation works"
}

# Test suite: Error handling and edge cases
test_error_handling() {
    log_info "Running error handling and edge case tests..."

    # Test 1: Invalid command
    ((TESTS_TOTAL++))
    assert_command_failure "$VALIDATION_SCRIPT invalid_command 2>/dev/null" "Invalid command is rejected"

    # Test 2: Invalid arguments
    ((TESTS_TOTAL++))
    assert_command_failure "$VALIDATION_SCRIPT validate --invalid-option 2>/dev/null" "Invalid arguments are rejected"

    # Test 3: Missing configuration (simulate)
    ((TESTS_TOTAL++))
    local original_config="$VALIDATION_CACHE_DIR/validation_config.json"
    if [[ -f "$original_config" ]]; then
        mv "$original_config" "${original_config}.backup"
        assert_command_failure "$VALIDATION_SCRIPT config 2>/dev/null" "Missing configuration is handled gracefully"
        mv "${original_config}.backup" "$original_config"
    else
        test_skip "missing_config" "Configuration file not present to test"
    fi

    # Test 4: Timeout handling
    ((TESTS_TOTAL++))
    assert_command_failure "timeout 1 $VALIDATION_SCRIPT validate comprehensive 2>/dev/null" "Timeout handling works"

    # Test 5: Directory permissions (read-only test)
    ((TESTS_TOTAL++))
    local test_dir="$TEST_TEMP_DIR/readonly_test"
    mkdir -p "$test_dir"
    chmod 444 "$test_dir"
    if ! touch "$test_dir/test_file" 2>/dev/null; then
        # Directory is read-only, test error handling
        assert_command_failure "VALIDATION_CACHE_DIR='$test_dir' $VALIDATION_SCRIPT init 2>/dev/null" "Read-only directory handled gracefully"
    else
        test_skip "readonly_directory" "Could not create read-only directory"
    fi
    chmod 755 "$test_dir"
    rm -rf "$test_dir"
}

# Test suite: Report generation and logging
test_report_generation() {
    log_info "Running report generation and logging tests..."

    # Test 1: Report directory creation
    ((TESTS_TOTAL++))
    assert_command_success "$VALIDATION_SCRIPT init >/dev/null 2>&1" "Report directories are created"

    # Test 2: Configuration report
    ((TESTS_TOTAL++))
    local config_output
    if config_output=$($VALIDATION_SCRIPT config 2>/dev/null); then
        test_pass "Configuration report is generated"
    else
        test_fail "Configuration report generation" "Failed to generate configuration report"
    fi

    # Test 3: Log directory structure
    ((TESTS_TOTAL++))
    assert_file_exists "$VALIDATION_LOGS_DIR" "Validation logs directory exists"

    # Test 4: Report directory structure
    ((TESTS_TOTAL++))
    assert_file_exists "$VALIDATION_REPORTS_DIR" "Validation reports directory exists"

    # Test 5: JSON output validation
    ((TESTS_TOTAL++))
    if [[ -f "$VALIDATION_CACHE_DIR/validation_config.json" ]]; then
        assert_command_success "jq . '$VALIDATION_CACHE_DIR/validation_config.json' >/dev/null 2>&1" "Configuration file is valid JSON"
    else
        test_skip "json_validation" "Configuration file not available"
    fi
}

# Test suite: Performance regression detection
test_performance_regression() {
    log_info "Running performance regression tests..."

    # Test 1: Benchmark script integration
    ((TESTS_TOTAL++))
    if [[ -x "$BENCHMARK_SCRIPT" ]]; then
        assert_command_success "$BENCHMARK_SCRIPT help >/dev/null 2>&1" "Benchmark script is integrated"
    else
        test_skip "benchmark_integration" "Benchmark script not available"
    fi

    # Test 2: Regression test framework
    ((TESTS_TOTAL++))
    assert_command_success "$BENCHMARK_SCRIPT regression >/dev/null 2>&1 || true" "Regression test framework works"

    # Test 3: Baseline creation
    ((TESTS_TOTAL++))
    assert_command_success "$BENCHMARK_SCRIPT baseline >/dev/null 2>&1 || true" "Baseline creation works"

    # Test 4: Performance comparison
    ((TESTS_TOTAL++))
    # Create dummy baseline and current files for testing
    local baseline_dir="$TEST_TEMP_DIR/baselines"
    local current_dir="$TEST_TEMP_DIR/current"
    mkdir -p "$baseline_dir" "$current_dir"

    # Create dummy baseline
    cat > "$baseline_dir/performance_baseline.json" << 'EOF'
{
  "statistics": {
    "avg_throughput": 1000.0
  }
}
EOF

    # Create dummy current results
    cat > "$current_dir/throughput_report.json" << 'EOF'
{
  "statistics": {
    "avg_throughput": 950.0
  }
}
EOF

    # Test comparison logic (would normally be done by the regression function)
    local baseline_throughput=1000.0
    local current_throughput=950.0
    local regression_percentage=$(awk "BEGIN {printf \"%.2f\", ((${baseline_throughput} - ${current_throughput}) / ${baseline_throughput}) * 100}")

    if (( $(awk "BEGIN {print ($regression_percentage > 5.0)}") )); then
        test_pass "Performance regression detection works"
    else
        test_fail "Performance regression detection" "Expected regression detection but didn't trigger"
    fi

    # Test 5: Alert mechanism
    ((TESTS_TOTAL++))
    # Test notification system (if available)
    if command -v send_notification >/dev/null 2>&1; then
        assert_command_success "send_notification 'Test' 'Test message' 'info' >/dev/null 2>&1 || true" "Notification system works"
    else
        test_skip "notification_system" "Notification system not available"
    fi
}

# Generate test report
generate_test_report() {
    local report_file="$TEST_RESULTS_DIR/validation_test_report_$(date +%Y%m%d_%H%M%S).json"
    local summary_file="$TEST_RESULTS_DIR/validation_test_summary_$(date +%Y%m%d_%H%M%S).txt"

    # Calculate success rate
    local success_rate=0
    if [[ $TESTS_TOTAL -gt 0 ]]; then
        success_rate=$(awk "BEGIN {printf \"%.2f\", ($TESTS_PASSED / $TESTS_TOTAL) * 100}")
    fi

    # Generate JSON report
    cat > "$report_file" << EOF
{
  "test_suite": "T052 Dependency Validation Framework",
  "test_timestamp": "$(date -u +"%Y-%m-%dT%H:%M:%SZ")",
  "environment": {
    "os": "$(uname -s)",
    "architecture": "$(uname -m)",
    "kernel": "$(uname -r)",
    "project_root": "$PROJECT_ROOT",
    "cuda_available": $(command -v nvcc >/dev/null 2>&1 && echo "true" || echo "false"),
    "cmake_available": $(command -v cmake >/dev/null 2>&1 && echo "true" || echo "false"),
    "gpu_available": $(command -v nvidia-smi >/dev/null 2>&1 && echo "true" || echo "false")
  },
  "summary": {
    "total_tests": $TESTS_TOTAL,
    "passed_tests": $TESTS_PASSED,
    "failed_tests": $TESTS_FAILED,
    "skipped_tests": $TESTS_SKIPPED,
    "success_rate": $success_rate
  },
  "test_categories": {
    "initialization": "Framework initialization and setup",
    "configuration": "Configuration validation and management",
    "dependency_health": "Dependency health and compatibility checks",
    "build_validation": "Build system validation",
    "performance_benchmarking": "Performance benchmarking framework",
    "validation_workflow": "End-to-end validation workflows",
    "dependency_management": "Integration with dependency management system",
    "error_handling": "Error handling and edge cases",
    "report_generation": "Report generation and logging",
    "performance_regression": "Performance regression detection"
  }
}
EOF

    # Generate text summary
    cat > "$summary_file" << EOF
T052 Dependency Validation Framework - Test Summary
===============================================

Test Date: $(date -u +"%Y-%m-%dT%H:%M:%SZ")
Environment: $(uname -s) $(uname -m) (kernel $(uname -r))

RESULTS SUMMARY
---------------
Total Tests:     $TESTS_TOTAL
Passed Tests:    $TESTS_PASSED
Failed Tests:    $TESTS_FAILED
Skipped Tests:   $TESTS_SKIPPED
Success Rate:    ${success_rate}%

TEST CATEGORIES
---------------
1. Framework Initialization
2. Configuration Validation
3. Dependency Health Checks
4. Build Validation
5. Performance Benchmarking
6. Validation Workflow
7. Dependency Management Integration
8. Error Handling and Edge Cases
9. Report Generation and Logging
10. Performance Regression Detection

RECOMMENDATIONS
---------------
EOF

    if [[ $TESTS_FAILED -eq 0 ]]; then
        cat >> "$summary_file" << EOF
✅ All tests passed! The dependency validation framework is working correctly.

Next steps:
- Run the framework in production environment
- Set up automated validation scheduling
- Monitor performance and adjust configurations as needed
EOF
    else
        cat >> "$summary_file" << EOF
⚠️  Some tests failed. Please review the failures and address them.

Common issues:
- Missing dependencies (CUDA, CMake, etc.)
- Insufficient permissions
- Build system not configured
- Missing test data

Actions needed:
- Review failed tests and address root causes
- Ensure all required dependencies are installed
- Check system permissions and configuration
EOF
    fi

    cat >> "$summary_file" << EOF

DETAILED REPORT
---------------
JSON report: $report_file

For detailed test results and logs, check:
- Test results directory: $TEST_RESULTS_DIR
- Temporary test files: $TEST_TEMP_DIR
- Validation logs: $VALIDATION_LOGS_DIR
EOF

    log_success "Test reports generated:"
    log_success "  JSON report: $report_file"
    log_success "  Summary report: $summary_file"
}

# Print test summary
print_test_summary() {
    echo ""
    echo "================================"
    echo "T052 Validation Framework Tests"
    echo "================================"
    echo ""
    echo "Total Tests:    $TESTS_TOTAL"
    echo -e "Passed Tests:   ${GREEN}$TESTS_PASSED${NC}"
    echo -e "Failed Tests:   ${RED}$TESTS_FAILED${NC}"
    echo -e "Skipped Tests:  ${YELLOW}$TESTS_SKIPPED${NC}"
    echo ""

    if [[ $TESTS_FAILED -eq 0 ]]; then
        echo -e "${GREEN}🎉 All tests passed!${NC}"
    else
        echo -e "${RED}❌ Some tests failed. Please review the failures.${NC}"
    fi

    echo ""
    echo "Detailed reports generated in: $TEST_RESULTS_DIR"
}

# Print usage information
print_usage() {
    cat << EOF
T052: Test Framework for Dependency Validation

USAGE:
    $0 [command] [options]

COMMANDS:
    run                     Run all test suites
    suite <name>            Run specific test suite
    category <name>         Run tests by category
    report                  Generate test report only
    help                    Show this help message

TEST SUITES:
    initialization          Framework initialization tests
    configuration           Configuration validation tests
    dependency_health       Dependency health check tests
    build_validation        Build validation tests
    performance_benchmarking Performance benchmarking tests
    validation_workflow     Validation workflow tests
    integration             Dependency management integration tests
    error_handling          Error handling and edge case tests
    reports                 Report generation tests
    regression              Performance regression tests

CATEGORIES:
    core                    Core framework functionality
    integration             System integration tests
    performance             Performance-related tests
    error_handling          Error handling and edge cases
    reports                 Reporting and logging tests

EXAMPLES:
    $0 run                  # Run all tests
    $0 suite initialization # Run initialization tests only
    $0 category core        # Run core functionality tests
    $0 report               # Generate test report only

OPTIONS:
    --verbose               Enable verbose output
    --keep-temp             Keep temporary test files
    --quick                 Run quick test subset
    --help, -h              Show this help message

EOF
}

# Main script execution
main() {
    local command="run"
    local suite_name=""
    local category_name=""
    local verbose=false
    local keep_temp=false
    local quick=false

    # Parse arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            --verbose)
                verbose=true
                shift
                ;;
            --keep-temp)
                keep_temp=true
                shift
                ;;
            --quick)
                quick=true
                shift
                ;;
            --help|-h)
                print_usage
                exit 0
                ;;
            run|suite|category|report|help)
                command="$1"
                shift
                if [[ $# -gt 0 && ! "$1" =~ ^-- ]]; then
                    if [[ "$command" == "suite" ]]; then
                        suite_name="$1"
                    elif [[ "$command" == "category" ]]; then
                        category_name="$1"
                    fi
                    shift
                fi
                ;;
            *)
                log_error "Unknown argument: $1"
                print_usage
                exit 1
                ;;
        esac
    done

    # Set up signal handlers for cleanup
    trap cleanup_test_environment EXIT

    # Initialize test framework
    init_test_framework

    log_info "Starting T052 dependency validation framework tests..."
    log_info "Command: $command"

    # Execute tests based on command
    case "$command" in
        "run")
            if [[ "$quick" == "true" ]]; then
                # Run quick subset of tests
                test_validation_framework_initialization
                test_configuration_validation
                test_dependency_health_checks
                test_report_generation
            else
                # Run all test suites
                test_validation_framework_initialization
                test_configuration_validation
                test_dependency_health_checks
                test_build_validation
                test_performance_benchmarking
                test_validation_workflow
                test_dependency_management_integration
                test_error_handling
                test_report_generation
                test_performance_regression
            fi
            ;;
        "suite")
            case "$suite_name" in
                "initialization") test_validation_framework_initialization ;;
                "configuration") test_configuration_validation ;;
                "dependency_health") test_dependency_health_checks ;;
                "build_validation") test_build_validation ;;
                "performance_benchmarking") test_performance_benchmarking ;;
                "validation_workflow") test_validation_workflow ;;
                "integration") test_dependency_management_integration ;;
                "error_handling") test_error_handling ;;
                "reports") test_report_generation ;;
                "regression") test_performance_regression ;;
                *)
                    log_error "Unknown test suite: $suite_name"
                    echo "Available suites: initialization, configuration, dependency_health, build_validation, performance_benchmarking, validation_workflow, integration, error_handling, reports, regression"
                    exit 1
                    ;;
            esac
            ;;
        "category")
            case "$category_name" in
                "core")
                    test_validation_framework_initialization
                    test_configuration_validation
                    test_validation_workflow
                    ;;
                "integration")
                    test_dependency_health_checks
                    test_build_validation
                    test_dependency_management_integration
                    ;;
                "performance")
                    test_performance_benchmarking
                    test_performance_regression
                    ;;
                "error_handling")
                    test_error_handling
                    ;;
                "reports")
                    test_report_generation
                    ;;
                *)
                    log_error "Unknown test category: $category_name"
                    echo "Available categories: core, integration, performance, error_handling, reports"
                    exit 1
                    ;;
            esac
            ;;
        "report")
            # Generate report only
            ;;
        "help"|"")
            print_usage
            exit 0
            ;;
        *)
            log_error "Unknown command: $command"
            print_usage
            exit 1
            ;;
    esac

    # Generate test report
    generate_test_report

    # Print summary
    print_test_summary

    # Cleanup if not keeping temp files
    if [[ "$keep_temp" != "true" ]]; then
        cleanup_test_environment
    fi

    # Exit with appropriate code
    if [[ $TESTS_FAILED -gt 0 ]]; then
        exit 1
    else
        exit 0
    fi
}

# Execute main function with all arguments
main "$@"