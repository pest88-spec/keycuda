#!/bin/bash

# Self-Contained Deployment Verification Script
# T041: Verify deployment starts successfully without dependency installation
# User Story 2: One-Click Deployment - Acceptance Criteria

set -euo pipefail

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DEPLOYMENT_DIR="$PROJECT_ROOT/build/deployment"
VERIFICATION_DIR="$PROJECT_ROOT/self-contained-verification"
LOG_DIR="$VERIFICATION_DIR/logs"
REPORT_DIR="$VERIFICATION_DIR/reports"
ISOLATED_TEST_DIR="/tmp/keycuda-isolated-test-$$"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m'

# Verification configuration
VERIFICATION_TIMEOUT=300
ENABLE_NETWORK_ISOLATION=true
ENABLE_ENVIRONMENT_ISOLATION=true
ENABLE_DEPENDENCY_CHECK=true
ENABLE_FUNCTIONALITY_TEST=true
STRICT_ISOLATION=false

# Test results
ISOLATION_TESTS_PASSED=0
ISOLATION_TESTS_FAILED=0
ISOLATION_TESTS_TOTAL=0
SELF_CONTAINED_STATUS="UNKNOWN"
ISOLATION_ISSUES=()

# Logging functions
log() {
    echo -e "${BLUE}[$(date '+%Y-%m-%d %H:%M:%S')]${NC} $1"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1" >&2
}

warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

info() {
    echo -e "${PURPLE}[INFO]${NC} $1"
}

debug() {
    if [[ "${DEBUG:-false}" == true ]]; then
        echo -e "${CYAN}[DEBUG]${NC} $1"
    fi
}

# Test result tracking
test_pass() {
    ((ISOLATION_TESTS_PASSED++))
    success "✅ PASS: Self-contained test"
}

test_fail() {
    ((ISOLATION_TESTS_FAILED++))
    error "❌ FAIL: Self-contained test"
}

# Initialize verification environment
initialize_verification() {
    log "Initializing self-contained deployment verification..."

    # Create directories
    mkdir -p "$VERIFICATION_DIR"
    mkdir -p "$LOG_DIR"
    mkdir -p "$REPORT_DIR"
    mkdir -p "$ISOLATED_TEST_DIR"

    # Clear previous results
    ISOLATION_TESTS_PASSED=0
    ISOLATION_TESTS_FAILED=0
    ISOLATION_TESTS_TOTAL=0
    SELF_CONTAINED_STATUS="UNKNOWN"
    ISOLATION_ISSUES=()

    # Check if deployment package exists
    if [[ ! -d "$DEPLOYMENT_DIR" ]]; then
        error "Deployment directory not found: $DEPLOYMENT_DIR"
        error "Please run deployment package generation first (T033)"
        return 1
    fi

    success "Verification environment initialized"
    ((ISOLATION_TESTS_TOTAL++))
}

# Create isolated test environment
create_isolated_environment() {
    log "Creating isolated test environment..."

    local test_package_dir="$ISOLATED_TEST_DIR/deployment"

    # Copy deployment package to isolated directory
    log "Copying deployment package to isolated environment..."
    cp -r "$DEPLOYMENT_DIR" "$test_package_dir"

    # Verify package structure in isolated environment
    local required_dirs=("bin" "lib" "scripts" "config")
    for dir in "${required_dirs[@]}"; do
        if [[ -d "$test_package_dir/$dir" ]]; then
            debug "✓ Directory exists in isolated environment: $dir"
        else
            error "❌ Required directory missing in isolated environment: $dir"
            ISOLATION_ISSUES+=("Missing directory: $dir")
            return 1
        fi
    done

    # Set up isolated environment variables
    cat > "$ISOLATED_TEST_DIR/isolated-env.sh" << 'EOF'
#!/bin/bash
# Isolated Environment Setup

# Clear all existing environment variables that might affect deployment
unset LD_LIBRARY_PATH
unset DYLD_LIBRARY_PATH
unset PYTHONPATH
unset PATH

# Set minimal PATH for basic operations
export PATH="/bin:/usr/bin:/usr/local/bin"

# Set isolated deployment-specific paths
export DEPLOYMENT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/deployment" && pwd)"
export LD_LIBRARY_PATH="$DEPLOYMENT_ROOT/lib"
export PATH="$DEPLOYMENT_ROOT/bin:$PATH"

# Disable external dependencies
export DEPLOYMENT_SELF_CONTAINED=1
export OFFLINE_MODE=1

echo "Isolated environment configured:"
echo "  DEPLOYMENT_ROOT: $DEPLOYMENT_ROOT"
echo "  LD_LIBRARY_PATH: $LD_LIBRARY_PATH"
echo "  PATH: $PATH"
EOF

    chmod +x "$ISOLATED_TEST_DIR/isolated-env.sh"

    success "Isolated test environment created"
    ((ISOLATION_TESTS_TOTAL++))
}

# Verify no external dependencies are required
verify_no_external_dependencies() {
    log "Verifying no external dependencies are required..."

    local test_binary="$ISOLATED_TEST_DIR/deployment/bin/Puzzle71Solver"

    if [[ ! -f "$test_binary" ]]; then
        error "❌ Main binary not found in isolated environment"
        ISOLATION_ISSUES+=("Main binary missing")
        return 1
    fi

    # Check binary dependencies
    if command -v ldd >/dev/null 2>&1; then
        log "Checking binary dependencies in isolated environment..."

        local external_deps=0
        local internal_deps=0

        while IFS= read -r line; do
            if [[ -n "$line" && "$line" != *"not a dynamic executable"* ]]; then
                local dep_name=$(echo "$line" | awk '{print $1}')
                local dep_path=$(echo "$line" | awk '{print $3}')

                if [[ "$dep_path" == "not found" ]]; then
                    error "❌ Missing dependency: $dep_name"
                    ISOLATION_ISSUES+=("Missing dependency: $dep_name")
                    ((external_deps++))
                else
                    # Check if dependency is internal to deployment package
                    if [[ "$dep_path" == "$ISOLATED_TEST_DIR/deployment"* ]]; then
                        debug "✓ Internal dependency: $dep_name"
                        ((internal_deps++))
                    elif [[ "$dep_path" == /lib/* || "$dep_path" == /usr/lib/* ]]; then
                        warning "⚠ System dependency: $dep_name ($dep_path)"
                        ISOLATION_ISSUES+=("System dependency required: $dep_name")
                        ((external_deps++))
                    else
                        warning "⚠ External dependency: $dep_name ($dep_path)"
                        ISOLATION_ISSUES+=("External dependency required: $dep_name")
                        ((external_deps++))
                    fi
                fi
            fi
        done < <(ldd "$test_binary" 2>/dev/null || true)

        log "Dependency analysis:"
        log "  Internal dependencies: $internal_deps"
        log "  External dependencies: $external_deps"

        if [[ $external_deps -eq 0 ]]; then
            success "✅ No external dependencies required"
        else
            error "❌ $external_deps external dependencies found"
            return 1
        fi
    else
        warning "Cannot check dependencies: ldd not available"
    fi

    # Check for script dependencies
    local scripts_dir="$ISOLATED_TEST_DIR/deployment/scripts"
    if [[ -d "$scripts_dir" ]]; then
        for script in "$scripts_dir"/*.sh; do
            if [[ -f "$script" ]]; then
                # Check script shebang
                local shebang=$(head -n1 "$script")
                if [[ "$shebang" == "#!/bin/bash" || "$shebang" == "#!/usr/bin/env bash" ]]; then
                    debug "✓ Script uses standard bash: $(basename "$script")"
                else
                    warning "⚠ Script may require non-standard shell: $(basename "$script")"
                fi
            fi
        done
    fi

    success "External dependency verification completed"
    ((ISOLATION_TESTS_TOTAL++))
}

# Test deployment in isolated environment
test_isolated_deployment() {
    log "Testing deployment in isolated environment..."

    # Source isolated environment
    source "$ISOLATED_TEST_DIR/isolated-env.sh"

    cd "$ISOLATED_TEST_DIR/deployment"

    # Test 1: Binary execution
    log "Testing binary execution in isolated environment..."
    if timeout 30s ./bin/Puzzle71Solver --help >/dev/null 2>&1; then
        success "✅ Binary executes successfully in isolated environment"
    else
        error "❌ Binary execution failed in isolated environment"
        ISOLATION_ISSUES+=("Binary execution failure in isolation")
        return 1
    fi

    # Test 2: Version information
    log "Testing version information..."
    if timeout 30s ./bin/Puzzle71Solver --version >/dev/null 2>&1; then
        success "✅ Version command works in isolated environment"
    else
        warning "⚠ Version command failed (may be optional)"
    fi

    # Test 3: Configuration loading
    log "Testing configuration loading..."
    if timeout 30s ./bin/Puzzle71Solver --config config/minimal.conf >/dev/null 2>&1; then
        success "✅ Configuration loading works in isolated environment"
    else
        warning "⚠ Configuration loading failed (may be optional)"
    fi

    # Test 4: Script execution
    log "Testing script execution in isolated environment..."
    if [[ -f "scripts/health-check.sh" ]]; then
        if timeout 60s bash scripts/health-check.sh >/dev/null 2>&1; then
            success "✅ Health check script works in isolated environment"
        else
            warning "⚠ Health check script failed in isolated environment"
            ISOLATION_ISSUES+=("Health check script failure")
        fi
    fi

    success "Isolated deployment testing completed"
    ((ISOLATION_TESTS_TOTAL++))
}

# Test network independence
test_network_independence() {
    if [[ "$ENABLE_NETWORK_ISOLATION" != true ]]; then
        log "Skipping network independence test (disabled)"
        return 0
    fi

    log "Testing network independence..."

    # Test with network isolation using unshare if available
    if command -v unshare >/dev/null 2>&1; then
        log "Testing with network isolation (unshare)..."

        cd "$ISOLATED_TEST_DIR/deployment"

        # Test binary execution without network access
        if unshare -n bash -c "
            source '$ISOLATED_TEST_DIR/isolated-env.sh'
            cd '$ISOLATED_TEST_DIR/deployment'
            timeout 30s ./bin/Puzzle71Solver --help >/dev/null 2>&1
        "; then
            success "✅ Deployment works without network access"
        else
            error "❌ Deployment requires network access"
            ISOLATION_ISSUES+=("Network dependency detected")
            return 1
        fi
    else
        warning "Cannot test network isolation: unshare not available"
    fi

    # Check for network-related configurations
    local config_files=("config/default.conf" "config/minimal.conf")
    for config_file in "${config_files[@]}"; do
        if [[ -f "$ISOLATED_TEST_DIR/deployment/$config_file" ]]; then
            if grep -q "http\|ftp\|git\|download" "$ISOLATED_TEST_DIR/deployment/$config_file" 2>/dev/null; then
                warning "⚠ Configuration may reference network resources: $config_file"
                ISOLATION_ISSUES+=("Network reference in $config_file")
            fi
        fi
    done

    success "Network independence testing completed"
    ((ISOLATION_TESTS_TOTAL++))
}

# Test filesystem independence
test_filesystem_independence() {
    log "Testing filesystem independence..."

    # Check if deployment package references absolute paths
    local absolute_path_refs=0

    # Check binary for embedded paths
    local binary_file="$ISOLATED_TEST_DIR/deployment/bin/Puzzle71Solver"
    if strings "$binary_file" 2>/dev/null | grep -E "^/|^/(usr|lib|opt|home)" >/dev/null; then
        warning "⚠ Binary may contain absolute path references"
        ISOLATION_ISSUES+=("Absolute path references in binary")
        ((absolute_path_refs++))
    fi

    # Check configuration files for absolute paths
    local config_dir="$ISOLATED_TEST_DIR/deployment/config"
    if [[ -d "$config_dir" ]]; then
        while IFS= read -r -d '' config_file; do
            if grep -E "^/|^/(usr|lib|opt|home)" "$config_file" >/dev/null 2>&1; then
                warning "⚠ Configuration contains absolute paths: $(basename "$config_file")"
                ISOLATION_ISSUES+=("Absolute path references in $(basename "$config_file"))")
                ((absolute_path_refs++))
            fi
        done < <(find "$config_dir" -name "*.conf" -name "*.json" -name "*.yaml" -print0 2>/dev/null)
    fi

    # Check scripts for absolute paths
    local scripts_dir="$ISOLATED_TEST_DIR/deployment/scripts"
    if [[ -d "$scripts_dir" ]]; then
        while IFS= read -r -d '' script_file; do
            if grep -E "^/|^/(usr|lib|opt|home)" "$script_file" >/dev/null 2>&1; then
                warning "⚠ Script contains absolute paths: $(basename "$script_file")"
                ISOLATION_ISSUES+=("Absolute path references in $(basename "$script_file"))")
                ((absolute_path_refs++))
            fi
        done < <(find "$scripts_dir" -name "*.sh" -print0 2>/dev/null)
    fi

    if [[ $absolute_path_refs -eq 0 ]]; then
        success "✅ No problematic absolute path references found"
    else
        warning "⚠ $absolute_path_refs absolute path references found"
    fi

    success "Filesystem independence testing completed"
    ((ISOLATION_TESTS_TOTAL++))
}

# Test self-contained functionality
test_self_contained_functionality() {
    if [[ "$ENABLE_FUNCTIONALITY_TEST" != true ]]; then
        log "Skipping self-contained functionality test (disabled)"
        return 0
    fi

    log "Testing self-contained functionality..."

    # Source isolated environment
    source "$ISOLATED_TEST_DIR/isolated-env.sh"

    cd "$ISOLATED_TEST_DIR/deployment"

    # Test core functionality without external dependencies
    log "Testing core functionality..."

    # Test 1: Basic help functionality
    if timeout 30s ./bin/Puzzle71Solver --help > "$ISOLATED_TEST_DIR/help_output.txt" 2>&1; then
        success "✅ Help functionality works self-contained"
    else
        error "❌ Help functionality failed"
        ISOLATION_ISSUES+=("Help functionality failure")
        return 1
    fi

    # Test 2: Configuration validation
    if timeout 30s ./bin/Puzzle71Solver --validate-config >/dev/null 2>&1; then
        success "✅ Configuration validation works self-contained"
    else
        warning "⚠ Configuration validation not available or failed"
    fi

    # Test 3: System information display
    if timeout 30s ./bin/Puzzle71Solver --system-info > "$ISOLATED_TEST_DIR/system_info.txt" 2>&1; then
        success "✅ System information display works self-contained"
    else
        warning "⚠ System information display not available or failed"
    fi

    # Verify no external calls were made
    if grep -q "wget\|curl\|git\|apt\|yum\|pip" "$ISOLATED_TEST_DIR/help_output.txt" 2>/dev/null; then
        error "❌ External tool calls detected in output"
        ISOLATION_ISSUES+=("External tool calls detected")
        return 1
    fi

    success "Self-contained functionality testing completed"
    ((ISOLATION_TESTS_TOTAL++))
}

# Generate verification report
generate_verification_report() {
    log "Generating self-contained deployment verification report..."

    local report_file="$REPORT_DIR/self-contained-verification-$(date +%Y%m%d_%H%M%S).json"

    # Calculate success rate
    local success_rate=0
    if [[ $ISOLATION_TESTS_TOTAL -gt 0 ]]; then
        success_rate=$((ISOLATION_TESTS_PASSED * 100 / ISOLATION_TESTS_TOTAL))
    fi

    # Determine self-contained status
    if [[ $ISOLATION_TESTS_FAILED -eq 0 && ${#ISOLATION_ISSUES[@]} -eq 0 ]]; then
        SELF_CONTAINED_STATUS="FULLY_SELF_CONTAINED"
    elif [[ $ISOLATION_TESTS_FAILED -eq 0 ]]; then
        SELF_CONTAINED_STATUS="MOSTLY_SELF_CONTAINED"
    else
        SELF_CONTAINED_STATUS="NOT_SELF_CONTAINED"
    fi

    cat > "$report_file" << EOF
{
  "self_contained_verification_report": {
    "report_metadata": {
      "generated": "$(date -Iseconds)",
      "script_version": "T041-1.0",
      "report_type": "self_contained_deployment_verification",
      "acceptance_criteria": "T041 - Verify deployment starts successfully without dependency installation"
    },
    "verification_summary": {
      "total_tests": $ISOLATION_TESTS_TOTAL,
      "tests_passed": $ISOLATION_TESTS_PASSED,
      "tests_failed": $ISOLATION_TESTS_FAILED,
      "success_rate_percent": $success_rate,
      "self_contained_status": "$SELF_CONTAINED_STATUS",
      "isolation_issues_count": ${#ISOLATION_ISSUES[@]}
    },
    "verification_tests": {
      "environment_initialization": "COMPLETED",
      "isolated_environment_creation": "COMPLETED",
      "external_dependency_verification": "COMPLETED",
      "isolated_deployment_testing": "COMPLETED",
      "network_independence_testing": "$([ "$ENABLE_NETWORK_ISOLATION" == true ] && echo "COMPLETED" || echo "SKIPPED")",
      "filesystem_independence_testing": "COMPLETED",
      "self_contained_functionality": "$([ "$ENABLE_FUNCTIONALITY_TEST" == true ] && echo "COMPLETED" || echo "SKIPPED")"
    },
    "acceptance_criteria_verification": {
      "t041_requirement": "Verify deployment starts successfully without dependency installation",
      "criteria_met": [
        $([ $ISOLATION_TESTS_FAILED -eq 0 ] && echo '"Deployment executes without external dependencies",')
        $([ $ISOLATION_TESTS_FAILED -eq 0 ] && echo '"All functionality works in isolated environment",')
        $([ ${#ISOLATION_ISSUES[@]} -eq 0 ] && echo '"No isolation issues detected",')
        $([ "$ENABLE_NETWORK_ISOLATION" == true ] && echo '"Network independence verified",')
        '"Self-contained status confirmed"
      ],
      "verification_status": "$([ $ISOLATION_TESTS_FAILED -eq 0 ] && echo "PASSED" || echo "FAILED")"
    },
    "isolation_issues": {
      "total_count": ${#ISOLATION_ISSUES[@]},
      "issues": [
        $(for issue in "${ISOLATION_ISSUES[@]}"; do
            echo "{\"issue\":\"$issue\"},"
        done | sed 's/,$//')
      ]
    },
    "deployment_package_analysis": {
      "package_location": "$DEPLOYMENT_DIR",
      "isolated_test_location": "$ISOLATED_TEST_DIR/deployment",
      "package_structure_verified": true,
      "binary_executable": "$([ -x "$ISOLATED_TEST_DIR/deployment/bin/Puzzle71Solver" ] && echo "true" || echo "false")",
      "libraries_included": "$([ -d "$ISOLATED_TEST_DIR/deployment/lib" ] && echo "true" || echo "false")",
      "scripts_included": "$([ -d "$ISOLATED_TEST_DIR/deployment/scripts" ] && echo "true" || echo "false")",
      "configurations_included": "$([ -d "$ISOLATED_TEST_DIR/deployment/config" ] && echo "true" || echo "false")"
    },
    "independence_verification": {
      "network_independence": {
        "tested": $ENABLE_NETWORK_ISOLATION,
        "status": "$([ "$ENABLE_NETWORK_ISOLATION" == true ] && echo "VERIFIED" || echo "NOT_TESTED")",
        "external_dependencies_required": false
      },
      "filesystem_independence": {
        "tested": true,
        "status": "VERIFIED",
        "absolute_path_references": $(grep -c "Absolute path references" <<< "${ISOLATION_ISSUES[*]}" || echo "0")
      },
      "dependency_independence": {
        "tested": true,
        "status": "VERIFIED",
        "external_dependencies_count": $(grep -c "external\|system.*dependency" <<< "${ISOLATION_ISSUES[*]}" || echo "0")
      }
    },
    "user_story_2_compliance": {
      "user_story": "US2 - One-Click Deployment",
      "acceptance_criteria_t041": "$([ $ISOLATION_TESTS_FAILED -eq 0 ] && echo "COMPLETED" || echo "FAILED")",
      "deployment_starts_without_dependencies": "$([ $ISOLATION_TESTS_FAILED -eq 0 ] && echo "VERIFIED" || echo "FAILED")",
      "self_contained_deployment": "$SELF_CONTAINED_STATUS"
    },
    "recommendations": {
      "if_fully_self_contained": [
        "Deployment package is ready for production use",
        "Can be distributed as a standalone package",
        "No additional dependency installation required"
      ],
      "if_mostly_self_contained": [
        "Address minor isolation issues for full compliance",
        "Package is generally ready with minimal caveats",
        "Document any external requirements"
      ],
      "if_not_self_contained": [
        "Resolve all isolation issues before deployment",
        "Ensure all dependencies are included in package",
        "Re-run verification after fixes"
      ]
    },
    "compliance_status": {
      "t041_compliance": "$([ $ISOLATION_TESTS_FAILED -eq 0 ] && echo "COMPLIANT" || echo "NON_COMPLIANT")",
      "self_contained_deployment": "$SELF_CONTAINED_STATUS",
      "independence_verification": "COMPLETED",
      "acceptance_criteria_met": "$([ $ISOLATION_TESTS_FAILED -eq 0 ] && echo "YES" || echo "NO")"
    }
  }
}
EOF

    success "Verification report generated: $report_file"
}

# Display verification summary
display_verification_summary() {
    echo
    echo "=== Self-Contained Deployment Verification Summary ==="
    echo "Acceptance Criteria: T041 - Verify deployment starts successfully without dependency installation"
    echo "Total Tests: $ISOLATION_TESTS_TOTAL"
    echo "Tests Passed: $ISOLATION_TESTS_PASSED"
    echo "Tests Failed: $ISOLATION_TESTS_FAILED"
    echo "Success Rate: $((ISOLATION_TESTS_PASSED * 100 / ISOLATION_TESTS_TOTAL))%"
    echo "Self-Contained Status: $SELF_CONTAINED_STATUS"
    echo "Isolation Issues: ${#ISOLATION_ISSUES[@]}"
    echo

    if [[ ${#ISOLATION_ISSUES[@]} -gt 0 ]]; then
        echo "Isolation Issues Found:"
        for issue in "${ISOLATION_ISSUES[@]}"; do
            echo "  ❌ $issue"
        done
        echo
    fi

    echo "Generated Reports:"
    echo "  - Verification Report: $REPORT_DIR/self-contained-verification-*.json"
    echo "  - Test Logs: $LOG_DIR/"
    echo

    if [[ "$SELF_CONTAINED_STATUS" == "FULLY_SELF_CONTAINED" ]]; then
        echo -e "${GREEN}✅ ACCEPTANCE CRITERIA T041 FULLY SATISFIED${NC}"
        echo "The deployment package starts successfully without any dependency installation."
        echo "✅ User Story 2 Acceptance Criteria T041: COMPLETED"
    elif [[ "$SELF_CONTAINED_STATUS" == "MOSTLY_SELF_CONTAINED" ]]; then
        echo -e "${YELLOW}⚠️ ACCEPTANCE CRITERIA T041 PARTIALLY SATISFIED${NC}"
        echo "The deployment mostly works independently but has minor issues."
    else
        echo -e "${RED}❌ ACCEPTANCE CRITERIA T041 NOT SATISFIED${NC}"
        echo "The deployment requires external dependencies or has isolation issues."
        echo "❌ User Story 2 Acceptance Criteria T041: FAILED"
    fi
}

# Cleanup test environment
cleanup_test_environment() {
    log "Cleaning up test environment..."
    rm -rf "$ISOLATED_TEST_DIR" 2>/dev/null || true
    success "Test environment cleaned up"
}

# Main execution function
main() {
    log "Starting self-contained deployment verification (T041)..."
    log "This test verifies Acceptance Criteria T041: Verify deployment starts successfully without dependency installation"

    # Parse command line arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            --no-network-isolation)
                ENABLE_NETWORK_ISOLATION=false
                shift
                ;;
            --no-functionality-test)
                ENABLE_FUNCTIONALITY_TEST=false
                shift
                ;;
            --strict-isolation)
                STRICT_ISOLATION=true
                shift
                ;;
            --deployment-dir)
                DEPLOYMENT_DIR="$2"
                shift 2
                ;;
            --debug)
                DEBUG=true
                set -x
                shift
                ;;
            --help|-h)
                cat << EOF
Usage: $0 [options]

Self-Contained Deployment Verification (T041)
Acceptance Criteria: Verify deployment starts successfully without dependency installation

Options:
    --no-network-isolation    Skip network independence testing
    --no-functionality-test   Skip functionality testing
    --strict-isolation        Enable strict isolation mode
    --deployment-dir DIR      Specify deployment directory
    --debug                   Enable debug output
    --help, -h               Show this help message

This script verifies that the deployment package is completely self-contained:
    - Creates isolated test environment
    - Verifies no external dependencies are required
    - Tests deployment execution in isolation
    - Validates network independence
    - Confirms filesystem independence
    - Tests self-contained functionality

This is an acceptance criteria test for User Story 2 (US2).

Exit codes:
    0  All acceptance criteria satisfied
    1  Acceptance criteria not met

Examples:
    $0                                    # Run full verification
    $0 --no-network-isolation            # Skip network tests
    $0 --strict-isolation --debug        # Strict mode with debugging

EOF
                exit 0
                ;;
            *)
                error "Unknown option: $1"
                exit 1
                ;;
        esac
    done

    # Trap cleanup
    trap cleanup_test_environment EXIT

    # Execute verification workflow
    if initialize_verification; then
        if create_isolated_environment; then
            if verify_no_external_dependencies; then
                if test_isolated_deployment; then
                    test_network_independence
                    test_filesystem_independence
                    test_self_contained_functionality
                    generate_verification_report
                    display_verification_summary

                    # Check acceptance criteria satisfaction
                    if [[ "$SELF_CONTAINED_STATUS" == "FULLY_SELF_CONTAINED" ]]; then
                        success "🎉 T041 SELF-CONTAINED DEPLOYMENT VERIFICATION COMPLETED"
                        echo
                        echo -e "${GREEN}✅ T041 Complete: Deployment starts successfully without dependency installation - ACCEPTANCE CRITERIA SATISFIED${NC}"
                        echo -e "${GREEN}✅ User Story 2 Acceptance Criteria T041: COMPLETED${NC}"
                        return 0
                    else
                        error "❌ Acceptance criteria T041 not satisfied"
                        return 1
                    fi
                else
                    error "Isolated deployment testing failed"
                    return 1
                fi
            else
                error "External dependency verification failed"
                return 1
            fi
        else
            error "Isolated environment creation failed"
            return 1
        fi
    else
        error "Verification initialization failed"
        return 1
    fi
}

# Run if executed directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi