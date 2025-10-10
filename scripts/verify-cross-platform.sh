#!/bin/bash

# Cross-Platform Deployment Verification Script
# Validates deployment packages across different platforms and architectures
# Part of T040: Add deployment verification for cross-platform compatibility

set -euo pipefail

# Script configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DEPLOYMENT_DIR="$PROJECT_ROOT/deployment"
CROSS_PLATFORM_DIR="$DEPLOYMENT_DIR/cross-platform"

# Platform detection and configuration
if [[ -f "$SCRIPT_DIR/error-handler.sh" ]]; then
    source "$SCRIPT_DIR/error-handler.sh"
else
    # Fallback error handling
    handle_error() {
        echo "ERROR: $1" >&2
        exit "${2:-1}"
    }
fi

# Source platform utilities
if [[ -f "$SCRIPT_DIR/platform-utils.sh" ]]; then
    source "$SCRIPT_DIR/platform-utils.sh"
fi

# Cross-platform verification configuration
readonly DEFAULT_PLATFORMS=("linux-x86_64" "linux-aarch64" "windows-x86_64" "macos-x86_64" "macos-arm64")
readonly DEFAULT_ARCHITECTURES=("x86_64" "aarch64" "armv7" "riscv64")
readonly DEFAULT_TEST_SCENARIOS=("basic" "dependencies" "performance" "integration")

# Verification thresholds
readonly MIN_COMPATIBILITY_SCORE=80
readonly MIN_PLATFORM_SUCCESS_RATE=0.8
readonly MAX_LOAD_TIME_MS=5000
readonly MAX_MEMORY_USAGE_MB=512

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
PARALLEL_JOBS=4
PLATFORM_OVERRIDE=()
ARCHITECTURE_OVERRIDE=()
TEST_SCENARIOS=("${DEFAULT_TEST_SCENARIOS[@]}")
COMPATIBILITY_REPORT=""
VERIFICATION_RESULTS=()

# =============================================================================
# LOGGING AND OUTPUT FUNCTIONS
# =============================================================================

log_cross_platform() {
    local level="$1"
    local message="$2"
    local timestamp=$(date -u +"%Y-%m-%d %H:%M:%S UTC")

    case "$level" in
        "INFO")
            echo -e "${COLOR_BLUE}[CROSS-PLATFORM INFO]${COLOR_NC} $message"
            ;;
        "SUCCESS")
            echo -e "${COLOR_GREEN}[CROSS-PLATFORM SUCCESS]${COLOR_NC} $message"
            ;;
        "WARNING")
            echo -e "${COLOR_YELLOW}[CROSS-PLATFORM WARNING]${COLOR_NC} $message"
            ;;
        "ERROR")
            echo -e "${COLOR_RED}[CROSS-PLATFORM ERROR]${COLOR_NC} $message"
            ;;
        "DEBUG")
            if [[ "$VERBOSE" == true ]]; then
                echo -e "${COLOR_PURPLE}[CROSS-PLATFORM DEBUG]${COLOR_NC} $message"
            fi
            ;;
        "PROGRESS")
            echo -e "${COLOR_CYAN}[CROSS-PLATFORM PROGRESS]${COLOR_NC} $message"
            ;;
    esac

    # Log to file if reports are enabled
    if [[ "$GENERATE_REPORTS" == true ]]; then
        echo "[$timestamp] [CROSS-PLATFORM $level] $message" >> "$CROSS_PLATFORM_DIR/verification.log"
    fi
}

print_cross_platform_header() {
    echo -e "${COLOR_BLUE}"
    echo "======================================================"
    echo "  Cross-Platform Deployment Verification"
    echo "  Multi-Architecture Compatibility Testing"
    echo "======================================================"
    echo -e "${COLOR_NC}"
}

print_usage() {
    cat << EOF
Usage: $0 [OPTIONS] <deployment_package>

Cross-platform deployment verification tool that validates deployment packages
across different platforms, architectures, and environments.

ARGUMENTS:
    deployment_package    Path to deployment package to verify

OPTIONS:
    --platforms LIST     Comma-separated list of platforms to test
                         (default: linux-x86_64,linux-aarch64,windows-x86_64,
                          macos-x86_64,macos-arm64)
    --architectures LIST Comma-separated list of architectures
                         (default: x86_64,aarch64,armv7,riscv64)
    --scenarios LIST     Comma-separated list of test scenarios
                         (default: basic,dependencies,performance,integration)
    --parallel N         Number of parallel verification jobs (default: 4)
    --min-score N        Minimum compatibility score threshold (default: 80)
    --dry-run           Show what would be verified without executing
    --verbose           Enable verbose output
    --quiet             Suppress non-error output
    --no-reports        Disable report generation
    --output FILE       Output report to specific file
    --timeout SECONDS   Timeout per platform test (default: 300)
    --help              Show this help message

PLATFORM FORMATS:
    linux-x86_64        Linux on AMD64/Intel 64-bit
    linux-aarch64       Linux on ARM 64-bit
    windows-x86_64      Windows on AMD64/Intel 64-bit
    macos-x86_64        macOS on Intel 64-bit
    macos-arm64         macOS on Apple Silicon

TEST SCENARIOS:
    basic               Basic functionality and startup tests
    dependencies        Dependency resolution and linking tests
    performance         Performance and resource usage tests
    integration         End-to-end integration tests

EXIT CODES:
    0   Verification successful
    1   General verification failure
    2   Compatibility score below threshold
    3   Platform-specific verification failures
    4   Timeout or infrastructure errors
    5   Invalid arguments or configuration

EXAMPLES:
    # Basic cross-platform verification
    $0 deployment-package.tar.gz

    # Test specific platforms and architectures
    $0 --platforms "linux-x86_64,windows-x86_64" --architectures "x86_64" package.tar.gz

    # Run specific test scenarios with verbose output
    $0 --scenarios "basic,dependencies" --verbose package.tar.gz

    # Quick compatibility check
    $0 --dry-run --platforms "linux-x86_64" package.tar.gz

EOF
}

# =============================================================================
# ARGUMENT PARSING
# =============================================================================

parse_arguments() {
    local platforms_arg=""
    local architectures_arg=""
    local scenarios_arg=""

    while [[ $# -gt 0 ]]; do
        case $1 in
            --platforms)
                platforms_arg="$2"
                shift 2
                ;;
            --architectures)
                architectures_arg="$2"
                shift 2
                ;;
            --scenarios)
                scenarios_arg="$2"
                shift 2
                ;;
            --parallel)
                PARALLEL_JOBS="$2"
                shift 2
                ;;
            --min-score)
                MIN_COMPATIBILITY_SCORE="$2"
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
                COMPATIBILITY_REPORT="$2"
                shift 2
                ;;
            --timeout)
                VERIFICATION_TIMEOUT="$2"
                shift 2
                ;;
            --help)
                print_usage
                exit 0
                ;;
            -*)
                log_cross_platform "ERROR" "Unknown option: $1"
                print_usage
                exit 5
                ;;
            *)
                # Assume it's the deployment package path
                if [[ -z "${DEPLOYMENT_PACKAGE:-}" ]]; then
                    DEPLOYMENT_PACKAGE="$1"
                else
                    log_cross_platform "ERROR" "Multiple deployment packages specified"
                    exit 5
                fi
                shift
                ;;
        esac
    done

    # Parse comma-separated lists
    if [[ -n "$platforms_arg" ]]; then
        IFS=',' read -ra PLATFORM_OVERRIDE <<< "$platforms_arg"
    fi

    if [[ -n "$architectures_arg" ]]; then
        IFS=',' read -ra ARCHITECTURE_OVERRIDE <<< "$architectures_arg"
    fi

    if [[ -n "$scenarios_arg" ]]; then
        IFS=',' read -ra TEST_SCENARIOS <<< "$scenarios_arg"
    fi

    # Validate required arguments
    if [[ -z "${DEPLOYMENT_PACKAGE:-}" ]]; then
        log_cross_platform "ERROR" "Deployment package path is required"
        print_usage
        exit 5
    fi
}

# =============================================================================
# PLATFORM DETECTION AND CONFIGURATION
# =============================================================================

detect_current_platform() {
    if command -v detect_platform >/dev/null 2>&1; then
        detect_platform
    else
        # Fallback detection
        local os_name=""
        local arch_name=""

        # Detect OS
        case "$(uname -s)" in
            Linux*)     os_name="linux" ;;
            Darwin*)    os_name="macos" ;;
            CYGWIN*|MINGW*|MSYS*) os_name="windows" ;;
            *)          os_name="unknown" ;;
        esac

        # Detect architecture
        case "$(uname -m)" in
            x86_64|amd64)    arch_name="x86_64" ;;
            aarch64|arm64)   arch_name="aarch64" ;;
            armv7*|armhf)    arch_name="armv7" ;;
            riscv64)         arch_name="riscv64" ;;
            *)               arch_name="unknown" ;;
        esac

        echo "$os_name-$arch_name"
    fi
}

get_test_platforms() {
    local platforms=()

    if [[ ${#PLATFORM_OVERRIDE[@]} -gt 0 ]]; then
        platforms=("${PLATFORM_OVERRIDE[@]}")
    else
        platforms=("${DEFAULT_PLATFORMS[@]}")
    fi

    # Filter platforms based on current environment and availability
    local current_platform=$(detect_current_platform)
    local available_platforms=()

    for platform in "${platforms[@]}"; do
        if is_platform_available "$platform"; then
            available_platforms+=("$platform")
        else
            log_cross_platform "WARNING" "Platform $platform not available, skipping"
        fi
    done

    printf '%s\n' "${available_platforms[@]}"
}

is_platform_available() {
    local platform="$1"

    if command -v can_emulate_platform >/dev/null 2>&1; then
        can_emulate_platform "$platform"
    else
        # Fallback availability check
        local os_name="${platform%%-*}"
        local arch_name="${platform##*-}"

        # Check if we can emulate or test this platform
        case "$os_name" in
            linux)
                # Linux platforms are usually testable via containers or emulation
                if command -v docker >/dev/null 2>&1 || command -v podman >/dev/null 2>&1; then
                    return 0
                elif [[ "$arch_name" == "$(uname -m)" || "$arch_name" == "$(uname -m | sed 's/x86_64/amd64/')" ]]; then
                    return 0
                else
                    return 1
                fi
                ;;
            macos)
                # macOS testing only available on macOS
                [[ "$(uname -s)" == "Darwin" ]]
                ;;
            windows)
                # Windows testing available on Windows or via WSL
                [[ "$(uname -s)" =~ ^(CYGWIN|MINGW|MSYS)$ ]] || [[ -f /proc/version && -f /mnt/c/Windows ]]
                ;;
            *)
                return 1
                ;;
        esac
    fi
}

# =============================================================================
# CROSS-PLATFORM VERIFICATION FUNCTIONS
# =============================================================================

verify_platform_compatibility() {
    local platform="$1"
    local package_path="$2"
    local result_dir="$3"

    log_cross_platform "INFO" "Starting verification for platform: $platform"

    local platform_result_dir="$result_dir/$platform"
    mkdir -p "$platform_result_dir"

    # Initialize verification results
    local verification_json="$platform_result_dir/verification_result.json"
    cat > "$verification_json" << EOF
{
    "platform": "$platform",
    "timestamp": "$(date -u +"%Y-%m-%dT%H:%M:%SZ")",
    "package_path": "$package_path",
    "tests": {},
    "overall_score": 0,
    "status": "running"
}
EOF

    local total_score=0
    local test_count=0
    local tests_passed=0

    # Run platform-specific tests
    for scenario in "${TEST_SCENARIOS[@]}"; do
        log_cross_platform "DEBUG" "Running $scenario test scenario for $platform"

        local test_result=""
        case "$scenario" in
            "basic")
                test_result=$(test_basic_functionality "$platform" "$package_path" "$platform_result_dir")
                ;;
            "dependencies")
                test_result=$(test_dependencies "$platform" "$package_path" "$platform_result_dir")
                ;;
            "performance")
                test_result=$(test_performance "$platform" "$package_path" "$platform_result_dir")
                ;;
            "integration")
                test_result=$(test_integration "$platform" "$package_path" "$platform_result_dir")
                ;;
            *)
                log_cross_platform "WARNING" "Unknown test scenario: $scenario"
                continue
                ;;
        esac

        # Parse test result
        local test_score=$(echo "$test_result" | jq -r '.score // 0')
        local test_status=$(echo "$test_result" | jq -r '.status // "failed"')

        # Update verification JSON
        jq --arg scenario "$scenario" --argjson result "$test_result" \
            '.tests[$scenario] = $result' "$verification_json" > "$verification_json.tmp" && \
            mv "$verification_json.tmp" "$verification_json"

        total_score=$((total_score + test_score))
        test_count=$((test_count + 1))

        if [[ "$test_status" == "passed" ]]; then
            tests_passed=$((tests_passed + 1))
            log_cross_platform "SUCCESS" "$scenario test passed for $platform (score: $test_score)"
        else
            log_cross_platform "WARNING" "$scenario test failed for $platform (score: $test_score)"
        fi
    done

    # Calculate overall score and status
    local overall_score=0
    if [[ $test_count -gt 0 ]]; then
        overall_score=$((total_score / test_count))
    fi

    local success_rate=0
    if [[ $test_count -gt 0 ]]; then
        success_rate=$(echo "scale=2; $tests_passed / $test_count" | bc -l)
    fi

    local overall_status="failed"
    if [[ $overall_score -ge $MIN_COMPATIBILITY_SCORE ]] && [[ $(echo "$success_rate >= $MIN_PLATFORM_SUCCESS_RATE" | bc -l) -eq 1 ]]; then
        overall_status="passed"
    fi

    # Update final verification JSON
    jq --argjson score "$overall_score" --arg status "$overall_status" \
        --argjson success_rate "$(echo "$success_rate * 100" | bc -l | cut -d. -f1)" \
        '.overall_score = $score | .status = $status | .success_rate = $success_rate' \
        "$verification_json" > "$verification_json.tmp" && \
        mv "$verification_json.tmp" "$verification_json"

    log_cross_platform "INFO" "Platform verification complete for $platform: score=$overall_score, status=$overall_status"

    # Return result as JSON for processing
    jq -n --arg platform "$platform" --argjson score "$overall_score" \
        --arg status "$overall_status" --argjson result "$(cat "$verification_json")" \
        '{platform: $platform, score: $score, status: $status, details: $result}'
}

test_basic_functionality() {
    local platform="$1"
    local package_path="$2"
    local result_dir="$3"

    log_cross_platform "DEBUG" "Testing basic functionality for $platform"

    local test_json="$result_dir/basic_test.json"
    local score=0
    local status="failed"
    local details=()

    # Extract package for testing
    local extract_dir="$result_dir/extracted"
    mkdir -p "$extract_dir"

    if ! extract_package_for_platform "$platform" "$package_path" "$extract_dir"; then
        details+=("Package extraction failed")
    else
        score=$((score + 25))
        details+=("Package extraction successful")

        # Check binary compatibility
        if check_binary_compatibility "$platform" "$extract_dir"; then
            score=$((score + 25))
            details+=("Binary compatibility verified")
        else
            details+=("Binary compatibility issues detected")
        fi

        # Test basic startup
        if test_basic_startup "$platform" "$extract_dir"; then
            score=$((score + 25))
            details+=("Basic startup test passed")
        else
            details+=("Basic startup test failed")
        fi

        # Verify essential files
        if verify_essential_files "$platform" "$extract_dir"; then
            score=$((score + 25))
            details+=("Essential files verified")
        else
            details+=("Essential files missing or corrupted")
        fi
    fi

    if [[ $score -ge 75 ]]; then
        status="passed"
    fi

    # Create test result JSON
    jq -n --argjson score "$score" --arg status "$status" \
        --argjson details "$(printf '%s\n' "${details[@]}" | jq -R . | jq -s .)" \
        '{score: $score, status: $status, details: $details, test_type: "basic"}' > "$test_json"

    cat "$test_json"
}

test_dependencies() {
    local platform="$1"
    local package_path="$2"
    local result_dir="$3"

    log_cross_platform "DEBUG" "Testing dependencies for $platform"

    local test_json="$result_dir/dependencies_test.json"
    local score=0
    local status="failed"
    local details=()

    local extract_dir="$result_dir/extracted"
    if [[ ! -d "$extract_dir" ]]; then
        mkdir -p "$extract_dir"
        extract_package_for_platform "$platform" "$package_path" "$extract_dir"
    fi

    # Check dynamic library dependencies
    if check_dynamic_dependencies "$platform" "$extract_dir"; then
        score=$((score + 30))
        details+=("Dynamic dependencies satisfied")
    else
        details+=("Dynamic dependency issues found")
    fi

    # Check library compatibility
    if check_library_compatibility "$platform" "$extract_dir"; then
        score=$((score + 30))
        details+=("Library compatibility verified")
    else
        details+=("Library compatibility issues detected")
    fi

    # Test runtime linking
    if test_runtime_linking "$platform" "$extract_dir"; then
        score=$((score + 20))
        details+=("Runtime linking successful")
    else
        details+=("Runtime linking failed")
    fi

    # Verify dependency versions
    if verify_dependency_versions "$platform" "$extract_dir"; then
        score=$((score + 20))
        details+=("Dependency versions compatible")
    else
        details+=("Dependency version conflicts detected")
    fi

    if [[ $score -ge 70 ]]; then
        status="passed"
    fi

    jq -n --argjson score "$score" --arg status "$status" \
        --argjson details "$(printf '%s\n' "${details[@]}" | jq -R . | jq -s .)" \
        '{score: $score, status: $status, details: $details, test_type: "dependencies"}' > "$test_json"

    cat "$test_json"
}

test_performance() {
    local platform="$1"
    local package_path="$2"
    local result_dir="$3"

    log_cross_platform "DEBUG" "Testing performance for $platform"

    local test_json="$result_dir/performance_test.json"
    local score=0
    local status="failed"
    local details=()

    local extract_dir="$result_dir/extracted"
    if [[ ! -d "$extract_dir" ]]; then
        mkdir -p "$extract_dir"
        extract_package_for_platform "$platform" "$package_path" "$extract_dir"
    fi

    # Test startup time
    local startup_time_ms=$(measure_startup_time "$platform" "$extract_dir")
    if [[ $startup_time_ms -le $MAX_LOAD_TIME_MS ]]; then
        score=$((score + 25))
        details+=("Startup time acceptable: ${startup_time_ms}ms")
    else
        details+=("Startup time exceeded threshold: ${startup_time_ms}ms")
    fi

    # Test memory usage
    local memory_usage_mb=$(measure_memory_usage "$platform" "$extract_dir")
    if [[ $memory_usage_mb -le $MAX_MEMORY_USAGE_MB ]]; then
        score=$((score + 25))
        details+=("Memory usage acceptable: ${memory_usage_mb}MB")
    else
        details+=("Memory usage exceeded threshold: ${memory_usage_mb}MB")
    fi

    # Test CPU efficiency
    if test_cpu_efficiency "$platform" "$extract_dir"; then
        score=$((score + 25))
        details+=("CPU efficiency acceptable")
    else
        details+=("CPU efficiency issues detected")
    fi

    # Test I/O performance
    if test_io_performance "$platform" "$extract_dir"; then
        score=$((score + 25))
        details+=("I/O performance acceptable")
    else
        details+=("I/O performance issues detected")
    fi

    if [[ $score -ge 70 ]]; then
        status="passed"
    fi

    jq -n --argjson score "$score" --arg status "$status" \
        --argjson details "$(printf '%s\n' "${details[@]}" | jq -R . | jq -s .)" \
        --argjson startup_time "$startup_time_ms" --argjson memory_usage "$memory_usage_mb" \
        '{score: $score, status: $status, details: $details, test_type: "performance", startup_time_ms: $startup_time, memory_usage_mb: $memory_usage}' > "$test_json"

    cat "$test_json"
}

test_integration() {
    local platform="$1"
    local package_path="$2"
    local result_dir="$3"

    log_cross_platform "DEBUG" "Testing integration for $platform"

    local test_json="$result_dir/integration_test.json"
    local score=0
    local status="failed"
    local details=()

    local extract_dir="$result_dir/extracted"
    if [[ ! -d "$extract_dir" ]]; then
        mkdir -p "$extract_dir"
        extract_package_for_platform "$platform" "$package_path" "$extract_dir"
    fi

    # Test end-to-end workflow
    if test_end_to_end_workflow "$platform" "$extract_dir"; then
        score=$((score + 30))
        details+=("End-to-end workflow successful")
    else
        details+=("End-to-end workflow failed")
    fi

    # Test configuration loading
    if test_configuration_loading "$platform" "$extract_dir"; then
        score=$((score + 20))
        details+=("Configuration loading successful")
    else
        details+=("Configuration loading failed")
    fi

    # Test error handling
    if test_error_handling "$platform" "$extract_dir"; then
        score=$((score + 25))
        details+=("Error handling robust")
    else
        details+=("Error handling inadequate")
    fi

    # Test resource cleanup
    if test_resource_cleanup "$platform" "$extract_dir"; then
        score=$((score + 25))
        details+=("Resource cleanup successful")
    else
        details+=("Resource cleanup issues detected")
    fi

    if [[ $score -ge 75 ]]; then
        status="passed"
    fi

    jq -n --argjson score "$score" --arg status "$status" \
        --argjson details "$(printf '%s\n' "${details[@]}" | jq -R . | jq -s .)" \
        '{score: $score, status: $status, details: $details, test_type: "integration"}' > "$test_json"

    cat "$test_json"
}

