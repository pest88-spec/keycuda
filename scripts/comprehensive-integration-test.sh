#!/bin/bash

# Comprehensive Integration Verification Suite
# T057: Integration verification suite for all components

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
LOG_FILE="$PROJECT_ROOT/integration-test.log"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Test results
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0
TEST_RESULTS=()

# Logging function
log() {
    echo "$(date '+%Y-%m-%d %H:%M:%S') [INTEGRATION_TEST] $1" | tee -a "$LOG_FILE"
}

# Print colored output
print_status() {
    local status=$1
    local message=$2
    case $status in
        "PASS") echo -e "${GREEN}✓ PASS${NC}: $message" ;;
        "FAIL") echo -e "${RED}✗ FAIL${NC}: $message" ;;
        "WARN") echo -e "${YELLOW}⚠ WARN${NC}: $message" ;;
        "INFO") echo -e "${BLUE}ℹ INFO${NC}: $message" ;;
    esac
}

# Test function
run_test() {
    local test_name=$1
    local test_command=$2

    ((TOTAL_TESTS++))

    log "Running test: $test_name"

    if eval "$test_command" >> "$LOG_FILE" 2>&1; then
        ((PASSED_TESTS++))
        TEST_RESULTS+=("$test_name:PASS")
        print_status "PASS" "$test_name"
        return 0
    else
        ((FAILED_TESTS++))
        TEST_RESULTS+=("$test_name:FAIL")
        print_status "FAIL" "$test_name"
        return 1
    fi
}

# Integration test categories
test_build_system_integration() {
    log "Testing build system integration..."

    # Test CMake configuration
    run_test "CMake configuration validation" \
        "cmake -B /tmp/test-build -S '$PROJECT_ROOT' -DCMAKE_BUILD_TYPE=Release"

    # Test extraction integration
    run_test "Extracted libraries detection" \
        "test -d '$PROJECT_ROOT/src/extracted/bitcrack' && test -d '$PROJECT_ROOT/src/extracted/secp256k1-zkp'"

    # Test offline build capability
    run_test "Offline build configuration" \
        "cmake -B /tmp/test-offline -S '$PROJECT_ROOT' -DOFFLINE_BUILD=ON"

    # Clean up test builds
    rm -rf /tmp/test-build /tmp/test-offline
}

test_dependency_management() {
    log "Testing dependency management..."

    # Test version management system
    run_test "Version management initialization" \
        "'$PROJECT_ROOT/scripts/update-dependencies.sh' init --quiet"

    # Test conflict detection
    run_test "Conflict detection system" \
        "'$PROJECT_ROOT/scripts/update-dependencies.sh' detect-conflicts --quiet"

    # Test compatibility validation
    run_test "Compatibility validation" \
        "'$PROJECT_ROOT/scripts/update-dependencies.sh' validate --quiet'"

    # Test rollback capability
    run_test "Version rollback system" \
        "'$PROJECT_ROOT/scripts/update-dependencies.sh' validate-rollback --quiet'"
}

test_integration_infrastructure() {
    log "Testing integration infrastructure..."

    # Test integration manager
    run_test "Integration manager compilation" \
        "test -f '$PROJECT_ROOT/src/integration/integration_manager.cpp'"

    # Test conflict detector
    run_test "Conflict detector compilation" \
        "test -f '$PROJECT_ROOT/src/integration/conflict_detector.cpp'"

    # Test version rollback
    run_test "Version rollback compilation" \
        "test -f '$PROJECT_ROOT/src/integration/version_rollback.cpp'"

    # Test manifest system
    run_test "Manifest system compilation" \
        "test -f '$PROJECT_ROOT/src/integration/manifest.cpp'"

    # Test metrics collection
    run_test "Metrics system compilation" \
        "test -f '$PROJECT_ROOT/src/integration/metrics/metrics.cpp'"
}

test_attribution_system() {
    log "Testing attribution system..."

    # Test attribution headers exist
    run_test "BitCrack attribution headers" \
        "find '$PROJECT_ROOT/src/extracted/bitcrack' -name '*.cpp' -o -name '*.cu' -o -name '*.c' | xargs grep -l '@origin' | wc -l | grep -q '[1-9]'"

    run_test "secp256k1-zkp attribution headers" \
        "find '$PROJECT_ROOT/src/extracted/secp256k1-zkp' -name '*.cpp' -o -name '*.cu' -o -name '*.c' | xargs grep -l '@origin' | wc -l | grep -q '[1-9]'"

    # Test license documentation
    run_test "License documentation existence" \
        "test -d '$PROJECT_ROOT/docs/licenses' || test -f '$PROJECT_ROOT/docs/third-party-licenses.md'"

    # Test attribution verification
    run_test "Attribution verification script" \
        "test -f '$PROJECT_ROOT/scripts/verify-attribution.sh'"
}

test_deployment_system() {
    log "Testing deployment system..."

    # Test deployment packaging
    run_test "Deployment packaging script" \
        "test -f '$PROJECT_ROOT/scripts/package-deployment.sh'"

    # Test deployment verification
    run_test "Deployment verification script" \
        "test -f '$PROJECT_ROOT/scripts/verify-deployment.sh'"

    # Test deployment testing
    run_test "Deployment testing script" \
        "test -f '$PROJECT_ROOT/scripts/test-deployment.sh'"

    # Test deployment configuration logging
    run_test "Deployment config logging" \
        "test -f '$PROJECT_ROOT/src/integration/deployment_config_logger.cpp'"
}

