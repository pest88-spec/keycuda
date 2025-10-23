#!/bin/bash

# validate-deployment-portability.sh
# Validates that deployment packages are self-contained and portable across environments

set -euo pipefail

# Script configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
VALIDATION_DIR="$PROJECT_ROOT/deployment-validation"
PACKAGE_DIR="$PROJECT_ROOT/deployment-packages"
LOG_FILE="$VALIDATION_DIR/portability-validation.log"

# Validation configuration
VALIDATION_TYPES=("self-contained" "portability" "offline-capability" "dependency-isolation")
CLEANUP_VALIDATION=true
DEEP_VALIDATION=true

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m'

# Validation results tracking
declare -A VALIDATION_RESULTS
TOTAL_VALIDATIONS=0
PASSED_VALIDATIONS=0
FAILED_VALIDATIONS=0

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
    echo "Validates that deployment packages are self-contained and portable."
    echo ""
    echo "Options:"
    echo "    -t, --types LIST          Comma-separated list of validation types"
    echo "    -d, --deep-validation     Enable deep validation (default: true)"
    echo "    -n, --no-cleanup          Keep validation artifacts"
    echo "    -v, --verbose             Enable verbose logging"
    echo "    -h, --help                Show this help message"
    echo ""
    echo "Validation Types:"
    echo "    self-contained           Verify package contains all required dependencies"
    echo "    portability              Test package portability across environments"
    echo "    offline-capability      Validate offline deployment capability"
    echo "    dependency-isolation    Verify no external dependencies required"
    echo ""
    echo "Examples:"
    echo "    $(basename $0)                                    # Run all validations"
    echo "    $(basename $0) -t self-contained,offline-capability"
}

# Parse command line arguments
parse_arguments() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            -t|--types)
                IFS=',' read -ra VALIDATION_TYPES <<< "$2"
                shift 2
                ;;
            -d|--deep-validation)
                DEEP_VALIDATION=true
                shift
                ;;
            --no-deep-validation)
                DEEP_VALIDATION=false
                shift
                ;;
            -n|--no-cleanup)
                CLEANUP_VALIDATION=false
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

# Initialize validation environment
initialize_validation_environment() {
    log_section "Initializing Deployment Portability Validation Environment"

    # Create validation directory structure
    mkdir -p "$VALIDATION_DIR"
    mkdir -p "$VALIDATION_DIR/test-environments"
    mkdir -p "$VALIDATION_DIR/reports"
    mkdir -p "$VALIDATION_DIR/artifacts"

    # Initialize log file
    {
        echo "Deployment Portability Validation Log"
        echo "Started: $(date)"
        echo "Validation Types: ${VALIDATION_TYPES[*]}"
        echo "Deep Validation: $DEEP_VALIDATION"
        echo "========================================"
    } > "$LOG_FILE"

    log_success "Validation environment initialized"
}

# Find deployment packages to validate
find_deployment_packages() {
    log_section "Finding Deployment Packages to Validate"

    local packages=()

    # Find all .tar.gz packages
    for package in "$PACKAGE_DIR"/*.tar.gz; do
        if [[ -f "$package" ]]; then
            packages+=("$package")
            log_info "Found package: $(basename "$package")"
        fi
    done

    # Also check for uncompressed packages
    for package_dir in "$PACKAGE_DIR"/puzzle71solver-*; do
        if [[ -d "$package_dir" ]]; then
            packages+=("$package_dir")
            log_info "Found uncompressed package: $(basename "$package_dir")"
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

# Validate package is self-contained
validate_self_contained() {
    local package="$1"
    local package_name=$(basename "$package" .tar.gz)
    local test_env_dir="$VALIDATION_DIR/test-environments/self-contained-$package_name"

    log_info "Validating self-contained nature for $package_name"

    local validation_passed=true
    local issues=()

    # Extract package for validation
    mkdir -p "$test_env_dir"
    if [[ "$package" == *.tar.gz ]]; then
        if ! tar -xzf "$package" -C "$test_env_dir" 2>/dev/null; then
            issues+=("Failed to extract package")
            validation_passed=false
        fi
        # Find the extracted directory
        local extracted_dir=$(find "$test_env_dir" -maxdepth 1 -type d ! -path "$test_env_dir" | head -1)
        cd "$extracted_dir" 2>/dev/null || cd "$test_env_dir"
    else
        cp -r "$package" "$test_env_dir/package"
        cd "$test_env_dir/package" 2>/dev/null || cd "$test_env_dir"
    fi

    # Check for essential components
    local essential_components=("bin/Puzzle71Solver" "lib" "include" "share")
    for component in "${essential_components[@]}"; do
        if [[ ! -e "$component" ]]; then
            issues+=("Missing essential component: $component")
            validation_passed=false
        fi
    done

    # Verify all executables are present and executable
    if [[ -d "bin" ]]; then
        while read -r -d '' executable; do
            if [[ ! -x "$executable" ]]; then
                issues+=("Executable not executable: $executable")
                validation_passed=false
            fi
        done < <(find "bin" -type f -print0 2>/dev/null || true)
    fi

    # Check for external dependencies
    if [[ "$DEEP_VALIDATION" == true ]]; then
        log_info "Performing deep validation for external dependencies"

        # Check for network references in scripts
        local network_indicators=("http://" "https://" "ftp://" "git@" "svn://")
        for indicator in "${network_indicators[@]}"; do
            if grep -r "$indicator" . 2>/dev/null; then
                issues+=("Network dependency detected: $indicator")
                validation_passed=false
            fi
        done

        # Check for system package dependencies
        local system_commands=("apt-get" "yum" "dnf" "pacman" "brew" "pkg")
        for cmd in "${system_commands[@]}"; do
            if grep -r "$cmd" . 2>/dev/null; then
                issues+=("System package manager dependency detected: $cmd")
                validation_passed=false
            fi
        done

        # Check for library dependencies that might not be included
        if [[ -f "bin/Puzzle71Solver" ]]; then
            # Use ldd to check dynamic library dependencies (Linux)
            if command -v ldd >/dev/null 2>&1; then
                local missing_libs=$(ldd "bin/Puzzle71Solver" 2>/dev/null | grep "not found" || true)
                if [[ -n "$missing_libs" ]]; then
                    issues+=("Missing system libraries: $missing_libs")
                    validation_passed=false
                fi
            fi
        fi
    fi

    # Verify documentation is self-contained
    if [[ -f "MANIFEST.json" ]]; then
        local external_refs=$(jq -r '.dependencies.external // []' "MANIFEST.json" 2>/dev/null || echo "[]")
        if [[ "$external_refs" != "[]" ]]; then
            issues+=("External dependencies listed in manifest: $external_refs")
            validation_passed=false
        fi
    fi

    # Check for completeness of included libraries
    if [[ -d "lib" ]]; then
        local lib_count=$(find "lib" -name "*.so*" -o -name "*.a" | wc -l)
        if [[ $lib_count -eq 0 ]]; then
            issues+=("No libraries included in package")
            validation_passed=false
        else
            log_info "Package includes $lib_count libraries"
        fi
    fi

    # Verify checksum files are present and valid
    if [[ -f "checksums.sha256" ]]; then
        if sha256sum -c "checksums.sha256" >/dev/null 2>&1; then
            log_success "Package checksums are valid"
        else
            issues+=("Package checksums are invalid")
            validation_passed=false
        fi
    else
        issues+=("Missing checksums.sha256 file")
        validation_passed=false
    fi

    # Record results
    if [[ "$validation_passed" == true ]]; then
        VALIDATION_RESULTS["${package_name}_self_contained"]="PASS"
        log_success "Self-contained validation passed for $package_name"
    else
        VALIDATION_RESULTS["${package_name}_self_contained"]="FAIL"
        for issue in "${issues[@]}"; do
            log_error "Self-contained issue: $issue"
        done
    fi

    cd "$PROJECT_ROOT"
    return $([[ "$validation_passed" == true ]] && echo 0 || echo 1)
}

# Validate package portability
validate_portability() {
    local package="$1"
    local package_name=$(basename "$package" .tar.gz)
    local test_env_dir="$VALIDATION_DIR/test-environments/portability-$package_name"

    log_info "Validating portability for $package_name"

    local validation_passed=true
    local issues=()

    # Extract package for validation
    mkdir -p "$test_env_dir"
    if [[ "$package" == *.tar.gz ]]; then
        tar -xzf "$package" -C "$test_env_dir" 2>/dev/null || validation_passed=false
        local extracted_dir=$(find "$test_env_dir" -maxdepth 1 -type d ! -path "$test_env_dir" | head -1)
        cd "$extracted_dir" 2>/dev/null || cd "$test_env_dir"
    else
        cp -r "$package" "$test_env_dir/package"
        cd "$test_env_dir/package" 2>/dev/null || cd "$test_env_dir"
    fi

    # Check for platform-specific paths
    local platform_specific_patterns=(
        "/usr/local/"
        "/opt/"
        "/home/"
        "~/."
        "/var/lib/"
        "/etc/"
    )

    for pattern in "${platform_specific_patterns[@]}"; do
        if grep -r "$pattern" . 2>/dev/null; then
            issues+=("Platform-specific path detected: $pattern")
            validation_passed=false
        fi
    done

    # Verify installation scripts use relative paths
    for install_script in "install.sh" "setup-env.sh"; do
        if [[ -f "$install_script" ]]; then
            if grep -r "^\s*cd\s*/" "$install_script" 2>/dev/null; then
                issues+=("Absolute path usage in $install_script")
                validation_passed=false
            fi

            if grep -r "\$PWD" "$install_script" 2>/dev/null; then
                log_info "Found portable path usage in $install_script"
            fi
        fi
    done

    # Check for hard-coded environment variables
    local env_vars=("PATH" "LD_LIBRARY_PATH" "HOME" "USER")
    for var in "${env_vars[@]}"; do
        if grep -r "export $var=" . 2>/dev/null; then
            log_info "Found environment variable setup for $var"
        fi
    done

    # Validate package manifest portability information
    if [[ -f "MANIFEST.json" ]]; then
        local platform_support=$(jq -r '.platform // "unknown"' "MANIFEST.json" 2>/dev/null || echo "unknown")
        if [[ "$platform_support" == "all" ]]; then
            log_success "Package supports all platforms"
        elif [[ "$platform_support" != "unknown" ]]; then
            log_info "Package targets platform: $platform_support"
        else
            issues+=("Platform support not specified in manifest")
            validation_passed=false
        fi
    fi

    # Test script syntax across different shell environments
    for script_file in "install.sh" "setup-env.sh" "scripts/"*.sh; do
        if [[ -f "$script_file" ]]; then
            # Test with bash
            if bash -n "$script_file" 2>/dev/null; then
                log_info "Script $script_file has valid bash syntax"
            else
                issues+=("Invalid bash syntax in $script_file")
                validation_passed=false
            fi

            # Test with sh if available
            if command -v sh >/dev/null 2>&1; then
                if sh -n "$script_file" 2>/dev/null; then
                    log_info "Script $script_file has valid sh syntax"
                else
                    log_warning "Script $script_file may have sh compatibility issues"
                fi
            fi
        fi
    done

    # Check for portability of file permissions
    if [[ -d "bin" ]]; then
        while read -r -d '' executable; do
            local perms=$(stat -c "%a" "$executable" 2>/dev/null || echo "755")
            if [[ "$perms" != "755" && "$perms" != "775" && "$perms" != "777" ]]; then
                issues+=("Unexpected executable permissions: $executable ($perms)")
                validation_passed=false
            fi
        done < <(find "bin" -type f -print0 2>/dev/null || true)
    fi

    # Record results
    if [[ "$validation_passed" == true ]]; then
        VALIDATION_RESULTS["${package_name}_portability"]="PASS"
        log_success "Portability validation passed for $package_name"
    else
        VALIDATION_RESULTS["${package_name}_portability"]="FAIL"
        for issue in "${issues[@]}"; do
            log_error "Portability issue: $issue"
        done
    fi

    cd "$PROJECT_ROOT"
    return $([[ "$validation_passed" == true ]] && echo 0 || echo 1)
}

# Validate offline capability
validate_offline_capability() {
    local package="$1"
    local package_name=$(basename "$package" .tar.gz)
    local test_env_dir="$VALIDATION_DIR/test-environments/offline-$package_name"

    log_info "Validating offline capability for $package_name"

    local validation_passed=true
    local issues=()

    # Extract package for validation
    mkdir -p "$test_env_dir"
    if [[ "$package" == *.tar.gz ]]; then
        tar -xzf "$package" -C "$test_env_dir" 2>/dev/null || validation_passed=false
        local extracted_dir=$(find "$test_env_dir" -maxdepth 1 -type d ! -path "$test_env_dir" | head -1)
        cd "$extracted_dir" 2>/dev/null || cd "$test_env_dir"
    else
        cp -r "$package" "$test_env_dir/package"
        cd "$test_env_dir/package" 2>/dev/null || cd "$test_env_dir"
    fi

    # Check for any network-related operations in scripts
    local network_commands=("curl" "wget" "git" "svn" "ftp" "scp" "rsync" "ping" "nslookup")
    local network_files=("install.sh" "setup-env.sh" "scripts/"*.sh "bin/"*)

    for file_pattern in "${network_files[@]}"; do
        for file in $file_pattern; do
            if [[ -f "$file" ]]; then
                for cmd in "${network_commands[@]}"; do
                    if grep -q "$cmd" "$file" 2>/dev/null; then
                        # Check if it's actually being used for network operations
                        if grep -q "$cmd.*http\|$cmd.*https\|$cmd.*git" "$file" 2>/dev/null; then
                            issues+=("Network command found in $file: $cmd")
                            validation_passed=false
                        fi
                    fi
                done
            fi
        done
    done

    # Verify all dependencies are included locally
    if [[ -d "lib" ]]; then
        local local_libs=$(find "lib" -name "*.so*" -o -name "*.a" | wc -l)
        log_info "Found $local_libs local libraries"

        if [[ $local_libs -eq 0 && -f "bin/Puzzle71Solver" ]]; then
            # Check if the binary is statically linked or has no external dependencies
            if command -v ldd >/dev/null 2>&1; then
                local ext_libs=$(ldd "bin/Puzzle71Solver" 2>/dev/null | grep -v "linux-vdso\|ld-linux" | wc -l)
                if [[ $ext_libs -gt 0 ]]; then
                    issues+=("Binary has external dependencies but no local libraries included")
                    validation_passed=false
                fi
            fi
        fi
    fi

    # Test environment setup without network
    if [[ -f "setup-env.sh" ]]; then
        # Create a test environment with no network access
        local test_cmd="(cd . && source ./setup-env.sh >/dev/null 2>&1 && echo 'SUCCESS')"

        # We can't easily simulate network isolation in this script, but we can check
        # that the setup script doesn't require network access
        if grep -q "wget\|curl\|git clone" "setup-env.sh" 2>/dev/null; then
            issues+=("setup-env.sh may require network access")
            validation_passed=false
        else
            log_info "setup-env.sh appears to be network-independent"
        fi
    fi

    # Check documentation for offline requirements
    if [[ -f "README.md" ]] || [[ -f "share/doc/README.md" ]]; then
        local readme_file="README.md"
        if [[ ! -f "$readme_file" ]]; then
            readme_file="share/doc/README.md"
        fi

        if grep -i -q "internet\|network\|online" "$readme_file" 2>/dev/null; then
            log_info "Documentation mentions network/internet requirements - checking context"
            # This is just informational - offline packages can still mention online docs
        fi
    fi

    # Verify that all required components for basic operation are included
    local required_for_offline=("bin/Puzzle71Solver" "checksums.sha256" "MANIFEST.json")
    for component in "${required_for_offline[@]}"; do
        if [[ ! -f "$component" ]]; then
            issues+=("Missing component required for offline operation: $component")
            validation_passed=false
        fi
    done

    # Check for offline configuration options
    if [[ -f "MANIFEST.json" ]]; then
        local offline_support=$(jq -r '.features.offline_deployment // false' "MANIFEST.json" 2>/dev/null || echo "false")
        if [[ "$offline_support" == "true" ]]; then
            log_success "Package explicitly supports offline deployment"
        else
            log_warning "Package offline support not explicitly documented"
        fi
    fi

    # Record results
    if [[ "$validation_passed" == true ]]; then
        VALIDATION_RESULTS["${package_name}_offline_capability"]="PASS"
        log_success "Offline capability validation passed for $package_name"
    else
        VALIDATION_RESULTS["${package_name}_offline_capability"]="FAIL"
        for issue in "${issues[@]}"; do
            log_error "Offline capability issue: $issue"
        done
    fi

    cd "$PROJECT_ROOT"
    return $([[ "$validation_passed" == true ]] && echo 0 || echo 1)
}

# Validate dependency isolation
validate_dependency_isolation() {
    local package="$1"
    local package_name=$(basename "$package" .tar.gz)
    local test_env_dir="$VALIDATION_DIR/test-environments/isolation-$package_name"

    log_info "Validating dependency isolation for $package_name"

    local validation_passed=true
    local issues=()

    # Extract package for validation
    mkdir -p "$test_env_dir"
    if [[ "$package" == *.tar.gz ]]; then
        tar -xzf "$package" -C "$test_env_dir" 2>/dev/null || validation_passed=false
        local extracted_dir=$(find "$test_env_dir" -maxdepth 1 -type d ! -path "$test_env_dir" | head -1)
        cd "$extracted_dir" 2>/dev/null || cd "$test_env_dir"
    else
        cp -r "$package" "$test_env_dir/package"
        cd "$test_env_dir/package" 2>/dev/null || cd "$test_env_dir"
    fi

    # Check that all libraries are included in the package
    if [[ -f "bin/Puzzle71Solver" ]]; then
        if command -v ldd >/dev/null 2>&1; then
            # Get list of required libraries
            local required_libs=$(ldd "bin/Puzzle71Solver" 2>/dev/null | grep -E "=>\s*/" | awk '{print $3}' || true)

            if [[ -n "$required_libs" ]]; then
                log_info "Checking if required libraries are included in package"

                for lib in $required_libs; do
                    local lib_name=$(basename "$lib")
                    local found_in_package=false

                    # Check if library is included in package lib directory
                    if [[ -d "lib" ]]; then
                        if find "lib" -name "$lib_name" -o -name "*${lib_name%.*}*" | grep -q .; then
                            found_in_package=true
                        fi
                    fi

                    # Allow system libraries that are typically always available
                    local system_libs=("libc.so" "libm.so" "libpthread.so" "libdl.so" "libz.so" "librt.so")
                    local is_system_lib=false
                    for sys_lib in "${system_libs[@]}"; do
                        if [[ "$lib_name" == *"$sys_lib"* ]]; then
                            is_system_lib=true
                            break
                        fi
                    done

                    if [[ "$found_in_package" == false && "$is_system_lib" == false ]]; then
                        issues+=("Required library not included: $lib_name ($lib)")
                        validation_passed=false
                    fi
                done
            fi
        fi
    fi

    # Check for any references to external dependencies
    local external_patterns=(
        "require.*'"
        "import.*from.*http"
        "load.*http"
        "fetch.*http"
        "download.*http"
    )

    for pattern in "${external_patterns[@]}"; do
        if grep -r "$pattern" . 2>/dev/null; then
            issues+=("External dependency pattern detected: $pattern")
            validation_passed=false
        fi
    done

    # Verify package provides its own dependencies
    if [[ -d "lib" ]]; then
        local provided_libs=$(find "lib" -name "*.so*" -o -name "*.a" | wc -l)
        log_info "Package provides $provided_libs libraries"

        if [[ -f "lib/pkgconfig/puzzle71.pc" ]]; then
            log_info "Package provides pkg-config file"
        fi

        if [[ -f "lib/cmake/Puzzle71Config.cmake" ]]; then
            log_info "Package provides CMake config file"
        fi
    else
        if [[ -f "bin/Puzzle71Solver" ]]; then
            # If there's an executable but no lib directory, check if it's statically linked
            if command -v file >/dev/null 2>&1; then
                local file_type=$(file "bin/Puzzle71Solver" 2>/dev/null || echo "unknown")
                if [[ "$file_type" == *"statically linked"* ]]; then
                    log_info "Binary is statically linked - no external libraries required"
                else
                    issues+=("No libraries directory found and binary is not statically linked")
                    validation_passed=false
                fi
            fi
        fi
    fi

    # Check for version conflicts in included libraries
    if [[ -d "lib" ]]; then
        local lib_versions=$(find "lib" -name "*.so*" | sed 's/.*\.so\.\([0-9.]*\).*/\1/' | sort -u | wc -l)
        if [[ $lib_versions -gt 1 ]]; then
            log_warning "Multiple library versions found - potential for conflicts"
        fi
    fi

    # Validate dependency manifests
    if [[ -f "MANIFEST.json" ]]; then
        local external_deps=$(jq -r '.dependencies.external // []' "MANIFEST.json" 2>/dev/null || echo "[]")
        if [[ "$external_deps" != "[]" ]]; then
            issues+=("External dependencies documented in manifest: $external_deps")
            validation_passed=false
        fi

        local integrated_libs=$(jq -r '.dependencies.integrated // []' "MANIFEST.json" 2>/dev/null || echo "[]")
        if [[ "$integrated_libs" != "[]" ]]; then
            log_success "Integrated dependencies documented: $integrated_libs"
        fi
    fi

    # Record results
    if [[ "$validation_passed" == true ]]; then
        VALIDATION_RESULTS["${package_name}_dependency_isolation"]="PASS"
        log_success "Dependency isolation validation passed for $package_name"
    else
        VALIDATION_RESULTS["${package_name}_dependency_isolation"]="FAIL"
        for issue in "${issues[@]}"; do
            log_error "Dependency isolation issue: $issue"
        done
    fi

    cd "$PROJECT_ROOT"
    return $([[ "$validation_passed" == true ]] && echo 0 || echo 1)
}

# Run validation for a specific type
run_validation_type() {
    local package="$1"
    local validation_type="$2"
    local package_name=$(basename "$package" .tar.gz)

    log_info "Running $validation_type validation for $package_name"

    case "$validation_type" in
        "self-contained")
            validate_self_contained "$package"
            ;;
        "portability")
            validate_portability "$package"
            ;;
        "offline-capability")
            validate_offline_capability "$package"
            ;;
        "dependency-isolation")
            validate_dependency_isolation "$package"
            ;;
        *)
            log_error "Unknown validation type: $validation_type"
            return 1
            ;;
    esac
}

# Generate comprehensive validation report
generate_validation_report() {
    local packages=("$@")

    log_section "Generating Comprehensive Portability Validation Report"

    local report_file="$VALIDATION_DIR/reports/portability-validation-report.json"

    cat > "$report_file" << EOF
{
    "validation_summary": {
        "validation_date": "$(date -Iseconds)",
        "total_packages": ${#packages[@]},
        "total_validations": $TOTAL_VALIDATIONS,
        "validations_passed": $PASSED_VALIDATIONS,
        "validations_failed": $FAILED_VALIDATIONS,
        "success_rate": "$(echo "scale=2; $PASSED_VALIDATIONS * 100 / $TOTAL_VALIDATIONS" | bc -l)%",
        "overall_result": "$([[ $FAILED_VALIDATIONS -eq 0 ]] && echo "PASS" || echo "FAIL")"
    },
    "validation_configuration": {
        "validation_types": [$(printf '"%s",' "${VALIDATION_TYPES[@]}" | sed 's/,$//')],
        "deep_validation": $DEEP_VALIDATION,
        "cleanup_enabled": $CLEANUP_VALIDATION
    },
    "package_results": {
EOF

    # Add package results
    local first_package=true
    for package in "${packages[@]}"; do
        if [[ "$first_package" == false ]]; then
            echo "," >> "$report_file"
        fi
        first_package=false

        local package_name=$(basename "$package" .tar.gz)

        cat >> "$report_file" << EOF
        "$package_name": {
EOF

        local first_validation=true
        for validation_type in "${VALIDATION_TYPES[@]}"; do
            if [[ "$first_validation" == false ]]; then
                echo "," >> "$report_file"
            fi
            first_validation=false

            local result="${VALIDATION_RESULTS[${package_name}_${validation_type}]:-NOT_RUN}"
            echo "            \"$validation_type\": \"$result\"" >> "$report_file"
        done

        cat >> "$report_file" << EOF
        }
EOF
    done

    cat >> "$report_file" << EOF
    },
    "recommendations": [
EOF

    # Add recommendations
    local recommendations=()

    if [[ $FAILED_VALIDATIONS -gt 0 ]]; then
        recommendations+=("Some validations failed - review validation logs and fix issues")
    fi

    if [[ "$DEEP_VALIDATION" == false ]]; then
        recommendations+=("Consider enabling deep validation for more comprehensive checks")
    fi

    recommendations+=("Regular portability validation recommended for consistent deployment behavior")
    recommendations+=("Test packages in target environments before production deployment")

    local first_rec=true
    for rec in "${recommendations[@]}"; do
        if [[ "$first_rec" == false ]]; then
            echo "," >> "$report_file"
        fi
        first_rec=false
        echo "        \"$rec\"" >> "$report_file"
    done

    cat >> "$report_file" << EOF
    ],
    "validation_artifacts": {
        "log_file": "$LOG_FILE",
        "test_environments": "$VALIDATION_DIR/test-environments"
    }
}
EOF

    log_success "Validation report generated: $report_file"
}

# Display validation summary
display_validation_summary() {
    log_section "Deployment Portability Validation Summary"

    echo
    echo "Validation Configuration:"
    echo "  Validation Types: ${#VALIDATION_TYPES[@]} (${VALIDATION_TYPES[*]})"
    echo "  Deep Validation: $DEEP_VALIDATION"
    echo

    echo "Validation Results:"
    echo "  Total Validations: $TOTAL_VALIDATIONS"
    echo "  Passed: $PASSED_VALIDATIONS"
    echo "  Failed: $FAILED_VALIDATIONS"
    echo "  Success Rate: $(echo "scale=1; $PASSED_VALIDATIONS * 100 / $TOTAL_VALIDATIONS" | bc -l)%"
    echo

    echo "Validation Breakdown:"
    for validation_type in "${VALIDATION_TYPES[@]}"; do
        local type_passed=0
        local type_total=0

        for package in "${PACKAGES_TESTED[@]}"; do
            local package_name=$(basename "$package" .tar.gz)
            ((type_total++))
            if [[ "${VALIDATION_RESULTS[${package_name}_${validation_type}]:-NOT_RUN}" == "PASS" ]]; then
                ((type_passed++))
            fi
        done

        local status="$([[ $type_passed -eq $type_total ]] && echo "✅ PASS" || echo "❌ FAIL")"
        echo "  $validation_type: $type_passed/$type_total packages passed $status"
    done

    echo
    if [[ $FAILED_VALIDATIONS -eq 0 ]]; then
        log_success "All portability validations passed! 🎉"
        echo "Deployment packages are validated as self-contained and portable."
    else
        log_error "Some portability validations failed."
        echo "Please review the detailed reports for specific issues."
    fi

    echo
    echo "Detailed Reports:"
    echo "  Validation Report: $VALIDATION_DIR/reports/portability-validation-report.json"
    echo "  Validation Log: $LOG_FILE"
    echo
}

# Main execution function
main() {
    # Parse command line arguments
    parse_arguments "$@"

    # Initialize validation environment
    initialize_validation_environment

    # Find deployment packages to validate
    local packages=()
    while IFS= read -r package; do
        packages+=("$package")
    done < <(find_deployment_packages)

    # Store packages for summary
    PACKAGES_TESTED=("${packages[@]}")

    # Run validations for each package
    for package in "${packages[@]}"; do
        log_section "Validating Package: $(basename "$package")"

        for validation_type in "${VALIDATION_TYPES[@]}"; do
            ((TOTAL_VALIDATIONS++))
            if run_validation_type "$package" "$validation_type"; then
                ((PASSED_VALIDATIONS++))
            else
                ((FAILED_VALIDATIONS++))
            fi
        done
    done

    # Generate validation report
    generate_validation_report "${packages[@]}"

    # Display validation summary
    display_validation_summary

    # Exit with appropriate code
    if [[ $FAILED_VALIDATIONS -eq 0 ]]; then
        log_success "Deployment portability validation completed successfully"
        exit 0
    else
        log_error "Deployment portability validation completed with failures"
        exit 1
    fi
}

# Execute main function if script is run directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi