#!/bin/bash

# test-cross-platform-deployment.sh
# Tests consistent deployment behavior across multiple target environments

set -euo pipefail

# Script configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TEST_DIR="$PROJECT_ROOT/test-deployment"
PACKAGE_DIR="$PROJECT_ROOT/deployment-packages"
LOG_FILE="$TEST_DIR/cross-platform-test.log"

# Test configuration
TEST_PLATFORMS=("linux-x86_64" "linux-aarch64" "darwin-x86_64" "darwin-arm64")
SIMULATION_MODE=true
NETWORK_ISOLATION=true
CLEANUP_TEST=true

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m'

# Test results tracking
declare -A PLATFORM_RESULTS
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

# Logging functions
log() {
    echo -e "${BLUE}[$(date '+%Y-%m-%d %H:%M:%S')]${NC} $1" | tee -a "$LOG_FILE"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1" | tee -a "$LOG_FILE"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1" | tee -a "$LOG_FILE"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1" | tee -a "$LOG_FILE"
}

log_info() {
    echo -e "${CYAN}[INFO]${NC} $1" | tee -a "$LOG_FILE"
}

log_section() {
    echo -e "\n${PURPLE}=== $1 ===${NC}" | tee -a "$LOG_FILE"
}

# Show usage information
show_usage() {
    echo "Usage: $(basename "$0") [options]"
    echo ""
    echo "Tests consistent deployment behavior across multiple target environments."
    echo ""
    echo "Options:"
    echo "    -p, --platforms LIST       Comma-separated list of platforms to test"
    echo "    -s, --simulation           Use platform simulation mode (default: true)"
    echo "    -r, --real-hardware        Test on real hardware"
    echo "    -n, --no-network-isolation Disable network isolation during testing"
    echo "    -c, --no-cleanup           Keep test artifacts after completion"
    echo "    -v, --verbose              Enable verbose logging"
    echo "    -h, --help                 Show this help message"
    echo ""
    echo "Platforms:"
    echo "    linux-x86_64   Linux 64-bit Intel/AMD"
    echo "    linux-aarch64  Linux 64-bit ARM"
    echo "    darwin-x86_64  macOS 64-bit Intel"
    echo "    darwin-arm64    macOS 64-bit Apple Silicon"
    echo ""
    echo "Examples:"
    echo "    $(basename $0)                                    # Test all platforms in simulation mode"
    echo "    $(basename $0) -p linux-x86_64,darwin-x86_64      # Test specific platforms"
}

# Parse command line arguments
parse_arguments() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            -p|--platforms)
                IFS=',' read -ra TEST_PLATFORMS <<< "$2"
                shift 2
                ;;
            -s|--simulation)
                SIMULATION_MODE=true
                shift
                ;;
            -r|--real-hardware)
                SIMULATION_MODE=false
                shift
                ;;
            -n|--no-network-isolation)
                NETWORK_ISOLATION=false
                shift
                ;;
            -c|--no-cleanup)
                CLEANUP_TEST=false
                shift
                ;;
            -v|--verbose)
                set -x
                shift
                ;;
            -h|--help)
                show_usage
                exit 0
                ;;
            *)
                log_error "Unknown option: $1"
                show_usage
                exit 1
                ;;
        esac
    done
}

# Initialize test environment
initialize_test_environment() {
    log_section "Initializing Cross-Platform Deployment Test Environment"

    # Create test directory structure
    mkdir -p "$TEST_DIR"
    mkdir -p "$TEST_DIR/platforms"
    mkdir -p "$TEST_DIR/reports"

    # Initialize log file
    {
        echo "Cross-Platform Deployment Test Log"
        echo "Started: $(date)"
        echo "Test Platforms: ${TEST_PLATFORMS[*]}"
        echo "Simulation Mode: $SIMULATION_MODE"
        echo "Network Isolation: $NETWORK_ISOLATION"
        echo "========================================"
    } > "$LOG_FILE"

    log_success "Test environment initialized"
}

# Find deployment packages to test
find_deployment_packages() {
    log_section "Finding Deployment Packages"

    local packages=()

    # Find all .tar.gz packages
    for package in "$PACKAGE_DIR"/*.tar.gz; do
        if [[ -f "$package" ]]; then
            packages+=("$package")
            log_info "Found package: $(basename "$package")"
        fi
    done

    if [[ ${#packages[@]} -eq 0 ]]; then
        log_error "No deployment packages found in $PACKAGE_DIR"
        log_info "Please run package-deployment.sh first to create packages"
        exit 1
    fi

    log_success "Found ${#packages[@]} deployment packages"
    echo "${packages[@]}"
}

# Simulate target platform environment
simulate_platform_environment() {
    local platform="$1"
    local test_env_dir="$TEST_DIR/platforms/$platform"

    log_info "Setting up simulation environment for $platform"

    mkdir -p "$test_env_dir"

    # Create platform-specific simulation
    cat > "$test_env_dir/platform-config.json" << EOF
{
    "platform": "$platform",
    "simulation": true,
    "test_date": "$(date -Iseconds)",
    "environment": {
        "PLATFORM": "$platform",
        "ARCH": "${platform#*-}"
    }
}
EOF

    log_success "Simulation environment setup for $platform"
}

# Test package extraction and structure
test_package_extraction() {
    local package="$1"
    local platform="$2"
    local package_name=$(basename "$package" .tar.gz)
    local test_env_dir="$TEST_DIR/platforms/$platform"
    local extract_dir="$test_env_dir/$package_name"

    log_info "Testing package extraction for $package_name on $platform"

    local test_passed=true
    local errors=()

    # Extract package
    mkdir -p "$extract_dir"
    if ! tar -xzf "$package" -C "$extract_dir" 2>/dev/null; then
        errors+=("Failed to extract package")
        test_passed=false
    fi

    # Verify package structure
    local required_dirs=("bin" "lib" "include" "share")
    for dir in "${required_dirs[@]}"; do
        if [[ ! -d "$extract_dir/$dir" ]]; then
            errors+=("Missing required directory: $dir")
            test_passed=false
        fi
    done

    # Verify executable exists
    if [[ ! -f "$extract_dir/bin/Puzzle71Solver" ]]; then
        errors+=("Missing main executable: bin/Puzzle71Solver")
        test_passed=false
    fi

    # Record results
    if [[ "$test_passed" == true ]]; then
        PLATFORM_RESULTS["${platform}_extraction"]="PASS"
        log_success "Package extraction test passed for $platform"
    else
        PLATFORM_RESULTS["${platform}_extraction"]="FAIL"
        for error in "${errors[@]}"; do
            log_error "Extraction error on $platform: $error"
        done
    fi

    return $([[ "$test_passed" == true ]] && echo 0 || echo 1)
}

# Test dependency resolution
test_dependency_resolution() {
    local package="$1"
    local platform="$2"
    local package_name=$(basename "$package" .tar.gz)
    local test_env_dir="$TEST_DIR/platforms/$platform"
    local extract_dir="$test_env_dir/$package_name"

    log_info "Testing dependency resolution for $package_name on $platform"

    local test_passed=true
    local errors=()

    # Test that all libraries are included locally
    if [[ -d "$extract_dir/lib" ]]; then
        local lib_count=$(find "$extract_dir/lib" -name "*.so*" -o -name "*.a" | wc -l)
        log_info "Found $lib_count local libraries in package"

        if [[ $lib_count -eq 0 ]]; then
            errors+=("No libraries found in package")
            test_passed=false
        fi
    else
        errors+=("Library directory missing")
        test_passed=false
    fi

    # Test that headers are included
    if [[ -d "$extract_dir/include" ]]; then
        local header_count=$(find "$extract_dir/include" -name "*.h" -o -name "*.hpp" | wc -l)
        log_info "Found $header_count header files in package"
    else
        errors+=("Include directory missing")
        test_passed=false
    fi

    # Record results
    if [[ "$test_passed" == true ]]; then
        PLATFORM_RESULTS["${platform}_dependencies"]="PASS"
        log_success "Dependency resolution test passed for $platform"
    else
        PLATFORM_RESULTS["${platform}_dependencies"]="FAIL"
        for error in "${errors[@]}"; do
            log_error "Dependency error on $platform: $error"
        done
    fi

    return $([[ "$test_passed" == true ]] && echo 0 || echo 1)
}

# Test deployment scripts
test_deployment_scripts() {
    local package="$1"
    local platform="$2"
    local package_name=$(basename "$package" .tar.gz)
    local test_env_dir="$TEST_DIR/platforms/$platform"
    local extract_dir="$test_env_dir/$package_name"

    log_info "Testing deployment scripts for $package_name on $platform"

    local test_passed=true
    local errors=()

    # Test environment setup script
    if [[ -f "$extract_dir/setup-env.sh" ]]; then
        if bash -n "$extract_dir/setup-env.sh" 2>/dev/null; then
            log_info "Environment setup script syntax is valid on $platform"
        else
            errors+=("Environment setup script has syntax errors")
            test_passed=false
        fi
    else
        errors+=("Missing environment setup script")
        test_passed=false
    fi

    # Test installation script
    if [[ -f "$extract_dir/install.sh" ]]; then
        if bash -n "$extract_dir/install.sh" 2>/dev/null; then
            log_info "Installation script syntax is valid on $platform"
        else
            errors+=("Installation script has syntax errors")
            test_passed=false
        fi
    else
        errors+=("Missing installation script")
        test_passed=false
    fi

    # Record results
    if [[ "$test_passed" == true ]]; then
        PLATFORM_RESULTS["${platform}_scripts"]="PASS"
        log_success "Deployment scripts test passed for $platform"
    else
        PLATFORM_RESULTS["${platform}_scripts"]="FAIL"
        for error in "${errors[@]}"; do
            log_error "Script error on $platform: $error"
        done
    fi

    return $([[ "$test_passed" == true ]] && echo 0 || echo 1)
}

# Test network isolation
test_network_isolation() {
    local package="$1"
    local platform="$2"
    local package_name=$(basename "$package" .tar.gz)
    local test_env_dir="$TEST_DIR/platforms/$platform"
    local extract_dir="$test_env_dir/$package_name"

    log_info "Testing network isolation for $package_name on $platform"

    local test_passed=true
    local errors=()

    if [[ "$NETWORK_ISOLATION" == true ]]; then
        # Check for network dependencies in scripts
        local network_commands=("curl" "wget" "git" "svn" "ping")

        for cmd in "${network_commands[@]}"; do
            if grep -r "$cmd.*http" "$extract_dir" 2>/dev/null; then
                errors+=("Network dependency detected: $cmd")
                test_passed=false
            fi
        done

        # Check for repository URLs
        if grep -r "http.*\.git" "$extract_dir" 2>/dev/null; then
            errors+=("Git repository URLs found in deployment")
            test_passed=false
        fi
    fi

    # Record results
    if [[ "$test_passed" == true ]]; then
        PLATFORM_RESULTS["${platform}_network_isolation"]="PASS"
        log_success "Network isolation test passed for $platform"
    else
        PLATFORM_RESULTS["${platform}_network_isolation"]="FAIL"
        for error in "${errors[@]}"; do
            log_error "Network isolation error on $platform: $error"
        done
    fi

    return $([[ "$test_passed" == true ]] && echo 0 || echo 1)
}

# Test platform compatibility
test_platform_compatibility() {
    local package="$1"
    local platform="$2"
    local package_name=$(basename "$package" .tar.gz)
    local test_env_dir="$TEST_DIR/platforms/$platform"
    local extract_dir="$test_env_dir/$package_name"

    log_info "Testing platform compatibility for $package_name on $platform"

    local test_passed=true
    local errors=()

    # Check platform compatibility in manifest
    if [[ -f "$extract_dir/MANIFEST.json" ]]; then
        local manifest_platform=$(jq -r '.platform // "all"' "$extract_dir/MANIFEST.json" 2>/dev/null || echo "unknown")
        if [[ "$manifest_platform" != "all" && "$manifest_platform" != "$platform" ]]; then
            errors+=("Platform mismatch: expected $platform, found $manifest_platform")
            test_passed=false
        fi
    fi

    # Test binary compatibility (basic checks)
    if [[ -f "$extract_dir/bin/Puzzle71Solver" ]]; then
        if [[ ! -x "$extract_dir/bin/Puzzle71Solver" ]]; then
            errors+=("Main binary is not executable")
            test_passed=false
        fi
    fi

    # Record results
    if [[ "$test_passed" == true ]]; then
        PLATFORM_RESULTS["${platform}_compatibility"]="PASS"
        log_success "Platform compatibility test passed for $platform"
    else
        PLATFORM_RESULTS["${platform}_compatibility"]="FAIL"
        for error in "${errors[@]}"; do
            log_error "Compatibility error on $platform: $error"
        done
    fi

    return $([[ "$test_passed" == true ]] && echo 0 || echo 1)
}

# Run comprehensive tests for a platform
test_platform() {
    local package="$1"
    local platform="$2"

    log_section "Testing Platform: $platform"

    # Initialize platform environment
    simulate_platform_environment "$platform"

    # Run all tests for this platform
    local platform_tests=0
    local platform_passed=0

    ((TOTAL_TESTS++))
    if test_package_extraction "$package" "$platform"; then
        ((platform_passed++))
    fi
    ((platform_tests++))

    ((TOTAL_TESTS++))
    if test_dependency_resolution "$package" "$platform"; then
        ((platform_passed++))
    fi
    ((platform_tests++))

    ((TOTAL_TESTS++))
    if test_deployment_scripts "$package" "$platform"; then
        ((platform_passed++))
    fi
    ((platform_tests++))

    ((TOTAL_TESTS++))
    if test_network_isolation "$package" "$platform"; then
        ((platform_passed++))
    fi
    ((platform_tests++))

    ((TOTAL_TESTS++))
    if test_platform_compatibility "$package" "$platform"; then
        ((platform_passed++))
    fi
    ((platform_tests++))

    # Update global counters
    PASSED_TESTS=$((PASSED_TESTS + platform_passed))
    FAILED_TESTS=$((FAILED_TESTS + (platform_tests - platform_passed)))

    # Log platform summary
    log_info "Platform $platform: $platform_passed/$platform_tests tests passed"
}

# Generate comprehensive report
generate_comprehensive_report() {
    local packages=("$@")

    log_section "Generating Comprehensive Cross-Platform Test Report"

    local report_file="$TEST_DIR/reports/cross-platform-test-report.json"

    cat > "$report_file" << EOF
{
    "test_summary": {
        "test_date": "$(date -Iseconds)",
        "total_packages": ${#packages[@]},
        "total_platforms": ${#TEST_PLATFORMS[@]},
        "total_tests": $TOTAL_TESTS,
        "tests_passed": $PASSED_TESTS,
        "tests_failed": $FAILED_TESTS,
        "success_rate": "$(echo "scale=2; $PASSED_TESTS * 100 / $TOTAL_TESTS" | bc -l)%",
        "overall_result": "$([[ $FAILED_TESTS -eq 0 ]] && echo "PASS" || echo "FAIL")"
    },
    "test_configuration": {
        "simulation_mode": $SIMULATION_MODE,
        "network_isolation": $NETWORK_ISOLATION,
        "cleanup_enabled": $CLEANUP_TEST,
        "test_platforms": [$(printf '"%s",' "${TEST_PLATFORMS[@]}" | sed 's/,$//')]
    },
    "platform_results": {
EOF

    # Add platform results
    local first_platform=true
    for platform in "${TEST_PLATFORMS[@]}"; do
        if [[ "$first_platform" == false ]]; then
            echo "," >> "$report_file"
        fi
        first_platform=false

        cat >> "$report_file" << EOF
        "$platform": {
            "extraction": "${PLATFORM_RESULTS[${platform}_extraction]:-NOT_RUN}",
            "dependencies": "${PLATFORM_RESULTS[${platform}_dependencies]:-NOT_RUN}",
            "scripts": "${PLATFORM_RESULTS[${platform}_scripts]:-NOT_RUN}",
            "network_isolation": "${PLATFORM_RESULTS[${platform}_network_isolation]:-NOT_RUN}",
            "compatibility": "${PLATFORM_RESULTS[${platform}_compatibility]:-NOT_RUN}"
        }
EOF
    done

    cat >> "$report_file" << EOF
    },
    "recommendations": [
EOF

    # Add recommendations
    local recommendations=()

    if [[ $FAILED_TESTS -gt 0 ]]; then
        recommendations+=("Some tests failed - review error logs and fix compatibility issues")
    fi

    if [[ "$SIMULATION_MODE" == true ]]; then
        recommendations+=("Consider running tests on real hardware for production validation")
    fi

    recommendations+=("Regular cross-platform testing recommended for consistent deployment behavior")

    local first_rec=true
    for rec in "${recommendations[@]}"; do
        if [[ "$first_rec" == false ]]; then
            echo "," >> "$report_file"
        fi
        first_rec=false
        echo "        \"$rec\"" >> "$report_file"
    done

    cat >> "$report_file" << EOF
    ]
}
EOF

    log_success "Comprehensive test report generated: $report_file"
}

# Display test summary
display_test_summary() {
    log_section "Cross-Platform Test Summary"

    echo
    echo "Test Configuration:"
    echo "  Platforms Tested: ${#TEST_PLATFORMS[@]} (${TEST_PLATFORMS[*]})"
    echo "  Simulation Mode: $SIMULATION_MODE"
    echo "  Network Isolation: $NETWORK_ISOLATION"
    echo

    echo "Test Results:"
    echo "  Total Tests: $TOTAL_TESTS"
    echo "  Passed: $PASSED_TESTS"
    echo "  Failed: $FAILED_TESTS"
    echo "  Success Rate: $(echo "scale=1; $PASSED_TESTS * 100 / $TOTAL_TESTS" | bc -l)%"
    echo

    echo "Platform Breakdown:"
    for platform in "${TEST_PLATFORMS[@]}"; do
        local platform_passed=0
        local platform_total=0

        for test_type in extraction dependencies scripts network_isolation compatibility; do
            ((platform_total++))
            if [[ "${PLATFORM_RESULTS[${platform}_${test_type}]:-NOT_RUN}" == "PASS" ]]; then
                ((platform_passed++))
            fi
        done

        local status="$([[ $platform_passed -eq $platform_total ]] && echo "✅ PASS" || echo "❌ FAIL")"
        echo "  $platform: $platform_passed/$platform_total tests passed $status"
    done

    echo
    if [[ $FAILED_TESTS -eq 0 ]]; then
        log_success "All cross-platform tests passed! 🎉"
        echo "Deployment packages demonstrate consistent behavior across all tested platforms."
    else
        log_error "Some cross-platform tests failed."
        echo "Please review the detailed reports for specific issues."
    fi

    echo
    echo "Detailed Reports:"
    echo "  Comprehensive Report: $TEST_DIR/reports/cross-platform-test-report.json"
    echo "  Test Log: $LOG_FILE"
    echo
}

# Main execution function
main() {
    # Parse command line arguments
    parse_arguments "$@"

    # Initialize test environment
    initialize_test_environment

    # Find deployment packages to test
    local packages=()
    while IFS= read -r package; do
        packages+=("$package")
    done < <(find_deployment_packages)

    # Test each platform with each package
    for package in "${packages[@]}"; do
        log_section "Testing Package: $(basename "$package")"

        for platform in "${TEST_PLATFORMS[@]}"; do
            test_platform "$package" "$platform"
        done
    done

    # Generate comprehensive report
    generate_comprehensive_report "${packages[@]}"

    # Display test summary
    display_test_summary

    # Exit with appropriate code
    if [[ $FAILED_TESTS -eq 0 ]]; then
        log_success "Cross-platform deployment testing completed successfully"
        exit 0
    else
        log_error "Cross-platform deployment testing completed with failures"
        exit 1
    fi
}

# Execute main function if script is run directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi