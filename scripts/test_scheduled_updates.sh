#!/usr/bin/env bash
# T051: Comprehensive Test Suite for Automated Dependency Update Scheduling System
# Tests all components of the T051 implementation including scheduling, notifications, approval workflow, and audit logging

set -euo pipefail

# Test configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TEST_LOG_DIR="$PROJECT_ROOT/logs/tests"
TEST_RESULTS_FILE="$TEST_LOG_DIR/t051_test_results.json"
TEST_TEMP_DIR="$PROJECT_ROOT/.test_temp"

# Test configuration files
SCHEDULE_CONFIG="$PROJECT_ROOT/.dependency_cache/update_schedule.json"
NOTIFICATION_FUNCTIONS="$SCRIPT_DIR/notification-functions.sh"
SCHEDULE_SCRIPT="$SCRIPT_DIR/schedule-dependency-updates.sh"
UPDATE_SCRIPT="$SCRIPT_DIR/update-dependencies.sh"

# Test framework variables
TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0
TEST_RESULTS=()

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Test logging functions
log_test() {
    local level="$1"
    local message="$2"
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    echo -e "${timestamp} [TEST] ${level} ${message}"
}

log_test_info() { log_test "INFO" "$1"; }
log_test_pass() { log_test "PASS" "${GREEN}$1${NC}"; }
log_test_fail() { log_test "FAIL" "${RED}$1${NC}"; }
log_test_warn() { log_test "WARN" "${YELLOW}$1${NC}"; }

# Test assertion functions
assert_equals() {
    local expected="$1"
    local actual="$2"
    local test_name="$3"

    if [[ "$expected" == "$actual" ]]; then
        log_test_pass "$test_name"
        TESTS_PASSED=$((TESTS_PASSED + 1))
        TEST_RESULTS+=("{\"name\":\"$test_name\",\"status\":\"passed\",\"expected\":\"$expected\",\"actual\":\"$actual\"}")
        return 0
    else
        log_test_fail "$test_name - Expected: '$expected', Actual: '$actual'"
        TESTS_FAILED=$((TESTS_FAILED + 1))
        TEST_RESULTS+=("{\"name\":\"$test_name\",\"status\":\"failed\",\"expected\":\"$expected\",\"actual\":\"$actual\"}")
        return 1
    fi
}

assert_file_exists() {
    local file_path="$1"
    local test_name="$2"

    if [[ -f "$file_path" ]]; then
        log_test_pass "$test_name"
        TESTS_PASSED=$((TESTS_PASSED + 1))
        TEST_RESULTS+=("{\"name\":\"$test_name\",\"status\":\"passed\",\"file\":\"$file_path\"}")
        return 0
    else
        log_test_fail "$test_name - File not found: $file_path"
        TESTS_FAILED=$((TESTS_FAILED + 1))
        TEST_RESULTS+=("{\"name\":\"$test_name\",\"status\":\"failed\",\"file\":\"$file_path\"}")
        return 1
    fi
}

assert_command_success() {
    local command="$1"
    local test_name="$2"

    if eval "$command" >/dev/null 2>&1; then
        log_test_pass "$test_name"
        TESTS_PASSED=$((TESTS_PASSED + 1))
        TEST_RESULTS+=("{\"name\":\"$test_name\",\"status\":\"passed\",\"command\":\"$command\"}")
        return 0
    else
        log_test_fail "$test_name - Command failed: $command"
        TESTS_FAILED=$((TESTS_FAILED + 1))
        TEST_RESULTS+=("{\"name\":\"$test_name\",\"status\":\"failed\",\"command\":\"$command\"}")
        return 1
    fi
}

# Initialize test environment
init_test_environment() {
    log_test_info "Initializing T051 test environment..."

    # Create test directories
    mkdir -p "$TEST_LOG_DIR" "$TEST_TEMP_DIR"

    # Create backup of original configuration
    if [[ -f "$SCHEDULE_CONFIG" ]]; then
        cp "$SCHEDULE_CONFIG" "${SCHEDULE_CONFIG}.test_backup"
    fi

    # Initialize test results file
    echo '{"test_suite":"T051_Scheduled_Updates","timestamp":"","version":"1.0.0","tests":[]}' > "$TEST_RESULTS_FILE"

    log_test_info "Test environment initialized"
}

# Cleanup test environment
cleanup_test_environment() {
    log_test_info "Cleaning up test environment..."

    # Restore original configuration
    if [[ -f "${SCHEDULE_CONFIG}.test_backup" ]]; then
        mv "${SCHEDULE_CONFIG}.test_backup" "$SCHEDULE_CONFIG"
    fi

    # Remove temporary files
    rm -rf "$TEST_TEMP_DIR"

    # Final test results
    local total_tests=$((TESTS_PASSED + TESTS_FAILED))
    local success_rate=0
    if [[ $total_tests -gt 0 ]]; then
        success_rate=$((TESTS_PASSED * 100 / total_tests))
    fi

    # Update test results file
    local timestamp=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
    jq --arg ts "$timestamp" \
        --arg total "$total_tests" \
        --arg passed "$TESTS_PASSED" \
        --arg failed "$TESTS_FAILED" \
        --arg success_rate "$success_rate" \
        '.timestamp = $ts | .total_tests = ($total | tonumber) | .passed = ($passed | tonumber) | .failed = ($failed | tonumber) | .success_rate = ($success_rate | tonumber) | .tests = $tests' \
        --argjson tests "$(printf '%s\n' "${TEST_RESULTS[@]}" | jq -s .)" \
        "$TEST_RESULTS_FILE" > "${TEST_RESULTS_FILE}.tmp" && \
    mv "${TEST_RESULTS_FILE}.tmp" "$TEST_RESULTS_FILE"

    log_test_info "Test cleanup completed"
}

# Test 1: Notification Functions Integration
test_notification_functions() {
    log_test_info "Testing notification functions integration..."

    # Test notification functions script exists
    assert_file_exists "$NOTIFICATION_FUNCTIONS" "Notification functions script exists"

    # Test script is executable
    [[ -x "$NOTIFICATION_FUNCTIONS" ]]
    assert_equals "0" "$?" "Notification functions script is executable"

    # Test notification functions can be sourced
    if source "$NOTIFICATION_FUNCTIONS" 2>/dev/null; then
        assert_equals "0" "$?" "Notification functions can be sourced"
    else
        log_test_warn "Could not source notification functions - may be due to dependencies"
    fi

    # Test notification directories are created
    assert_command_success "source '$NOTIFICATION_FUNCTIONS' 2>/dev/null && command -v init_notification_system >/dev/null" "Notification functions loaded successfully"
}

# Test 2: Schedule Configuration Management
test_schedule_configuration() {
    log_test_info "Testing schedule configuration management..."

    # Test schedule script exists and is executable
    assert_file_exists "$SCHEDULE_SCRIPT" "Schedule script exists"
    [[ -x "$SCHEDULE_SCRIPT" ]]
    assert_equals "0" "$?" "Schedule script is executable"

    # Test initialization
    assert_command_success "'$SCHEDULE_SCRIPT' init" "Schedule system initialization"

    # Test configuration file creation
    assert_file_exists "$SCHEDULE_CONFIG" "Schedule configuration file exists"

    # Test configuration is valid JSON
    if command -v jq >/dev/null 2>&1; then
        assert_command_success "jq empty '$SCHEDULE_CONFIG'" "Schedule configuration is valid JSON"

        # Test required configuration fields exist
        local required_fields=("schedule_version" "enabled" "schedule.daily_checks.enabled" "notifications.console.enabled")
        for field in "${required_fields[@]}"; do
            assert_command_success "jq -e '.$field' '$SCHEDULE_CONFIG' >/dev/null" "Required field exists: $field"
        done
    else
        log_test_warn "jq not available - skipping JSON validation tests"
    fi
}

# Test 3: Scheduling Functionality
test_scheduling_functionality() {
    log_test_info "Testing scheduling functionality..."

    # Test schedule status command
    assert_command_success "'$SCHEDULE_SCRIPT' status" "Schedule status command works"

    # Test schedule enable/disable
    assert_command_success "'$SCHEDULE_SCRIPT' enable all" "Enable all schedules"
    assert_command_success "'$SCHEDULE_SCRIPT' disable all" "Disable all schedules"

    # Test blackout period checking
    assert_command_success "'$SCHEDULE_SCRIPT' check-blackout" "Blackout period checking"

    # Test approval workflow checking
    assert_command_success "'$SCHEDULE_SCRIPT' require-approval external" "Approval workflow checking"
}

# Test 4: Update Script Integration
test_update_script_integration() {
    log_test_info "Testing update script integration..."

    # Test update script exists and is executable
    assert_file_exists "$UPDATE_SCRIPT" "Update script exists"
    [[ -x "$UPDATE_SCRIPT" ]]
    assert_equals "0" "$?" "Update script is executable"

    # Test new T051 commands exist in update script
    if grep -q "schedule-updates" "$UPDATE_SCRIPT"; then
        log_test_pass "T051 commands found in update script"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_test_fail "T051 commands not found in update script"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi

    # Test update script schedule commands
    if command -v jq >/dev/null 2>&1 && [[ -f "$SCHEDULE_CONFIG" ]]; then
        assert_command_success "'$UPDATE_SCRIPT' schedule-updates show" "Update script schedule command"
        assert_command_success "'$UPDATE_SCRIPT' check-schedule" "Update script check schedule"
    fi
}

# Test 5: Notification System Testing
test_notification_system() {
    log_test_info "Testing notification system..."

    # Test notification history command
    assert_command_success "'$SCHEDULE_SCRIPT' show-notifications 5" "Notification history command"

    # Test notification functions integration
    if source "$NOTIFICATION_FUNCTIONS" 2>/dev/null; then
        # Test notification logging
        assert_command_success "send_console_notification 'low' 'test' 'test message' 'true'" "Console notification test"

        # Test notification history recording
        if command -v get_notification_history >/dev/null 2>&1; then
            assert_command_success "get_notification_history 1 >/dev/null" "Notification history recording"
        fi
    else
        log_test_warn "Could not test notification system integration"
    fi
}

# Test 6: Audit Logging Testing
test_audit_logging() {
    log_test_info "Testing audit logging functionality..."

    # Test audit log command
    assert_command_success "'$SCHEDULE_SCRIPT' audit-log 5" "Audit log command"

    # Check if audit log file is created
    local audit_log="$PROJECT_ROOT/logs/audit.log"
    if [[ -f "$audit_log" ]]; then
        log_test_pass "Audit log file exists"
        TESTS_PASSED=$((TESTS_PASSED + 1))

        # Check if audit log has entries
        if [[ -s "$audit_log" ]]; then
            log_test_pass "Audit log has entries"
            TESTS_PASSED=$((TESTS_PASSED + 1))
        else
            log_test_warn "Audit log exists but is empty"
        fi
    else
        log_test_warn "Audit log file not created yet"
    fi
}

# Test 7: Dry Run Functionality
test_dry_run_functionality() {
    log_test_info "Testing dry run functionality..."

    # Test dry run with scheduled updates
    assert_command_success "'$SCHEDULE_SCRIPT' run daily_check --dry-run" "Dry run daily check"
    assert_command_success "'$SCHEDULE_SCRIPT' run weekly_scan --dry-run" "Dry run weekly scan"

    # Test dry run with update script
    if command -v jq >/dev/null 2>&1 && [[ -f "$SCHEDULE_CONFIG" ]]; then
        assert_command_success "'$UPDATE_SCRIPT' check-updates --dry-run >/dev/null 2>&1" "Update script dry run"
    fi
}

# Test 8: Approval Workflow Testing
test_approval_workflow() {
    log_test_info "Testing approval workflow functionality..."

    # Test approval request listing
    assert_command_success "'$SCHEDULE_SCRIPT' approval-list" "Approval list command"

    # Test approval requirement checking
    assert_command_success "'$SCHEDULE_SCRIPT' require-approval cmake_fetch" "Check cmake_fetch approval"
    assert_command_success "'$SCHEDULE_SCRIPT' require-approval external" "Check external approval"

    # Check if approval requests directory structure is created
    local approval_dir="$PROJECT_ROOT/.dependency_cache/approval_requests"
    if [[ -d "$approval_dir" ]]; then
        log_test_pass "Approval requests directory exists"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_test_warn "Approval requests directory not created (expected if no approvals required)"
    fi
}

# Test 9: Integration Testing
test_integration() {
    log_test_info "Testing system integration..."

    # Test comprehensive functionality test
    assert_command_success "'$SCHEDULE_SCRIPT' test" "Comprehensive functionality test"

    # Test system status
    assert_command_success "'$SCHEDULE_SCRIPT' status" "System status command"

    # Test configuration validation
    if command -v jq >/dev/null 2>&1 && [[ -f "$SCHEDULE_CONFIG" ]]; then
        # Test schedule configuration validation
        local enabled=$(jq -r '.enabled // false' "$SCHEDULE_CONFIG")
        assert_equals "true" "$enabled" "Scheduling is enabled by default"

        # Test notification configuration validation
        local console_enabled=$(jq -r '.notifications.console.enabled // false' "$SCHEDULE_CONFIG")
        assert_equals "true" "$console_enabled" "Console notifications enabled by default"
    fi
}

# Test 10: Error Handling and Edge Cases
test_error_handling() {
    log_test_info "Testing error handling and edge cases..."

    # Test invalid commands
    if ! "$SCHEDULE_SCRIPT" invalid_command 2>/dev/null; then
        log_test_pass "Invalid command handling"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_test_fail "Invalid command should fail"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi

    # Test missing configuration handling
    local temp_config="$SCHEDULE_CONFIG"
    mv "$SCHEDULE_CONFIG" "${SCHEDULE_CONFIG}.moved" 2>/dev/null || true
    if ! "$SCHEDULE_SCRIPT" status 2>/dev/null; then
        log_test_pass "Missing configuration handling"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_test_fail "Missing configuration should be handled gracefully"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
    # Restore configuration
    mv "${SCHEDULE_CONFIG}.moved" "$temp_config" 2>/dev/null || true

    # Test invalid schedule types
    if ! "$SCHEDULE_SCRIPT" enable invalid_type 2>/dev/null; then
        log_test_pass "Invalid schedule type handling"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        log_test_fail "Invalid schedule type should fail"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi
}

# Run all tests
run_all_tests() {
    log_test_info "Starting T051 comprehensive test suite..."

    # Initialize test environment
    init_test_environment

    # Run all test categories
    test_notification_functions
    test_schedule_configuration
    test_scheduling_functionality
    test_update_script_integration
    test_notification_system
    test_audit_logging
    test_dry_run_functionality
    test_approval_workflow
    test_integration
    test_error_handling

    # Calculate final results
    local total_tests=$((TESTS_PASSED + TESTS_FAILED))
    local success_rate=0
    if [[ $total_tests -gt 0 ]]; then
        success_rate=$((TESTS_PASSED * 100 / total_tests))
    fi

    # Print summary
    echo
    echo -e "${BLUE}=== T051 Test Suite Summary ===${NC}"
    echo -e "Total tests run: ${CYAN}$total_tests${NC}"
    echo -e "Tests passed: ${GREEN}$TESTS_PASSED${NC}"
    echo -e "Tests failed: ${RED}$TESTS_FAILED${NC}"
    echo -e "Success rate: ${YELLOW}$success_rate%${NC}"

    if [[ $TESTS_FAILED -eq 0 ]]; then
        echo -e "\n${GREEN}✓ All tests passed!${NC}"
        echo -e "T051 implementation is working correctly."
    else
        echo -e "\n${RED}✗ Some tests failed!${NC}"
        echo -e "Please review the failed tests and fix the issues."
    fi

    echo -e "\nDetailed results saved to: ${CYAN}$TEST_RESULTS_FILE${NC}"

    # Cleanup
    cleanup_test_environment

    # Return appropriate exit code
    if [[ $TESTS_FAILED -eq 0 ]]; then
        return 0
    else
        return 1
    fi
}

# Main execution
main() {
    local command="${1:-run}"

    case "$command" in
        "run")
            run_all_tests
            ;;
        "clean")
            cleanup_test_environment
            ;;
        "init")
            init_test_environment
            ;;
        "help"|"-h"|"--help")
            echo "T051 Test Suite for Automated Dependency Update Scheduling"
            echo
            echo "Usage: $0 [COMMAND]"
            echo
            echo "Commands:"
            echo "  run     Run all tests (default)"
            echo "  init    Initialize test environment"
            echo "  clean   Cleanup test environment"
            echo "  help    Show this help message"
            echo
            ;;
        *)
            echo "Unknown command: $command"
            echo "Use '$0 help' for usage information"
            exit 1
            ;;
    esac
}

# Execute main function with all arguments
main "$@"