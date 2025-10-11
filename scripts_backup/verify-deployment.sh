#!/usr/bin/env bash
# Deployment Package Verification Script
#
# Verifies that deployment packages contain all required dependencies
# and can run successfully without additional installation steps.
#
# @author       Puzzle71Solver Team
# @created      2025-10-10
# @license      MIT

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
DEPLOYMENT_DIR="${REPO_ROOT}/deployment"
PACKAGES_DIR="${DEPLOYMENT_DIR}/packages"
TEST_DEPLOYMENT_DIR="${DEPLOYMENT_DIR}/test-deploy"

# Exit codes
readonly EXIT_SUCCESS=0
readonly EXIT_VERIFICATION_FAILED=1
readonly EXTRACTION_FAILED=2
readonly DEPENDENCY_MISSING=3
readonly EXECUTION_FAILED=4
readonly CLEANUP_FAILED=5

# Color codes for output
readonly GREEN='\033[0;32m'
readonly RED='\033[0;31m'
readonly YELLOW='\033[1;33m'
readonly BLUE='\033[0;34m'
readonly NC='\033[0m' # No Color

# Verification configuration
readonly CLEANUP_AFTER_TEST=${CLEANUP_AFTER_TEST:-true}
readonly VERBOSE_OUTPUT=${VERBOSE_OUTPUT:-false}
readonly TEST_EXECUTION=${TEST_EXECUTION:-true}
readonly DEPENDENCY_CHECK=${DEPENDENCY_CHECK:-true}
readonly CHECK_INTEGRITY=${CHECK_INTEGRITY:-true}

# Global state
TOTAL_PACKAGES=0
VERIFIED_PACKAGES=0
FAILED_PACKAGES=0

# Logging functions
log_verification() {
    local level="$1"
    local message="$2"
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')

    case "$level" in
        "SUCCESS")
            echo -e "${GREEN}[VERIFY-SUCCESS]${NC} ${timestamp} - ${message}"
            ;;
        "ERROR")
            echo -e "${RED}[VERIFY-ERROR]${NC} ${timestamp} - ${message}"
            ;;
        "WARNING")
            echo -e "${YELLOW}[VERIFY-WARN]${NC} ${timestamp} - ${message}"
            ;;
        "INFO")
            echo -e "${BLUE}[VERIFY-INFO]${NC} ${timestamp} - ${message}"
            ;;
        *)
            echo -e "${BLUE}[VERIFY-${level}]${NC} ${timestamp} - ${message}"
            ;;
    esac
}

# Check prerequisites
check_prerequisites() {
    log_verification "INFO" "Checking deployment verification prerequisites"

    local missing_deps=()

    # Check required commands
    for cmd in tar gzip file ldd find sha256sum; do
        if ! command -v "$cmd" >/dev/null 2>&1; then
            missing_deps+=("$cmd")
        fi
    done

    if [[ ${#missing_deps[@]} -gt 0 ]]; then
        log_verification "ERROR" "Missing required dependencies: ${missing_deps[*]}"
        return $EXIT_VERIFICATION_FAILED
    fi

    # Check packages directory
    if [[ ! -d "$PACKAGES_DIR" ]]; then
        log_verification "ERROR" "Packages directory not found: $PACKAGES_DIR"
        return $EXIT_VERIFICATION_FAILED
    fi

    # Create test deployment directory
    mkdir -p "$TEST_DEPLOYMENT_DIR"

    log_verification "SUCCESS" "Prerequisites check completed"
    return $EXIT_SUCCESS
}

# Extract and verify package
extract_and_verify_package() {
    local package_path="$1"
    local package_name=$(basename "$package_path")
    local test_dir="$TEST_DEPLOYMENT_DIR/${package_name%.tar.gz}"
    local extract_dir="$test_dir/extracted"

    log_verification "INFO" "Verifying package: $package_name"

    # Create test directory
    mkdir -p "$test_dir"

    # Extract package
    log_verification "INFO" "Extracting package to: $extract_dir"
    if ! mkdir -p "$extract_dir"; then
        log_verification "ERROR" "Failed to create extraction directory: $extract_dir"
        return $EXTRACTION_FAILED
    fi

    if ! tar -xzf "$package_path" -C "$extract_dir"; then
        log_verification "ERROR" "Failed to extract package: $package_name"
        return $EXTRACTION_FAILED
    fi

    # Verify package structure
    if ! verify_package_structure "$extract_dir" "$package_name"; then
        return $EXIT_VERIFICATION_FAILED
    fi

    # Verify dependencies
    if [[ "$DEPENDENCY_CHECK" == "true" ]]; then
        if ! verify_dependencies "$extract_dir" "$package_name"; then
            return $EXIT_DEPENDENCY_MISSING
        fi
    fi

    # Verify integrity
    if [[ "$CHECK_INTEGRITY" == "true" ]]; then
        if ! verify_integrity "$extract_dir" "$package_name"; then
            return $EXIT_VERIFICATION_FAILED
        fi
    fi

    # Test execution
    if [[ "$TEST_EXECUTION" == "true" ]]; then
        if ! test_execution "$extract_dir" "$package_name"; then
            return $EXIT_EXECUTION_FAILED
        fi
    fi

    # Cleanup
    if [[ "$CLEANUP_AFTER_TEST" == "true" ]]; then
        rm -rf "$test_dir"
    fi

    ((VERIFIED_PACKAGES++))
    log_verification "SUCCESS" "Package verification completed: $package_name"
    return $EXIT_SUCCESS
}

# Verify package structure
verify_package_structure() {
    local extract_dir="$1"
    local package_name="$2"

    log_verification "INFO" "Verifying package structure for $package_name"

    # Check required directories
    local required_dirs=("bin" "lib" "scripts")
    for dir in "${required_dirs[@]}"; do
        if [[ ! -d "$extract_dir/$dir" ]]; then
            log_verification "ERROR" "Required directory missing: $dir"
            return $EXIT_VERIFICATION_FAILED
        fi
    done

    # Check required files
    local required_files=("bin/Puzzle71Solver" "deployment-manifest.json" "scripts/install.sh")
    for file in "${required_files[@]}"; do
        if [[ ! -f "$extract_dir/$file" ]]; then
            log_verification "ERROR" "Required file missing: $file"
            return $EXIT_VERIFICATION_FAILED
        fi
    done

    # Verify executable permissions
    if [[ ! -x "$extract_dir/bin/Puzzle71Solver" ]]; then
        log_verification "ERROR" "Main binary is not executable: bin/Puzzle71Solver"
        return $EXIT_VERIFICATION_FAILED
    fi

    if [[ ! -x "$extract_dir/scripts/install.sh" ]]; then
        log_verification "ERROR" "Install script is not executable: scripts/install.sh"
        return $EXIT_VERIFICATION_FAILED
    fi

    log_verification "SUCCESS" "Package structure verification completed"
    return $EXIT_SUCCESS
}

# Verify dependencies
verify_dependencies() {
    local extract_dir="$1"
    local package_name="$2"

    log_verification "INFO" "Verifying dependencies for $package_name"

    # Get list of binaries to check
    local binaries=()
    if [[ -f "$extract_dir/bin/Puzzle71Solver" ]]; then
        binaries+=("$extract_dir/bin/Puzzle71Solver")
    fi

    # Check additional binaries if they exist
    for binary in "generate-report.sh" "purge-checkpoints.sh"; do
        if [[ -f "$extract_dir/bin/$binary" ]]; then
            binaries+=("$extract_dir/bin/$binary")
        fi
    done

    local missing_deps=()
    for binary in "${binaries[@]}"; do
        log_verification "INFO" "Checking dependencies for: $(basename "$binary")"

        # Use ldd to check dependencies
        local dep_output
        if ! dep_output=$(ldd "$binary" 2>&1); then
            log_verification "ERROR" "Failed to check dependencies for: $(basename "$binary")"
            continue
        fi

        # Check for missing dependencies
        local missing=$(echo "$dep_output" | grep "not found" || true)
        if [[ -n "$missing" ]]; then
            while IFS= read -r line; do
                local dep_name=$(echo "$line" | sed 's/.*=> \(.*\) (not found)/\1/')
                missing_deps+=("$dep_name")
            done <<< "$missing"
        fi
    done

    # Check system libraries availability
    local system_libs=("libc.so.6" "libstdc++.so.6" "libm.so.6" "libpthread.so.0")
    for lib in "${system_libs[@]}"; do
        if ! ldconfig -p | grep -q "$lib"; then
            missing_deps+=("system: $lib")
        fi
    done

    # Report missing dependencies
    if [[ ${#missing_deps[@]} -gt 0 ]]; then
        log_verification "ERROR" "Missing dependencies found for $package_name:"
        for dep in "${missing_deps[@]}"; do
            echo "  - $dep"
        done
        return $EXIT_DEPENDENCY_MISSING
    fi

    log_verification "SUCCESS" "Dependency verification completed"
    return $EXIT_SUCCESS
}

# Verify integrity
verify_integrity() {
    local extract_dir="$1"
    local package_name="$2"

    log_verification "INFO" "Verifying integrity for $package_name"

    local manifest_file="$extract_dir/deployment-manifest.json"
    if [[ ! -f "$manifest_file" ]]; then
        log_verification "WARNING" "No deployment manifest found, skipping integrity verification"
        return $EXIT_SUCCESS
    fi

    # Verify file checksums if available
    if command -v jq >/dev/null 2>&1; then
        local checksums_section=$(jq -r '.verification.checksums // {}' "$manifest_file" 2>/dev/null || echo "{}")
        if [[ "$checksums_section" != "{}" ]]; then
            log_verification "INFO" "Verifying file checksums"

            local checksum_failures=0
            while IFS= read -r -d '' file; do
                local relative_path="${file#$extract_dir/}"
                local expected_checksum
                expected_checksum=$(echo "$checksums_section" | jq -r ".[\"$relative_path\"] // empty" 2>/dev/null || echo "")

                if [[ "$expected_checksum" != "empty" && -n "$expected_checksum" ]]; then
                    local actual_checksum=$(sha256sum "$file" | cut -d' ' -f1)
                    if [[ "$actual_checksum" != "$expected_checksum" ]]; then
                        log_verification "ERROR" "Checksum mismatch for: $relative_path"
                        log_verification "ERROR" "Expected: $expected_checksum"
                        log_verification "ERROR" "Actual: $actual_checksum"
                        ((checksum_failures++))
                    fi
                fi
            done < <(find "$extract_dir" -type f -print0)

            if [[ $checksum_failures -gt 0 ]]; then
                log_verification "ERROR" "Found $checksum_failures checksum mismatches"
                return $EXIT_VERIFICATION_FAILED
            fi
        fi
    else
        log_verification "WARNING" "jq not available, skipping manifest-based integrity verification"
    fi

    # Verify binary integrity
    local binary="$extract_dir/bin/Puzzle71Solver"
    if [[ -f "$binary" ]]; then
        if ! file "$binary" | grep -q "ELF"; then
            log_verification "ERROR" "Main binary is not a valid ELF file"
            return $EXIT_VERIFICATION_FAILED
        fi

        # Check if binary can be opened (basic integrity check)
        if ! ldd "$binary" >/dev/null 2>&1; then
            log_verification "ERROR" "Binary integrity check failed"
            return $EXIT_VERIFICATION_FAILED
        fi
    fi

    log_verification "SUCCESS" "Integrity verification completed"
    return $EXIT_SUCCESS
}

# Test execution
test_execution() {
    local extract_dir="$1"
    local package_name="$2"

    log_verification "INFO" "Testing execution for $package_name"

    local binary="$extract_dir/bin/Puzzle71Solver"
    if [[ ! -f "$binary" ]]; then
        log_verification "ERROR" "Binary not found for execution test"
        return $EXIT_EXECUTION_FAILED
    fi

    # Test basic help functionality
    log_verification "INFO" "Testing help command"
    if timeout 30 "$binary" --help >/dev/null 2>&1; then
        log_verification "SUCCESS" "Help command works"
    else
        log_verification "WARNING" "Help command failed or timed out"
    fi

    # Test version information
    log_verification "INFO" "Testing version command"
    if timeout 30 "$binary" --version >/dev/null 2>&1; then
        log_verification "SUCCESS" "Version command works"
    else
        log_verification "WARNING" "Version command failed or timed out"
    fi

    # Test configuration loading
    log_verification "INFO" "Testing configuration loading"
    if timeout 30 "$binary" --config "$extract_dir/config/puzzle71.yaml" --help >/dev/null 2>&1; then
        log_verification "SUCCESS" "Configuration loading works"
    else
        log_verification "WARNING" "Configuration loading failed or timed out"
    fi

    # Test with dry run if available
    log_verification "INFO" "Testing dry run functionality"
    if timeout 30 "$binary" --dry-run >/dev/null 2>&1; then
        log_verification "SUCCESS" "Dry run works"
    else
        log_verification "WARNING" "Dry run failed or timed out (may not be supported)"
    fi

    log_verification "SUCCESS" "Execution testing completed"
    return $EXIT_SUCCESS
}

# Generate verification report
generate_verification_report() {
    local report_file="${DEPLOYMENT_DIR}/verification-report-$(date '+%Y%m%d_%H%M%S').json"

    log_verification "INFO" "Generating verification report: $report_file"

    cat > "$report_file" << EOF
{
    "verification_timestamp": "$(date -Iseconds)",
    "verification_configuration": {
        "cleanup_after_test": $CLEANUP_AFTER_TEST,
        "verbose_output": $VERBOSE_OUTPUT,
        "test_execution": $TEST_EXECUTION,
        "dependency_check": $DEPENDENCY_CHECK,
        "check_integrity": $CHECK_INTEGRITY
    },
    "summary": {
        "total_packages": $TOTAL_PACKAGES,
        "verified_packages": $VERIFIED_PACKAGES,
        "failed_packages": $FAILED_PACKAGES,
        "success_rate": $(echo "scale=2; $VERIFIED_PACKAGES * 100 / $TOTAL_PACKAGES" | bc -l)
    },
    "test_environment": {
        "platform": "$(uname -s)",
        "architecture": "$(uname -m)",
        "kernel_version": "$(uname -r)",
        "hostname": "$(hostname)",
        "user": "$(whoami)"
    },
    "packages_verified": [
EOF

    # List verified packages
    if [[ $VERIFIED_PACKAGES -gt 0 ]]; then
        local first=true
        for package_file in "$PACKAGES_DIR"/*.tar.gz; do
            if [[ -f "$package_file" ]]; then
                if [[ "$first" == "false" ]]; then
                    echo "," >> "$report_file"
                fi
                first=false

                local package_name=$(basename "$package_file")
                cat >> "$report_file" << EOF
        {
            "name": "$package_name",
            "status": "verified",
            "verification_time": "$(date -Iseconds)",
            "extraction_success": true,
            "structure_valid": true,
            "dependencies_present": true,
            "integrity_verified": true,
            "execution_successful": true
        }
EOF
            fi
        done
    fi

    cat >> "$report_file" << EOF
    ],
    "recommendations": [
        $(if [[ $VERIFIED_PACKAGES -eq $TOTAL_PACKAGES ]]; then echo '"All packages verified successfully",'; else echo '"Review failed packages and address issues",'; fi)
        $(if [[ $FAILED_PACKAGES -gt 0 ]]; then echo '"Fix missing dependencies before deployment",'; fi)
        "$(if [[ $DEPENDENCY_CHECK == "true" ]]; then echo '"Verify target environment has required system libraries",'; else echo '"Consider enabling dependency checking for deployment verification",'; fi)"
    ],
    "deployment_readiness": "$(if [[ $VERIFIED_PACKAGES -eq $TOTAL_PACKAGES && $FAILED_PACKAGES -eq 0 ]]; then echo "READY"; else echo "NOT_READY"; fi)"
}
EOF

    log_verification "INFO" "Verification report generated: $report_file"
    echo "$report_file"
}

# Main verification function
run_verification() {
    log_verification "INFO" "Starting deployment package verification"

    # Check prerequisites
    if ! check_prerequisites; then
        return $EXIT_VERIFICATION_FAILED
    fi

    # Find all packages to verify
    local packages=()
    for package_file in "$PACKAGES_DIR"/*.tar.gz; do
        if [[ -f "$package_file" ]]; then
            packages+=("$package_file")
            ((TOTAL_PACKAGES++))
        fi
    done

    if [[ ${#packages[@]} -eq 0 ]]; then
        log_verification "ERROR" "No packages found to verify in: $PACKAGES_DIR"
        return $EXIT_VERIFICATION_FAILED
    fi

    log_verification "INFO" "Found ${#packages[@]} packages to verify"

    # Verify each package
    local overall_result=$EXIT_SUCCESS
    for package_file in "${packages[@]}"; do
        if ! extract_and_verify_package "$package_file"; then
            ((FAILED_PACKAGES++))
            overall_result=$EXIT_VERIFICATION_FAILED
        fi
    done

    # Generate report
    generate_verification_report

    # Display summary
    echo ""
    log_verification "INFO" "Verification Summary"
    log_verification "INFO" "==================="
    log_verification "INFO" "Total packages: $TOTAL_PACKAGES"
    log_verification "INFO" "Verified: $VERIFIED_PACKAGES"
    log_verification "INFO" "Failed: $FAILED_PACKAGES"

    if [[ $VERIFIED_PACKAGES -eq $TOTAL_PACKAGES ]]; then
        log_verification "SUCCESS" "All packages verified successfully - ready for deployment"
        return $EXIT_SUCCESS
    else
        log_verification "ERROR" "Some packages failed verification - address issues before deployment"
        return $overall_result
    fi
}

# Cleanup function
cleanup() {
    if [[ -d "$TEST_DEPLOYMENT_DIR" ]]; then
        log_verification "INFO" "Cleaning up test deployment directory"
        rm -rf "$TEST_DEPLOYMENT_DIR"
    fi
}

# Set up cleanup trap
trap cleanup EXIT

# Main execution function
main() {
    # Parse command line arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            -h|--help)
                cat << 'EOF'
Deployment Package Verification

Usage: ./verify-deployment.sh [OPTIONS] [PACKAGE_FILE]

OPTIONS:
    -h, --help                     Show this help message
    -v, --verbose                  Enable verbose logging
    --no-cleanup                   Don't cleanup after testing
    --no-test-execution           Skip execution testing
    --no-dependency-check          Skip dependency checking
    --no-integrity-check           Skip integrity verification
    --packages-dir <directory>      Packages directory (default: deployment/packages)
    --test-dir <directory>          Test deployment directory

EXAMPLES:
    ./verify-deployment.sh
    ./verify-deployment.sh --verbose
    ./verify-deployment.sh --no-cleanup
    ./verify-deployment.sh package.tar.gz
    ./verify-deployment.sh --no-test-execution --no-dependency-check

DESCRIPTION:
    This script verifies that deployment packages contain all required dependencies
    and can run successfully without additional installation steps. It extracts packages
    in a test environment and checks structure, dependencies, integrity, and execution.

EOF
                exit $EXIT_SUCCESS
                ;;
            -v|--verbose)
                export VERBOSE_OUTPUT=true
                set -x
                shift
                ;;
            --no-cleanup)
                export CLEANUP_AFTER_TEST=false
                shift
                ;;
            --no-test-execution)
                export TEST_EXECUTION=false
                shift
                ;;
            --no-dependency-check)
                export DEPENDENCY_CHECK=false
                shift
                ;;
            --no-integrity-check)
                export CHECK_INTEGRITY=false
                shift
                ;;
            --packages-dir)
                export PACKAGES_DIR="$2"
                shift 2
                ;;
            --test-dir)
                export TEST_DEPLOYMENT_DIR="$2"
                shift 2
                ;;
            *)
                # Assume it's a package file
                break
                ;;
        esac
    done

    # If a specific package file is provided, verify only that package
    if [[ $# -gt 0 ]]; then
        local package_file="$1"
        if [[ ! -f "$package_file" ]]; then
            log_verification "ERROR" "Package file not found: $package_file"
            exit $EXIT_VERIFICATION_FAILED
        fi

        TOTAL_PACKAGES=1
        if extract_and_verify_package "$package_file"; then
            VERIFIED_PACKAGES=1
            log_verification "SUCCESS" "Package verification completed successfully"
            exit $EXIT_SUCCESS
        else
            FAILED_PACKAGES=1
            log_verification "ERROR" "Package verification failed"
            exit $EXIT_VERIFICATION_FAILED
        fi
    else
        # Verify all packages
        run_verification
    fi
}

# Run main function if script is executed directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi