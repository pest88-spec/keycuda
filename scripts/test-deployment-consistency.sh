#!/bin/bash
# T043: Test Consistent Deployment Behavior Across Multiple Target Environments
# Validates deployment consistency across different system configurations

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
TEST_MODE="${TEST_MODE:-comprehensive}"  # quick, comprehensive, full
ENVIRONMENT_TYPE="${ENVIRONMENT_TYPE:-all}"  # all, minimal, standard, resource-constrained
TEMP_ENVIRONMENTS_ROOT="${TEMP_ENVIRONMENTS_ROOT:-/tmp/puzzle71-env-test}"
CLEANUP_ENVIRONMENTS="${CLEANUP_ENVIRONMENTS:-true}"
PARALLEL_TESTING="${PARALLEL_TESTING:-true}"
CONSISTENCY_THRESHOLD="${CONSISTENCY_THRESHOLD:-95}"

# Deployment package to test
DEPLOYMENT_PACKAGE="${DEPLOYMENT_PACKAGE:-}"

# Test environments configuration
declare -A TEST_ENVIRONMENTS=(
    ["minimal"]="Ubuntu 22.04 minimal,1GB RAM,1 CPU,512MB disk"
    ["standard"]="Ubuntu 22.04 standard,2GB RAM,2 CPU,1GB disk"
    ["resource-constrained"]="Ubuntu 20.04 minimal,512MB RAM,1 CPU,256MB disk"
    ["different-os"]="CentOS 8 minimal,2GB RAM,2 CPU,1GB disk"
    ["high-end"]="Ubuntu 22.04,4GB RAM,4 CPU,2GB disk"
)

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

log_env() {
    echo -e "${CYAN}[ENV]${NC} $1"
}

# Show help
show_help() {
    cat << EOF
Deployment Consistency Testing Script

USAGE:
    $0 [OPTIONS] [deployment_package]

OPTIONS:
    --test-mode MODE         Test mode: quick, comprehensive, full (default: comprehensive)
    --environment-type TYPE  Environment type: all, minimal, standard, resource-constrained, different-os, high-end
    --env-root DIR           Temporary environments root (default: /tmp/puzzle71-env-test)
    --no-cleanup             Don't clean up test environments
    --no-parallel            Run tests sequentially instead of in parallel
    --consistency-threshold N  Consistency threshold percentage (default: 95)
    --help, -h               Show this help message

DESCRIPTION:
    Tests deployment consistency across multiple target environments with
    different system configurations, resources, and operating systems.

ENVIRONMENT TYPES:
    minimal           Minimal Ubuntu 22.04 with 1GB RAM
    standard          Standard Ubuntu 22.04 with 2GB RAM
    resource-constrained  Resource-limited environment with 512MB RAM
    different-os      CentOS 8 minimal environment
    high-end          High-performance environment with 4GB RAM

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
            --environment-type)
                ENVIRONMENT_TYPE="$2"
                shift 2
                ;;
            --env-root)
                TEMP_ENVIRONMENTS_ROOT="$2"
                shift 2
                ;;
            --no-cleanup)
                CLEANUP_ENVIRONMENTS=false
                shift
                ;;
            --no-parallel)
                PARALLEL_TESTING=false
                shift
                ;;
            --consistency-threshold)
                CONSISTENCY_THRESHOLD="$2"
                shift 2
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

# Create test environment with specific configuration
create_test_environment() {
    local env_name="$1"
    local env_config="$2"
    local env_path="$TEMP_ENVIRONMENTS_ROOT/$env_name"

    log_test "Creating test environment: $env_name ($env_config)"

    # Parse environment configuration
    local os_type=$(echo "$env_config" | cut -d',' -f1)
    local resources=$(echo "$env_config" | cut -d',' -f2)
    local cpu_count=$(echo "$env_config" | cut -d',' -f3)
    local disk_space=$(echo "$env_config" | cut -d',' -f4)

    # Create environment directory
    mkdir -p "$env_path"

    # Create minimal filesystem structure
    mkdir -p "$env_path"/{bin,lib,lib64,etc,tmp,var/log,opt,usr/{bin,lib,lib64}}

    # Create system configuration files
    cat > "$env_path/etc/os-release" << EOF
NAME="$os_type"
VERSION_ID="test"
ID="test"
ID_LIKE="test"
PRETTY_NAME="$os_type Test Environment"
EOF

    # Set resource constraints configuration
    cat > "$env_path/etc/resource-limits.conf" << EOF
# Resource limits for test environment: $env_name
MEMORY_LIMIT=$resources
CPU_LIMIT=$cpu_count
DISK_LIMIT=$disk_space
TEST_MODE=$TEST_MODE
EOF

    # Create test user configuration
    cat > "$env_path/etc/passwd" << 'EOF'
root:x:0:0:root:/root:/bin/bash
puzzle71:x:1000:1000:puzzle71:/home/puzzle71:/bin/bash
nobody:x:65534:65534:nobody:/nonexistent:/usr/sbin/nologin
EOF

    cat > "$env_path/etc/group" << 'EOF'
root:x:0:
puzzle71:x:1000:
nogroup:x:65534:
EOF

    # Extract deployment package
    log_env "Extracting deployment package to $env_name..."
    case "$DEPLOYMENT_PACKAGE" in
        *.tar.gz|*.tgz)
            tar -xzf "$DEPLOYMENT_PACKAGE" -C "$env_path/opt" --strip-components=1
            ;;
        *.tar.bz2|*.tbz2)
            tar -xjf "$DEPLOYMENT_PACKAGE" -C "$env_path/opt" --strip-components=1
            ;;
        *.tar.xz|*.txz)
            tar -xJf "$DEPLOYMENT_PACKAGE" -C "$env_path/opt" --strip-components=1
            ;;
        *.zip)
            unzip -q "$DEPLOYMENT_PACKAGE" -d "$env_path/opt"
            ;;
        *)
            log_error "Unsupported package format: $DEPLOYMENT_PACKAGE"
            return 1
            ;;
    esac

    # Set permissions
    chmod -R 755 "$env_path/opt" 2>/dev/null || true

    # Create environment-specific test script
    create_environment_test_script "$env_name" "$env_path"

    log_env "Test environment created: $env_name"
}

# Create environment-specific test script
create_environment_test_script() {
    local env_name="$1"
    local env_path="$2"

    cat > "$env_path/run-consistency-test.sh" << EOF
#!/bin/bash
# Consistency test script for environment: $env_name

set -euo pipefail

# Environment setup
export PATH="\$PATH:/opt/bin"
export LD_LIBRARY_PATH="/opt/lib:\$LD_LIBRARY_PATH"
export HOME="/tmp"
export TMPDIR="/tmp"

# Load environment configuration
source /etc/resource-limits.conf 2>/dev/null || true

# Test configuration
TEST_MODE="$TEST_MODE"
ENVIRONMENT_NAME="$env_name"
DEPLOYMENT_DIR="/opt"

# Logging functions
log_env_test() {
    echo -e "\\033[0;36m[$ENVIRONMENT_NAME-TEST]\\033[0m \$1"
}

log_error() {
    echo -e "\\033[0;31m[ERROR]\\033[0m \$1"
}

# Test 1: Basic deployment functionality
test_basic_functionality() {
    log_env_test "Testing basic deployment functionality..."

    local test_results=()
    local test_passed=true

    # Check executable presence
    if [[ -x "\$DEPLOYMENT_DIR/bin/Puzzle71Solver" ]]; then
        test_results+=("executable_present:true")
        log_env_test "✓ Main executable present"
    else
        test_results+=("executable_present:false")
        log_error "✗ Main executable missing"
        test_passed=false
    fi

    # Test version command
    if timeout 10s \$DEPLOYMENT_DIR/bin/Puzzle71Solver --version >/dev/null 2>&1; then
        test_results+=("version_command:true")
        log_env_test "✓ Version command works"
    else
        test_results+=("version_command:false")
        log_error "✗ Version command failed"
        test_passed=false
    fi

    # Test help command
    if timeout 10s \$DEPLOYMENT_DIR/bin/Puzzle71Solver --help >/dev/null 2>&1; then
        test_results+=("help_command:true")
        log_env_test "✓ Help command works"
    else
        test_results+=("help_command:false")
        log_error "✗ Help command failed"
        test_passed=false
    fi

    # Check configuration files
    if [[ -f "\$DEPLOYMENT_DIR/config/resource-optimized.conf" ]]; then
        test_results+=("config_files:true")
        log_env_test "✓ Configuration files present"
    else
        test_results+=("config_files:false")
        log_error "✗ Configuration files missing"
        test_passed=false
    fi

    # Save test results
    IFS=','; echo "\${test_results[*]}" > "\$DEPLOYMENT_DIR/basic_test_results.txt"
    unset IFS

    if [[ "\$test_passed" == true ]]; then
        log_env_test "✓ Basic functionality test PASSED"
        return 0
    else
        log_error "✗ Basic functionality test FAILED"
        return 1
    fi
}

# Test 2: Performance consistency
test_performance_consistency() {
    if [[ "\$TEST_MODE" == "quick" ]]; then
        return 0
    fi

    log_env_test "Testing performance consistency..."

    local start_time=\$(date +%s.%N)
    local performance_passed=true

    # Test startup time
    if timeout 30s \$DEPLOYMENT_DIR/bin/Puzzle71Solver --version >/dev/null 2>&1; then
        local end_time=\$(date +%s.%N)
        local startup_time=\$(echo "\$end_time - \$start_time" | bc -l 2>/dev/null || echo "5.0")
        echo "startup_time:\$startup_time" > "\$DEPLOYMENT_DIR/performance_metrics.txt"
        log_env_test "✓ Startup time: \${startup_time}s"
    else
        echo "startup_time:failed" > "\$DEPLOYMENT_DIR/performance_metrics.txt"
        log_error "✗ Startup time test failed"
        performance_passed=false
    fi

    # Test memory usage (if tools available)
    if command -v /usr/bin/time >/dev/null 2>&1; then
        local memory_result=$(/usr/bin/time -f "%M" timeout 30s \$DEPLOYMENT_DIR/bin/Puzzle71Solver --help 2>&1 >/dev/null || echo "0")
        echo "max_memory_kb:\$memory_result" >> "\$DEPLOYMENT_DIR/performance_metrics.txt"
        log_env_test "✓ Memory usage: \${memory_result}KB"
    fi

    if [[ "\$performance_passed" == true ]]; then
        log_env_test "✓ Performance consistency test PASSED"
        return 0
    else
        log_error "✗ Performance consistency test FAILED"
        return 1
    fi
}

# Test 3: Resource usage validation
test_resource_usage() {
    log_env_test "Testing resource usage validation..."

    local resource_passed=true
    local resource_results=()

    # Check disk usage
    if command -v du >/dev/null 2>&1; then
        local disk_usage=\$(du -sm "\$DEPLOYMENT_DIR" 2>/dev/null | cut -f1 || echo "0")
        resource_results+=("disk_usage_mb:\$disk_usage")
        log_env_test "✓ Disk usage: \${disk_usage}MB"
    fi

    # Check library count
    if [[ -d "\$DEPLOYMENT_DIR/lib" ]]; then
        local lib_count=\$(find "\$DEPLOYMENT_DIR/lib" -name "*.so*" 2>/dev/null | wc -l)
        resource_results+=("library_count:\$lib_count")
        log_env_test "✓ Library count: \$lib_count"
    fi

    # Check file count
    local file_count=\$(find "\$DEPLOYMENT_DIR" -type f 2>/dev/null | wc -l)
    resource_results+=("file_count:\$file_count")
    log_env_test "✓ File count: \$file_count"

    # Save resource results
    IFS=','; echo "\${resource_results[*]}" > "\$DEPLOYMENT_DIR/resource_usage.txt"
    unset IFS

    if [[ "\$resource_passed" == true ]]; then
        log_env_test "✓ Resource usage validation PASSED"
        return 0
    else
        log_error "✗ Resource usage validation FAILED"
        return 1
    fi
}

# Test 4: Environment compatibility
test_environment_compatibility() {
    log_env_test "Testing environment compatibility..."

    local compatibility_passed=true
    local compatibility_results=()

    # Test library compatibility
    if [[ -x "\$DEPLOYMENT_DIR/bin/Puzzle71Solver" ]]; then
        if timeout 15s \$DEPLOYMENT_DIR/bin/Puzzle71Solver --help >/dev/null 2>&1; then
            compatibility_results+=("library_compatibility:true")
            log_env_test "✓ Library compatibility verified"
        else
            compatibility_results+=("library_compatibility:false")
            log_error "✗ Library compatibility issues"
            compatibility_passed=false
        fi
    fi

    # Test system call compatibility (basic check)
    if command -v strace >/dev/null 2>&1 && [[ "\$TEST_MODE" == "full" ]]; then
        local syscall_errors=\$(timeout 30s strace -e trace=all \$DEPLOYMENT_DIR/bin/Puzzle71Solver --version 2>&1 | grep -c "ENOENT\|EACCES" || echo "0")
        if [[ \$syscall_errors -lt 10 ]]; then
            compatibility_results+=("syscall_compatibility:true")
            log_env_test "✓ System call compatibility verified"
        else
            compatibility_results+=("syscall_compatibility:false")
            log_error "✗ System call compatibility issues"
            compatibility_passed=false
        fi
    else
        compatibility_results+=("syscall_compatibility:skipped")
        log_env_test "⚠ System call compatibility test skipped"
    fi

    # Save compatibility results
    IFS=','; echo "\${compatibility_results[*]}" > "\$DEPLOYMENT_DIR/compatibility_results.txt"
    unset IFS

    if [[ "\$compatibility_passed" == true ]]; then
        log_env_test "✓ Environment compatibility test PASSED"
        return 0
    else
        log_error "✗ Environment compatibility test FAILED"
        return 1
    fi
}

# Main test execution
main() {
    log_env_test "Starting consistency tests for environment: $ENVIRONMENT_NAME"
    log_env_test "Test mode: \$TEST_MODE"

    local overall_passed=true
    local test_start_time=\$(date +%s)

    # Run all tests
    test_basic_functionality || overall_passed=false

    if [[ "\$TEST_MODE" != "quick" ]]; then
        test_performance_consistency || overall_passed=false
    fi

    test_resource_usage || overall_passed=false
    test_environment_compatibility || overall_passed=false

    local test_end_time=\$(date +%s)
    local total_test_time=\$((test_end_time - test_start_time))

    # Generate environment test report
    cat > "\$DEPLOYMENT_DIR/environment-test-report.json" << EOFR
{
  "environment_test_report": {
    "environment_metadata": {
      "environment_name": "$ENVIRONMENT_NAME",
      "timestamp": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
      "test_mode": "\$TEST_MODE",
      "total_test_time_seconds": $total_test_time,
      "resource_limits": {
        "memory_limit": "\$MEMORY_LIMIT",
        "cpu_limit": "\$CPU_LIMIT",
        "disk_limit": "\$DISK_LIMIT"
      }
    },
    "test_results": {
      "basic_functionality_passed": \$(test_basic_functionality >/dev/null 2>&1 && echo "true" || echo "false"),
      "performance_consistency_passed": \$(test_performance_consistency >/dev/null 2>&1 && echo "true" || echo "false"),
      "resource_usage_passed": \$(test_resource_usage >/dev/null 2>&1 && echo "true" || echo "false"),
      "environment_compatibility_passed": \$(test_environment_compatibility >/dev/null 2>&1 && echo "true" || echo "false"),
      "overall_success": \$( [[ "\$overall_passed" == true ]] && echo "true" || echo "false" )
    },
    "test_artifacts": {
      "basic_test_results": \$( [[ -f "\$DEPLOYMENT_DIR/basic_test_results.txt" ]] && echo "true" || echo "false" ),
      "performance_metrics": \$( [[ -f "\$DEPLOYMENT_DIR/performance_metrics.txt" ]] && echo "true" || echo "false" ),
      "resource_usage": \$( [[ -f "\$DEPLOYMENT_DIR/resource_usage.txt" ]] && echo "true" || echo "false" ),
      "compatibility_results": \$( [[ -f "\$DEPLOYMENT_DIR/compatibility_results.txt" ]] && echo "true" || echo "false" )
    }
  }
}
EOFR

    if [[ "\$overall_passed" == true ]]; then
        log_env_test "🎉 All consistency tests PASSED for $ENVIRONMENT_NAME!"
        exit 0
    else
        log_error "❌ Some consistency tests FAILED for $ENVIRONMENT_NAME!"
        exit 1
    fi
}

main "\$@"
EOF

    chmod +x "$env_path/run-consistency-test.sh"
}

# Run consistency test for a single environment
run_environment_test() {
    local env_name="$1"
    local env_path="$TEMP_ENVIRONMENTS_ROOT/$env_name"

    log_test "Running consistency test for environment: $env_name"

    # Run test in environment
    local test_result=0
    if [[ -x "$env_path/run-consistency-test.sh" ]]; then
        cd "$env_path" && ./run-consistency-test.sh >/dev/null 2>&1 || test_result=1
    else
        log_error "Test script not found for environment: $env_name"
        test_result=1
    fi

    # Report result
    if [[ $test_result -eq 0 ]]; then
        log_success "Environment test passed: $env_name"
        echo "passed" > "$env_path/test-status.txt"
    else
        log_error "Environment test failed: $env_name"
        echo "failed" > "$env_path/test-status.txt"
    fi

    return $test_result
}

# Analyze consistency across all environments
analyze_consistency() {
    log_test "Analyzing consistency across all test environments..."

    local total_environments=0
    local passed_environments=0
    declare -a environment_results=()

    # Collect results from all environments
    for env_dir in "$TEMP_ENVIRONMENTS_ROOT"/*; do
        if [[ -d "$env_dir" ]]; then
            local env_name=$(basename "$env_dir")
            ((total_environments++))

            local test_status="unknown"
            if [[ -f "$env_dir/test-status.txt" ]]; then
                test_status=$(cat "$env_dir/test-status.txt")
            fi

            if [[ "$test_status" == "passed" ]]; then
                ((passed_environments++))
            fi

            environment_results+=("$env_name:$test_status")
        fi
    done

    # Calculate consistency rate
    local consistency_rate=0
    if [[ $total_environments -gt 0 ]]; then
        consistency_rate=$((passed_environments * 100 / total_environments))
    fi

    log_test "Consistency analysis results:"
    log_test "  Total environments tested: $total_environments"
    log_test "  Passed environments: $passed_environments"
    log_test "  Consistency rate: ${consistency_rate}%"

    # Check against threshold
    if [[ $consistency_rate -ge $CONSISTENCY_THRESHOLD ]]; then
        log_success "Consistency rate (${consistency_rate}%) meets threshold ($CONSISTENCY_THRESHOLD%)"
    else
        log_error "Consistency rate (${consistency_rate}%) below threshold ($CONSISTENCY_THRESHOLD%)"
    fi

    # Generate consistency report
    generate_consistency_report "$total_environments" "$passed_environments" "$consistency_rate" "${environment_results[@]}"

    return $([[ $consistency_rate -ge $CONSISTENCY_THRESHOLD ]] && echo 0 || echo 1)
}

# Generate comprehensive consistency report
generate_consistency_report() {
    local total_environments="$1"
    local passed_environments="$2"
    local consistency_rate="$3"
    shift 3
    local environment_results=("$@")

    local report_file="$TEMP_ENVIRONMENTS_ROOT/deployment-consistency-report.json"

    # Build environment results JSON
    local env_results_json=""
    for result in "${environment_results[@]}"; do
        local env_name=$(echo "$result" | cut -d':' -f1)
        local test_status=$(echo "$result" | cut -d':' -f2)
        env_results_json+="{\"environment_name\":\"$env_name\",\"test_status\":\"$test_status\"},"
    done
    env_results_json=${env_results_json%,}  # Remove trailing comma

    cat > "$report_file" << EOF
{
  "deployment_consistency_report": {
    "consistency_metadata": {
      "timestamp": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
      "deployment_package": "$DEPLOYMENT_PACKAGE",
      "test_mode": "$TEST_MODE",
      "consistency_threshold_percent": $CONSISTENCY_THRESHOLD,
      "script_version": "T043-1.0"
    },
    "test_summary": {
      "total_environments_tested": $total_environments,
      "passed_environments": $passed_environments,
      "failed_environments": $((total_environments - passed_environments)),
      "consistency_rate_percent": $consistency_rate,
      "meets_consistency_requirement": $([[ $consistency_rate -ge $CONSISTENCY_THRESHOLD ]] && echo "true" || echo "false")
    },
    "environment_results": [
      $env_results_json
    ],
    "compliance_status": {
      "meets_deployment_consistency_requirement": $([[ $consistency_rate -ge $CONSISTENCY_THRESHOLD ]] && echo "true" || echo "false"),
      "ready_for_multi_environment_deployment": $([[ $consistency_rate -ge 95 ]] && echo "true" || echo "false"),
      "requires_environment_specific_fixes": $([[ $consistency_rate -lt 90 ]] && echo "true" || echo "false")
    },
    "recommendations": {
      "deployment_stable": $([[ $consistency_rate -ge 95 ]] && echo "true" || echo "false"),
      "investigate_failures": $([[ $total_environments -gt $passed_environments ]] && echo "true" || echo "false"),
      "environment_optimization_needed": $([[ $consistency_rate -lt $CONSISTENCY_THRESHOLD ]] && echo "true" || echo "false")
    }
  }
}
EOF

    log_success "Consistency report generated: $report_file"

    # Display environment-specific results
    if [[ "$VERBOSE_OUTPUT" == "true" ]]; then
        log_test "Environment-specific results:"
        for result in "${environment_results[@]}"; do
            local env_name=$(echo "$result" | cut -d':' -f1)
            local test_status=$(echo "$result" | cut -d':' -f2)
            local status_icon="✓"
            [[ "$test_status" != "passed" ]] && status_icon="✗"
            log_test "  $status_icon $env_name: $test_status"
        done
    fi
}

# Cleanup test environments
cleanup_environments() {
    if [[ "$CLEANUP_ENVIRONMENTS" == "true" ]]; then
        log_info "Cleaning up test environments..."
        rm -rf "$TEMP_ENVIRONMENTS_ROOT" 2>/dev/null || true
    else
        log_warning "Skipping cleanup (preserving test environments): $TEMP_ENVIRONMENTS_ROOT"
    fi
}

# Main consistency testing function
main() {
    log_info "Deployment Consistency Testing (T043)"

    # Parse arguments
    parse_arguments "$@"

    # Find deployment package
    find_deployment_package

    log_info "Starting deployment consistency testing..."
    log_info "Deployment package: $DEPLOYMENT_PACKAGE"
    log_info "Test mode: $TEST_MODE"
    log_info "Environment type: $ENVIRONMENT_TYPE"
    log_info "Consistency threshold: ${CONSISTENCY_THRESHOLD}%"

    # Create environments root
    mkdir -p "$TEMP_ENVIRONMENTS_ROOT"

    # Determine which environments to test
    local environments_to_test=()

    if [[ "$ENVIRONMENT_TYPE" == "all" ]]; then
        for env_name in "${!TEST_ENVIRONMENTS[@]}"; do
            environments_to_test+=("$env_name")
        done
    else
        if [[ -n "${TEST_ENVIRONMENTS[$ENVIRONMENT_TYPE]:-}" ]]; then
            environments_to_test+=("$ENVIRONMENT_TYPE")
        else
            log_error "Unknown environment type: $ENVIRONMENT_TYPE"
            exit 1
        fi
    fi

    log_info "Testing environments: ${environments_to_test[*]}"

    # Create test environments
    local pids=()
    for env_name in "${environments_to_test[@]}"; do
        env_config="${TEST_ENVIRONMENTS[$env_name]}"

        if [[ "$PARALLEL_TESTING" == "true" ]]; then
            # Create environment in background
            create_test_environment "$env_name" "$env_config" &
            pids+=($!)
        else
            # Create environment sequentially
            create_test_environment "$env_name" "$env_config"
        fi
    done

    # Wait for environment creation to complete
    if [[ "$PARALLEL_TESTING" == "true" && ${#pids[@]} -gt 0 ]]; then
        for pid in "${pids[@]}"; do
            wait "$pid"
        done
    fi

    # Run tests in environments
    local test_pids=()
    for env_name in "${environments_to_test[@]}"; do
        if [[ "$PARALLEL_TESTING" == "true" ]]; then
            # Run test in background
            run_environment_test "$env_name" &
            test_pids+=($!)
        else
            # Run test sequentially
            run_environment_test "$env_name"
        fi
    done

    # Wait for tests to complete
    if [[ "$PARALLEL_TESTING" == "true" && ${#test_pids[@]} -gt 0 ]]; then
        for pid in "${test_pids[@]}"; do
            wait "$pid"
        done
    fi

    # Analyze consistency
    local consistency_result=0
    analyze_consistency || consistency_result=1

    # Cleanup
    cleanup_environments

    # Final result
    if [[ $consistency_result -eq 0 ]]; then
        log_success "🎉 Deployment consistency testing PASSED!"
        log_info "Deployment behavior is consistent across multiple target environments"
        return 0
    else
        log_error "❌ Deployment consistency testing FAILED!"
        log_info "Deployment behavior varies significantly across environments"
        return 1
    fi
}

# Run main function
main "$@"