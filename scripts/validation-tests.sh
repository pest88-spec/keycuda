#!/usr/bin/env bash
# T052: Validation Test Utilities
# Additional test utilities and helpers for dependency validation framework

set -euo pipefail

# Script configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
VALIDATION_UTILS_DIR="$PROJECT_ROOT/.validation_utils"
TEST_DATA_DIR="$PROJECT_ROOT/test_data/validation"
PERFORMANCE_RESULTS_DIR="$PROJECT_ROOT/results/performance"

# Import main validation framework
VALIDATION_SCRIPT="$SCRIPT_DIR/validate-dependency-updates.sh"
if [[ ! -f "$VALIDATION_SCRIPT" ]]; then
    echo "ERROR: Validation script not found at $VALIDATION_SCRIPT" >&2
    exit 1
fi

# Function to call validation framework functions via subprocess
call_validation_function() {
    local function_name="$1"
    shift
    "$VALIDATION_SCRIPT" "$function_name" "$@"
}

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Logging functions
log_utils() {
    local level="$1"
    local message="$2"
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    echo -e "${timestamp} [VALIDATION_UTILS] ${level} ${message}"
}

log_info() { log_utils "INFO" "$1"; }
log_success() { log_utils "SUCCESS" "$1"; }
log_warning() { log_utils "WARNING" "$1"; }
log_error() { log_utils "ERROR" "$1"; }
log_debug() { log_utils "DEBUG" "$1"; }

# Initialize validation utilities
init_validation_utils() {
    log_info "Initializing validation utilities..."

    # Create directories
    mkdir -p "$VALIDATION_UTILS_DIR" "$TEST_DATA_DIR" "$PERFORMANCE_RESULTS_DIR"

    # Create test data if it doesn't exist
    create_test_data

    log_success "Validation utilities initialized"
}

# Create test data
create_test_data() {
    if [[ ! -d "$TEST_DATA_DIR" ]]; then
        mkdir -p "$TEST_DATA_DIR"
    fi

    # Create sample key ranges for testing
    cat > "$TEST_DATA_DIR/test_ranges.json" << 'EOF'
{
  "test_ranges": [
    {
      "name": "small_range",
      "start": "0000000000000000000000000000000000000000000000000000000000000000",
      "end": "00000000000000000000000000000000000000000000000000000000000000FF",
      "expected_keys": 256
    },
    {
      "name": "medium_range",
      "start": "0000000000000000000000000000000000000000000000000000000000000000",
      "end": "000000000000000000000000000000000000000000000000000000000000FFFF",
      "expected_keys": 65536
    },
    {
      "name": "large_range",
      "start": "0000000000000000000000000000000000000000000000000000000000000000",
      "end": "00000000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF",
      "expected_keys": "large"
    }
  ]
}
EOF

    # Create test patterns for GPU validation
    cat > "$TEST_DATA_DIR/test_patterns.txt" << 'EOF'
# Test patterns for GPU validation
# Each line: name:pattern:expected_result
basic_search:0000000000000000000000000000000000000000000000000000000000000000:success
edge_case_min:0000000000000000000000000000000000000000000000000000000000000000:success
edge_case_max:FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF:success
zero_range:0000000000000000000000000000000000000000000000000000000000000000:success
random_pattern1:1A2B3C4D5E6F7890ABCDEF1234567890ABCDEF1234567890ABCDEF1234567890:success
random_pattern2:FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210FEDCBA9876543210:success
EOF

    log_info "Test data created in $TEST_DATA_DIR"
}

# Enhanced dependency health check
enhanced_dependency_health_check() {
    log_info "Running enhanced dependency health check..."

    local health_report="$VALIDATION_UTILS_DIR/dependency_health_$(date +%Y%m%d_%H%M%S).json"
    local health_issues=0
    local warnings=0
    local critical_issues=0

    # Initialize health report
    cat > "$health_report" << 'EOF'
{
  "check_timestamp": "",
  "overall_status": "healthy",
  "dependencies": {},
  "build_system": {},
  "runtime_environment": {},
  "performance": {},
  "security": {},
  "recommendations": []
}
EOF

    # Update timestamp
    jq --arg timestamp "$(date -u +"%Y-%m-%dT%H:%M:%SZ")" '.check_timestamp = $timestamp' "$health_report" > "${health_report}.tmp" && mv "${health_report}.tmp" "$health_report"

    # Check CUDA availability
    log_info "Checking CUDA availability..."
    local cuda_version
    if cuda_version=$(nvcc --version 2>/dev/null | grep "release" | head -1 | awk '{print $6}' | cut -d, -f1); then
        log_success "CUDA found: $cuda_version"
        jq --arg cuda_version "$cuda_version" '.runtime_environment.cuda = {status: "available", version: $cuda_version}' "$health_report" > "${health_report}.tmp" && mv "${health_report}.tmp" "$health_report"
    else
        log_error "CUDA not found or not working"
        ((critical_issues++))
        jq '.runtime_environment.cuda = {status: "missing", version: "unknown"}' "$health_report" > "${health_report}.tmp" && mv "${health_report}.tmp" "$health_report"
    fi

    # Check GPU devices
    log_info "Checking GPU devices..."
    local gpu_count
    if gpu_count=$(nvidia-smi --query-gpu=count --format=csv,noheader,nounits 2>/dev/null); then
        log_success "GPU devices found: $gpu_count"
        jq --argjson gpu_count "$gpu_count" '.runtime_environment.gpus = {status: "available", count: $gpu_count}' "$health_report" > "${health_report}.tmp" && mv "${health_report}.tmp" "$health_report"
    else
        log_warning "No GPU devices detected"
        ((warnings++))
        jq '.runtime_environment.gpus = {status: "unavailable", count: 0}' "$health_report" > "${health_report}.tmp" && mv "${health_report}.tmp" "$health_report"
    fi

    # Check CMake
    log_info "Checking CMake..."
    local cmake_version
    if cmake_version=$(cmake --version 2>/dev/null | head -1 | awk '{print $3}'); then
        log_success "CMake found: $cmake_version"
        jq --arg cmake_version "$cmake_version" '.build_system.cmake = {status: "available", version: $cmake_version}' "$health_report" > "${health_report}.tmp" && mv "${health_report}.tmp" "$health_report"
    else
        log_error "CMake not found"
        ((critical_issues++))
        jq '.build_system.cmake = {status: "missing", version: "unknown"}' "$health_report" > "${health_report}.tmp" && mv "${health_report}.tmp" "$health_report"
    fi

    # Check system dependencies
    log_info "Checking system dependencies..."
    local sys_deps=("make" "g++" "git" "curl" "wget")
    local missing_deps=()

    for dep in "${sys_deps[@]}"; do
        if command -v "$dep" >/dev/null 2>&1; then
            local version
            case "$dep" in
                "g++") version=$(g++ --version | head -1 | awk '{print $4}') ;;
                *) version=$($dep --version 2>/dev/null | head -1 || echo "available") ;;
            esac
            log_debug "System dependency $dep: $version"
            jq --arg dep "$dep" --arg version "$version" '.dependencies.system[$dep] = {status: "available", version: $version}' "$health_report" > "${health_report}.tmp" && mv "${health_report}.tmp" "$health_report"
        else
            log_warning "System dependency missing: $dep"
            missing_deps+=("$dep")
            jq --arg dep "$dep" '.dependencies.system[$dep] = {status: "missing", version: "unknown"}' "$health_report" > "${health_report}.tmp" && mv "${health_report}.tmp" "$health_report"
        fi
    done

    if [[ ${#missing_deps[@]} -gt 0 ]]; then
        ((warnings++))
    fi

    # Check Python dependencies
    log_info "Checking Python dependencies..."
    local python_deps=("numpy" "matplotlib" "pandas" "requests")
    for dep in "${python_deps[@]}"; do
        if python3 -c "import $dep" 2>/dev/null; then
            local version=$(python3 -c "import $dep; print($dep.__version__)" 2>/dev/null || echo "available")
            log_debug "Python dependency $dep: $version"
            jq --arg dep "$dep" --arg version "$version" '.dependencies.python[$dep] = {status: "available", version: $version}' "$health_report" > "${health_report}.tmp" && mv "${health_report}.tmp" "$health_report"
        else
            log_debug "Python dependency missing: $dep"
            jq --arg dep "$dep" '.dependencies.python[$dep] = {status: "missing", version: "unknown"}' "$health_report" > "${health_report}.tmp" && mv "${health_report}.tmp" "$health_report"
        fi
    done

    # Check build directory
    log_info "Checking build directory..."
    local build_dir="$PROJECT_ROOT/build"
    if [[ -d "$build_dir" ]]; then
        local binary_found=false
        if [[ -x "$build_dir/Puzzle71Solver" ]]; then
            binary_found=true
            log_success "Puzzle71Solver binary found"
            jq '.build_system.binary = {status: "available", path: "build/Puzzle71Solver"}' "$health_report" > "${health_report}.tmp" && mv "${health_report}.tmp" "$health_report"
        else
            log_warning "Puzzle71Solver binary not found"
            ((warnings++))
            jq '.build_system.binary = {status: "missing", path: "build/Puzzle71Solver"}' "$health_report" > "${health_report}.tmp" && mv "${health_report}.tmp" "$health_report"
        fi
    else
        log_info "Build directory not found"
        jq '.build_system.build_dir = {status: "missing", path: "build"}' "$health_report" > "${health_report}.tmp" && mv "${health_report}.tmp" "$health_report"
    fi

    # Performance baseline check
    log_info "Checking performance baseline..."
    local baseline_file="$VALIDATION_CACHE_DIR/performance_baseline.json"
    if [[ -f "$baseline_file" ]]; then
        log_success "Performance baseline found"
        jq '.performance.baseline = {status: "available"}' "$health_report" > "${health_report}.tmp" && mv "${health_report}.tmp" "$health_report"
    else
        log_info "No performance baseline available"
        jq '.performance.baseline = {status: "missing"}' "$health_report" > "${health_report}.tmp" && mv "${health_report}.tmp" "$health_report"
    fi

    # Security checks
    log_info "Running security checks..."
    local security_issues_found=false

    # Check for insecure permissions
    if find "$PROJECT_ROOT" -type f -name "*.sh" -perm /o+w 2>/dev/null | head -5 | grep -q .; then
        log_warning "Found shell scripts with world-writable permissions"
        ((warnings++))
        security_issues_found=true
    fi

    # Check for hardcoded secrets (basic)
    if grep -r -i "password\|secret\|key.*=" "$PROJECT_ROOT/src" --include="*.cpp" --include="*.h" --include="*.cu" 2>/dev/null | grep -v "//.*password\|//.*secret\|//.*key.*=" | head -3 | grep -q .; then
        log_warning "Potential hardcoded secrets found in source code"
        ((warnings++))
        security_issues_found=true
    fi

    if [[ "$security_issues_found" == "true" ]]; then
        jq '.security.status = "warnings_found"' "$health_report" > "${health_report}.tmp" && mv "${health_report}.tmp" "$health_report"
    else
        jq '.security.status = "clean"' "$health_report" > "${health_report}.tmp" && mv "${health_report}.tmp" "$health_report"
    fi

    # Determine overall status
    local overall_status="healthy"
    local recommendations=()

    if [[ $critical_issues -gt 0 ]]; then
        overall_status="critical"
        recommendations+=("Address critical issues immediately")
    elif [[ $warnings -gt 5 ]]; then
        overall_status="degraded"
        recommendations+=("Address multiple warnings")
    elif [[ $warnings -gt 0 ]]; then
        overall_status="warnings"
        recommendations+=("Review warnings")
    fi

    # Add additional recommendations based on checks
    if [[ ! -d "$build_dir" ]]; then
        recommendations+=("Build the project before running validation")
    fi

    if [[ ! -f "$baseline_file" ]]; then
        recommendations+=("Establish performance baseline")
    fi

    # Update overall status and recommendations
    jq --arg status "$overall_status" --argjson recs "$(printf '%s\n' "${recommendations[@]}" | jq -R . | jq -s .)" \
        '.overall_status = $status | .recommendations = $recs' "$health_report" > "${health_report}.tmp" && mv "${health_report}.tmp" "$health_report"

    # Summary
    log_info "Dependency health check completed:"
    log_info "  Overall status: $overall_status"
    log_info "  Critical issues: $critical_issues"
    log_info "  Warnings: $warnings"
    log_info "  Report saved to: $health_report"

    if [[ $critical_issues -gt 0 ]]; then
        return 1
    else
        return 0
    fi
}

# Enhanced build validation
enhanced_build_validation() {
    log_info "Running enhanced build validation..."

    local build_dir="$PROJECT_ROOT/build"
    local build_log="$VALIDATION_LOGS_DIR/enhanced_build_$(date +%Y%m%d_%H%M%S).log"
    local build_result=0

    # Check if build directory exists
    if [[ ! -d "$build_dir" ]]; then
        log_info "Creating build directory..."
        mkdir -p "$build_dir"
    fi

    cd "$build_dir"

    # Clean previous build
    log_info "Cleaning previous build..."
    rm -rf ./*

    # Configure with detailed logging
    log_info "Configuring with CMake..."
    if ! cmake .. \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        -DENABLE_INTEGRATION_SYSTEM=ON \
        -DENABLE_DEPLOYMENT_SYSTEM=ON \
        -DSECP256K1_AVAILABLE=ON \
        2>&1 | tee "$build_log"; then
        log_error "CMake configuration failed"
        return 1
    fi

    # Build with detailed output
    log_info "Building with parallel compilation..."
    local parallel_jobs=$(nproc)
    local start_time=$(date +%s)

    if ! cmake --build . \
        --parallel "$parallel_jobs" \
        --verbose \
        2>&1 | tee -a "$build_log"; then
        log_error "Build failed"
        return 1
    fi

    local end_time=$(date +%s)
    local build_duration=$((end_time - start_time))

    log_success "Build completed successfully in ${build_duration}s"

    # Verify build artifacts
    log_info "Verifying build artifacts..."
    local artifacts_verified=0

    # Check main executable
    if [[ -x "$build_dir/Puzzle71Solver" ]]; then
        log_success "Puzzle71Solver executable found and executable"
        ((artifacts_verified++))
    else
        log_error "Puzzle71Solver executable not found or not executable"
    fi

    # Check test executable
    if [[ -x "$build_dir/puzzle71_tests" ]]; then
        log_success "puzzle71_tests executable found and executable"
        ((artifacts_verified++))
    else
        log_warning "puzzle71_tests executable not found (may be expected in offline mode)"
    fi

    # Check for libraries
    local lib_count=$(find "$build_dir" -name "*.so" -o -name "*.a" | wc -l)
    if [[ $lib_count -gt 0 ]]; then
        log_success "Found $lib_count library files"
        ((artifacts_verified++))
    else
        log_info "No library files found (may be expected)"
    fi

    # Check compile_commands.json
    if [[ -f "$build_dir/compile_commands.json" ]]; then
        log_success "compile_commands.json generated"
        ((artifacts_verified++))
    else
        log_warning "compile_commands.json not found"
    fi

    # Run basic smoke test
    log_info "Running basic smoke test..."
    if timeout 10 "$build_dir/Puzzle71Solver" --help >/dev/null 2>&1; then
        log_success "Basic smoke test passed"
        ((artifacts_verified++))
    else
        log_error "Basic smoke test failed"
    fi

    # Generate build summary
    local build_summary="$VALIDATION_UTILS_DIR/build_summary_$(date +%Y%m%d_%H%M%S).json"
    cat > "$build_summary" << EOF
{
  "build_timestamp": "$(date -u +"%Y-%m-%dT%H:%M:%SZ")",
  "build_duration_seconds": $build_duration,
  "parallel_jobs": $parallel_jobs,
  "build_type": "RelWithDebInfo",
  "artifacts_verified": $artifacts_verified,
  "total_checks": 5,
  "success_rate": $(awk "BEGIN {printf \"%.2f\", ($artifacts_verified / 5) * 100}"),
  "build_log": "$build_log"
}
EOF

    log_info "Build summary saved to: $build_summary"

    if [[ $artifacts_verified -ge 4 ]]; then
        log_success "Enhanced build validation passed ($artifacts_verified/5 artifacts verified)"
        return 0
    else
        log_error "Enhanced build validation failed ($artifacts_verified/5 artifacts verified)"
        return 1
    fi
}

# Comprehensive runtime validation
comprehensive_runtime_validation() {
    log_info "Running comprehensive runtime validation..."

    local test_binary="$PROJECT_ROOT/build/Puzzle71Solver"
    local runtime_log="$VALIDATION_LOGS_DIR/runtime_validation_$(date +%Y%m%d_%H%M%S).log"
    local validation_results=()
    local total_tests=0
    local passed_tests=0

    if [[ ! -x "$test_binary" ]]; then
        log_error "Puzzle71Solver binary not found or not executable"
        return 1
    fi

    # Test 1: Help functionality
    log_info "Testing help functionality..."
    ((total_tests++))
    if timeout 5 "$test_binary" --help > "$runtime_log.help" 2>&1; then
        log_success "Help functionality test passed"
        ((passed_tests++))
        validation_results+=("{\"test\": \"help\", \"status\": \"passed\"}")
    else
        log_error "Help functionality test failed"
        validation_results+=("{\"test\": \"help\", \"status\": \"failed\"}")
    fi

    # Test 2: Version information
    log_info "Testing version information..."
    ((total_tests++))
    if timeout 5 "$test_binary" --version > "$runtime_log.version" 2>&1; then
        log_success "Version information test passed"
        ((passed_tests++))
        validation_results+=("{\"test\": \"version\", \"status\": \"passed\"}")
    else
        log_error "Version information test failed"
        validation_results+=("{\"test\": \"version\", \"status\": \"failed\"}")
    fi

    # Test 3: Device enumeration
    log_info "Testing device enumeration..."
    ((total_tests++))
    if timeout 10 "$test_binary" --list-devices > "$runtime_log.devices" 2>&1; then
        log_success "Device enumeration test passed"
        ((passed_tests++))
        validation_results+=("{\"test\": \"device_enumeration\", \"status\": \"passed\"}")
    else
        log_error "Device enumeration test failed"
        validation_results+=("{\"test\": \"device_enumeration\", \"status\": \"failed\"}")
    fi

    # Test 4: Basic search functionality
    log_info "Testing basic search functionality..."
    ((total_tests++))
    if timeout 30 "$test_binary" \
        --device 0 \
        --range "0000000000000000000000000000000000000000000000000000000000000000:00000000000000000000000000000000000000000000000000000000000000FF" \
        --checkpoints 10 \
        > "$runtime_log.search" 2>&1; then
        log_success "Basic search test passed"
        ((passed_tests++))
        validation_results+=("{\"test\": \"basic_search\", \"status\": \"passed\"}")
    else
        log_error "Basic search test failed"
        validation_results+=("{\"test\": \"basic_search\", \"status\": \"failed\"}")
    fi

    # Test 5: Range validation
    log_info "Testing range validation..."
    ((total_tests++))
    if timeout 5 "$test_binary" \
        --device 0 \
        --range "invalid:range" \
        > "$runtime_log.invalid_range" 2>&1; then
        log_error "Range validation test failed (should have failed with invalid range)"
        validation_results+=("{\"test\": \"range_validation\", \"status\": \"failed\"}")
    else
        log_success "Range validation test passed (correctly rejected invalid range)"
        ((passed_tests++))
        validation_results+=("{\"test\": \"range_validation\", \"status\": \"passed\"}")
    fi

    # Test 6: GPU memory allocation
    log_info "Testing GPU memory allocation..."
    ((total_tests++))
    if timeout 10 "$test_binary" \
        --device 0 \
        --benchmark \
        --threads 1 \
        --range "0000000000000000000000000000000000000000000000000000000000000000:000000000000000000000000000000000000000000000000000000000000FFFF" \
        > "$runtime_log.gpu_alloc" 2>&1; then
        log_success "GPU memory allocation test passed"
        ((passed_tests++))
        validation_results+=("{\"test\": \"gpu_allocation\", \"status\": \"passed\"}")
    else
        log_error "GPU memory allocation test failed"
        validation_results+=("{\"test\": \"gpu_allocation\", \"status\": \"failed\"}")
    fi

    # Generate runtime validation report
    local runtime_report="$VALIDATION_UTILS_DIR/runtime_validation_$(date +%Y%m%d_%H%M%S).json"
    cat > "$runtime_report" << EOF
{
  "validation_timestamp": "$(date -u +"%Y-%m-%dT%H:%M:%SZ")",
  "binary_path": "$test_binary",
  "summary": {
    "total_tests": $total_tests,
    "passed_tests": $passed_tests,
    "failed_tests": $((total_tests - passed_tests)),
    "success_rate": $(awk "BEGIN {printf \"%.2f\", ($passed_tests / $total_tests) * 100}")
  },
  "test_results": $(printf '%s\n' "${validation_results[@]}" | jq -s .),
  "runtime_log": "$runtime_log"
}
EOF

    log_info "Runtime validation report saved to: $runtime_report"

    if [[ $passed_tests -ge $((total_tests - 1)) ]]; then  # Allow 1 failure
        log_success "Comprehensive runtime validation passed ($passed_tests/$total_tests tests passed)"
        return 0
    else
        log_error "Comprehensive runtime validation failed ($passed_tests/$total_tests tests passed)"
        return 1
    fi
}

# Cross-platform compatibility validation
cross_platform_compatibility_validation() {
    log_info "Running cross-platform compatibility validation..."

    local compatibility_report="$VALIDATION_UTILS_DIR/cross_platform_$(date +%Y%m%d_%H%M%S).json"
    local issues_found=0
    local warnings_found=0

    cat > "$compatibility_report" << 'EOF'
{
  "validation_timestamp": "",
  "platform_info": {
    "os": "",
    "architecture": "",
    "kernel_version": "",
    "distribution": ""
  },
  "toolchain_compatibility": {},
  "runtime_compatibility": {},
  "build_compatibility": {},
  "gpu_compatibility": {},
  "recommendations": []
}
EOF

    # Update timestamp
    jq --arg timestamp "$(date -u +"%Y-%m-%dT%H:%M:%SZ")" '.validation_timestamp = $timestamp' "$compatibility_report" > "${compatibility_report}.tmp" && mv "${compatibility_report}.tmp" "$compatibility_report"

    # Get platform information
    local os_info
    os_info=$(uname -a)
    local architecture=$(uname -m)
    local kernel_version=$(uname -r)

    jq --arg os_info "$os_info" --arg arch "$architecture" --arg kernel "$kernel_version" \
        '.platform_info.os = $os_info | .platform_info.architecture = $arch | .platform_info.kernel_version = $kernel' "$compatibility_report" > "${compatibility_report}.tmp" && mv "${compatibility_report}.tmp" "$compatibility_report"

    # Get distribution information
    if [[ -f /etc/os-release ]]; then
        local distro_info=$(source /etc/os-release && echo "$NAME $VERSION")
        jq --arg distro "$distro_info" '.platform_info.distribution = $distro' "$compatibility_report" > "${compatibility_report}.tmp" && mv "${compatibility_report}.tmp" "$compatibility_report"
    fi

    # Check toolchain compatibility
    log_info "Checking toolchain compatibility..."

    # GCC version
    local gcc_version
    if gcc_version=$(g++ --version 2>/dev/null | head -1); then
        log_debug "GCC version: $gcc_version"
        jq --arg version "$gcc_version" '.toolchain_compatibility.gcc = {status: "available", version: $version}' "$compatibility_report" > "${compatibility_report}.tmp" && mv "${compatibility_report}.tmp" "$compatibility_report"
    else
        log_warning "GCC not found"
        ((warnings_found++))
        jq '.toolchain_compatibility.gcc = {status: "missing", version: "unknown"}' "$compatibility_report" > "${compatibility_report}.tmp" && mv "${compatibility_report}.tmp" "$compatibility_report"
    fi

    # CUDA version compatibility
    local cuda_version
    if cuda_version=$(nvcc --version 2>/dev/null | grep "release" | head -1 | awk '{print $6}' | cut -d, -f1); then
        log_debug "CUDA version: $cuda_version"
        local cuda_major=$(echo "$cuda_version" | cut -d. -f1)
        local cuda_minor=$(echo "$cuda_version" | cut -d. -f2)

        # Check CUDA version compatibility
        local cuda_compatible="true"
        local cuda_notes=""

        if [[ $cuda_major -lt 11 ]]; then
            cuda_compatible="false"
            cuda_notes="CUDA version $cuda_version is below minimum requirement (11.0)"
            ((issues_found++))
        elif [[ $cuda_major -eq 11 && $cuda_minor -lt 2 ]]; then
            cuda_compatible="false"
            cuda_notes="CUDA version $cuda_version may have compatibility issues"
            ((warnings_found++))
        fi

        jq --arg version "$cuda_version" --arg compatible "$cuda_compatible" --arg notes "$cuda_notes" \
            '.toolchain_compatibility.cuda = {status: "available", version: $version, compatible: $compatible, notes: $notes}' "$compatibility_report" > "${compatibility_report}.tmp" && mv "${compatibility_report}.tmp" "$compatibility_report"
    else
        log_error "CUDA not found"
        ((issues_found++))
        jq '.toolchain_compatibility.cuda = {status: "missing", version: "unknown", compatible: false, notes: "CUDA is required for GPU acceleration"}' "$compatibility_report" > "${compatibility_report}.tmp" && mv "${compatibility_report}.tmp" "$compatibility_report"
    fi

    # Check runtime compatibility
    log_info "Checking runtime compatibility..."

    # Check GPU driver version
    local driver_version
    if driver_version=$(nvidia-smi --query-gpu=driver_version --format=csv,noheader,nounits 2>/dev/null | head -1); then
        log_debug "GPU driver version: $driver_version"
        jq --arg version "$driver_version" '.runtime_compatibility.gpu_driver = {status: "available", version: $version}' "$compatibility_report" > "${compatibility_report}.tmp" && mv "${compatibility_report}.tmp" "$compatibility_report"
    else
        log_warning "GPU driver version not available"
        ((warnings_found++))
        jq '.runtime_compatibility.gpu_driver = {status: "unavailable", version: "unknown"}' "$compatibility_report" > "${compatibility_report}.tmp" && mv "${compatibility_report}.tmp" "$compatibility_report"
    fi

    # Check GPU architecture compatibility
    local gpu_archs
    if gpu_archs=$(nvidia-smi --query-gpu=compute_cap --format=csv,noheader,nounits 2>/dev/null | tr '\n' ','); then
        gpu_archs=${gpu_archs%,}
        log_debug "GPU architectures: $gpu_archs"

        # Check if supported architectures are present
        local supported_archs=("7.5" "8.0" "8.6" "8.9" "9.0")
        local compatible_archs=()

        IFS=',' read -ra ARCH_ARRAY <<< "$gpu_archs"
        for arch in "${ARCH_ARRAY[@]}"; do
            for supported in "${supported_archs[@]}"; do
                if [[ "$arch" == "$supported" ]]; then
                    compatible_archs+=("$arch")
                    break
                fi
            done
        done

        if [[ ${#compatible_archs[@]} -gt 0 ]]; then
            jq --arg archs "$gpu_archs" --argjson compatible "$(printf '%s\n' "${compatible_archs[@]}" | jq -R . | jq -s .)" \
                '.runtime_compatibility.gpu_architecture = {status: "compatible", architectures: $archs, compatible_architectures: $compatible}' "$compatibility_report" > "${compatibility_report}.tmp" && mv "${compatibility_report}.tmp" "$compatibility_report"
        else
            ((warnings_found++))
            jq --arg archs "$gpu_archs" \
                '.runtime_compatibility.gpu_architecture = {status: "warning", architectures: $archs, compatible_architectures: [], notes: "No supported GPU architectures found"}' "$compatibility_report" > "${compatibility_report}.tmp" && mv "${compatibility_report}.tmp" "$compatibility_report"
        fi
    else
        log_warning "GPU architecture information not available"
        ((warnings_found++))
        jq '.runtime_compatibility.gpu_architecture = {status: "unavailable", architectures: "unknown"}' "$compatibility_report" > "${compatibility_report}.tmp" && mv "${compatibility_report}.tmp" "$compatibility_report"
    fi

    # Generate recommendations
    local recommendations=()

    if [[ $issues_found -gt 0 ]]; then
        recommendations+=("Address compatibility issues before proceeding")
    fi

    if [[ $warnings_found -gt 0 ]]; then
        recommendations+=("Review compatibility warnings")
    fi

    if [[ ${#compatible_archs[@]} -eq 0 ]]; then
        recommendations+=("Consider upgrading GPU hardware for better performance")
    fi

    # Update recommendations
    jq --argjson recs "$(printf '%s\n' "${recommendations[@]}" | jq -R . | jq -s .)" \
        '.recommendations = $recs' "$compatibility_report" > "${compatibility_report}.tmp" && mv "${compatibility_report}.tmp" "$compatibility_report"

    log_info "Cross-platform compatibility validation completed:"
    log_info "  Issues found: $issues_found"
    log_info "  Warnings found: $warnings_found"
    log_info "  Report saved to: $compatibility_report"

    if [[ $issues_found -gt 0 ]]; then
        return 1
    else
        return 0
    fi
}

# Memory leak validation
memory_leak_validation() {
    log_info "Running memory leak validation..."

    local test_binary="$PROJECT_ROOT/build/Puzzle71Solver"
    local valgrind_log="$VALIDATION_LOGS_DIR/valgrind_$(date +%Y%m%d_%H%M%S).log"
    local memory_report="$VALIDATION_UTILS_DIR/memory_validation_$(date +%Y%m%d_%H%M%S).json"

    if [[ ! -x "$test_binary" ]]; then
        log_error "Puzzle71Solver binary not found"
        return 1
    fi

    # Check if valgrind is available
    if ! command -v valgrind >/dev/null 2>&1; then
        log_warning "Valgrind not found, skipping memory leak validation"
        return 0
    fi

    log_info "Running valgrind memory analysis..."

    # Run valgrind with a short test
    local start_time=$(date +%s)
    timeout 60 valgrind \
        --tool=memcheck \
        --leak-check=full \
        --show-leak-kinds=all \
        --track-origins=yes \
        --verbose \
        --log-file="$valgrind_log" \
        "$test_binary" \
        --device 0 \
        --range "0000000000000000000000000000000000000000000000000000000000000000:00000000000000000000000000000000000000000000000000000000000000FF" \
        --checkpoints 10 \
        >/dev/null 2>&1 || true

    local end_time=$(date +%s)
    local valgrind_duration=$((end_time - start_time))

    # Parse valgrind results
    local errors_found=$(grep -c "ERROR SUMMARY:" "$valgrind_log" | awk '{print $4}' || echo "0")
    local leaks_found=$(grep -c "definitely lost:" "$valgrind_log" | awk '{print $3}' || echo "0")

    log_info "Valgrind analysis completed in ${valgrind_duration}s"
    log_info "Errors found: $errors_found"
    log_info "Memory leaks found: $leaks_found"

    # Generate memory validation report
    cat > "$memory_report" << EOF
{
  "validation_timestamp": "$(date -u +"%Y-%m-%dT%H:%M:%SZ")",
  "binary_path": "$test_binary",
  "analysis_duration_seconds": $valgrind_duration,
  "valgrind_log": "$valgrind_log",
  "results": {
    "errors_found": $errors_found,
    "leaks_found": $leaks_found,
    "validation_passed": $([ "$errors_found" -eq 0 ] && echo "true" || echo "false")
  }
}
EOF

    log_info "Memory validation report saved to: $memory_report"

    if [[ $errors_found -eq 0 ]]; then
        log_success "Memory leak validation passed"
        return 0
    else
        log_error "Memory leak validation failed ($errors_found errors found)"
        return 1
    fi
}

# Security vulnerability scan
security_vulnerability_scan() {
    log_info "Running security vulnerability scan..."

    local security_report="$VALIDATION_UTILS_DIR/security_scan_$(date +%Y%m%d_%H%M%S).json"
    local vulnerabilities_found=0
    local warnings_found=0

    cat > "$security_report" << 'EOF'
{
  "scan_timestamp": "",
  "scan_type": "basic_security_check",
  "vulnerabilities": [],
  "warnings": [],
  "recommendations": []
}
EOF

    # Update timestamp
    jq --arg timestamp "$(date -u +"%Y-%m-%dT%H:%M:%SZ")" '.scan_timestamp = $timestamp' "$security_report" > "${security_report}.tmp" && mv "${security_report}.tmp" "$security_report"

    # Check for common security issues
    log_info "Scanning for common security issues..."

    # 1. Check for insecure file permissions
    log_info "Checking file permissions..."
    local insecure_files=$(find "$PROJECT_ROOT" -type f \( -name "*.sh" -o -name "*.py" -o -name "*.cpp" -o -name "*.h" -o -name "*.cu" \) -perm /o+w 2>/dev/null | head -10)

    if [[ -n "$insecure_files" ]]; then
        log_warning "Found files with world-writable permissions"
        ((warnings_found++))
        while IFS= read -r file; do
            jq --arg file "$file" '.warnings += [{"type": "insecure_permissions", "file": $file, "severity": "medium"}]' "$security_report" > "${security_report}.tmp" && mv "${security_report}.tmp" "$security_report"
        done <<< "$insecure_files"
    fi

    # 2. Check for hardcoded secrets
    log_info "Checking for hardcoded secrets..."
    local secret_patterns=(
        "password.*="
        "secret.*="
        "api_key.*="
        "private_key.*="
        "token.*="
        "credential.*="
    )

    for pattern in "${secret_patterns[@]}"; do
        if grep -r -i "$pattern" "$PROJECT_ROOT/src" --include="*.cpp" --include="*.h" --include="*.cu" --include="*.py" 2>/dev/null | grep -v "//.*$pattern" | head -5 | grep -q .; then
            log_warning "Potential hardcoded secrets found matching pattern: $pattern"
            ((warnings_found++))
            jq --arg pattern "$pattern" '.warnings += [{"type": "potential_secrets", "pattern": $pattern, "severity": "high"}]' "$security_report" > "${security_report}.tmp" && mv "${security_report}.tmp" "$security_report"
        fi
    done

    # 3. Check for unsafe functions (basic C/C++ check)
    log_info "Checking for unsafe functions..."
    local unsafe_functions=("strcpy" "strcat" "gets" "sprintf" "vsprintf" "strncpy")

    for func in "${unsafe_functions[@]}"; do
        local unsafe_usage=$(grep -r "$func" "$PROJECT_ROOT/src" --include="*.cpp" --include="*.h" --include="*.cu" 2>/dev/null | head -3 || echo "")
        if [[ -n "$unsafe_usage" ]]; then
            log_warning "Potentially unsafe function usage found: $func"
            ((warnings_found++))
            jq --arg func "$func" '.warnings += [{"type": "unsafe_function", "function": $func, "severity": "medium"}]' "$security_report" > "${security_report}.tmp" && mv "${security_report}.tmp" "$security_report"
        fi
    done

    # 4. Check for weak cryptographic practices
    log_info "Checking cryptographic practices..."
    local weak_crypto_patterns=("md5" "sha1" "des\|rc4")

    for pattern in "${weak_crypto_patterns[@]}"; do
        if grep -r -i "$pattern" "$PROJECT_ROOT/src" --include="*.cpp" --include="*.h" --include="*.cu" 2>/dev/null | head -3 | grep -q .; then
            log_warning "Weak cryptographic algorithm found: $pattern"
            ((warnings_found++))
            jq --arg pattern "$pattern" '.warnings += [{"type": "weak_crypto", "algorithm": $pattern, "severity": "high"}]' "$security_report" > "${security_report}.tmp" && mv "${security_report}.tmp" "$security_report"
        fi
    done

    # 5. Check for buffer overflow vulnerabilities
    log_info "Checking for buffer overflow patterns..."
    local overflow_patterns=("malloc.*strcpy" "malloc.*strcat" "char.*\[[0-9]*\].*strcpy")

    for pattern in "${overflow_patterns[@]}"; do
        if grep -r -E "$pattern" "$PROJECT_ROOT/src" --include="*.cpp" --include="*.h" --include="*.cu" 2>/dev/null | head -3 | grep -q .; then
            log_warning "Potential buffer overflow pattern found: $pattern"
            ((vulnerabilities_found++))
            jq --arg pattern "$pattern" '.vulnerabilities += [{"type": "buffer_overflow", "pattern": $pattern, "severity": "high"}]' "$security_report" > "${security_report}.tmp" && mv "${security_report}.tmp" "$security_report"
        fi
    done

    # Generate recommendations
    local recommendations=()

    if [[ $vulnerabilities_found -gt 0 ]]; then
        recommendations+=("Address security vulnerabilities immediately")
    fi

    if [[ $warnings_found -gt 5 ]]; then
        recommendations+=("Review security warnings and improve code practices")
    fi

    recommendations+=("Consider using automated security scanning tools")
    recommendations+=("Implement secure coding practices")

    # Update recommendations
    jq --argjson recs "$(printf '%s\n' "${recommendations[@]}" | jq -R . | jq -s .)" \
        '.recommendations = $recs' "$security_report" > "${security_report}.tmp" && mv "${security_report}.tmp" "$security_report"

    log_info "Security vulnerability scan completed:"
    log_info "  Vulnerabilities found: $vulnerabilities_found"
    log_info "  Warnings found: $warnings_found"
    log_info "  Report saved to: $security_report"

    if [[ $vulnerabilities_found -gt 0 ]]; then
        return 1
    else
        return 0
    fi
}

# Generate comprehensive validation report
generate_comprehensive_validation_report() {
    log_info "Generating comprehensive validation report..."

    local report_file="$VALIDATION_REPORTS_DIR/comprehensive_validation_$(date +%Y%m%d_%H%M%S).md"

    # Find the latest validation reports
    local latest_pre_update=$(find "$VALIDATION_REPORTS_DIR" -name "validation_report_pre_update_*.json" -type f | sort -r | head -1)
    local latest_post_update=$(find "$VALIDATION_REPORTS_DIR" -name "validation_report_post_update_*.json" -type f | sort -r | head -1)
    local latest_dependency_health=$(find "$VALIDATION_UTILS_DIR" -name "dependency_health_*.json" -type f | sort -r | head -1)
    local latest_build_summary=$(find "$VALIDATION_UTILS_DIR" -name "build_summary_*.json" -type f | sort -r | head -1)

    cat > "$report_file" << EOF
# Comprehensive Validation Report

**Generated:** $(date -u +"%Y-%m-%dT%H:%M:%SZ")
**Project:** Puzzle71Solver
**Validation Framework:** T052 Dependency Update Validation

## Executive Summary

EOF

    # Add summary based on available reports
    if [[ -f "$latest_post_update" ]]; then
        local post_summary=$(jq '.summary' "$latest_post_update")
        local post_passed=$(jq '.summary.passed_tests' "$latest_post_update")
        local post_total=$(jq '.summary.total_tests' "$latest_post_update")
        local success_rate=$(jq '.summary.success_rate' "$latest_post_update")

        cat >> "$report_file" << EOF
- **Overall Status:** $(jq -r '.validation_passed' "$latest_post_update" | sed 's/true/✅ Passed/; s/false/❌ Failed/')
- **Test Results:** $post_passed/$post_total tests passed (${success_rate}% success rate)
- **Validation Level:** $(jq -r '.validation_level' "$latest_post_update")
EOF
    else
        cat >> "$report_file" << EOF
- **Overall Status:** ⚠️ No post-update validation results available
EOF
    fi

    cat >> "$report_file" << EOF

## Dependency Health Status

EOF

    if [[ -f "$latest_dependency_health" ]]; then
        local health_status=$(jq -r '.overall_status' "$latest_dependency_health")
        local health_issues=$(jq '.recommendations | length' "$latest_dependency_health")

        cat >> "$report_file" << EOF
- **Health Status:** $(echo "$health_status" | sed 's/healthy/✅ Healthy/; s/critical/❌ Critical/; s/degraded/⚠️ Degraded/; s/warnings/⚠️ Warnings/')
- **Recommendations:** $health_issues items to review

### Critical Dependencies

EOF

        # Check critical components
        local cuda_status=$(jq -r '.runtime_environment.cuda.status' "$latest_dependency_health")
        local cmake_status=$(jq -r '.build_system.cmake.status' "$latest_dependency_health")
        local gpu_status=$(jq -r '.runtime_environment.gpus.status' "$latest_dependency_health")

        cat >> "$report_file" << EOF
- **CUDA:** $(echo "$cuda_status" | sed 's/available/✅ Available/; s/missing/❌ Missing/')
- **CMake:** $(echo "$cmake_status" | sed 's/available/✅ Available/; s/missing/❌ Missing/')
- **GPU Devices:** $(echo "$gpu_status" | sed 's/available/✅ Available/; s/unavailable/❌ Unavailable/')

EOF
    else
        cat >> "$report_file" << EOF
- **Health Status:** ⚠️ No dependency health check performed
EOF
    fi

    cat >> "$report_file" << EOF

## Build System Status

EOF

    if [[ -f "$latest_build_summary" ]]; then
        local build_duration=$(jq '.build_duration_seconds' "$latest_build_summary")
        local build_success_rate=$(jq '.success_rate' "$latest_build_summary")
        local artifacts_verified=$(jq '.artifacts_verified' "$latest_build_summary")

        cat >> "$report_file" << EOF
- **Build Duration:** ${build_duration}s
- **Artifacts Verified:** $artifacts_verified/5
- **Success Rate:** ${build_success_rate}%

EOF
    else
        cat >> "$report_file" << EOF
- **Build Status:** ⚠️ No build validation performed
EOF
    fi

    cat >> "$report_file" << EOF

## Test Results

EOF

    if [[ -f "$latest_post_update" ]]; then
        cat >> "$report_file" << EOF
### Post-Update Validation Tests

$(jq -r '.test_results[] | "- \(.test): \(.status | ascii_upcase)"' "$latest_post_update" | sed 's/PASSED/✅ PASSED/; s/FAILED/❌ FAILED/')

EOF
    fi

    if [[ -f "$latest_pre_update" ]]; then
        cat >> "$report_file" << EOF
### Pre-Update Validation Tests

$(jq -r '.test_results[] | "- \(.test): \(.status | ascii_upcase)"' "$latest_pre_update" | sed 's/PASSED/✅ PASSED/; s/FAILED/❌ FAILED/')

EOF
    fi

    cat >> "$report_file" << EOF

## Recommendations

EOF

    if [[ -f "$latest_dependency_health" ]]; then
        cat >> "$report_file" << EOF
### Dependency Health Recommendations

$(jq -r '.recommendations[] | "- \(.)"' "$latest_dependency_health")

EOF
    fi

    cat >> "$report_file" << EOF
### General Recommendations

1. **Regular Validation:** Run validation tests regularly to ensure system health
2. **Performance Monitoring:** Monitor performance metrics for regression
3. **Security Scanning:** Implement regular security vulnerability scanning
4. **Backup Strategy:** Maintain regular backups of working configurations
5. **Documentation:** Keep validation reports and logs for audit purposes

## Validation Log Files

- **Validation Reports:** \`$VALIDATION_REPORTS_DIR/\`
- **Validation Logs:** \`$VALIDATION_LOGS_DIR/\`
- **Validation Utilities:** \`$VALIDATION_UTILS_DIR/\`

## Next Steps

1. Review any failed tests and address the underlying issues
2. Implement automated validation scheduling
3. Set up notification alerts for validation failures
4. Regular performance baseline updates
5. Continuous integration pipeline integration

---

*This report was generated by the T052 Dependency Update Validation and Testing Framework*
EOF

    log_success "Comprehensive validation report generated: $report_file"
    echo "$report_file"
}

# Generate summary report function (for main validation script)
generate_summary_report() {
    generate_comprehensive_validation_report >/dev/null
}

# Utility functions for specific validation scenarios

# Check GPU memory availability
check_gpu_memory() {
    log_info "Checking GPU memory availability..."

    local memory_report="$VALIDATION_UTILS_DIR/gpu_memory_$(date +%Y%m%d_%H%M%S).json"

    if ! command -v nvidia-smi >/dev/null 2>&1; then
        log_error "nvidia-smi not found"
        return 1
    fi

    # Get GPU memory information
    local gpu_memory_info=$(nvidia-smi --query-gpu=memory.total,memory.used,memory.free --format=csv,noheader,nounits 2>/dev/null)

    cat > "$memory_report" << EOF
{
  "check_timestamp": "$(date -u +"%Y-%m-%dT%H:%M:%SZ")",
  "gpus": []
}
EOF

    local gpu_index=0
    while IFS=, read -r total used free; do
        local usage_percent=$(awk "BEGIN {printf \"%.1f\", ($used / $total) * 100}")

        cat >> "$memory_report" << EOF
  {
    "gpu_index": $gpu_index,
    "memory_total_mb": $total,
    "memory_used_mb": $used,
    "memory_free_mb": $free,
    "memory_usage_percent": $usage_percent
  },
EOF

        log_info "GPU $gpu_index: ${used}MB/${total}MB used (${usage_percent}%)"
        ((gpu_index++))
    done <<< "$gpu_memory_info"

    # Remove trailing comma and close JSON
    sed -i '$ s/,$//' "$memory_report"
    echo "]" >> "$memory_report"

    return 0
}

# Validate CUDA installation
validate_cuda_installation() {
    log_info "Validating CUDA installation..."

    local cuda_report="$VALIDATION_UTILS_DIR/cuda_validation_$(date +%Y%m%d_%H%M%S).json"

    cat > "$cuda_report" << EOF
{
  "validation_timestamp": "$(date -u +"%Y-%m-%dT%H:%M:%SZ")",
  "cuda_status": "unknown",
  "cuda_version": "unknown",
  "gpu_count": 0,
  "driver_version": "unknown",
  "compatibility_issues": [],
  "recommendations": []
}
EOF

    # Check CUDA installation
    if command -v nvcc >/dev/null 2>&1; then
        local cuda_version=$(nvcc --version 2>/dev/null | grep "release" | head -1 | awk '{print $6}' | cut -d, -f1)
        jq --arg version "$cuda_version" --arg status "installed" '.cuda_version = $version | .cuda_status = $status' "$cuda_report" > "${cuda_report}.tmp" && mv "${cuda_report}.tmp" "$cuda_report"
        log_success "CUDA found: $cuda_version"
    else
        jq --arg status "not_installed" '.cuda_status = $status' "$cuda_report" > "${cuda_report}.tmp" && mv "${cuda_report}.tmp" "$cuda_report"
        log_error "CUDA not found"
        return 1
    fi

    # Check GPU count
    if command -v nvidia-smi >/dev/null 2>&1; then
        local gpu_count=$(nvidia-smi --query-gpu=count --format=csv,noheader,nounits 2>/dev/null)
        local driver_version=$(nvidia-smi --query-gpu=driver_version --format=csv,noheader,nounits 2>/dev/null | head -1)

        jq --argjson count "$gpu_count" --arg driver "$driver_version" '.gpu_count = $count | .driver_version = $driver' "$cuda_report" > "${cuda_report}.tmp" && mv "${cuda_report}.tmp" "$cuda_report"

        log_success "GPU count: $gpu_count, Driver: $driver_version"
    else
        log_warning "nvidia-smi not available"
        jq --arg driver "unknown" '.driver_version = $driver' "$cuda_report" > "${cuda_report}.tmp" && mv "${cuda_report}.tmp" "$cuda_report"
    fi

    # Check compatibility issues
    local cuda_major=$(echo "$cuda_version" | cut -d. -f1)
    local cuda_minor=$(echo "$cuda_version" | cut -d. -f2)

    if [[ $cuda_major -lt 11 ]]; then
        jq '.compatibility_issues += ["CUDA version below minimum requirement (11.0)"]' "$cuda_report" > "${cuda_report}.tmp" && mv "${cuda_report}.tmp" "$cuda_report"
    fi

    return 0
}

# Main utility function
main() {
    local command="${1:-help}"

    case "$command" in
        "init")
            init_validation_utils
            ;;
        "health-check")
            enhanced_dependency_health_check
            ;;
        "build-validation")
            enhanced_build_validation
            ;;
        "runtime-validation")
            comprehensive_runtime_validation
            ;;
        "cross-platform")
            cross_platform_compatibility_validation
            ;;
        "memory-leak")
            memory_leak_validation
            ;;
        "security-scan")
            security_vulnerability_scan
            ;;
        "report")
            generate_comprehensive_validation_report
            ;;
        "gpu-memory")
            check_gpu_memory
            ;;
        "cuda-validation")
            validate_cuda_installation
            ;;
        "help"|*)
            echo "T052 Validation Test Utilities"
            echo ""
            echo "USAGE:"
            echo "  $0 <command>"
            echo ""
            echo "COMMANDS:"
            echo "  init                 Initialize validation utilities"
            echo "  health-check         Run enhanced dependency health check"
            echo "  build-validation     Run enhanced build validation"
            echo "  runtime-validation   Run comprehensive runtime validation"
            echo "  cross-platform       Run cross-platform compatibility validation"
            echo "  memory-leak          Run memory leak validation (requires valgrind)"
            echo "  security-scan        Run security vulnerability scan"
            echo "  report               Generate comprehensive validation report"
            echo "  gpu-memory           Check GPU memory availability"
            echo "  cuda-validation      Validate CUDA installation"
            echo "  help                 Show this help message"
            echo ""
            ;;
    esac
}

# Execute main function
main "$@"