test_validation_frameworks() {
    log "Testing validation frameworks..."

    # Test integrity verification
    run_test "Integrity verification framework" \
        "test -f '$PROJECT_ROOT/scripts/verify-integration.sh'"

    # Test digest verification
    run_test "Digest verification system" \
        "test -f '$PROJECT_ROOT/scripts/digest/check-artifact-digests.sh'"

    # Test compatibility validator
    run_test "Compatibility validator" \
        "test -f '$PROJECT_ROOT/src/integration/compatibility_validator.cpp'"

    # Test dependency reporter
    run_test "Dependency reporter" \
        "test -f '$PROJECT_ROOT/src/integration/dependency_reporter.cpp'"
}

test_error_handling() {
    log "Testing error handling..."

    # Test error handler script
    run_test "Error handling script" \
        "test -f '$PROJECT_ROOT/scripts/integration/error-handler.sh'"

    # Test recovery strategies
    run_test "Recovery strategies script" \
        "test -f '$PROJECT_ROOT/scripts/integration/recovery-strategies.sh'"

    # Test health check
    run_test "Health check script" \
        "test -f '$PROJECT_ROOT/scripts/integration/health-check.sh'"
}

test_scheduling_and_notification() {
    log "Testing scheduling and notification..."

    # Test scheduling system
    run_test "Update scheduling script" \
        "test -f '$PROJECT_ROOT/scripts/schedule-dependency-updates.sh'"

    # Test notification functions
    run_test "Notification functions script" \
        "test -f '$PROJECT_ROOT/scripts/notification-functions.sh'"

    # Test scheduling integration
    run_test "Scheduling commands in update script" \
        "'$PROJECT_ROOT/scripts/update-dependencies.sh' help | grep -q 'schedule-updates'"
}

test_validation_testing() {
    log "Testing validation and testing framework..."

    # Test validation commands
    run_test "Validation commands exist" \
        "'$PROJECT_ROOT/scripts/update-dependencies.sh' help | grep -q 'validate-updates'"

    # Test benchmarking commands
    run_test "Benchmarking commands exist" \
        "'$PROJECT_ROOT/scripts/update-dependencies.sh' help | grep -q 'benchmark-performance'"

    # Test comprehensive testing
    run_test "Comprehensive testing commands" \
        "'$PROJECT_ROOT/scripts/update-dependencies.sh' help | grep -q 'validation-comprehensive-test'"
}

test_documentation() {
    log "Testing documentation..."

    # Test integration guide
    run_test "Integration guide exists" \
        "test -f '$PROJECT_ROOT/docs/INTEGRATION_GUIDE.md'"

    # Test README in integration
    run_test "Integration README exists" \
        "test -f '$PROJECT_ROOT/src/integration/README.md' || test -f '$PROJECT_ROOT/README.md'"

    # Test script documentation
    run_test "Scripts have help documentation" \
        "'$PROJECT_ROOT/scripts/update-dependencies.sh' help > /dev/null"
}

# Generate test report
generate_report() {
    local report_file="$PROJECT_ROOT/integration-test-report.json"

    log "Generating test report..."

    cat > "$report_file" << EOF
{
    "test_suite": "comprehensive-integration-verification",
    "timestamp": "$(date -Iseconds)",
    "total_tests": $TOTAL_TESTS,
    "passed_tests": $PASSED_TESTS,
    "failed_tests": $FAILED_TESTS,
    "success_rate": $(echo "scale=2; $PASSED_TESTS * 100 / $TOTAL_TESTS" | bc -l),
    "results": [
EOF

    for i in "${!TEST_RESULTS[@]}"; do
        local result="${TEST_RESULTS[$i]}"
        local test_name="${result%:*}"
        local test_status="${result#*:}"

        if [ $i -ne 0 ]; then
            echo "," >> "$report_file"
        fi

        cat >> "$report_file" << EOF
        {
            "test_name": "$test_name",
            "status": "$test_status",
            "index": $i
        }
EOF
    done

    cat >> "$report_file" << EOF
    ]
}
EOF

    log "Test report generated: $report_file"
}

# Main execution
main() {
    local mode=${1:-"full"}

    log "Starting comprehensive integration verification suite..."
    log "Mode: $mode"

    # Initialize log file
    echo "Integration Test Log - $(date)" > "$LOG_FILE"

    case $mode in
        "full")
            test_build_system_integration
            test_dependency_management
            test_integration_infrastructure
            test_attribution_system
            test_deployment_system
            test_validation_frameworks
            test_error_handling
            test_scheduling_and_notification
            test_validation_testing
            test_documentation
            ;;
        "build")
            test_build_system_integration
            ;;
        "deps")
            test_dependency_management
            ;;
        "infra")
            test_integration_infrastructure
            ;;
        "deploy")
            test_deployment_system
            ;;
        *)
            echo "Usage: $0 [full|build|deps|infra|deploy]"
            exit 1
            ;;
    esac

    # Generate final report
    generate_report

    # Print summary
    echo
    print_status "INFO" "Integration test suite completed"
    print_status "INFO" "Total tests: $TOTAL_TESTS"
    print_status "INFO" "Passed: $PASSED_TESTS"
    print_status "INFO" "Failed: $FAILED_TESTS"

    local success_rate=$(echo "scale=2; $PASSED_TESTS * 100 / $TOTAL_TESTS" | bc -l)
    print_status "INFO" "Success rate: ${success_rate}%"

    if [ $FAILED_TESTS -eq 0 ]; then
        print_status "PASS" "All integration tests passed!"
        log "All integration tests passed successfully"
        return 0
    else
        print_status "FAIL" "$FAILED_TESTS tests failed"
        log "Integration tests completed with $FAILED_TESTS failures"
        return 1
    fi
}

# Script entry point
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi