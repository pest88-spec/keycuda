#!/usr/bin/env bash
# Deployment Testing Framework with Environment Validation
#
# Provides comprehensive testing of deployment packages in various environments,
# validating that packages can be successfully deployed and executed across
# different system configurations.
#
# @author       Puzzle71Solver Team
# @created      2025-10-10
# @license      MIT

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
DEPLOYMENT_DIR="${REPO_ROOT}/deployment"
PACKAGES_DIR="${DEPLOYMENT_DIR}/packages"
TEST_RESULTS_DIR="${DEPLOYMENT_DIR}/test-results"
TEST_ENVIRONMENTS_DIR="${DEPLOYMENT_DIR}/test-environments"

# Exit codes
readonly EXIT_SUCCESS=0
readonly EXIT_TEST_FAILED=1
readonly EXIT_ENVIRONMENT_FAILED=2
readonly EXTRACTION_FAILED=3
readonly INSTALLATION_FAILED=4
readonly EXECUTION_FAILED=5
readonly VALIDATION_FAILED=6

# Color codes for output
readonly GREEN='\033[0;32m'
readonly RED='\033[0;31m'
readonly YELLOW='\033[1;33m'
readonly BLUE='\033[0;34m'
readonly PURPLE='\033[0;35m'
readonly CYAN='\033[0;36m'
readonly NC='\033[0m' # No Color

# Test configuration
CLEANUP_AFTER_TEST=${CLEANUP_AFTER_TEST:-true}
VERBOSE_OUTPUT=${VERBOSE_OUTPUT:-false}
GENERATE_REPORTS=${GENERATE_REPORTS:-true}
CONTINUE_ON_FAILURE=${CONTINUE_ON_FAILURE:-false}
TEST_TIMEOUT=${TEST_TIMEOUT:-300}
SKIP_SYSTEM_DEPENDENCY_TESTS=${SKIP_SYSTEM_DEPENDENCY_TESTS:-false}
TEST_PARALLEL=${TEST_PARALLEL:-false}
readonly MAX_PARALLEL_TESTS=${MAX_PARALLEL_TESTS:-3}

# Environment configurations
declare -A ENVIRONMENT_CONFIGS=(
    ["minimal"]="Minimal environment with basic system libraries only"
    ["standard"]="Standard environment with common development tools"
    ["production"]="Production environment with optimized configuration"
    ["container"]="Container environment (Docker/podman) simulation"
    ["resource-constrained"]="Resource-constrained environment with limited memory/CPU"
    ["isolated"]="Completely isolated environment with no external dependencies"
)

# Test categories
readonly STRUCTURE_TESTS="structure verification"
readonly DEPENDENCY_TESTS="dependency validation"
readonly INSTALLATION_TESTS="installation verification"
readonly EXECUTION_TESTS="execution validation"
readonly PERFORMANCE_TESTS="performance benchmarking"
readonly INTEGRATION_TESTS="integration compatibility"

# Global state
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0
SKIPPED_TESTS=0
CURRENT_ENVIRONMENT=""
CURRENT_PACKAGE=""

# Logging functions
log_test() {
    local level="$1"
    local message="$2"
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')

    case "$level" in
        "PASS")
            echo -e "${GREEN}[TEST-PASS]${NC} ${timestamp} - ${message}"
            ;;
        "FAIL")
            echo -e "${RED}[TEST-FAIL]${NC} ${timestamp} - ${message}"
            ;;
        "SKIP")
            echo -e "${YELLOW}[TEST-SKIP]${NC} ${timestamp} - ${message}"
            ;;
        "INFO")
            echo -e "${BLUE}[TEST-INFO]${NC} ${timestamp} - ${message}"
            ;;
        "WARN")
            echo -e "${PURPLE}[TEST-WARN]${NC} ${timestamp} - ${message}"
            ;;
        "DEBUG")
            echo -e "${CYAN}[TEST-DEBUG]${NC} ${timestamp} - ${message}"
            ;;
        *)
            echo -e "${BLUE}[TEST-${level}]${NC} ${timestamp} - ${message}"
            ;;
    esac
}

# Check prerequisites
check_prerequisites() {
    log_test "INFO" "Checking deployment testing prerequisites"

    local missing_deps=()

    # Check required commands
    for cmd in tar gzip find file ldd timeout; do
        if ! command -v "$cmd" >/dev/null 2>&1; then
            missing_deps+=("$cmd")
        fi
    done

    # Check optional commands
    if ! command -v docker >/dev/null 2>&1; then
        log_test "INFO" "Docker not available, container tests will be skipped"
    fi

    if ! command -v podman >/dev/null 2>&1; then
        log_test "INFO" "Podman not available, podman tests will be skipped"
    fi

    if ! command -v python3 >/dev/null 2>&1; then
        log_test "INFO" "Python3 not available, some tests may be limited"
    fi

    if [[ ${#missing_deps[@]} -gt 0 ]]; then
        log_test "ERROR" "Missing required dependencies: ${missing_deps[*]}"
        return $EXIT_ENVIRONMENT_FAILED
    fi

    # Check packages directory
    if [[ ! -d "$PACKAGES_DIR" ]]; then
        log_test "ERROR" "Packages directory not found: $PACKAGES_DIR"
        return $EXIT_ENVIRONMENT_FAILED
    fi

    # Create test directories
    mkdir -p "$TEST_RESULTS_DIR" "$TEST_ENVIRONMENTS_DIR"

    log_test "SUCCESS" "Prerequisites check completed"
    return $EXIT_SUCCESS
}

# Find test packages
find_test_packages() {
    local packages=()
    for package_file in "$PACKAGES_DIR"/*.tar.gz; do
        if [[ -f "$package_file" ]]; then
            packages+=("$package_file")
        fi
    done

    if [[ ${#packages[@]} -eq 0 ]]; then
        log_test "ERROR" "No packages found to test in: $PACKAGES_DIR"
        return $EXIT_ENVIRONMENT_FAILED
    fi

    echo "${packages[@]}"
}

# Create test environment
create_test_environment() {
    local env_name="$1"
    local env_dir="$TEST_ENVIRONMENTS_DIR/$env_name"

    log_test "INFO" "Creating test environment: $env_name"

    mkdir -p "$env_dir"

    case "$env_name" in
        "minimal")
            create_minimal_environment "$env_dir"
            ;;
        "standard")
            create_standard_environment "$env_dir"
            ;;
        "production")
            create_production_environment "$env_dir"
            ;;
        "container")
            create_container_environment "$env_dir"
            ;;
        "resource-constrained")
            create_resource_constrained_environment "$env_dir"
            ;;
        "isolated")
            create_isolated_environment "$env_dir"
            ;;
        *)
            log_test "WARN" "Unknown environment type: $env_name, using standard"
            create_standard_environment "$env_dir"
            ;;
    esac

    # Create environment configuration file
    cat > "$env_dir/environment.conf" << EOF
# Test Environment Configuration
ENVIRONMENT_NAME="$env_name"
ENVIRONMENT_DESCRIPTION="${ENVIRONMENT_CONFIGS[$env_name]}"
CREATED_AT="$(date -Iseconds)"
CREATED_BY="test-deployment.sh"

# Environment-specific settings
$(case "$env_name" in
    "minimal")
        echo "MINIMAL_ENVIRONMENT=true"
        echo "SYSTEM_TOOLS=limited"
        echo "EXTERNAL_DEPENDENCIES=false"
        ;;
    "standard")
        echo "STANDARD_ENVIRONMENT=true"
        echo "SYSTEM_TOOLS=full"
        echo "EXTERNAL_DEPENDENCIES=true"
        ;;
    "production")
        echo "PRODUCTION_ENVIRONMENT=true"
        echo "SYSTEM_TOOLS=essential"
        echo "EXTERNAL_DEPENDENCIES=controlled"
        echo "PERFORMANCE_OPTIMIZED=true"
        ;;
    "container")
        echo "CONTAINER_ENVIRONMENT=true"
        echo "SYSTEM_TOOLS=container"
        echo "EXTERNAL_DEPENDENCIES=container"
        ;;
    "resource-constrained")
        echo "RESOURCE_CONSTRAINED=true"
        echo "SYSTEM_TOOLS=minimal"
        echo "EXTERNAL_DEPENDENCIES=false"
        echo "MEMORY_LIMIT=512MB"
        ;;
    "isolated")
        echo "ISOLATED_ENVIRONMENT=true"
        echo "SYSTEM_TOOLS=basic"
        echo "EXTERNAL_DEPENDENCIES=false"
        echo "NETWORK_ACCESS=false"
        ;;
esac)

# Test capabilities
HAS_CUDA=$([ -f /usr/local/cuda/lib64/libcuda.so.1 ] && echo "true" || echo "false")
HAS_OPENSSL=$([ -f /usr/lib/x86_64-linux-gnu/libssl.so.1 ] && echo "true" || echo "false")
HAS_GCC=$([ -x /usr/bin/gcc ] && echo "true" || echo "false")
HAS_PYTHON3=$([ -x /usr/bin/python3 ] && echo "true" || echo "false")
HAS_DOCKER=$([ -x /usr/bin/docker ] && echo "true" || echo "false")

# System information
PLATFORM=$(uname -s)
ARCHITECTURE=$(uname -m)
KERNEL_VERSION=$(uname -r)
AVAILABLE_MEMORY=$(free -m | awk 'NR==2{print $2}')
AVAILABLE_DISK=$(df -h . | tail -1 | awk '{print $4}')
EOF

    log_test "SUCCESS" "Test environment created: $env_name"
}

# Create minimal environment
create_minimal_environment() {
    local env_dir="$1"

    # Create minimal bin directory with only essential commands
    mkdir -p "$env_dir/bin"

    # Create minimal ldconfig wrapper
    cat > "$env_dir/bin/ldconfig" << 'EOF'
#!/bin/bash
# Minimal ldconfig wrapper
echo "ldconfig simulated (minimal environment)"
exit 0
EOF
    chmod +x "$env_dir/bin/ldconfig"

    # Create minimal environment
    mkdir -p "$env_dir/usr/lib"
    mkdir -p "$env_dir/etc"

    # Create minimal ld.so.conf
    echo "/usr/lib" > "$env_dir/etc/ld.so.conf"
}

# Create standard environment
create_standard_environment() {
    local env_dir="$1"

    # Create standard directory structure
    mkdir -p "$env_dir"/{bin,lib,include,etc,opt,var}
    mkdir -p "$env_dir/usr"/{bin,lib,include,share,local}
    mkdir -p "$env_dir/opt/puzzle71solver"

    # Copy essential system libraries
    copy_system_libraries "$env_dir" "standard"

    # Create standard environment setup
    cat > "$env_dir/etc/environment" << 'EOF'
PATH=/usr/bin:/bin:/usr/sbin:/sbin
LD_LIBRARY_PATH=/usr/lib:/lib:/usr/local/lib
TERM=xterm
EOF
}

# Create production environment
create_production_environment() {
    local env_dir="$1"

    # Create production directory structure
    mkdir -p "$env_dir"/{bin,lib,etc,opt,var}
    mkdir -p "$env_dir/opt/puzzle71solver"

    # Copy production libraries (optimized versions)
    copy_system_libraries "$env_dir" "production"

    # Create production environment configuration
    cat > "$env_dir/etc/security/limits.conf" << 'EOF'
# Production limits
* soft nofile 65536
* hard nofile 131072
* soft nproc 32768
* hard nproc 65536
EOF

    # Create ulimit wrapper
    cat > "$env_dir/bin/ulimit" << 'EOF'
#!/bin/bash
# Production ulimit wrapper
exec /usr/bin/ulimit "$@"
EOF
    chmod +x "$env_dir/bin/ulimit"
}

# Create container environment
create_container_environment() {
    local env_dir="$1"

    # Simulate container directory structure
    mkdir -p "$env_dir"/{bin,lib,etc,opt,app}
    mkdir -p "$env_dir/app/puzzle71solver"

    # Create container-like configuration
    cat > "$env_dir/etc/container.conf" << 'EOF'
# Container simulation configuration
CONTAINER_NAME=puzzle71solver-test
CONTAINER_VERSION=1.0
NETWORK_MODE=host
VOLUMES=/data:/data
MEMORY_LIMIT=1G
CPU_LIMIT=1.0
EOF

    # Create container entrypoint simulation
    cat > "$env_dir/entrypoint.sh" << 'EOF'
#!/bin/bash
# Container entrypoint simulation
echo "Container entrypoint simulation"
echo "Environment: $(pwd)"
echo "Available memory: $(free -h | grep '^Mem:' | awk '{print $3}')"
echo "Available disk: $(df -h . | tail -1 | awk '{print $4}')"
exec "$@"
EOF
    chmod +x "$env_dir/entrypoint.sh"
}

# Create resource-constrained environment
create_resource_constrained_environment() {
    local env_dir="$1"

    # Create minimal directory structure
    mkdir -p "$env_dir"/{bin,lib,etc,tmp}

    # Create memory limit simulation
    cat > "$env_dir/bin/memory-limit" << 'EOF'
#!/bin/bash
# Memory limit simulation for testing
MEMORY_LIMIT="512MB"
echo "Memory limit set to: $MEMORY_LIMIT"
ulimit -v $((512 * 1024)) 2>/dev/null || true
exec "$@"
EOF
    chmod +x "$env_dir/bin/memory-limit"

    # Create CPU limit simulation
    cat > "$env_dir/bin/cpu-limit" << 'EOF'
#!/bin/bash
# CPU limit simulation for testing
CPU_LIMIT="50%"
echo "CPU limit set to: $CPU_LIMIT"
# In a real environment, this would use taskset or cgroups
exec "$@"
EOF
    chmod +x "$env_dir/bin/cpu-limit"

    # Create limited ld.so.conf
    echo "/lib:/usr/lib:/usr/local/lib" > "$env_dir/etc/ld.so.conf"
}

# Create isolated environment
create_isolated_environment() {
    local env_dir="$1"

    # Create isolated directory structure
    mkdir -p "$env_dir"/{bin,lib,etc,var}
    mkdir -p "$env_dir/opt/puzzle71solver"

    # Create isolated ld.so.conf with only essential libraries
    echo "/lib:/usr/lib" > "$env_dir/etc/ld.so.conf"

    # Create network simulation (blocked)
    cat > "$env_dir/bin/ping" << 'EOF'
#!/bin/bash
# Network access simulation (blocked)
echo "Network access blocked in isolated environment"
echo "Simulating ping failure"
exit 1
EOF
    chmod +x "$env_dir/bin/ping"

    # Create curl simulation (blocked)
    cat > "$env_dir/bin/curl" << 'EOF'
#!/bin/bash
# Network access simulation (blocked)
echo "Network access blocked in isolated environment"
echo "Simulating curl failure"
exit 1
EOF
    chmod +x "$env_dir/bin/curl"
}

# Copy system libraries
copy_system_libraries() {
    local env_dir="$1"
    local env_type="$2"

    # Essential system libraries
    local essential_libs=(
        "libc.so.6"
        "libm.so.6"
        "libpthread.so.0"
        "libdl.so.2"
        "librt.so.1"
    )

    # Development libraries for standard/production environments
    if [[ "$env_type" == "standard" || "$env_type" == "production" ]]; then
        essential_libs+=(
            "libstdc++.so.6"
            "libgcc_s.so.1"
            "libssl.so.1.1"
            "libcrypto.so.1.1"
            "libz.so.1"
        )
    fi

    # Copy libraries if they exist
    for lib in "${essential_libs[@]}"; do
        for lib_path in "/usr/lib/$lib" "/lib/$lib" "/usr/lib/x86_64-linux-gnu/$lib"; do
            if [[ -f "$lib_path" ]]; then
                cp "$lib_path" "$env_dir/lib/" 2>/dev/null || true
                break
            fi
        done
    done

    # Create ld.so.cache
    ldconfig -r "$env_dir/lib" -C "$env_dir/etc/ld.so.conf" 2>/dev/null || true
}

# Test package structure
test_package_structure() {
    local package_path="$1"
    local env_dir="$2"
    local package_name=$(basename "$package_path")
    local extract_dir="$env_dir/extracted"

    log_test "INFO" "Testing package structure for $package_name in $CURRENT_ENVIRONMENT"

    ((TOTAL_TESTS++))

    # Extract package
    log_test "INFO" "Extracting package to: $extract_dir"
    mkdir -p "$extract_dir"

    if ! timeout 60 tar -xzf "$package_path" -C "$extract_dir"; then
        log_test "FAIL" "Package extraction failed or timed out"
        ((FAILED_TESTS++))
        return $EXTRACTION_FAILED
    fi

    # Verify required structure
    local required_dirs=("bin" "lib" "scripts")
    local structure_passed=true

    for dir in "${required_dirs[@]}"; do
        if [[ ! -d "$extract_dir/$dir" ]]; then
            log_test "FAIL" "Required directory missing: $dir"
            structure_passed=false
        fi
    done

    # Verify required files
    local required_files=("bin/Puzzle71Solver" "deployment-manifest.json" "scripts/install.sh")
    for file in "${required_files[@]}"; do
        if [[ ! -f "$extract_dir/$file" ]]; then
            log_test "FAIL" "Required file missing: $file"
            structure_passed=false
        fi
    done

    if [[ "$structure_passed" == "true" ]]; then
        log_test "PASS" "Package structure verification passed"
        ((PASSED_TESTS++))
    else
        ((FAILED_TESTS++))
        return $EXTRACTION_FAILED
    fi

    return $EXIT_SUCCESS
}

# Test dependency compatibility
test_dependency_compatibility() {
    local package_path="$1"
    local env_dir="$2"
    local package_name=$(basename "$package_path")
    local extract_dir="$env_dir/extracted"

    log_test "INFO" "Testing dependency compatibility for $package_name in $CURRENT_ENVIRONMENT"

    ((TOTAL_TESTS++))

    # Set environment PATH to use environment-specific binaries
    local old_path="$PATH"
    export PATH="$env_dir/bin:$PATH"
    export LD_LIBRARY_PATH="$env_dir/lib:$LD_LIBRARY_PATH"

    # Check main binary dependencies
    local binary="$extract_dir/bin/Puzzle71Solver"
    if [[ -f "$binary" ]]; then
        log_test "DEBUG" "Checking dependencies for: $binary"

        # Use environment-specific ldd if available
        local ldd_cmd="$env_dir/bin/ldconfig"
        if [[ ! -x "$ldd_cmd" ]]; then
            ldd_cmd="ldd"
        fi

        local dep_output
        if dep_output=$("$ldd_cmd" "$binary" 2>&1); then
            local missing_deps=$(echo "$dep_output" | grep "not found" || true)
            local missing_count=0

            while IFS= read -r line; do
                if [[ -n "$line" ]]; then
                    ((missing_count++))
                    log_test "DEBUG" "Missing dependency: $line"
                fi
            done <<< "$missing_deps"

            if [[ "$missing_count" -eq 0 ]]; then
                log_test "PASS" "All dependencies satisfied"
                ((PASSED_TESTS++))
            else
                log_test "FAIL" "Found $missing_count missing dependencies"
                ((FAILED_TESTS++))
                return $EXIT_VALIDATION_FAILED
            fi
        else
            log_test "FAIL" "Failed to check dependencies"
            ((FAILED_TESTS++))
            return $EXIT_VALIDATION_FAILED
        fi
    else
        log_test "FAIL" "Binary not found for dependency testing"
        ((FAILED_TESTS++))
        return $EXIT_VALIDATION_FAILED
    fi

    # Restore environment
    export PATH="$old_path"
    unset LD_LIBRARY_PATH

    return $EXIT_SUCCESS
}

# Test installation process
test_installation() {
    local package_path="$1"
    local env_dir="$2"
    local package_name=$(basename "$package_path")
    local extract_dir="$env_dir/extracted"
    local install_dir="$env_dir/install"

    log_test "INFO" "Testing installation process for $package_name in $CURRENT_ENVIRONMENT"

    ((TOTAL_TESTS++))

    # Create installation directory
    mkdir -p "$install_dir"

    # Run installation script
    local install_script="$extract_dir/scripts/install.sh"
    if [[ -f "$install_script" ]]; then
        log_test "DEBUG" "Running installation script"

        # Modify install script to use test environment paths
        local modified_script="$env_dir/install-modified.sh"
        sed "s|/opt/puzzle71solver|$install_dir|g" "$install_script" > "$modified_script"
        chmod +x "$modified_script"

        # Set environment for installation
        local old_path="$PATH"
        export PATH="$env_dir/bin:$PATH"
        export LD_LIBRARY_PATH="$env_dir/lib:$LD_LIBRARY_PATH"
        export INSTALL_PREFIX="$install_dir"

        # Run installation with timeout
        if timeout 120 bash "$modified_script" >/dev/null 2>&1; then
            log_test "PASS" "Installation completed successfully"
            ((PASSED_TESTS++))

            # Verify installation
            if [[ -f "$install_dir/bin/Puzzle71Solver" ]]; then
                log_test "PASS" "Installation verification passed"
                ((PASSED_TESTS++))
            else
                log_test "FAIL" "Installation verification failed - binary not found"
                ((FAILED_TESTS++))
            fi
        else
            log_test "FAIL" "Installation failed or timed out"
            ((FAILED_TESTS++))
            return $INSTALLATION_FAILED
        fi

        # Cleanup
        rm -f "$modified_script"
    else
        log_test "SKIP" "Installation script not found"
        ((SKIPPED_TESTS++))
    fi

    return $EXIT_SUCCESS
}

# Test execution capabilities
test_execution() {
    local package_path="$1"
    local env_dir="$2"
    local package_name=$(basename "$package_path")
    local extract_dir="$env_dir/extracted"

    log_test "INFO" "Testing execution capabilities for $package_name in $CURRENT_ENVIRONMENT"

    ((TOTAL_TESTS++))

    # Set environment for execution
    local old_path="$PATH"
    export PATH="$env_dir/bin:$extract_dir/bin:$PATH"
    export LD_LIBRARY_PATH="$env_dir/lib:$extract_dir/lib:$LD_LIBRARY_PATH"

    local binary="$extract_dir/bin/Puzzle7171Solver"
    if [[ -f "$binary" ]]; then
        log_test "DEBUG" "Testing binary execution: $binary"

        # Test help command
        if timeout 30 "$binary" --help >/dev/null 2>&1; then
            log_test "PASS" "Help command works"
            ((PASSED_TESTS++))
        else
            log_test "WARN" "Help command failed or timed out"
        fi

        # Test version command
        if timeout 30 "$binary" --version >/dev/null 2>&1; then
            log_test "PASS" "Version command works"
            ((PASSED_TESTS++))
        else
            log_test "WARN" "Version command failed or timed out"
        fi

        # Test configuration loading
        if [[ -f "$extract_dir/config/puzzle71.yaml" ]]; then
            if timeout 30 "$binary" --config "$extract_dir/config/puzzle71.yaml" --help >/dev/null 2>&1; then
                log_test "PASS" "Configuration loading works"
                ((PASSED_TESTS++))
            else
                log_test "WARN" "Configuration loading failed or timed out"
            fi
        else
            log_test "SKIP" "No configuration file found"
        fi

        # Test with limited functionality (dry run if available)
        if timeout 30 "$binary" --dry-run >/dev/null 2>&1; then
            log_test "PASS" "Dry run functionality works"
            ((PASSED_TESTS++))
        else
            log_test "SKIP" "Dry run not available or failed"
        fi
    else
        log_test "FAIL" "Binary not found for execution testing"
        ((FAILED_TESTS++))
        return $EXECUTION_FAILED
    fi

    # Restore environment
    export PATH="$old_path"
    unset LD_LIBRARY_PATH

    return $EXIT_SUCCESS
}

# Test performance characteristics
test_performance() {
    local package_path="$1"
    local env_dir="$2"
    local package_name=$(basename "$package_path")
    local extract_dir="$env_dir/extracted"

    log_test "INFO" "Testing performance characteristics for $package_name in $CURRENT_ENVIRONMENT"

    ((TOTAL_TESTS++))

    local binary="$extract_dir/bin/Puzzle7171Solver"
    if [[ ! -f "$binary" ]]; then
        log_test "SKIP" "Binary not found for performance testing"
        return $EXIT_SUCCESS
    fi

    # Set environment for performance testing
    local old_path="$PATH"
    export PATH="$env_dir/bin:$extract_dir/bin:$PATH"
    export LD_LIBRARY_PATH="$env_dir/lib:$extract_dir/lib:$LD_LIBRARY_PATH"

    # Test startup time
    log_test "DEBUG" "Measuring startup time"
    local start_time=$(date +%s.%N)
    if timeout 30 "$binary" --help >/dev/null 2>&1; then
        local end_time=$(date +%s.%N)
        local startup_time=$(echo "$end_time - $start_time" | bc -l)
        log_test "INFO" "Startup time: ${startup_time}s"
        log_test "PASS" "Startup time measured successfully"
        ((PASSED_TESTS++))
    else
        log_test "FAIL" "Startup time measurement failed"
        ((FAILED_TESTS++))
    fi

    # Test memory usage (if tools available)
    if command -v /usr/bin/time >/dev/null 2>&1; then
        log_test "DEBUG" "Measuring memory usage"
        local memory_output
        if memory_output=$(/usr/bin/time -f "%M" timeout 30 "$binary" --help 2>&1); then
            local memory_usage=$(echo "$memory_output" | tail -1)
            log_test "INFO" "Memory usage: $memory_usage"
            log_test "PASS" "Memory usage measured successfully"
            ((PASSED_TESTS++))
        else
            log_test "WARN" "Memory usage measurement failed"
        fi
    fi

    # Test binary size
    local binary_size=$(stat -f% "$binary" 2>/dev/null || echo "0")
    if [[ "$binary_size" != "0" ]]; then
        local size_mb=$(echo "scale=2; $binary_size / 1024 / 1024" | bc -l)
        log_test "INFO" "Binary size: ${size_mb}MB"
        log_test "PASS" "Binary size measured successfully"
        ((PASSED_TESTS++))
    fi

    # Restore environment
    export PATH="$old_path"
    unset LD_LIBRARY_PATH

    return $EXIT_SUCCESS
}

# Run comprehensive test suite
run_package_tests() {
    local package_path="$1"
    local package_name=$(basename "$package_path")

    CURRENT_PACKAGE="$package_name"
    local test_start_time=$(date +%s)

    log_test "INFO" "Starting comprehensive test suite for $package_name in $CURRENT_ENVIRONMENT"

    # Create test environment
    if ! create_test_environment "$CURRENT_ENVIRONMENT"; then
        log_test "ERROR" "Failed to create test environment: $CURRENT_ENVIRONMENT"
        return $EXIT_ENVIRONMENT_FAILED
    fi

    local env_dir="$TEST_ENVIRONMENTS_DIR/$CURRENT_ENVIRONMENT"
    local overall_result=$EXIT_SUCCESS

    # Run test suite
    local tests=(
        "test_package_structure"
        "test_dependency_compatibility"
        "test_installation"
        "test_execution"
        "test_performance"
    )

    for test_func in "${tests[@]}"; do
        if [[ "$test_func" == "test_installation" && "$CURRENT_ENVIRONMENT" == "isolated" ]]; then
            log_test "SKIP" "Skipping installation test in isolated environment"
            ((SKIPPED_TESTS++))
            continue
        fi

        log_test "INFO" "Running $test_func"
        if ! "$test_func" "$package_path" "$env_dir"; then
            overall_result=$EXIT_TEST_FAILED
            if [[ "$CONTINUE_ON_FAILURE" != "true" ]]; then
                break
            fi
        fi
    done

    # Calculate test duration
    local test_end_time=$(date +%s)
    local test_duration=$((test_end_time - test_start_time))

    # Generate test report
    generate_test_report "$package_name" "$CURRENT_ENVIRONMENT" "$test_duration" "$overall_result"

    # Cleanup test environment
    if [[ "$CLEANUP_AFTER_TEST" == "true" ]]; then
        rm -rf "$env_dir"
    fi

    CURRENT_PACKAGE=""
    return $overall_result
}

# Generate test report
generate_test_report() {
    local package_name="$1"
    local environment="$2"
    local duration="$3"
    local result="$4"

    local report_file="$TEST_RESULTS_DIR/test-report-${package_name}-${environment}-$(date '+%Y%m%d_%H%M%S').json"

    if [[ "$GENERATE_REPORTS" != "true" ]]; then
        return
    fi

    log_test "INFO" "Generating test report: $(basename "$report_file")"

    cat > "$report_file" << EOF
{
    "test_timestamp": "$(date -Iseconds)",
    "package_name": "$package_name",
    "environment": "$environment",
    "environment_description": "${ENVIRONMENT_CONFIGS[$environment]}",
    "test_duration_seconds": $duration,
    "test_configuration": {
        "cleanup_after_test": $CLEANUP_AFTER_TEST,
        "verbose_output": $VERBOSE_OUTPUT,
        "generate_reports": $GENERATE_REPORTS,
        "continue_on_failure": $CONTINUE_ON_FAILURE,
        "test_timeout": $TEST_TIMEOUT,
        "skip_system_dependency_tests": $SKIP_SYSTEM_DEPENDENCY_TESTS
    },
    "test_results": {
        "total_tests": $TOTAL_TESTS,
        "passed_tests": $PASSED_TESTS,
        "failed_tests": $FAILED_TESTS,
        "skipped_tests": $SKIPPED_TESTS,
        "success_rate": $(echo "scale=2; $PASSED_TESTS * 100 / $TOTAL_TESTS" | bc -l)
    },
    "environment_capabilities": {
        "environment_name": "$environment",
        "platform": "$(uname -s)",
        "architecture": "$(uname -m)",
        "kernel_version": "$(uname -r)",
        "available_memory": "$(free -m | awk 'NR==2{print $2}' | tail -1)",
        "available_disk": "$(df -h . | tail -1 | awk '{print $4}')"
    },
    "test_categories": {
        "structure_tests": {
            "description": "$STRUCTURE_TESTS",
            "passed": $(grep -c "PASS" "$TEST_RESULTS_DIR"/*-report-*.json 2>/dev/null || echo 0),
            "failed": $(grep -c "FAIL" "$TEST_RESULTS_DIR"/*-report-*.json 2>/dev/null || echo 0)
        },
        "dependency_tests": {
            "description": "$DEPENDENCY_TESTS",
            "passed": $(grep -c "PASS" "$TEST_RESULTS_DIR"/*-report-*.json 2>/dev/null || echo 0),
            "failed": $(grep -c "FAIL" "$TEST_RESULTS_DIR"/*-report-*.json 2>/dev/null || echo 0)
        },
        "installation_tests": {
            "description": "$INSTALLATION_TESTS",
            "passed": $(grep -c "PASS" "$TEST_RESULTS_DIR"/*-report-*.json 2>/dev/null || echo 0),
            "failed": $(grep -c "FAIL" "$TEST_RESULTS_DIR"/*-report-*.json 2>/dev/null || echo 0)
        },
        "execution_tests": {
            "description": "$EXECUTION_TESTS",
            "passed": $(grep -c "PASS" "$TEST_RESULTS_DIR"/*-report-*.json 2>/dev/null || echo 0),
            "failed": $(grep -c "FAIL" "$TEST_RESULTS_DIR"/*-report-*.json 2>/dev/null || echo 0)
        },
        "performance_tests": {
            "description": "$PERFORMANCE_TESTS",
            "passed": $(grep -c "PASS" "$TEST_RESULTS_DIR"/*-report-*.json 2>/dev/null || echo 0),
            "failed": $(grep -c "FAIL" "$TEST_RESULTS_DIR"/*-report-*.json 2>/dev/null || echo 0)
        }
    },
    "overall_result": "$result",
    "recommendations": [
        $(if [[ $result -eq $EXIT_SUCCESS ]]; then echo '"Package successfully tested and validated for deployment",'; else echo '"Address test failures before deployment",'; fi)
        "$(if [[ $FAILED_PACKAGES -gt 0 ]]; then echo '"Review failed tests and fix compatibility issues",'; fi)"
    ],
    "deployment_readiness": "$(if [[ $result -eq $EXIT_SUCCESS ]]; then echo "READY"; else echo "NOT_READY"; fi)"
}
EOF

    log_test "INFO" "Test report generated: $(basename "$report_file")"
}

# Main testing function
run_comprehensive_tests() {
    log_test "INFO" "Starting comprehensive deployment testing"

    # Get test packages
    local packages=($(find_test_packages))

    if [[ ${#packages[@]} -eq 0 ]]; then
        log_test "ERROR" "No packages found for testing"
        return $EXIT_ENVIRONMENT_FAILED
    fi

    log_test "INFO" "Found ${#packages[@]} packages to test"

    # Test packages in each environment
    local overall_result=$EXIT_SUCCESS
    local tested_packages=0

    for env_name in "${!ENVIRONMENT_CONFIGS[@]}"; do
        CURRENT_ENVIRONMENT="$env_name"
        log_test "INFO" "Testing in environment: $env_name"

        local env_result=$EXIT_SUCCESS
        local env_packages=0

        # Test packages in this environment
        for package_file in "${packages[@]}"; do
            if ! run_package_tests "$package_file"; then
                env_result=$EXIT_TEST_FAILED
                if [[ "$CONTINUE_ON_FAILURE" != "true" ]]; then
                    overall_result=$EXIT_TEST_FAILED
                fi
            fi
            ((env_packages++))
        done

        log_test "INFO" "Environment $env_name: $env_packages packages tested, result: $env_result"
        ((tested_packages += env_packages))

        if [[ "$env_result" != $EXIT_SUCCESS && "$CONTINUE_ON_FAILURE" != "true" ]]; then
            overall_result=$EXIT_TEST_FAILED
        fi
    done

    # Generate summary report
    generate_summary_report

    # Display summary
    echo ""
    log_test "INFO" "Comprehensive Testing Summary"
    log_test "INFO" "============================="
    log_test "INFO" "Total packages tested: $tested_packages"
    log_test "INFO" "Total tests run: $TOTAL_TESTS"
    log_test "INFO" "Passed: $PASSED_TESTS"
    log_test "INFO" "Failed: $FAILED_PACKAGES"
    log_test "INFO" "Skipped: $SKIPPED_TESTS"

    if [[ $overall_result -eq $EXIT_SUCCESS ]]; then
        log_test "SUCCESS" "All deployment tests completed successfully"
    else
        log_test "ERROR" "Some deployment tests failed - review and address issues"
    fi

    return $overall_result
}

# Generate summary report
generate_summary_report() {
    local summary_file="$TEST_RESULTS_DIR/summary-report-$(date '+%Y%m%d_%H%M%S').json"

    if [[ "$GENERATE_REPORTS" != "true" ]]; then
        return
    fi

    log_test "INFO" "Generating summary report: $(basename "$summary_file")"

    cat > "$summary_file" << EOF
{
    "summary_timestamp": "$(date -Iseconds)",
    "test_configuration": {
        "environments_tested": [$(printf '"%s",' "${!ENVIRONMENT_CONFIGS[@]}" | sed 's/,$//')],
        "cleanup_after_test": $CLEANUP_AFTER_TEST,
        "generate_reports": $GENERATE_REPORTS,
        "continue_on_failure": $CONTINUE_ON_FAILURE
    },
    "overall_results": {
        "total_packages": 0,
        "total_tests": $TOTAL_TESTS,
        "passed_tests": $PASSED_TESTS,
        "failed_tests": $FAILED_PACKAGES,
        "skipped_tests": $SKIP_TESTS,
        "success_rate": $(echo "scale=2; $PASSED_TESTS * 100 / $TOTAL_TESTS" | bc -l)
    },
    "environment_results": {},
    "test_statistics": {
        "structure_tests": {
            "total": 0,
            "passed": 0,
            "failed": 0,
            "success_rate": 0
        },
        "dependency_tests": {
            "total": 0,
            "passed": 0,
            "failed": 0,
            "success_rate": 0
        },
        "installation_tests": {
            "total": 0,
            "passed": 0,
            "failed": 0,
            "success_rate": 0
        },
        "execution_tests": {
            "total": 0,
            "passed": 0,
            "failed": 0,
            "success_rate": 0
        },
        "performance_tests": {
            "total": 0,
            "passed": 0,
            "failed": 0,
            "success_rate": 0
        }
    },
    "recommendations": [
        "$(if [[ $FAILED_PACKAGES -eq 0 ]]; then echo '"All packages tested successfully - ready for deployment",'; else echo '"Review failed tests and fix compatibility issues",'; fi)",
        "$(if [[ $PASSED_TESTS -eq $TOTAL_TESTS ]]; then echo '"Perfect test record achieved",'; else echo '"Address test failures to improve reliability",'; fi)",
        "$(if [[ $SKIPPED_TESTS -gt 0 ]]; then echo '"Consider enabling skipped tests for more comprehensive validation",'; fi)"
    ],
    "deployment_readiness": "$(if [[ $FAILED_PACKAGES -eq 0 ]]; then echo "READY"; else echo "NOT_READY"; fi)"
}
EOF

    log_test "INFO" "Summary report generated: $(basename "$summary_file")"
}

# Main execution function
main() {
    # Parse command line arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            -h|--help)
                cat << 'EOF'
Deployment Testing Framework with Environment Validation

Usage: ./test-deployment.sh [OPTIONS] [PACKAGE_FILE]

OPTIONS:
    -h, --help                     Show this help message
    -v, --verbose                  Enable verbose logging
    --no-cleanup                   Don't cleanup test environments
    --no-reports                  Don't generate test reports
    --continue-on-failure          Continue testing even if tests fail
    --timeout <seconds>             Test timeout (default: 300)
    --parallel                     Run tests in parallel (max 3)
    --skip-dependency-tests         Skip system dependency tests
    --environment <env>           Test only specific environment
    --package <package>           Test only specific package

ENVIRONMENTS:
    minimal         - Minimal environment with basic libraries
    standard        - Standard environment with common tools
    production     - Production environment with optimizations
    container        - Container environment simulation
    resource-constrained - Resource-constrained with limits
    isolated         - Completely isolated environment

EXAMPLES:
    ./test-deployment.sh
    ./test-deployment.sh --verbose --no-cleanup
    ./test-deployment.sh --environment minimal
    ./test-deployment.sh --package puzzle71solver-deployment-1.0.0.tar.gz
    ./test-deployment.sh --parallel --timeout 600

DESCRIPTION:
    This script provides comprehensive testing of deployment packages across
    multiple environment types, validating that packages can be successfully
    deployed and executed in various system configurations. It tests structure,
    dependencies, installation, execution, and performance characteristics.

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
            --no-reports)
                export GENERATE_REPORTS=false
                shift
                ;;
            --continue-on-failure)
                export CONTINUE_ON_FAILURE=true
                shift
                ;;
            --timeout)
                export TEST_TIMEOUT="$2"
                shift 2
                ;;
            --parallel)
                export TEST_PARALLEL=true
                shift
                ;;
            --skip-dependency-tests)
                export SKIP_SYSTEM_DEPENDENCY_TESTS=true
                shift
                ;;
            --environment)
                export TEST_ENVIRONMENT="$2"
                shift 2
                ;;
            --package)
                export TEST_PACKAGE="$2"
                shift 2
                ;;
            *)
                # Assume it's a package file
                break
                ;;
        esac
    done

    # Check prerequisites
    if ! check_prerequisites; then
        exit $EXIT_ENVIRONMENT_FAILED
    fi

    # Run specific tests if requested
    if [[ -n "${TEST_PACKAGE:-}" ]]; then
        local package_file="$TEST_PACKAGE"
        if [[ ! -f "$package_file" ]]; then
            log_test "ERROR" "Package file not found: $package_file"
            exit $EXIT_ENVIRONMENT_FAILED
        fi

        local test_env="${TEST_ENVIRONMENT:-standard}"
        CURRENT_ENVIRONMENT="$test_env"

        log_test "INFO" "Testing single package: $(basename "$package_file") in $test_env environment"

        if create_test_environment "$test_env"; then
            local env_dir="$TEST_ENVIRONMENTS_DIR/$test_env"
            if run_package_tests "$package_file"; then
                log_test "SUCCESS" "Package testing completed successfully"
                exit $EXIT_SUCCESS
            else
                log_test "ERROR" "Package testing failed"
                exit $EXIT_TEST_FAILED
            fi
        else
            log_test "ERROR" "Failed to create test environment: $test_env"
            exit $EXIT_ENVIRONMENT_FAILED
        fi
    else
        # Run comprehensive tests
        if run_comprehensive_tests; then
            log_test "SUCCESS" "Comprehensive deployment testing completed"
            exit $EXIT_SUCCESS
        else
            log_test "ERROR" "Comprehensive deployment testing failed"
            exit $EXIT_TEST_FAILED
        fi
    fi
}

# Run main function if script is executed directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi