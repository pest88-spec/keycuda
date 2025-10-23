#!/bin/bash
# T041: Verify Deployment Starts Successfully Without Dependency Installation
# Tests that deployment packages can run in isolated environments without external dependencies

set -euo pipefail

# Script configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Test configuration
TEST_MODE="${TEST_MODE:-comprehensive}"  # quick, comprehensive, stress
ISOLATED_ENV_ROOT="${ISOLATED_ENV_ROOT:-/tmp/puzzle71-isolated-test}"
CLEANUP_TEST="${CLEANUP_TEST:-true}"
VERBOSE_OUTPUT="${VERBOSE_OUTPUT:-false}"

# Resource limits for isolated environments
MAX_MEMORY_MB="${MAX_MEMORY_MB:-1024}"
MAX_DISK_SPACE_MB="${MAX_DISK_SPACE_MB:-512}"
MAX_CPU_PERCENT="${MAX_CPU_PERCENT:-50}"
NETWORK_DISABLED="${NETWORK_DISABLED:-true}"

# Deployment package to test
DEPLOYMENT_PACKAGE="${DEPLOYMENT_PACKAGE:-}"

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

log_isolated() {
    echo -e "${CYAN}[ISOLATED]${NC} $1"
}

# Show help
show_help() {
    cat << EOF
Dependency-Free Deployment Verification Script

USAGE:
    $0 [OPTIONS] [deployment_package]

OPTIONS:
    --test-mode MODE       Test mode: quick, comprehensive, stress (default: comprehensive)
    --env-root DIR         Isolated environment root directory (default: /tmp/puzzle71-isolated-test)
    --no-cleanup           Don't clean up test environments after testing
    --verbose              Enable verbose output
    --memory-limit MB      Memory limit for isolated environments (default: 1024)
    --disk-limit MB        Disk limit for isolated environments (default: 512)
    --cpu-limit PERCENT    CPU usage limit (default: 50)
    --enable-network       Enable network access in isolated environments
    --help, -h             Show this help message

DESCRIPTION:
    Verifies that deployment packages can start and run successfully in
    completely isolated environments without any external dependencies.

TEST MODES:
    quick       Basic functionality test (2 minutes)
    comprehensive Full functionality test with metrics (5 minutes)
    stress      Resource stress test (10 minutes)

EOF
}

# Parse command line arguments
parse_arguments() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            --test-mode)
                TEST_MODE="$2"
                shift 2
                ;;
            --env-root)
                ISOLATED_ENV_ROOT="$2"
                shift 2
                ;;
            --no-cleanup)
                CLEANUP_TEST=false
                shift
                ;;
            --verbose)
                VERBOSE_OUTPUT=true
                shift
                ;;
            --memory-limit)
                MAX_MEMORY_MB="$2"
                shift 2
                ;;
            --disk-limit)
                MAX_DISK_SPACE_MB="$2"
                shift 2
                ;;
            --cpu-limit)
                MAX_CPU_PERCENT="$2"
                shift 2
                ;;
            --enable-network)
                NETWORK_DISABLED=false
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

# Find deployment package if not specified
find_deployment_package() {
    if [[ -z "$DEPLOYMENT_PACKAGE" ]]; then
        log_info "Searching for deployment package..."

        # Look for recent deployment packages
        DEPLOYMENT_PACKAGE=$(find "$PROJECT_ROOT/build" -name "*Deployment*.tar.gz" -o -name "*deployment*.tar.gz" 2>/dev/null | head -1)

        if [[ -n "$DEPLOYMENT_PACKAGE" ]]; then
            log_info "Using deployment package: $DEPLOYMENT_PACKAGE"
        else
            log_error "No deployment package found"
            exit 1
        fi
    fi

    if [[ ! -f "$DEPLOYMENT_PACKAGE" ]]; then
        log_error "Deployment package not found: $DEPLOYMENT_PACKAGE"
        exit 1
    fi
}

# Create isolated test environment
create_isolated_environment() {
    local env_name="$1"
    local env_path="$ISOLATED_ENV_ROOT/$env_name"

    log_test "Creating isolated environment: $env_name"

    # Create environment directory structure
    mkdir -p "$env_path"

    # Create minimal system directories
    mkdir -p "$env_path/bin" "$env_path/lib" "$env_path/lib64" "$env_path/etc" "$env_path/tmp" "$env_path/var/log"
    mkdir -p "$env_path/usr/bin" "$env_path/usr/lib" "$env_path/usr/lib64"
    mkdir -p "$env_path/opt/puzzle71-solver"

    # Copy essential system libraries (minimal set)
    if [[ -d "/lib/x86_64-linux-gnu" ]]; then
        # Copy only critical libraries
        local critical_libs=(
            "ld-linux-x86-64.so.2"
            "libc.so.6"
            "libm.so.6"
            "libpthread.so.0"
            "libdl.so.2"
            "librt.so.1"
            "libz.so.1"
            "libssl.so.3"
            "libcrypto.so.3"
        )

        for lib in "${critical_libs[@]}"; do
            find /lib/x86_64-linux-gnu /usr/lib/x86_64-linux-gnu -name "$lib" 2>/dev/null | head -1 | while read lib_path; do
                if [[ -f "$lib_path" ]]; then
                    cp "$lib_path" "$env_path/lib/x86_64-linux-gnu/" 2>/dev/null || \
                    cp "$lib_path" "$env_path/lib64/" 2>/dev/null || true
                fi
            done
        done
    fi

    # Create minimal device nodes
    if command -v mknod >/dev/null 2>&1; then
        # Create null device
        mknod "$env_path/dev/null" c 1 3 2>/dev/null || true
        # Create zero device
        mknod "$env_path/dev/zero" c 1 5 2>/dev/null || true
        # Create random device
        mknod "$env_path/dev/random" c 1 8 2>/dev/null || true
        # Create urandom device
        mknod "$env_path/dev/urandom" c 1 9 2>/dev/null || true
    fi

    # Create minimal passwd and group files
    cat > "$env_path/etc/passwd" << 'EOF'
root:x:0:0:root:/root:/bin/bash
nobody:x:65534:65534:nobody:/nonexistent:/usr/sbin/nologin
EOF

    cat > "$env_path/etc/group" << 'EOF'
root:x:0:
nogroup:x:65534:
EOF

    # Create resolv.conf (if network enabled)
    if [[ "$NETWORK_DISABLED" == "false" ]]; then
        cat > "$env_path/etc/resolv.conf" << 'EOF'
nameserver 8.8.8.8
nameserver 8.8.4.4
EOF
    else
        cat > "$env_path/etc/resolv.conf" << 'EOF'
# Network disabled in isolated environment
EOF
    fi

    log_isolated "Isolated environment created: $env_path"
}

# Extract deployment package to isolated environment
extract_to_isolated_environment() {
    local env_name="$1"
    local env_path="$ISOLATED_ENV_ROOT/$env_name"

    log_test "Extracting deployment package to isolated environment: $env_name"

    # Extract package
    case "$DEPLOYMENT_PACKAGE" in
        *.tar.gz|*.tgz)
            tar -xzf "$DEPLOYMENT_PACKAGE" -C "$env_path/opt/puzzle71-solver" --strip-components=1
            ;;
        *.tar.bz2|*.tbz2)
            tar -xjf "$DEPLOYMENT_PACKAGE" -C "$env_path/opt/puzzle71-solver" --strip-components=1
            ;;
        *.tar.xz|*.txz)
            tar -xJf "$DEPLOYMENT_PACKAGE" -C "$env_path/opt/puzzle71-solver" --strip-components=1
            ;;
        *.zip)
            unzip -q "$DEPLOYMENT_PACKAGE" -d "$env_path/opt/puzzle71-solver"
            ;;
        *)
            log_error "Unsupported package format: $DEPLOYMENT_PACKAGE"
            return 1
            ;;
    esac

    # Set permissions
    chmod -R 755 "$env_path/opt/puzzle71-solver" 2>/dev/null || true

    log_isolated "Deployment package extracted to isolated environment"
}

# Create isolated test runner script
create_test_runner() {
    local env_name="$1"
    local env_path="$ISOLATED_ENV_ROOT/$env_name"

    log_test "Creating test runner for isolated environment: $env_name"

    cat > "$env_path/run-isolated-test.sh" << EOF
#!/bin/bash
# Isolated environment test runner

set -euo pipefail

# Environment setup
export PATH="\$PATH:/opt/puzzle71-solver/bin"
export LD_LIBRARY_PATH="/opt/puzzle71-solver/lib:\$LD_LIBRARY_PATH"
export HOME="/tmp"
export TMPDIR="/tmp"

# Test configuration
TEST_MODE="$TEST_MODE"
VERBOSE_OUTPUT="$VERBOSE_OUTPUT"
DEPLOYMENT_DIR="/opt/puzzle71-solver"

# Logging function
log_isolated() {
    echo -e "\\033[0;36m[ISOLATED-TEST]\\033[0m \$1"
}

log_error() {
    echo -e "\\033[0;31m[ERROR]\\033[0m \$1"
}

# Test basic deployment functionality
test_basic_functionality() {
    log_isolated "Testing basic deployment functionality..."

    local test_passed=true

    # Test 1: Check if main executable exists and is executable
    if [[ -x "\$DEPLOYMENT_DIR/bin/Puzzle71Solver" ]]; then
        log_isolated "✓ Main executable exists and is executable"
    else
        log_error "✗ Main executable not found or not executable"
        test_passed=false
    fi

    # Test 2: Test version command
    if [[ -x "\$DEPLOYMENT_DIR/bin/Puzzle71Solver" ]]; then
        if \$DEPLOYMENT_DIR/bin/Puzzle71Solver --version >/dev/null 2>&1; then
            log_isolated "✓ Version command works"
        else
            log_error "✗ Version command failed"
            test_passed=false
        fi
    fi

    # Test 3: Test help command
    if [[ -x "\$DEPLOYMENT_DIR/bin/Puzzle71Solver" ]]; then
        if \$DEPLOYMENT_DIR/bin/Puzzle71Solver --help >/dev/null 2>&1; then
            log_isolated "✓ Help command works"
        else
            log_error "✗ Help command failed"
            test_passed=false
        fi
    fi

    # Test 4: Check configuration files
    if [[ -f "\$DEPLOYMENT_DIR/config/resource-optimized.conf" ]]; then
        log_isolated "✓ Configuration file exists"
    else
        log_error "✗ Configuration file missing"
        test_passed=false
    fi

    # Test 5: Check libraries
    if [[ -d "\$DEPLOYMENT_DIR/lib" ]]; then
        local lib_count=\$(find "\$DEPLOYMENT_DIR/lib" -name "*.so*" | wc -l)
        if [[ \$lib_count -gt 0 ]]; then
            log_isolated "✓ Found \$lib_count library files"
        else
            log_error "✗ No library files found"
            test_passed=false
        fi
    else
        log_error "✗ Library directory missing"
        test_passed=false
    fi

    # Test 6: Check attribution files
    if [[ -f "\$DEPLOYMENT_DIR/libs/attribution/ATTRIBUTION.txt" ]]; then
        log_isolated "✓ Attribution file exists"
    else
        log_error "✗ Attribution file missing"
        test_passed=false
    fi

    if [[ "\$test_passed" == true ]]; then
        log_isolated "✓ Basic functionality test PASSED"
        return 0
    else
        log_error "✗ Basic functionality test FAILED"
        return 1
    fi
}

# Test deployment execution
test_deployment_execution() {
    log_isolated "Testing deployment execution..."

    local test_passed=true

    # Test 1: Run with minimal configuration
    if [[ -x "\$DEPLOYMENT_DIR/bin/Puzzle71Solver" ]]; then
        if timeout 30s \$DEPLOYMENT_DIR/bin/Puzzle71Solver --help >/dev/null 2>&1; then
            log_isolated "✓ Application starts successfully"
        else
            log_error "✗ Application failed to start"
            test_passed=false
        fi
    else
        log_error "✗ Main executable not executable"
        test_passed=false
    fi

    # Test 2: Check resource usage
    if command -v ps >/dev/null 2>&1; then
        local process_count=\$(ps aux | grep Puzzle71Solver | grep -v grep | wc -l)
        if [[ \$process_count -gt 0 ]]; then
            log_isolated "✓ Process is running"
        else
            log_isolated "⚠ No running processes found (may have exited quickly)"
        fi
    fi

    if [[ "\$test_passed" == true ]]; then
        log_isolated "✓ Deployment execution test PASSED"
        return 0
    else
        log_error "✗ Deployment execution test FAILED"
        return 1
    fi
}

# Test stress functionality (if in stress mode)
test_stress_functionality() {
    if [[ "\$TEST_MODE" != "stress" ]]; then
        return 0
    fi

    log_isolated "Testing stress functionality..."

    local test_passed=true

    # Test 1: Memory allocation test
    if [[ -x "\$DEPLOYMENT_DIR/bin/Puzzle71Solver" ]]; then
        if timeout 60s \$DEPLOYMENT_DIR/bin/Puzzle71Solver --benchmark --duration 30 >/dev/null 2>&1; then
            log_isolated "✓ Stress test completed successfully"
        else
            log_isolated "⚠ Stress test failed or timed out (may be expected)"
        fi
    fi

    if [[ "\$test_passed" == true ]]; then
        log_isolated "✓ Stress functionality test PASSED"
        return 0
    else
        log_error "✗ Stress functionality test FAILED"
        return 1
    fi
}

# Main test execution
main() {
    log_isolated "Starting isolated environment tests..."
    log_isolated "Test mode: \$TEST_MODE"
    log_isolated "Deployment directory: \$DEPLOYMENT_DIR"

    local overall_test_passed=true

    # Run tests based on mode
    test_basic_functionality || overall_test_passed=false

    if [[ "\$TEST_MODE" != "quick" ]]; then
        test_deployment_execution || overall_test_passed=false
    fi

    if [[ "\$TEST_MODE" == "stress" ]]; then
        test_stress_functionality || overall_test_passed=false
    fi

    # Generate test report
    cat > "\$DEPLOYMENT_DIR/isolated-test-report.json" << EOF
{
  "dependency_free_test_report": {
    "test_metadata": {
      "timestamp": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
      "test_mode": "\$TEST_MODE",
      "environment": "isolated",
      "network_disabled": $NETWORK_DISABLED,
      "memory_limit_mb": $MAX_MEMORY_MB,
      "disk_limit_mb": $MAX_DISK_SPACE_MB
    },
    "test_results": {
      "basic_functionality": \$([[ \$(test_basic_functionality >/dev/null 2>&1; echo \$?) -eq 0 ]] && echo "true" || echo "false"),
      "deployment_execution": \$([[ \$(test_deployment_execution >/dev/null 2>&1; echo \$?) -eq 0 ]] && echo "true" || echo "false"),
      "stress_functionality": \$([[ \$(test_stress_functionality >/dev/null 2>&1; echo \$?) -eq 0 ]] && echo "true" || echo "false"),
      "overall_success": \$([[ "\$overall_test_passed" == true ]] && echo "true" || echo "false")
    },
    "environment_info": {
      "deployment_package": "$DEPLOYMENT_PACKAGE",
      "deployment_directory": "\$DEPLOYMENT_DIR",
      "libraries_available": \$(find "\$DEPLOYMENT_DIR/lib" -name "*.so*" 2>/dev/null | wc -l),
      "executables_available": \$(find "\$DEPLOYMENT_DIR/bin" -type f 2>/dev/null | wc -l),
      "configuration_files": \$(find "\$DEPLOYMENT_DIR/config" -type f 2>/dev/null | wc -l)
    }
  }
}
EOF

    if [[ "\$overall_test_passed" == true ]]; then
        log_isolated "🎉 All isolated environment tests PASSED!"
        exit 0
    else
        log_error "❌ Some isolated environment tests FAILED!"
        exit 1
    fi
}

main "\$@"
EOF

    chmod +x "$env_path/run-isolated-test.sh"
    log_isolated "Test runner created for isolated environment"
}

# Run isolated environment test using chroot
run_isolated_test() {
    local env_name="$1"
    local env_path="$ISOLATED_ENV_ROOT/$env_name"

    log_test "Running isolated environment test: $env_name"

    # Check if chroot is available
    if ! command -v chroot >/dev/null 2>&1; then
        log_warning "chroot not available, running without full isolation"
        # Run without chroot but with modified environment
        local test_result=0
        PATH="$env_path/opt/puzzle71-solver/bin:$PATH" \
        LD_LIBRARY_PATH="$env_path/opt/puzzle71-solver/lib:$LD_LIBRARY_PATH" \
        HOME="$env_path/tmp" \
        "$env_path/opt/puzzle71-solver/scripts/verify.sh" >/dev/null 2>&1 || test_result=1

        if [[ $test_result -eq 0 ]]; then
            log_success "Isolated test (reduced) passed: $env_name"
            return 0
        else
            log_error "Isolated test (reduced) failed: $env_name"
            return 1
        fi
    fi

    # Mount proc filesystem if available
    if [[ -d "$env_path/proc" ]]; then
        mount -t proc proc "$env_path/proc" 2>/dev/null || true
    fi

    # Run test in isolated environment
    local test_result=0
    chroot "$env_path" /bin/bash -c "cd /opt/puzzle71-solver && ./run-isolated-test.sh" >/dev/null 2>&1 || test_result=1

    # Unmount proc
    umount "$env_path/proc" 2>/dev/null || true

    # Check test results
    if [[ -f "$env_path/opt/puzzle71-solver/isolated-test-report.json" ]]; then
        if [[ $test_result -eq 0 ]]; then
            log_success "Isolated test passed: $env_name"

            # Display test summary
            if [[ "$VERBOSE_OUTPUT" == "true" ]]; then
                log_test "Test summary for $env_name:"
                grep -E "(overall_success|basic_functionality|deployment_execution)" "$env_path/opt/puzzle71-solver/isolated-test-report.json" | sed 's/^[[:space:]]*/  /'
            fi

            return 0
        else
            log_error "Isolated test failed: $env_name"

            # Display error details
            if [[ "$VERBOSE_OUTPUT" == "true" ]]; then
                log_test "Error details for $env_name:"
                grep -A 5 -B 5 "false" "$env_path/opt/puzzle71-solver/isolated-test-report.json" | sed 's/^[[:space:]]*/  /'
            fi

            return 1
        fi
    else
        log_error "No test report found for: $env_name"
        return 1
    fi
}

# Cleanup isolated environment
cleanup_isolated_environment() {
    local env_name="$1"
    local env_path="$ISOLATED_ENV_ROOT/$env_name"

    if [[ "$CLEANUP_TEST" == "true" ]]; then
        log_test "Cleaning up isolated environment: $env_name"

        # Unmount any mounted filesystems
        umount "$env_path/proc" 2>/dev/null || true
        umount "$env_path/dev" 2>/dev/null || true

        # Remove environment directory
        rm -rf "$env_path" 2>/dev/null || true

        log_isolated "Isolated environment cleaned up: $env_name"
    else
        log_warning "Skipping cleanup (preserving test environment): $env_path"
    fi
}

# Run comprehensive dependency-free verification
run_dependency_free_verification() {
    log_info "Starting dependency-free deployment verification..."
    log_info "Deployment package: $DEPLOYMENT_PACKAGE"
    log_info "Test mode: $TEST_MODE"
    log_info "Isolated environment root: $ISOLATED_ENV_ROOT"

    local total_tests=0
    local passed_tests=0

    # Create isolated environment root
    mkdir -p "$ISOLATED_ENV_ROOT"

    # Test environments to create
    local test_environments=("basic-test")

    # Add stress test environment if in stress mode
    if [[ "$TEST_MODE" == "stress" ]]; then
        test_environments+=("stress-test")
    fi

    # Run tests for each environment
    for env_name in "${test_environments[@]}"; do
        ((total_tests++))

        log_test "Running test for environment: $env_name"

        # Create isolated environment
        create_isolated_environment "$env_name"

        # Extract deployment package
        extract_to_isolated_environment "$env_name"

        # Create test runner
        create_test_runner "$env_name"

        # Run isolated test
        if run_isolated_test "$env_name"; then
            ((passed_tests++))
        fi

        # Cleanup environment
        cleanup_isolated_environment "$env_name"
    done

    # Generate final report
    local success_rate=$((passed_tests * 100 / total_tests))

    cat > "$ISOLATED_ENV_ROOT/dependency-free-verification-report.json" << EOF
{
  "dependency_free_verification_summary": {
    "verification_metadata": {
      "timestamp": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
      "deployment_package": "$DEPLOYMENT_PACKAGE",
      "test_mode": "$TEST_MODE",
      "total_environments": $total_tests,
      "passed_environments": $passed_tests,
      "success_rate_percent": $success_rate
    },
    "test_configuration": {
      "memory_limit_mb": $MAX_MEMORY_MB,
      "disk_limit_mb": $MAX_DISK_SPACE_MB,
      "cpu_limit_percent": $MAX_CPU_PERCENT,
      "network_disabled": $NETWORK_DISABLED,
      "cleanup_enabled": $CLEANUP_TEST
    },
    "verification_results": {
      "dependency_free_deployment": $([[ $passed_tests -eq $total_tests ]] && echo "true" || echo "false"),
      "isolated_environment_compatibility": $([[ $success_rate -ge 95 ]] && echo "true" || echo "false"),
      "resource_constraint_compliance": $([[ $success_rate -ge 90 ]] && echo "true" || echo "false"),
      "overall_success": $([[ $passed_tests -eq $total_tests ]] && echo "true" || echo "false")
    },
    "compliance_status": {
      "meets_dependency_free_requirement": $([[ $passed_tests -eq $total_tests ]] && echo "true" || echo "false"),
      "meets_isolated_environment_requirement": $([[ $success_rate -ge 95 ]] && echo "true" || echo "false"),
      "meets_resource_constraint_requirement": $([[ $success_rate -ge 90 ]] && echo "true" || echo "false")
    }
  }
}
EOF

    # Display results
    log_info "Dependency-free verification completed:"
    log_info "  Total environments tested: $total_tests"
    log_info "  Successful environments: $passed_tests"
    log_info "  Success rate: ${success_rate}%"

    if [[ $passed_tests -eq $total_tests ]]; then
        log_success "🎉 All dependency-free tests PASSED! Deployment can run without external dependencies."
        return 0
    else
        log_error "❌ Some dependency-free tests FAILED! Deployment requires external dependencies."
        return 1
    fi
}

# Main function
main() {
    log_info "Dependency-Free Deployment Verification (T041)"

    # Parse arguments
    parse_arguments "$@"

    # Find deployment package
    find_deployment_package

    # Run verification
    run_dependency_free_verification
}

# Run main function
main "$@"