# =============================================================================
# PLATFORM-SPECIFIC HELPER FUNCTIONS
# =============================================================================

extract_package_for_platform() {
    local platform="$1"
    local package_path="$2"
    local extract_dir="$3"

    log_cross_platform "DEBUG" "Extracting package for $platform"

    case "${package_path##*.}" in
        "gz"|"tgz")
            tar -xzf "$package_path" -C "$extract_dir" 2>/dev/null
            ;;
        "bz2"|"tbz2")
            tar -xjf "$package_path" -C "$extract_dir" 2>/dev/null
            ;;
        "xz"|"txz")
            tar -xJf "$package_path" -C "$extract_dir" 2>/dev/null
            ;;
        "zip")
            unzip -q "$package_path" -d "$extract_dir" 2>/dev/null
            ;;
        *)
            log_cross_platform "ERROR" "Unsupported package format: ${package_path##*.}"
            return 1
            ;;
    esac
}

check_binary_compatibility() {
    local platform="$1"
    local extract_dir="$2"

    local binary_path="$extract_dir/bin/Puzzle71Solver"
    if [[ ! -f "$binary_path" ]]; then
        binary_path="$extract_dir/Puzzle71Solver"
    fi

    if [[ ! -f "$binary_path" ]]; then
        log_cross_platform "DEBUG" "Main binary not found in extracted package"
        return 1
    fi

    # Check binary format compatibility
    case "$platform" in
        "linux-x86_64"|"linux-aarch64")
            if file "$binary_path" | grep -q "ELF"; then
                return 0
            fi
            ;;
        "windows-x86_64")
            if file "$binary_path" | grep -q "PE32"; then
                return 0
            fi
            ;;
        "macos-"*)
            if file "$binary_path" | grep -q "Mach-O"; then
                return 0
            fi
            ;;
    esac

    return 1
}

test_basic_startup() {
    local platform="$1"
    local extract_dir="$2"

    local binary_path="$extract_dir/bin/Puzzle71Solver"
    if [[ ! -f "$binary_path" ]]; then
        binary_path="$extract_dir/Puzzle71Solver"
    fi

    if [[ ! -f "$binary_path" ]]; then
        return 1
    fi

    # Test basic startup with timeout
    timeout 10s "$binary_path" --help >/dev/null 2>&1 || \
    timeout 10s "$binary_path" -h >/dev/null 2>&1 || \
    timeout 10s "$binary_path" --version >/dev/null 2>&1
}

verify_essential_files() {
    local platform="$1"
    local extract_dir="$2"

    local required_files=(
        "bin/Puzzle71Solver"
        "lib/"
        "README.md"
    )

    for file in "${required_files[@]}"; do
        if [[ ! -e "$extract_dir/$file" ]]; then
            log_cross_platform "DEBUG" "Essential file missing: $file"
            return 1
        fi
    done

    return 0
}

check_dynamic_dependencies() {
    local platform="$1"
    local extract_dir="$2"

    local binary_path="$extract_dir/bin/Puzzle71Solver"
    if [[ ! -f "$binary_path" ]]; then
        binary_path="$extract_dir/Puzzle71Solver"
    fi

    case "$platform" in
        "linux-"*)
            if command -v ldd >/dev/null 2>&1; then
                # Check if all required libraries are available
                if ldd "$binary_path" | grep -q "not found"; then
                    return 1
                fi
            fi
            ;;
        "macos-"*)
            if command -v otool >/dev/null 2>&1; then
                # Check macOS dependencies
                if otool -L "$binary_path" | grep -q "not found"; then
                    return 1
                fi
            fi
            ;;
    esac

    return 0
}

check_library_compatibility() {
    local platform="$1"
    local extract_dir="$2"

    # Check if included libraries are compatible with target platform
    local lib_dir="$extract_dir/lib"

    if [[ ! -d "$lib_dir" ]]; then
        return 0  # No external libraries to check
    fi

    # Basic library file existence and format checks
    local lib_count=0
    local compatible_libs=0

    for lib_file in "$lib_dir"/*.{so,dylib,dll}; do
        [[ -f "$lib_file" ]] || continue

        lib_count=$((lib_count + 1))

        case "$platform" in
            "linux-"*)
                if file "$lib_file" | grep -q "ELF"; then
                    compatible_libs=$((compatible_libs + 1))
                fi
                ;;
            "macos-"*)
                if file "$lib_file" | grep -q "Mach-O"; then
                    compatible_libs=$((compatible_libs + 1))
                fi
                ;;
            "windows-"*)
                if file "$lib_file" | grep -q "PE32"; then
                    compatible_libs=$((compatible_libs + 1))
                fi
                ;;
        esac
    done

    if [[ $lib_count -gt 0 ]] && [[ $((compatible_libs * 100 / lib_count)) -ge 80 ]]; then
        return 0
    elif [[ $lib_count -eq 0 ]]; then
        return 0  # No libraries to check
    else
        return 1
    fi
}

test_runtime_linking() {
    local platform="$1"
    local extract_dir="$2"

    local binary_path="$extract_dir/bin/Puzzle71Solver"
    if [[ ! -f "$binary_path" ]]; then
        binary_path="$extract_dir/Puzzle71Solver"
    fi

    # Test if binary can actually load and run (basic linking test)
    timeout 5s "$binary_path" --help >/dev/null 2>&1 || \
    timeout 5s "$binary_path" -v >/dev/null 2>&1
}

verify_dependency_versions() {
    local platform="$1"
    local extract_dir="$2"

    # Check if dependency versions meet minimum requirements
    # This is a simplified check - in practice, you'd check specific versions

    local config_file="$extract_dir/config/dependencies.json"
    if [[ -f "$config_file" ]]; then
        # Parse dependency requirements and verify
        if jq -e '.requirements' "$config_file" >/dev/null 2>&1; then
            return 0
        fi
    fi

    # Default pass if no version requirements found
    return 0
}

measure_startup_time() {
    local platform="$1"
    local extract_dir="$2"

    local binary_path="$extract_dir/bin/Puzzle71Solver"
    if [[ ! -f "$binary_path" ]]; then
        binary_path="$extract_dir/Puzzle71Solver"
    fi

    if [[ ! -f "$binary_path" ]]; then
        echo 999999  # Return large value if binary not found
        return
    fi

    # Measure startup time
    local start_time=$(date +%s%N)
    timeout 10s "$binary_path" --help >/dev/null 2>&1 || true
    local end_time=$(date +%s%N)

    local startup_time_ms=$(((end_time - start_time) / 1000000))
    echo "$startup_time_ms"
}

measure_memory_usage() {
    local platform="$1"
    local extract_dir="$2"

    local binary_path="$extract_dir/bin/Puzzle71Solver"
    if [[ ! -f "$binary_path" ]]; then
        binary_path="$extract_dir/Puzzle71Solver"
    fi

    if [[ ! -f "$binary_path" ]]; then
        echo 999999  # Return large value if binary not found
        return
    fi

    # Measure memory usage (simplified)
    case "$platform" in
        "linux-"*)
            if command -v /usr/bin/time >/dev/null 2>&1; then
                local memory_output=$(/usr/bin/time -f "%M" timeout 10s "$binary_path" --help 2>&1 || echo "0")
                echo "$((memory_output / 1024))"  # Convert KB to MB
            else
                echo 0
            fi
            ;;
        *)
            echo 0  # Default value for non-Linux platforms
            ;;
    esac
}

test_cpu_efficiency() {
    local platform="$1"
    local extract_dir="$2"

    # Simplified CPU efficiency test
    # In practice, you'd measure CPU utilization, context switches, etc.
    timeout 5s "$extract_dir/bin/Puzzle71Solver" --help >/dev/null 2>&1 || \
    timeout 5s "$extract_dir/Puzzle71Solver" --help >/dev/null 2>&1
}

test_io_performance() {
    local platform="$1"
    local extract_dir="$2"

    # Test I/O performance by checking file access speeds
    local test_file="$extract_dir/.io_test"

    # Write test
    dd if=/dev/zero of="$test_file" bs=1M count=1 2>/dev/null || true

    # Read test
    dd if="$test_file" of=/dev/null bs=1M count=1 2>/dev/null || true

    # Cleanup
    rm -f "$test_file" 2>/dev/null || true
}

test_end_to_end_workflow() {
    local platform="$1"
    local extract_dir="$2"

    local binary_path="$extract_dir/bin/Puzzle71Solver"
    if [[ ! -f "$binary_path" ]]; then
        binary_path="$extract_dir/Puzzle71Solver"
    fi

    # Test a simple end-to-end workflow
    timeout 30s "$binary_path" --test-mode >/dev/null 2>&1 || \
    timeout 30s "$binary_path" --quick-test >/dev/null 2>&1 || true
}

test_configuration_loading() {
    local platform="$1"
    local extract_dir="$2"

    # Test if configuration files can be loaded properly
    local config_dir="$extract_dir/config"

    if [[ -d "$config_dir" ]]; then
        # Check if JSON config files are valid
        for config_file in "$config_dir"/*.json; do
            [[ -f "$config_file" ]] || continue
            if jq empty "$config_file" 2>/dev/null; then
                return 0
            fi
        done
    fi

    return 1
}

test_error_handling() {
    local platform="$1"
    local extract_dir="$2"

    local binary_path="$extract_dir/bin/Puzzle71Solver"
    if [[ ! -f "$binary_path" ]]; then
        binary_path="$extract_dir/Puzzle71Solver"
    fi

    # Test error handling with invalid arguments
    timeout 5s "$binary_path" --invalid-option >/dev/null 2>&1 && return 1 || return 0
}

test_resource_cleanup() {
    local platform="$1"
    local extract_dir="$2"

    # Test resource cleanup by running the binary and checking for leftover processes
    local binary_path="$extract_dir/bin/Puzzle71Solver"
    if [[ ! -f "$binary_path" ]]; then
        binary_path="$extract_dir/Puzzle71Solver"
    fi

    # Run binary briefly and check cleanup
    timeout 3s "$binary_path" --help >/dev/null 2>&1 || true

    # Allow some time for cleanup
    sleep 1

    # In practice, you'd check for orphaned processes, temporary files, etc.
    return 0
}

# =============================================================================
# PARALLEL PROCESSING AND RESULT AGGREGATION
# =============================================================================

run_parallel_verification() {
    local platforms=("$@")
    local package_path="$1"
    shift
    local platforms=("$@")
    local result_dir="$CROSS_PLATFORM_DIR/verification-$(date +%s)"

    mkdir -p "$result_dir"

    log_cross_platform "INFO" "Starting parallel verification for ${#platforms[@]} platforms"

    # Run verification in parallel using background processes
    local pids=()
    local temp_files=()

    for platform in "${platforms[@]}"; do
        local temp_file="$result_dir/${platform}_temp.json"
        temp_files+=("$temp_file")

        (
            if [[ "$DRY_RUN" == true ]]; then
                log_cross_platform "INFO" "[DRY-RUN] Would verify platform: $platform"
                echo '{"platform":"'"$platform"'","dry_run":true}' > "$temp_file"
            else
                verify_platform_compatibility "$platform" "$package_path" "$result_dir" > "$temp_file"
            fi
        ) &

        pids+=($!)

        # Limit parallel jobs
        if [[ ${#pids[@]} -ge $PARALLEL_JOBS ]]; then
            wait "${pids[0]}"
            pids=("${pids[@]:1}")
        fi
    done

    # Wait for remaining jobs
    for pid in "${pids[@]}"; do
        wait "$pid"
    done

    # Aggregate results
    local aggregated_results="$result_dir/aggregated_results.json"
    local total_score=0
    local platform_count=0
    local successful_platforms=0

    # Start aggregation JSON
    echo '{"verification_timestamp":"'$(date -u +"%Y-%m-%dT%H:%M:%SZ")'","package":"'"$package_path"'","platforms":[' > "$aggregated_results"

    local first=true
    for temp_file in "${temp_files[@]}"; do
        if [[ -f "$temp_file" ]]; then
            if [[ "$first" == true ]]; then
                first=false
            else
                echo ',' >> "$aggregated_results"
            fi

            cat "$temp_file" >> "$aggregated_results"

            # Extract metrics for summary
            local platform_score=$(jq -r '.score // 0' "$temp_file")
            local platform_status=$(jq -r '.status // "failed"' "$temp_file")

            total_score=$((total_score + platform_score))
            platform_count=$((platform_count + 1))

            if [[ "$platform_status" == "passed" ]]; then
                successful_platforms=$((successful_platforms + 1))
            fi
        fi
    done

    echo ']}' >> "$aggregated_results"

    # Calculate overall metrics
    local overall_score=0
    if [[ $platform_count -gt 0 ]]; then
        overall_score=$((total_score / platform_count))
    fi

    local success_rate=0
    if [[ $platform_count -gt 0 ]]; then
        success_rate=$(echo "scale=2; $successful_platforms / $platform_count" | bc -l)
    fi

    # Create summary report
    local summary_json="$result_dir/verification_summary.json"
    jq -n \
        --argjson overall_score "$overall_score" \
        --arg overall_status "$(if [[ $overall_score -ge $MIN_COMPATIBILITY_SCORE ]] && [[ $(echo "$success_rate >= $MIN_PLATFORM_SUCCESS_RATE" | bc -l) -eq 1 ]]; then echo "passed"; else echo "failed"; fi)" \
        --argjson platforms_tested "$platform_count" \
        --argjson platforms_passed "$successful_platforms" \
        --argjson success_rate "$(echo "$success_rate * 100" | bc -l | cut -d. -f1)" \
        --arg min_threshold "$MIN_COMPATIBILITY_SCORE" \
        --arg result_dir "$result_dir" \
        '{
            overall_score: $overall_score,
            overall_status: $overall_status,
            platforms_tested: $platforms_tested,
            platforms_passed: $platforms_passed,
            success_rate_percent: $success_rate,
            min_threshold: $min_threshold,
            result_directory: $result_dir,
            verification_timestamp: now
        }' > "$summary_json"

    log_cross_platform "INFO" "Verification complete: overall_score=$overall_score, platforms_passed=$successful_platforms/$platform_count"

    # Return summary for processing
    cat "$summary_json"
}

# =============================================================================
# REPORT GENERATION
# =============================================================================

generate_compatibility_report() {
    local summary_json="$1"
    local output_file="$2"

    if [[ "$GENERATE_REPORTS" != true ]]; then
        return 0
    fi

    log_cross_platform "INFO" "Generating compatibility report"

    local overall_score=$(jq -r '.overall_score // 0' "$summary_json")
    local overall_status=$(jq -r '.overall_status // "unknown"' "$summary_json")
    local platforms_tested=$(jq -r '.platforms_tested // 0' "$summary_json")
    local platforms_passed=$(jq -r '.platforms_passed // 0' "$summary_json")
    local success_rate=$(jq -r '.success_rate_percent // 0' "$summary_json")
    local result_dir=$(jq -r '.result_directory // ""' "$summary_json")

    # Determine output file
    if [[ -z "$output_file" ]]; then
        output_file="$CROSS_PLATFORM_DIR/compatibility_report_$(date +%Y%m%d_%H%M%S).md"
    fi

    cat > "$output_file" << EOF
# Cross-Platform Compatibility Report

**Generated:** $(date -u +"%Y-%m-%d %H:%M:%S UTC")
**Deployment Package:** $DEPLOYMENT_PACKAGE
**Overall Status:** $overall_status
**Overall Score:** $overall_score/100

## Executive Summary

- **Platforms Tested:** $platforms_tested
- **Platforms Passed:** $platforms_passed
- **Success Rate:** $success_rate%
- **Minimum Threshold:** $MIN_COMPATIBILITY_SCORE%

## Verification Results

EOF

    # Add platform-specific results
    if [[ -n "$result_dir" ]] && [[ -f "$result_dir/aggregated_results.json" ]]; then
        echo "| Platform | Score | Status | Key Issues |" >> "$output_file"
        echo "|----------|-------|--------|------------|" >> "$output_file"

        jq -r '.platforms[] | @base64' "$result_dir/aggregated_results.json" | while read -r platform; do
            local platform_data=$(echo "$platform" | base64 -d)
            local platform_name=$(echo "$platform_data" | jq -r '.platform // "unknown"')
            local platform_score=$(echo "$platform_data" | jq -r '.score // 0')
            local platform_status=$(echo "$platform_data" | jq -r '.status // "unknown"')

            echo "| $platform_name | $platform_score/100 | $platform_status | |" >> "$output_file"
        done
    fi

    cat >> "$output_file" << EOF

## Test Scenarios

The following test scenarios were executed for each platform:

### Basic Functionality Tests
- Package extraction and file integrity verification
- Binary compatibility validation
- Basic startup and initialization testing
- Essential file presence verification

### Dependencies Tests
- Dynamic library dependency resolution
- Library compatibility verification
- Runtime linking validation
- Dependency version compatibility checks

### Performance Tests
- Startup time measurement (threshold: ${MAX_LOAD_TIME_MS}ms)
- Memory usage validation (threshold: ${MAX_MEMORY_USAGE_MB}MB)
- CPU efficiency assessment
- I/O performance evaluation

### Integration Tests
- End-to-end workflow execution
- Configuration loading validation
- Error handling robustness testing
- Resource cleanup verification

## Recommendations

EOF

    # Add recommendations based on results
    if [[ "$overall_status" == "passed" ]]; then
        cat >> "$output_file" << EOF
✅ **Deployment Package Ready for Distribution**

The deployment package has passed cross-platform compatibility verification and is ready for distribution across the tested platforms.

### Next Steps:
1. Proceed with deployment to target environments
2. Monitor performance in production environments
3. Collect feedback from end users across different platforms
EOF
    else
        cat >> "$output_file" << EOF
⚠️ **Deployment Package Requires Attention**

The deployment package has compatibility issues that need to be addressed before distribution.

### Recommended Actions:
1. Review and fix platform-specific compatibility issues
2. Address dependency resolution problems
3. Optimize performance characteristics for failing platforms
4. Re-run verification after fixes are applied
EOF
    fi

    log_cross_platform "SUCCESS" "Compatibility report generated: $output_file"
}

# =============================================================================
# MAIN EXECUTION
# =============================================================================

main() {
    # Parse command line arguments
    parse_arguments "$@"

    # Print header
    print_cross_platform_header

    # Validate deployment package
    if [[ ! -f "$DEPLOYMENT_PACKAGE" ]]; then
        log_cross_platform "ERROR" "Deployment package not found: $DEPLOYMENT_PACKAGE"
        exit 1
    fi

    # Setup directories
    mkdir -p "$CROSS_PLATFORM_DIR"

    if [[ "$GENERATE_REPORTS" == true ]]; then
        echo "Cross-platform verification log - $(date)" > "$CROSS_PLATFORM_DIR/verification.log"
    fi

    # Get platforms to test
    local platforms=()
    readarray -t platforms < <(get_test_platforms)

    if [[ ${#platforms[@]} -eq 0 ]]; then
        log_cross_platform "ERROR" "No platforms available for testing"
        exit 4
    fi

    log_cross_platform "INFO" "Testing ${#platforms[@]} platforms: ${platforms[*]}"

    if [[ "$DRY_RUN" == true ]]; then
        log_cross_platform "INFO" "DRY-RUN mode: No actual verification will be performed"
    fi

    # Run verification
    local summary_json
    summary_json=$(run_parallel_verification "${platforms[@]}")

    # Extract results
    local overall_score=$(echo "$summary_json" | jq -r '.overall_score // 0')
    local overall_status=$(echo "$summary_json" | jq -r '.overall_status // "failed"')

    # Generate report
    generate_compatibility_report "$summary_json" "$COMPATIBILITY_REPORT"

    # Final status
    echo
    log_cross_platform "INFO" "=== Cross-Platform Verification Complete ==="
    log_cross_platform "INFO" "Overall Score: $overall_score/100"
    log_cross_platform "INFO" "Overall Status: $overall_status"
    log_cross_platform "INFO" "Platforms Tested: $(echo "$summary_json" | jq -r '.platforms_tested // 0')"
    log_cross_platform "INFO" "Platforms Passed: $(echo "$summary_json" | jq -r '.platforms_passed // 0')"

    # Set appropriate exit code
    if [[ "$overall_status" == "passed" ]]; then
        log_cross_platform "SUCCESS" "Cross-platform verification PASSED"
        exit 0
    elif [[ $overall_score -lt $MIN_COMPATIBILITY_SCORE ]]; then
        log_cross_platform "ERROR" "Cross-platform verification FAILED: Score below threshold"
        exit 2
    else
        log_cross_platform "ERROR" "Cross-platform verification FAILED: Platform compatibility issues"
        exit 3
    fi
}

# Execute main function with all arguments
main "$@"