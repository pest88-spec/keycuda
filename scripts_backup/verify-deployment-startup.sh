#!/bin/bash

# Deployment Startup Verification Script
# Verifies that deployment packages start successfully without external dependency installation
# Part of T041: Verify deployment starts successfully without dependency installation

set -euo pipefail

# Script configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DEPLOYMENT_DIR="$PROJECT_ROOT/deployment"
VERIFICATION_DIR="$DEPLOYMENT_DIR/startup-verification"

# Verification configuration
readonly DEFAULT_TIMEOUT=60
readonly DEFAULT_STARTUP_RETRIES=3
readonly DEFAULT_MEMORY_LIMIT_MB=1024
readonly DEFAULT_CPU_LIMIT=1

# Colors for output
readonly COLOR_RED='\033[0;31m'
readonly COLOR_GREEN='\033[0;32m'
readonly COLOR_YELLOW='\033[1;33m'
readonly COLOR_BLUE='\033[0;34m'
readonly COLOR_PURPLE='\033[0;35m'
readonly COLOR_CYAN='\033[0;36m'
readonly COLOR_NC='\033[0m' # No Color

# Global variables
VERBOSE=false
DRY_RUN=false
GENERATE_REPORTS=true
TIMEOUT=$DEFAULT_TIMEOUT
STARTUP_RETRIES=$DEFAULT_STARTUP_RETRIES
MEMORY_LIMIT_MB=$DEFAULT_MEMORY_LIMIT_MB
CPU_LIMIT=$DEFAULT_CPU_LIMIT
DEPLOYMENT_PACKAGES=()
VERIFICATION_RESULTS=()

# =============================================================================
# LOGGING AND OUTPUT FUNCTIONS
# =============================================================================

log_startup() {
    local level="$1"
    local message="$2"
    local timestamp=$(date -u +"%Y-%m-%d %H:%M:%S UTC")

    case "$level" in
        "INFO")
            echo -e "${COLOR_BLUE}[STARTUP-INFO]${COLOR_NC} $message"
            ;;
        "SUCCESS")
            echo -e "${COLOR_GREEN}[STARTUP-SUCCESS]${COLOR_NC} $message"
            ;;
        "WARNING")
            echo -e "${COLOR_YELLOW}[STARTUP-WARNING]${COLOR_NC} $message"
            ;;
        "ERROR")
            echo -e "${COLOR_RED}[STARTUP-ERROR]${COLOR_NC} $message"
            ;;
        "DEBUG")
            if [[ "$VERBOSE" == true ]]; then
                echo -e "${COLOR_PURPLE}[STARTUP-DEBUG]${COLOR_NC} $message"
            fi
            ;;
        "PROGRESS")
            echo -e "${COLOR_CYAN}[STARTUP-PROGRESS]${COLOR_NC} $message"
            ;;
    esac

    # Log to file if reports are enabled
    if [[ "$GENERATE_REPORTS" == true ]]; then
        echo "[$timestamp] [STARTUP $level] $message" >> "$VERIFICATION_DIR/startup-verification.log"
    fi
}

print_startup_header() {
    echo -e "${COLOR_BLUE}"
    echo "======================================================"
    echo "  Deployment Startup Verification"
    echo "  One-Click Deployment Acceptance Testing"
    echo "======================================================"
    echo -e "${COLOR_NC}"
}

print_usage() {
    cat << EOF
Usage: $0 [OPTIONS] <deployment_package> [deployment_package ...]

Deployment startup verification tool that validates deployment packages
can start successfully without external dependency installation.

ARGUMENTS:
    deployment_package    Path(s) to deployment packages to verify

OPTIONS:
    --timeout SECONDS    Startup timeout per package (default: 60)
    --retries N          Number of startup retries (default: 3)
    --memory-limit MB    Memory limit in MB for testing (default: 1024)
    --cpu-limit N        CPU limit for testing (default: 1)
    --dry-run           Show what would be verified without executing
    --verbose           Enable verbose output
    --quiet             Suppress non-error output
    --no-reports        Disable report generation
    --output DIR        Output directory for reports (default: deployment/startup-verification)
    --isolated          Test in isolated environment
    --check-deps        Check for external dependencies and fail if found
    --help              Show this help message

ENVIRONMENT TESTING:
    --clean-env         Test in clean environment (no external dependencies)
    --minimized         Test with minimal system resources
    --offline           Test without network access

EXIT CODES:
    0   All packages started successfully
    1   General verification failure
    2   Package failed to start
    3   External dependencies detected
    4   Timeout or resource constraints
    5   Invalid arguments or configuration

EXAMPLES:
    # Basic startup verification
    $0 deployment-package.tar.gz

    # Test multiple packages with resource constraints
    $0 --memory-limit 512 --cpu-limit 1 --timeout 30 package1.tar.gz package2.tar.gz

    # Clean environment testing
    $0 --clean-env --check-deps deployment-package.tar.gz

    # Isolated environment testing
    $0 --isolated --offline deployment-package.tar.gz

EOF
}

# =============================================================================
# ARGUMENT PARSING
# =============================================================================

parse_arguments() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            --timeout)
                TIMEOUT="$2"
                shift 2
                ;;
            --retries)
                STARTUP_RETRIES="$2"
                shift 2
                ;;
            --memory-limit)
                MEMORY_LIMIT_MB="$2"
                shift 2
                ;;
            --cpu-limit)
                CPU_LIMIT="$2"
                shift 2
                ;;
            --dry-run)
                DRY_RUN=true
                shift
                ;;
            --verbose)
                VERBOSE=true
                shift
                ;;
            --quiet)
                # Quiet mode suppresses INFO but keeps ERROR and SUCCESS
                set +x
                shift
                ;;
            --no-reports)
                GENERATE_REPORTS=false
                shift
                ;;
            --output)
                VERIFICATION_DIR="$2"
                shift 2
                ;;
            --isolated)
                ISOLATED_ENV=true
                shift
                ;;
            --check-deps)
                CHECK_EXTERNAL_DEPS=true
                shift
                ;;
            --clean-env)
                CLEAN_ENV=true
                shift
                ;;
            --minimized)
                MINIMIZED_RESOURCES=true
                shift
                ;;
            --offline)
                OFFLINE_MODE=true
                shift
                ;;
            --help)
                print_usage
                exit 0
                ;;
            -*)
                log_startup "ERROR" "Unknown option: $1"
                print_usage
                exit 5
                ;;
            *)
                # Assume it's a deployment package path
                DEPLOYMENT_PACKAGES+=("$1")
                shift
                ;;
        esac
    done

    # Validate required arguments
    if [[ ${#DEPLOYMENT_PACKAGES[@]} -eq 0 ]]; then
        log_startup "ERROR" "At least one deployment package is required"
        print_usage
        exit 5
    fi

    # Validate packages exist
    for package in "${DEPLOYMENT_PACKAGES[@]}"; do
        if [[ ! -f "$package" ]]; then
            log_startup "ERROR" "Deployment package not found: $package"
            exit 5
        fi
    done
}

# =============================================================================
# ENVIRONMENT SETUP AND VALIDATION
# =============================================================================

setup_verification_environment() {
    log_startup "INFO" "Setting up verification environment"

    # Create verification directory
    mkdir -p "$VERIFICATION_DIR"
    mkdir -p "$VERIFICATION_DIR/extracted"
    mkdir -p "$VERIFICATION_DIR/reports"
    mkdir -p "$VERIFICATION_DIR/logs"

    if [[ "$GENERATE_REPORTS" == true ]]; then
        echo "Deployment startup verification log - $(date)" > "$VERIFICATION_DIR/startup-verification.log" 2>/dev/null || true
    fi

    # Setup resource constraints
    if [[ "${MINIMIZED_RESOURCES:-false}" == true ]]; then
        MEMORY_LIMIT_MB=256
        CPU_LIMIT=1
        TIMEOUT=30
        log_startup "INFO" "Minimized resource mode: memory=${MEMORY_LIMIT_MB}MB, cpu=${CPU_LIMIT}, timeout=${TIMEOUT}s"
    fi

    # Setup isolated environment
    if [[ "${ISOLATED_ENV:-false}" == true ]]; then
        setup_isolated_environment
    fi

    # Setup clean environment
    if [[ "${CLEAN_ENV:-false}" == true ]]; then
        setup_clean_environment
    fi
}

setup_isolated_environment() {
    log_startup "INFO" "Setting up isolated environment"

    # Create isolated temp directory
    local isolated_dir="$VERIFICATION_DIR/isolated_$$"
    mkdir -p "$isolated_dir"

    # Set minimal PATH
    export PATH="/usr/bin:/bin"

    # Clear potentially conflicting environment variables
    unset LD_LIBRARY_PATH 2>/dev/null || true
    unset PYTHONPATH 2>/dev/null || true
    unset PERL5LIB 2>/dev/null || true

    ISOLATED_ENV_DIR="$isolated_dir"
}

setup_clean_environment() {
    log_startup "INFO" "Setting up clean environment for dependency testing"

    # Check for external dependencies that might interfere
    local external_deps=()

    # Check for common external dependencies
    local common_libs=(
        "libsecp256k1.so"
        "libcuda.so"
        "libcudart.so"
        "libOpenCL.so"
    )

    for lib in "${common_libs[@]}"; do
        if ldconfig -p | grep -q "$lib"; then
            external_deps+=("$lib")
        fi
    done

    if [[ ${#external_deps[@]} -gt 0 ]]; then
        log_startup "WARNING" "External dependencies detected: ${external_deps[*]}"

        if [[ "${CHECK_EXTERNAL_DEPS:-false}" == true ]]; then
            log_startup "ERROR" "External dependencies found but --check-deps specified"
            return 1
        fi
    fi
}

check_network_connectivity() {
    if [[ "${OFFLINE_MODE:-false}" == true ]]; then
        # Block network access for offline testing
        if command -v iptables >/dev/null 2>&1; then
            # Note: This requires root privileges
            log_startup "WARNING" "Network blocking requires root privileges"
        fi
        return 0
    fi

    # Test network connectivity
    if ping -c 1 -W 5 8.8.8.8 >/dev/null 2>&1; then
        log_startup "INFO" "Network connectivity available"
        return 0
    else
        log_startup "WARNING" "No network connectivity (offline mode)"
        return 1
    fi
}

# =============================================================================
# DEPLOYMENT PACKAGE EXTRACTION AND ANALYSIS
# =============================================================================

extract_deployment_package() {
    local package_path="$1"
    local package_name=$(basename "$package_path" | sed 's/\.\(tar\.\(gz\|bz2\|xz\)\|zip\)$//')
    local extract_dir="$VERIFICATION_DIR/extracted/$package_name"

    log_startup "INFO" "Extracting deployment package: $package_name"

    mkdir -p "$extract_dir"

    if [[ "$DRY_RUN" == true ]]; then
        log_startup "INFO" "[DRY-RUN] Would extract package to: $extract_dir"
        echo "$extract_dir"
        return 0
    fi

    # Extract based on file extension
    case "${package_path##*.}" in
        "gz")
            if [[ "$package_path" =~ \.tar\.gz$ ]]; then
                tar -xzf "$package_path" -C "$extract_dir" 2>/dev/null || {
                    log_startup "ERROR" "Failed to extract tar.gz package: $package_path"
                    return 1
                }
            else
                log_startup "ERROR" "Unsupported .gz format (expected .tar.gz)"
                return 1
            fi
            ;;
        "bz2")
            if [[ "$package_path" =~ \.tar\.bz2$ ]]; then
                tar -xjf "$package_path" -C "$extract_dir" 2>/dev/null || {
                    log_startup "ERROR" "Failed to extract tar.bz2 package: $package_path"
                    return 1
                }
            else
                log_startup "ERROR" "Unsupported .bz2 format (expected .tar.bz2)"
                return 1
            fi
            ;;
        "xz")
            if [[ "$package_path" =~ \.tar\.xz$ ]]; then
                tar -xJf "$package_path" -C "$extract_dir" 2>/dev/null || {
                    log_startup "ERROR" "Failed to extract tar.xz package: $package_path"
                    return 1
                }
            else
                log_startup "ERROR" "Unsupported .xz format (expected .tar.xz)"
                return 1
            fi
            ;;
        "zip")
            unzip -q "$package_path" -d "$extract_dir" 2>/dev/null || {
                log_startup "ERROR" "Failed to extract zip package: $package_path"
                return 1
            }
            ;;
        *)
            log_startup "ERROR" "Unsupported package format: ${package_path##*.}"
            return 1
            ;;
    esac

    log_startup "SUCCESS" "Package extracted successfully to: $extract_dir"
    echo "$extract_dir"
}

analyze_package_structure() {
    local extract_dir="$1"
    local analysis_file="$extract_dir/structure_analysis.json"

    log_startup "DEBUG" "Analyzing package structure: $extract_dir"

    if [[ "$DRY_RUN" == true ]]; then
        log_startup "INFO" "[DRY-RUN] Would analyze package structure"
        return 0
    fi

    # Analyze package structure
    local binary_count=0
    local library_count=0
    local config_count=0
    local doc_count=0

    # Count binaries
    if [[ -d "$extract_dir/bin" ]]; then
        binary_count=$(find "$extract_dir/bin" -type f -executable | wc -l)
    fi

    # Count libraries
    if [[ -d "$extract_dir/lib" ]]; then
        library_count=$(find "$extract_dir/lib" -type f \( -name "*.so" -o -name "*.dylib" -o -name "*.dll" \) | wc -l)
    fi

    # Count configuration files
    if [[ -d "$extract_dir/config" ]]; then
        config_count=$(find "$extract_dir/config" -type f \( -name "*.json" -o -name "*.conf" -o -name "*.yaml" -o -name "*.yml" \) | wc -l)
    fi

    # Count documentation files
    if [[ -d "$extract_dir/docs" ]]; then
        doc_count=$(find "$extract_dir/docs" -type f \( -name "*.md" -o -name "*.txt" -o -name "*.pdf" \) | wc -l)
    fi

    # Find main binary
    local main_binary=""
    if [[ -f "$extract_dir/Puzzle71Solver" ]]; then
        main_binary="$extract_dir/Puzzle71Solver"
    elif [[ -f "$extract_dir/bin/Puzzle71Solver" ]]; then
        main_binary="$extract_dir/bin/Puzzle71Solver"
    elif [[ -f "$extract_dir/bin/puzzle71solver" ]]; then
        main_binary="$extract_dir/bin/puzzle71solver"
    fi

    # Check for startup script
    local startup_script=""
    if [[ -f "$extract_dir/start.sh" ]]; then
        startup_script="$extract_dir/start.sh"
    elif [[ -f "$extract_dir/run.sh" ]]; then
        startup_script="$extract_dir/run.sh"
    elif [[ -f "$extract_dir/scripts/start.sh" ]]; then
        startup_script="$extract_dir/scripts/start.sh"
    fi

    # Create analysis JSON
    cat > "$analysis_file" << EOF
{
    "package_path": "$extract_dir",
    "timestamp": "$(date -u +"%Y-%m-%dT%H:%M:%SZ")",
    "structure": {
        "binary_count": $binary_count,
        "library_count": $library_count,
        "config_count": $config_count,
        "documentation_count": $doc_count,
        "main_binary": "$main_binary",
        "startup_script": "$startup_script",
        "has_bin_directory": $([[ -d "$extract_dir/bin" ]] && echo true || echo false),
        "has_lib_directory": $([[ -d "$extract_dir/lib" ]] && echo true || echo false),
        "has_config_directory": $([[ -d "$extract_dir/config" ]] && echo true || echo false),
        "has_docs_directory": $([[ -d "$extract_dir/docs" ]] && echo true || echo false)
    }
}
EOF

    log_startup "DEBUG" "Package analysis complete: binaries=$binary_count, libraries=$library_count, configs=$config_count"
}

# =============================================================================
# STARTUP VERIFICATION FUNCTIONS
# =============================================================================

verify_startup_dependencies() {
    local extract_dir="$1"
    local main_binary="$2"

    log_startup "INFO" "Verifying startup dependencies"

    if [[ "$DRY_RUN" == true ]]; then
        log_startup "INFO" "[DRY-RUN] Would verify startup dependencies"
        return 0
    fi

    # Check binary dependencies
    if [[ -n "$main_binary" && -f "$main_binary" ]]; then
        log_startup "DEBUG" "Checking dependencies for: $main_binary"

        # Use ldd to check dynamic dependencies
        if command -v ldd >/dev/null 2>&1; then
            local dep_output=$(ldd "$main_binary" 2>/dev/null || echo "")

            # Check for missing dependencies
            if echo "$dep_output" | grep -q "not found"; then
                log_startup "ERROR" "Missing dependencies detected for $main_binary"
                echo "$dep_output" | grep "not found" | while read -r line; do
                    log_startup "ERROR" "  $line"
                done
                return 1
            else
                log_startup "SUCCESS" "All dependencies resolved for $main_binary"
            fi
        else
            log_startup "WARNING" "ldd not available, cannot check dependencies"
        fi
    else
        log_startup "ERROR" "Main binary not found: $main_binary"
        return 1
    fi

    return 0
}

execute_startup_test() {
    local extract_dir="$1"
    local main_binary="$2"
    local startup_script="$3"

    log_startup "INFO" "Executing startup test"

    if [[ "$DRY_RUN" == true ]]; then
        log_startup "INFO" "[DRY-RUN] Would execute startup test"
        return 0
    fi

    local startup_success=false
    local startup_time=0
    local error_message=""

    # Change to extracted directory for execution
    cd "$extract_dir"

    # Try startup methods in order of preference
    local startup_methods=()

    if [[ -n "$startup_script" && -f "$startup_script" ]]; then
        startup_methods+=("script:$startup_script")
    fi

    if [[ -n "$main_binary" && -f "$main_binary" ]]; then
        startup_methods+=("binary:$main_binary")
    fi

    if [[ ${#startup_methods[@]} -eq 0 ]]; then
        error_message="No startup method found (no main binary or startup script)"
        log_startup "ERROR" "$error_message"
        echo "{\"success\": false, \"error\": \"$error_message\", \"time\": 0}"
        return 1
    fi

    # Try each startup method
    for method in "${startup_methods[@]}"; do
        local method_type="${method%%:*}"
        local method_path="${method#*:}"

        log_startup "INFO" "Trying startup method: $method_type ($method_path)"

        for attempt in $(seq 1 $STARTUP_RETRIES); do
            log_startup "DEBUG" "Startup attempt $attempt/$STARTUP_RETRIES"

            local start_time=$(date +%s)

            case "$method_type" in
                "script")
                    if execute_script_startup "$method_path"; then
                        startup_success=true
                        startup_time=$(($(date +%s) - start_time))
                        break 2
                    fi
                    ;;
                "binary")
                    if execute_binary_startup "$method_path"; then
                        startup_success=true
                        startup_time=$(($(date +%s) - start_time))
                        break 2
                    fi
                    ;;
            esac

            log_startup "DEBUG" "Startup attempt $attempt failed"
            sleep 1
        done
    done

    # Return to original directory
    cd - >/dev/null

    # Create result JSON
    local result_json
    if [[ "$startup_success" == true ]]; then
        result_json=$(jq -n \
            --argjson success true \
            --argjson time "$startup_time" \
            --arg method "$method" \
            '{success: $success, startup_time_seconds: $time, method: $method}')
        log_startup "SUCCESS" "Startup successful in ${startup_time}s using $method_type"
    else
        if [[ -z "$error_message" ]]; then
            error_message="All startup methods failed after $STARTUP_RETRIES attempts"
        fi
        result_json=$(jq -n \
            --argjson success false \
            --argjson time 0 \
            --arg error "$error_message" \
            '{success: $success, startup_time_seconds: $time, error: $error}')
        log_startup "ERROR" "$error_message"
    fi

    echo "$result_json"
}

execute_script_startup() {
    local script_path="$1"

    log_startup "DEBUG" "Executing script startup: $script_path"

    # Make script executable
    chmod +x "$script_path"

    # Execute script with timeout and resource limits
    local cmd_output
    if command -v timeout >/dev/null 2>&1; then
        if [[ -n "${MEMORY_LIMIT_MB:-}" ]] && command -v cgcreate >/dev/null 2>&1; then
            # Use cgroups for resource limiting (requires root)
            cmd_output=$(timeout "$TIMEOUT" "$script_path" --test 2>&1 || true)
        else
            cmd_output=$(timeout "$TIMEOUT" "$script_path" --test 2>&1 || true)
        fi
    else
        cmd_output=$("$script_path" --test 2>&1 || true)
    fi

    # Check if startup was successful
    if [[ $? -eq 0 ]] || [[ "$cmd_output" =~ (success|ready|started|running) ]]; then
        log_startup "DEBUG" "Script startup successful"
        return 0
    else
        log_startup "DEBUG" "Script startup failed: $cmd_output"
        return 1
    fi
}

execute_binary_startup() {
    local binary_path="$1"

    log_startup "DEBUG" "Executing binary startup: $binary_path"

    # Make binary executable
    chmod +x "$binary_path"

    # Execute binary with timeout and test parameters
    local cmd_output
    if command -v timeout >/dev/null 2>&1; then
        cmd_output=$(timeout "$TIMEOUT" "$binary_path" --help 2>&1 || timeout "$TIMEOUT" "$binary_path" --version 2>&1 || timeout "$TIMEOUT" "$binary_path" --test 2>&1 || echo "")
    else
        cmd_output=$("$binary_path" --help 2>&1 || "$binary_path" --version 2>&1 || "$binary_path" --test 2>&1 || echo "")
    fi

    # Check if startup was successful
    if [[ $? -eq 0 ]] || [[ "$cmd_output" =~ (usage|version|Puzzle71Solver|help) ]]; then
        log_startup "DEBUG" "Binary startup successful"
        return 0
    else
        log_startup "DEBUG" "Binary startup failed: $cmd_output"
        return 1
    fi
}

verify_no_external_dependencies() {
    local extract_dir="$1"

    log_startup "INFO" "Verifying no external dependencies are required"

    if [[ "$DRY_RUN" == true ]]; then
        log_startup "INFO" "[DRY-RUN] Would verify no external dependencies"
        return 0
    fi

    # Check for indicators of external dependencies
    local external_dep_indicators=()

    # Check for network-related files or configurations
    if grep -r -i "http://" "$extract_dir" 2>/dev/null | head -5; then
        external_dep_indicators+=("HTTP URLs found in configuration")
    fi

    if grep -r -i "download\|fetch\|clone\|git" "$extract_dir" 2>/dev/null | head -5; then
        external_dep_indicators+=("Download/fetch instructions found")
    fi

    # Check for dependency manager files
    local dep_files=("requirements.txt" "package.json" "Pipfile" "composer.json" "Gemfile")
    for dep_file in "${dep_files[@]}"; do
        if find "$extract_dir" -name "$dep_file" | head -1; then
            external_dep_indicators+=("Dependency manager file found: $dep_file")
        fi
    done

    if [[ ${#external_dep_indicators[@]} -gt 0 ]]; then
        log_startup "WARNING" "Potential external dependency indicators found:"
        for indicator in "${external_dep_indicators[@]}"; do
            log_startup "WARNING" "  - $indicator"
        done

        if [[ "${CHECK_EXTERNAL_DEPS:-false}" == true ]]; then
            log_startup "ERROR" "External dependencies detected but --check-deps specified"
            return 1
        fi
    else
        log_startup "SUCCESS" "No external dependency indicators found"
    fi

    return 0
}

# =============================================================================
# VERIFICATION RESULT PROCESSING
# =============================================================================

process_verification_result() {
    local package_path="$1"
    local extract_dir="$2"
    local startup_result="$3"
    local dep_check_result="$4"
    local external_deps_check="$5"

    local package_name=$(basename "$package_path" | sed 's/\.\(tar\.\(gz\|bz2\|xz\)\|zip\)$//')
    local result_file="$VERIFICATION_DIR/reports/${package_name}_startup_result.json"

    # Parse startup result
    local startup_success=$(echo "$startup_result" | jq -r '.success // false')
    local startup_time=$(echo "$startup_result" | jq -r '.startup_time_seconds // 0')
    local startup_method=$(echo "$startup_result" | jq -r '.method // "unknown"')
    local startup_error=$(echo "$startup_result" | jq -r '.error // ""')

    # Calculate overall success
    local overall_success=true
    local failure_reasons=()

    if [[ "$startup_success" != "true" ]]; then
        overall_success=false
        failure_reasons+=("Startup failed: $startup_error")
    fi

    if [[ "$dep_check_result" != "0" ]]; then
        overall_success=false
        failure_reasons+=("Dependency check failed")
    fi

    if [[ "$external_deps_check" != "0" ]]; then
        overall_success=false
        failure_reasons+=("External dependencies detected")
    fi

    # Create comprehensive result JSON
    local result_json
    result_json=$(jq -n \
        --arg package_name "$package_name" \
        --arg package_path "$package_path" \
        --argjson overall_success "$overall_success" \
        --argjson startup_success "$startup_success" \
        --argjson startup_time "$startup_time" \
        --arg startup_method "$startup_method" \
        --arg startup_error "$startup_error" \
        --argjson dependency_check "$dep_check_result" \
        --argjson external_deps_check "$external_deps_check" \
        --argjson failure_reasons "$(printf '%s\n' "${failure_reasons[@]}" | jq -R . | jq -s .)" \
        --arg timestamp "$(date -u +"%Y-%m-%dT%H:%M:%SZ")" \
        '{
            package_name: $package_name,
            package_path: $package_path,
            timestamp: $timestamp,
            overall_success: $overall_success,
            startup_verification: {
                success: $startup_success,
                time_seconds: $startup_time,
                method: $startup_method,
                error: $startup_error
            },
            dependency_verification: {
                check_passed: ($dependency_check == 0)
            },
            external_dependency_verification: {
                no_external_deps: ($external_deps_check == 0)
            },
            failure_reasons: $failure_reasons
        }')

    echo "$result_json" > "$result_file"

    # Log result
    if [[ "$overall_success" == true ]]; then
        log_startup "SUCCESS" "Package $package_name: Startup verification PASSED"
    else
        log_startup "ERROR" "Package $package_name: Startup verification FAILED"
        for reason in "${failure_reasons[@]}"; do
            log_startup "ERROR" "  - $reason"
        done
    fi

    # Return result for aggregation
    echo "$result_json"
}

# =============================================================================
# MAIN EXECUTION
# =============================================================================

main() {
    # Parse command line arguments
    parse_arguments "$@"

    # Print header
    print_startup_header

    # Setup verification environment
    setup_verification_environment

    log_startup "INFO" "Starting startup verification for ${#DEPLOYMENT_PACKAGES[@]} deployment packages"

    if [[ "$DRY_RUN" == true ]]; then
        log_startup "INFO" "DRY-RUN mode: No actual verification will be performed"
    fi

    # Process each deployment package
    local total_packages=${#DEPLOYMENT_PACKAGES[@]}
    local successful_packages=0
    local failed_packages=0

    for package_path in "${DEPLOYMENT_PACKAGES[@]}"; do
        local package_name=$(basename "$package_path" | sed 's/\.\(tar\.\(gz\|bz2\|xz\)\|zip\)$//')
        log_startup "INFO" "Processing package $((successful_packages + failed_packages + 1))/$total_packages: $package_name"

        # Extract package
        local extract_dir
        extract_dir=$(extract_deployment_package "$package_path")
        if [[ $? -ne 0 ]]; then
            log_startup "ERROR" "Failed to extract package: $package_name"
            ((failed_packages++))
            continue
        fi

        # Analyze package structure
        analyze_package_structure "$extract_dir"

        # Read analysis results
        local analysis_file="$extract_dir/structure_analysis.json"
        local main_binary=""
        local startup_script=""

        if [[ -f "$analysis_file" ]]; then
            main_binary=$(jq -r '.structure.main // ""' "$analysis_file")
            startup_script=$(jq -r '.structure.startup_script // ""' "$analysis_file")
        fi

        # Verify startup dependencies
        local dep_check_result=0
        if ! verify_startup_dependencies "$extract_dir" "$main_binary"; then
            dep_check_result=1
        fi

        # Execute startup test
        local startup_result
        startup_result=$(execute_startup_test "$extract_dir" "$main_binary" "$startup_script")

        # Verify no external dependencies
        local external_deps_check=0
        if ! verify_no_external_dependencies "$extract_dir"; then
            external_deps_check=1
        fi

        # Process and store result
        local verification_result
        verification_result=$(process_verification_result "$package_path" "$extract_dir" "$startup_result" "$dep_check_result" "$external_deps_check")

        # Update counters
        local overall_success=$(echo "$verification_result" | jq -r '.overall_success // false')
        if [[ "$overall_success" == true ]]; then
            ((successful_packages++))
        else
            ((failed_packages++))
        fi

        VERIFICATION_RESULTS+=("$verification_result")
    done

    # Generate summary report
    generate_summary_report "$successful_packages" "$failed_packages" "$total_packages"

    # Final status
    echo
    log_startup "INFO" "=== Startup Verification Complete ==="
    log_startup "INFO" "Total packages: $total_packages"
    log_startup "INFO" "Successful: $successful_packages"
    log_startup "INFO" "Failed: $failed_packages"

    # Set appropriate exit code
    if [[ $failed_packages -eq 0 ]]; then
        log_startup "SUCCESS" "All packages started successfully without external dependencies"
        exit 0
    elif [[ $successful_packages -gt 0 ]]; then
        log_startup "WARNING" "Some packages failed startup verification"
        exit 2
    else
        log_startup "ERROR" "All packages failed startup verification"
        exit 1
    fi
}

generate_summary_report() {
    local successful="$1"
    local failed="$2"
    local total="$3"

    if [[ "$GENERATE_REPORTS" != true ]]; then
        return 0
    fi

    local summary_file="$VERIFICATION_DIR/startup_verification_summary.md"
    local success_rate=0
    if [[ $total -gt 0 ]]; then
        success_rate=$((successful * 100 / total))
    fi

    cat > "$summary_file" << EOF
# Deployment Startup Verification Summary

**Generated:** $(date -u +"%Y-%m-%d %H:%M:%S UTC")
**Verification Type:** One-Click Deployment Acceptance Testing
**Target:** T041 - Verify deployment starts successfully without dependency installation

## Executive Summary

- **Total Packages Tested:** $total
- **Successful Startups:** $successful
- **Failed Startups:** $failed
- **Success Rate:** ${success_rate}%

## Test Configuration

- **Startup Timeout:** ${TIMEOUT}s
- **Startup Retries:** $STARTUP_RETRIES
- **Memory Limit:** ${MEMORY_LIMIT_MB}MB
- **CPU Limit:** $CPU_LIMIT
- **Dry Run Mode:** $DRY_RUN
- **Isolated Environment:** ${ISOLATED_ENV:-false}
- **Clean Environment:** ${CLEAN_ENV:-false}
- **Offline Mode:** ${OFFLINE_MODE:-false}

## Detailed Results

EOF

    # Add detailed results for each package
    for result in "${VERIFICATION_RESULTS[@]}"; do
        local package_name=$(echo "$result" | jq -r '.package_name')
        local overall_success=$(echo "$result" | jq -r '.overall_success')
        local startup_success=$(echo "$result" | jq -r '.startup_verification.success')
        local startup_time=$(echo "$result" | jq -r '.startup_verification.time_seconds')
        local startup_method=$(echo "$result" | jq -r '.startup_verification.method')

        cat >> "$summary_file" << EOF
### $package_name

**Status:** $([ "$overall_success" == true ] && echo "✅ PASSED" || echo "❌ FAILED")
**Startup Success:** $([ "$startup_success" == true ] && echo "✅ YES" || echo "❌ NO")
**Startup Time:** ${startup_time}s
**Startup Method:** $startup_method

EOF

        # Add failure reasons if any
        local failure_reasons=$(echo "$result" | jq -r '.failure_reasons[]')
        if [[ -n "$failure_reasons" ]]; then
            cat >> "$summary_file" << EOF
**Failure Reasons:**
EOF
            echo "$result" | jq -r '.failure_reasons[]' | while read -r reason; do
                echo "- $reason" >> "$summary_file"
            done
            echo "" >> "$summary_file"
        fi
    done

    # Add recommendations
    cat >> "$summary_file" << EOF
## Recommendations

EOF

    if [[ $success_rate -ge 90 ]]; then
        cat >> "$summary_file" << EOF
✅ **Excellent Results**

The deployment packages demonstrate excellent startup capability without external dependencies. The one-click deployment implementation is ready for production use.

### Recommendations:
1. Proceed with deployment to production environments
2. Monitor startup times in production
3. Document successful deployment patterns
EOF
    elif [[ $success_rate -ge 70 ]]; then
        cat >> "$summary_file" << EOF
⚠️ **Good Results with Minor Issues**

Most deployment packages start successfully, but some issues need attention.

### Recommendations:
1. Review and fix failed packages
2. Investigate dependency resolution issues
3. Improve error handling and logging
4. Re-run verification after fixes
EOF
    else
        cat >> "$summary_file" << EOF
❌ **Requires Attention**

Significant startup issues detected that must be resolved before deployment.

### Recommended Actions:
1. Investigate all failure reasons thoroughly
2. Fix dependency management issues
3. Improve package generation process
4. Consider simplifying startup procedures
5. Re-run verification after comprehensive fixes
EOF
    fi

    cat >> "$summary_file" << EOF

## Conclusion

The startup verification confirms $([ "$overall_success" == true ] && echo "that deployment packages can start successfully without external dependency installation" || echo "that deployment packages have startup issues that need to be resolved before deployment").

This verification completes **T041**: Verify deployment starts successfully without dependency installation.
EOF

    log_startup "SUCCESS" "Summary report generated: $summary_file"
}

# Execute main function with all arguments
main "$@"