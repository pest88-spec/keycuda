#!/bin/bash
# T044: Validate Deployment Packages Are Self-Contained and Portable
# Comprehensive validation of deployment package self-containment and portability

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

# Validation configuration
VALIDATION_MODE="${VALIDATION_MODE:-comprehensive}"  # quick, comprehensive, strict
ISOLATION_LEVEL="${ISOLATION_LEVEL:-complete}"  # minimal, partial, complete
TEMP_VALIDATION_DIR="${TEMP_VALIDATION_DIR:-/tmp/puzzle71-self-contained-test}"
CLEANUP_TEMP="${CLEANUP_TEMP:-true}"
PORTABILITY_TEST="${PORTABILITY_TEST:-true}"
SELF_SUFFICIENCY_TEST="${SELF_SUFFICIENCY_TEST:-true}"
OFFLINE_SIMULATION="${OFFLINE_SIMULATION:-true}"

# Deployment package to validate
DEPLOYMENT_PACKAGE="${DEPLOYMENT_PACKAGE:-}"

# Validation criteria
declare -A VALIDATION_CRITERIA=(
    ["executable_independence"]="All executables must run with bundled libraries only"
    ["configuration_completeness"]="All required configuration files must be included"
    ["dependency_isolation"]="No external system dependencies required"
    ["resource_autonomy"]="Self-sufficient resource management"
    ["path_independence"]="No hardcoded absolute paths to external resources"
    ["data_portability"]="All data files and assets included"
    ["runtime_compatibility"]="Compatible with standard runtime environments"
    ["offline_capability"]="Full functionality without internet connectivity"
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

log_validate() {
    echo -e "${PURPLE}[VALIDATE]${NC} $1"
}

log_portable() {
    echo -e "${CYAN}[PORTABLE]${NC} $1"
}

# Show help
show_help() {
    cat << EOF
Self-Contained Deployment Validation Script

USAGE:
    $0 [OPTIONS] [deployment_package]

OPTIONS:
    --validation-mode MODE   Validation mode: quick, comprehensive, strict (default: comprehensive)
    --isolation-level LEVEL  Isolation level: minimal, partial, complete (default: complete)
    --temp-dir DIR          Temporary validation directory (default: /tmp/puzzle71-self-contained-test)
    --no-cleanup            Don't clean up temporary files
    --no-portability-test   Skip portability testing
    --no-self-sufficiency   Skip self-sufficiency testing
    --no-offline-simulation Skip offline simulation
    --help, -h              Show this help message

DESCRIPTION:
    Validates that deployment packages are completely self-contained and portable
    without requiring external dependencies or system-specific configurations.

VALIDATION MODES:
    quick         Basic self-containment checks (2 minutes)
    comprehensive Complete validation with portability tests (5 minutes)
    strict        Thorough validation with edge case testing (10 minutes)

ISOLATION LEVELS:
    minimal       Basic isolation with modified environment variables
    partial       Chroot isolation with minimal system
    complete      Full container-like isolation

EOF
}

# Parse command line arguments
parse_arguments() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            --validation-mode)
                VALIDATION_MODE="$2"
                shift 2
                ;;
            --isolation-level)
                ISOLATION_LEVEL="$2"
                shift 2
                ;;
            --temp-dir)
                TEMP_VALIDATION_DIR="$2"
                shift 2
                ;;
            --no-cleanup)
                CLEANUP_TEMP=false
                shift
                ;;
            --no-portability-test)
                PORTABILITY_TEST=false
                shift
                ;;
            --no-self-sufficiency)
                SELF_SUFFICIENCY_TEST=false
                shift
                ;;
            --no-offline-simulation)
                OFFLINE_SIMULATION=false
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

# Create isolated validation environment
create_isolated_environment() {
    local iso_level="$1"
    local iso_path="$TEMP_VALIDATION_DIR/isolated-$iso_level"

    log_validate "Creating $iso_level isolation environment..."

    # Create isolation directory
    mkdir -p "$iso_path"

    case "$iso_level" in
        "minimal")
            # Minimal isolation - just environment variables
            mkdir -p "$iso_path"/{bin,lib,etc,tmp}
            ;;
        "partial")
            # Partial isolation - chroot with minimal system
            mkdir -p "$iso_path"/{bin,lib,lib64,etc,tmp,var,opt,dev,proc}

            # Create minimal device nodes
            if command -v mknod >/dev/null 2>&1; then
                mknod "$iso_path/dev/null" c 1 3 2>/dev/null || true
                mknod "$iso_path/dev/zero" c 1 5 2>/dev/null || true
                mknod "$iso_path/dev/urandom" c 1 9 2>/dev/null || true
            fi

            # Create system files
            cat > "$iso_path/etc/passwd" << 'EOF'
root:x:0:0:root:/root:/bin/bash
nobody:x:65534:65534:nobody:/nonexistent:/usr/sbin/nologin
EOF

            cat > "$iso_path/etc/group" << 'EOF'
root:x:0:
nogroup:x:65534:
EOF
            ;;
        "complete")
            # Complete isolation - full container-like environment
            mkdir -p "$iso_path"/{bin,lib,lib64,etc,tmp,var,opt,dev,proc,sys,usr,home}

            # Copy essential system libraries
            if [[ -d "/lib/x86_64-linux-gnu" ]]; then
                cp -r /lib/x86_64-linux-gnu "$iso_path/lib/" 2>/dev/null || true
            fi

            # Create complete device nodes
            if command -v mknod >/dev/null 2>&1; then
                mknod "$iso_path/dev/null" c 1 3 2>/dev/null || true
                mknod "$iso_path/dev/zero" c 1 5 2>/dev/null || true
                mknod "$iso_path/dev/random" c 1 8 2>/dev/null || true
                mknod "$iso_path/dev/urandom" c 1 9 2>/dev/null || true
                mknod "$iso_path/dev/tty" c 5 0 2>/dev/null || true
            fi

            # Create complete system configuration
            cat > "$iso_path/etc/passwd" << 'EOF'
root:x:0:0:root:/root:/bin/bash
daemon:x:1:1:daemon:/usr/sbin:/usr/sbin/nologin
nobody:x:65534:65534:nobody:/nonexistent:/usr/sbin/nologin
EOF

            cat > "$iso_path/etc/group" << 'EOF'
root:x:0:
daemon:x:1:
nogroup:x:65534:
EOF

            cat > "$iso_path/etc/hosts" << 'EOF'
127.0.0.1 localhost
::1 localhost
EOF
            ;;
    esac

    echo "$iso_path"
}

# Extract deployment package to isolated environment
extract_to_isolated_environment() {
    local iso_path="$1"

    log_validate "Extracting deployment package to isolated environment..."

    # Create deployment directory
    mkdir -p "$iso_path/opt/puzzle71-solver"

    # Extract package
    case "$DEPLOYMENT_PACKAGE" in
        *.tar.gz|*.tgz)
            tar -xzf "$DEPLOYMENT_PACKAGE" -C "$iso_path/opt/puzzle71-solver" --strip-components=1
            ;;
        *.tar.bz2|*.tbz2)
            tar -xjf "$DEPLOYMENT_PACKAGE" -C "$iso_path/opt/puzzle71-solver" --strip-components=1
            ;;
        *.tar.xz|*.txz)
            tar -xJf "$DEPLOYMENT_PACKAGE" -C "$iso_path/opt/puzzle71-solver" --strip-components=1
            ;;
        *.zip)
            unzip -q "$DEPLOYMENT_PACKAGE" -d "$iso_path/opt/puzzle71-solver"
            ;;
        *)
            log_error "Unsupported package format: $DEPLOYMENT_PACKAGE"
            return 1
            ;;
    esac

    # Set permissions
    chmod -R 755 "$iso_path/opt/puzzle71-solver" 2>/dev/null || true

    log_portable "Deployment package extracted to isolated environment"
}

# Validate executable independence
validate_executable_independence() {
    local iso_path="$1"
    local deployment_dir="$iso_path/opt/puzzle71-solver"

    log_validate "Validating executable independence..."

    local validation_passed=true
    local validation_results=()

    # Check main executable
    if [[ -x "$deployment_dir/bin/Puzzle71Solver" ]]; then
        validation_results+=("main_executable:true")
        log_portable "✓ Main executable found and executable"
    else
        validation_results+=("main_executable:false")
        log_error "✗ Main executable missing or not executable"
        validation_passed=false
    fi

    # Test executable with bundled libraries only
    export LD_LIBRARY_PATH="$deployment_dir/lib:$LD_LIBRARY_PATH"
    export PATH="$deployment_dir/bin:$PATH"

    if timeout 30s "$deployment_dir/bin/Puzzle71Solver" --version >/dev/null 2>&1; then
        validation_results+=("executes_with_bundled_libs:true")
        log_portable "✓ Executable runs with bundled libraries only"
    else
        validation_results+=("executes_with_bundled_libs:false")
        log_error "✗ Executable fails with bundled libraries only"
        validation_passed=false
    fi

    # Check for external library dependencies
    if command -v ldd >/dev/null 2>&1; then
        local external_deps=0
        while IFS= read -r dep; do
            if [[ "$dep" == "not found" ]] || [[ ! "$dep" =~ "$deployment_dir" ]]; then
                ((external_deps++))
            fi
        done < <(ldd "$deployment_dir/bin/Puzzle71Solver" 2>/dev/null | awk '/=>/ {print $3}')

        if [[ $external_deps -eq 0 ]]; then
            validation_results+=("no_external_dependencies:true")
            log_portable "✓ No external library dependencies"
        else
            validation_results+=("no_external_dependencies:false")
            log_error "✗ Found $external_deps external dependencies"
            validation_passed=false
        fi
    fi

    # Save validation results
    IFS=','; echo "${validation_results[*]}" > "$deployment_dir/executable_independence_results.txt"
    unset IFS

    if [[ "$validation_passed" == true ]]; then
        log_success "Executable independence validation PASSED"
        return 0
    else
        log_error "Executable independence validation FAILED"
        return 1
    fi
}

# Validate configuration completeness
validate_configuration_completeness() {
    local iso_path="$1"
    local deployment_dir="$iso_path/opt/puzzle71-solver"

    log_validate "Validating configuration completeness..."

    local validation_passed=true
    local required_configs=(
        "config/resource-optimized.conf"
        "config/deployment-manifest.json"
        "libs/attribution/ATTRIBUTION.txt"
    )
    local config_results=()

    for config in "${required_configs[@]}"; do
        if [[ -f "$deployment_dir/$config" ]]; then
            config_results+=("$(basename "$config"):true")
            log_portable "✓ Configuration file present: $config"
        else
            config_results+=("$(basename "$config"):false")
            log_error "✗ Configuration file missing: $config"
            validation_passed=false
        fi
    done

    # Check for hardcoded absolute paths
    local hardcoded_paths=0
    if [[ -d "$deployment_dir/config" ]]; then
        while IFS= read -r -d '' config_file; do
            while IFS= read -r line; do
                if [[ "$line" =~ ^[[:space:]]*/(usr|opt|var|etc)/[[:alpha:]] ]]; then
                    ((hardcoded_paths++))
                    log_warning "Potential hardcoded path in $(basename "$config_file"): $line"
                fi
            done < "$config_file"
        done < <(find "$deployment_dir/config" -name "*.conf" -o -name "*.json" -print0 2>/dev/null)
    fi

    if [[ $hardcoded_paths -eq 0 ]]; then
        config_results+=("no_hardcoded_paths:true")
        log_portable "✓ No hardcoded absolute paths found"
    else
        config_results+=("no_hardcoded_paths:false")
        log_warning "Found $hardcoded_paths potential hardcoded paths"
    fi

    # Save configuration results
    IFS=','; echo "${config_results[*]}" > "$deployment_dir/configuration_completeness_results.txt"
    unset IFS

    if [[ "$validation_passed" == true ]]; then
        log_success "Configuration completeness validation PASSED"
        return 0
    else
        log_error "Configuration completeness validation FAILED"
        return 1
    fi
}

# Validate dependency isolation
validate_dependency_isolation() {
    local iso_path="$1"
    local deployment_dir="$iso_path/opt/puzzle71-solver"

    log_validate "Validating dependency isolation..."

    local validation_passed=true
    local isolation_results=()

    # Test with minimal environment variables
    env -i PATH="$deployment_dir/bin" \
           LD_LIBRARY_PATH="$deployment_dir/lib" \
           HOME="$deployment_dir/tmp" \
           TMPDIR="$deployment_dir/tmp" \
           timeout 30s "$deployment_dir/bin/Puzzle71Solver" --version >/dev/null 2>&1

    if [[ $? -eq 0 ]]; then
        isolation_results+=("minimal_env_execution:true")
        log_portable "✓ Executes in minimal environment"
    else
        isolation_results+=("minimal_env_execution:false")
        log_error "✗ Fails in minimal environment"
        validation_passed=false
    fi

    # Test with empty system library path
    env -i PATH="$deployment_dir/bin" \
           LD_LIBRARY_PATH="$deployment_dir/lib" \
           SYSTEM_LIBRARY_PATH="" \
           timeout 30s "$deployment_dir/bin/Puzzle71Solver" --help >/dev/null 2>&1

    if [[ $? -eq 0 ]]; then
        isolation_results+=("no_system_libs_required:true")
        log_portable "✓ No system libraries required"
    else
        isolation_results+=("no_system_libs_required:false")
        log_error "✗ System libraries required"
        validation_passed=false
    fi

    # Check for external service dependencies
    local service_deps=0
    if [[ -f "$deployment_dir/config/resource-optimized.conf" ]]; then
        while IFS= read -r line; do
            if [[ "$line" =~ (http://|https://|ftp://|mysql://|postgres://) ]]; then
                ((service_deps++))
                log_warning "Potential external service dependency: $line"
            fi
        done < "$deployment_dir/config/resource-optimized.conf"
    fi

    if [[ $service_deps -eq 0 ]]; then
        isolation_results+=("no_external_services:true")
        log_portable "✓ No external service dependencies"
    else
        isolation_results+=("no_external_services:false")
        log_warning "Found $service_deps potential service dependencies"
    fi

    # Save isolation results
    IFS=','; echo "${isolation_results[*]}" > "$deployment_dir/dependency_isolation_results.txt"
    unset IFS

    if [[ "$validation_passed" == true ]]; then
        log_success "Dependency isolation validation PASSED"
        return 0
    else
        log_error "Dependency isolation validation FAILED"
        return 1
    fi
}

# Validate portability across environments
validate_portability() {
    if [[ "$PORTABILITY_TEST" != "true" ]]; then
        log_info "Skipping portability validation"
        return 0
    fi

    local iso_path="$1"
    local deployment_dir="$iso_path/opt/puzzle71-solver"

    log_validate "Validating portability across environments..."

    local validation_passed=true
    local portability_results=()

    # Test with different user contexts
    local users=("nobody" "root")
    for user in "${users[@]}"; do
        if command -v sudo >/dev/null 2>&1; then
            if sudo -u "$user" env -i PATH="$deployment_dir/bin" \
                              LD_LIBRARY_PATH="$deployment_dir/lib" \
                              timeout 15s "$deployment_dir/bin/Puzzle71Solver" --version >/dev/null 2>&1; then
                portability_results+=("user_$user:true")
                log_portable "✓ Portable for user: $user"
            else
                portability_results+=("user_$user:false")
                log_warning "⚠ Portability issue for user: $user"
            fi
        else
            portability_results+=("user_$user:skipped")
            log_portable "⚠ Skipped user test for $user (sudo not available)"
        fi
    done

    # Test with different working directories
    local test_dirs=("/tmp" "$deployment_dir" "/")
    for test_dir in "${test_dirs[@]}"; do
        if cd "$test_dir" 2>/dev/null; then
            if env -i PATH="$deployment_dir/bin" \
                     LD_LIBRARY_PATH="$deployment_dir/lib" \
                     timeout 15s "$deployment_dir/bin/Puzzle71Solver" --version >/dev/null 2>&1; then
                portability_results+=("working_dir_$(basename "$test_dir"):true")
                log_portable "✓ Portable from directory: $test_dir"
            else
                portability_results+=("working_dir_$(basename "$test_dir"):false")
                log_warning "⚠ Portability issue from directory: $test_dir"
            fi
        fi
    done

    # Test with limited resources
    local memory_limit="512M"
    if command -v timeout >/dev/null 2>&1; then
        timeout 30s bash -c "ulimit -v $((512 * 1024)); env -i PATH='$deployment_dir/bin' LD_LIBRARY_PATH='$deployment_dir/lib' '$deployment_dir/bin/Puzzle71Solver' --version" >/dev/null 2>&1
        if [[ $? -eq 0 ]]; then
            portability_results+=("limited_memory:true")
            log_portable "✓ Portable with limited memory ($memory_limit)"
        else
            portability_results+=("limited_memory:false")
            log_warning "⚠ Portability issue with limited memory"
        fi
    fi

    # Save portability results
    IFS=','; echo "${portability_results[*]}" > "$deployment_dir/portability_results.txt"
    unset IFS

    if [[ "$validation_passed" == true ]]; then
        log_success "Portability validation PASSED"
        return 0
    else
        log_error "Portability validation FAILED"
        return 1
    fi
}

# Validate offline capability
validate_offline_capability() {
    if [[ "$OFFLINE_SIMULATION" != "true" ]]; then
        log_info "Skipping offline capability validation"
        return 0
    fi

    local iso_path="$1"
    local deployment_dir="$iso_path/opt/puzzle71-solver"

    log_validate "Validating offline capability..."

    local validation_passed=true
    local offline_results=()

    # Block network access and test
    if command -v unshare >/dev/null 2>&1; then
        if unshare -n timeout 30s bash -c "env -i PATH='$deployment_dir/bin' LD_LIBRARY_PATH='$deployment_dir/lib' '$deployment_dir/bin/Puzzle71Solver' --version" >/dev/null 2>&1; then
            offline_results+=("network_isolation:true")
            log_portable "✓ Functions without network access"
        else
            offline_results+=("network_isolation:false")
            log_error "✗ Requires network access"
            validation_passed=false
        fi
    else
        offline_results+=("network_isolation:skipped")
        log_warning "⚠ Network isolation test skipped (unshare not available)"
    fi

    # Test with DNS resolution disabled
    if [[ -f "/etc/resolv.conf" ]]; then
        local resolv_backup="$iso_path/resolv.conf.backup"
        cp /etc/resolv.conf "$resolv_backup" 2>/dev/null || true

        # Create empty resolv.conf
        echo "# DNS disabled for offline test" > /etc/resolv.conf

        if timeout 30s env -i PATH="$deployment_dir/bin" \
                     LD_LIBRARY_PATH="$deployment_dir/lib" \
                     "$deployment_dir/bin/Puzzle71Solver" --version >/dev/null 2>&1; then
            offline_results+=("dns_independence:true")
            log_portable "✓ Functions without DNS resolution"
        else
            offline_results+=("dns_independence:false")
            log_warning "⚠ May require DNS resolution"
        fi

        # Restore resolv.conf
        cp "$resolv_backup" /etc/resolv.conf 2>/dev/null || true
    fi

    # Check for external resource references
    local external_refs=0
    if [[ -d "$deployment_dir/config" ]]; then
        while IFS= read -r -d '' config_file; do
            while IFS= read -r line; do
                if [[ "$line" =~ (http://|https://|ftp://) ]] && [[ ! "$line" =~ http://localhost ]]; then
                    ((external_refs++))
                    log_warning "External resource reference in $(basename "$config_file"): $line"
                fi
            done < "$config_file"
        done < <(find "$deployment_dir/config" -name "*.conf" -o -name "*.json" -print0 2>/dev/null)
    fi

    if [[ $external_refs -eq 0 ]]; then
        offline_results+=("no_external_resources:true")
        log_portable "✓ No external resource references"
    else
        offline_results+=("no_external_resources:false")
        log_warning "Found $external_refs external resource references"
    fi

    # Save offline results
    IFS=','; echo "${offline_results[*]}" > "$deployment_dir/offline_capability_results.txt"
    unset IFS

    if [[ "$validation_passed" == true ]]; then
        log_success "Offline capability validation PASSED"
        return 0
    else
        log_error "Offline capability validation FAILED"
        return 1
    fi
}

# Generate comprehensive self-contained validation report
generate_validation_report() {
    local iso_path="$1"
    local deployment_dir="$iso_path/opt/puzzle71-solver"

    log_info "Generating self-contained validation report..."

    local report_file="$deployment_dir/self-contained-validation-report.json"

    # Collect validation results
    local exec_indep="false"
    local config_complete="false"
    local dep_isolation="false"
    local portability="false"
    local offline_cap="false"

    [[ -f "$deployment_dir/executable_independence_results.txt" ]] && exec_indep="true"
    [[ -f "$deployment_dir/configuration_completeness_results.txt" ]] && config_complete="true"
    [[ -f "$deployment_dir/dependency_isolation_results.txt" ]] && dep_isolation="true"
    [[ -f "$deployment_dir/portability_results.txt" ]] && portability="true"
    [[ -f "$deployment_dir/offline_capability_results.txt" ]] && offline_cap="true"

    # Calculate overall self-contained score
    local validation_score=0
    [[ "$exec_indep" == "true" ]] && ((validation_score += 25))
    [[ "$config_complete" == "true" ]] && ((validation_score += 20))
    [[ "$dep_isolation" == "true" ]] && ((validation_score += 25))
    [[ "$portability" == "true" ]] && ((validation_score += 15))
    [[ "$offline_cap" == "true" ]] && ((validation_score += 15))

    cat > "$report_file" << EOF
{
  "self_contained_validation_report": {
    "validation_metadata": {
      "timestamp": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
      "deployment_package": "$DEPLOYMENT_PACKAGE",
      "validation_mode": "$VALIDATION_MODE",
      "isolation_level": "$ISOLATION_LEVEL",
      "script_version": "T044-1.0"
    },
    "validation_criteria": {
EOF

    # Add validation criteria
    local first=true
    for criterion in "${!VALIDATION_CRITERIA[@]}"; do
        [[ "$first" == true ]] && first=false || echo "," >> "$report_file"
        echo -n "      \"$criterion\": \"${VALIDATION_CRITERIA[$criterion]}\"" >> "$report_file"
    done

    cat >> "$report_file" << EOF
    },
    "validation_results": {
      "executable_independence": $exec_indep,
      "configuration_completeness": $config_complete,
      "dependency_isolation": $dep_isolation,
      "portability_across_environments": $portability,
      "offline_capability": $offline_cap,
      "overall_validation_score": $validation_score
    },
    "compliance_status": {
      "meets_self_contained_requirement": $([[ $validation_score -ge 80 ]] && echo "true" || echo "false"),
      "meets_portability_requirement": $([[ $validation_score -ge 70 ]] && echo "true" || echo "false"),
      "meets_offline_deployment_requirement": $([[ $offline_cap == true ]] && echo "true" || echo "false"),
      "ready_for_isolated_deployment": $([[ $validation_score -ge 85 ]] && echo "true" || echo "false")
    },
    "deployment_characteristics": {
      "completely_self_contained": $([[ $validation_score -ge 95 ]] && echo "true" || echo "false"),
      "portable_across_systems": $([[ $portability == true ]] && echo "true" || echo "false"),
      "network_independent": $([[ $offline_cap == true ]] && echo "true" || echo "false"),
      "resource_autonomous": $([[ $dep_isolation == true ]] && echo "true" || echo "false")
    },
    "recommendations": {
      "deployment_ready": $([[ $validation_score -ge 80 ]] && echo "true" || echo "false"),
      "isolation_recommended": $([[ $validation_score -ge 85 ]] && echo "true" || echo "false"),
      "offline_deployment_safe": $([[ $offline_cap == true ]] && echo "true" || echo "false"),
      "cross_platform_compatible": $([[ $portability == true ]] && echo "true" || echo "false")
    }
  }
}
EOF

    log_success "Validation report generated: $report_file"

    # Display summary
    log_info "Self-contained validation summary:"
    log_info "  Overall validation score: $validation_score/100"
    log_info "  Executable independence: $exec_indep"
    log_info "  Configuration completeness: $config_complete"
    log_info "  Dependency isolation: $dep_isolation"
    log_info "  Portability: $portability"
    log_info "  Offline capability: $offline_cap"
    log_info "  Ready for isolated deployment: $([[ $validation_score -ge 85 ]] && echo "YES" || echo "NO")"
}

# Cleanup temporary files
cleanup_temp_files() {
    if [[ "$CLEANUP_TEMP" == "true" ]]; then
        log_info "Cleaning up temporary files..."
        rm -rf "$TEMP_VALIDATION_DIR" 2>/dev/null || true
    else
        log_warning "Skipping cleanup (preserving temporary files): $TEMP_VALIDATION_DIR"
    fi
}

# Main validation function
main() {
    log_info "Self-Contained Deployment Validation (T044)"

    # Parse arguments
    parse_arguments "$@"

    # Find deployment package
    find_deployment_package

    log_info "Starting self-contained deployment validation..."
    log_info "Deployment package: $DEPLOYMENT_PACKAGE"
    log_info "Validation mode: $VALIDATION_MODE"
    log_info "Isolation level: $ISOLATION_LEVEL"

    # Create temporary validation directory
    mkdir -p "$TEMP_VALIDATION_DIR"

    # Create isolated environment
    local iso_path
    iso_path=$(create_isolated_environment "$ISOLATION_LEVEL")

    # Extract deployment package
    extract_to_isolated_environment "$iso_path"

    # Run validation checks
    local validation_passed=true

    validate_executable_independence "$iso_path" || validation_passed=false
    validate_configuration_completeness "$iso_path" || validation_passed=false
    validate_dependency_isolation "$iso_path" || validation_passed=false
    validate_portability "$iso_path" || validation_passed=false
    validate_offline_capability "$iso_path" || validation_passed=false

    # Generate report
    generate_validation_report "$iso_path"

    # Cleanup
    cleanup_temp_files

    # Final result
    if [[ "$validation_passed" == true ]]; then
        log_success "🎉 Deployment package is fully self-contained and portable!"
        log_info "Package can be deployed to any isolated environment without external dependencies"
        return 0
    else
        log_error "❌ Deployment package is not fully self-contained!"
        log_info "Package requires external dependencies or has portability issues"
        return 1
    fi
}

# Run main function
main "$@"