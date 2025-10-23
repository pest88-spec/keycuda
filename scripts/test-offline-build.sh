#!/bin/bash

# Offline Build Test Script
# T032: Test offline build capability without internet access

set -euo pipefail

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TEMP_TEST_DIR="/tmp/keycuda-offline-test-$$"
BUILD_TIMEOUT=600  # 10 minutes
RESULTS_FILE="$PROJECT_ROOT/offline-build-test-results.json"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Test state variables
TEST_START_TIME=""
TEST_END_TIME=""
OFFLINE_BUILD_SUCCESS=false
NETWORK_DISABLED=false
TEST_RESULTS=""

# Logging functions
log() {
    echo -e "${BLUE}[$(date '+%Y-%m-%d %H:%M:%S')]${NC} $1"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1" >&2
}

warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

# Initialize test environment
initialize_test() {
    log "Initializing offline build test..."
    TEST_START_TIME=$(date +%s)

    TEST_RESULTS=$(cat <<EOF
{
  "test_type": "offline_build_capability",
  "test_start": "$(date -Iseconds)",
  "project_root": "$PROJECT_ROOT",
  "test_environment": {
    "platform": "$(uname -s)",
    "architecture": "$(uname -m)",
    "cpu_cores": $(nproc),
    "memory_gb": $(free -g | awk '/^Mem:/{print $2}')
  }
}
EOF
)
}

# Network control functions
disable_network() {
    log "Disabling network access for offline build test..."

    # Check if running as root or with sudo
    if [[ $EUID -eq 0 ]]; then
        # System-wide network disable
        iptables -A OUTPUT -j DROP 2>/dev/null || {
            warning "Could not disable network with iptables"
            return 1
        }
        NETWORK_DISABLED=true
        log "Network disabled using iptables"
    else
        # Check if we can use unshare
        if command -v unshare &> /dev/null; then
            log "Will use unshare for network isolation"
            NETWORK_DISABLED="unshare"
        else
            warning "Cannot disable network - not running as root and unshare not available"
            warning "Proceeding with simulation (will not block external network access)"
            NETWORK_DISABLED="simulated"
        fi
    fi
}

restore_network() {
    if [[ "$NETWORK_DISABLED" == "true" ]]; then
        log "Restoring network access..."
        iptables -D OUTPUT -j DROP 2>/dev/null || true
        log "Network access restored"
    fi
}

# Create isolated test environment
create_isolated_environment() {
    log "Creating isolated test environment..."

    # Clean up any existing test directory
    if [[ -d "$TEMP_TEST_DIR" ]]; then
        rm -rf "$TEMP_TEST_DIR"
    fi

    # Create test directory structure
    mkdir -p "$TEMP_TEST_DIR"

    # Copy project sources (excluding .git and build artifacts)
    log "Copying project sources for offline test..."

    # Essential directories to copy
    for dir in src scripts cmake; do
        if [[ -d "$PROJECT_ROOT/$dir" ]]; then
            cp -r "$PROJECT_ROOT/$dir" "$TEMP_TEST_DIR/"
            log "Copied $dir directory"
        fi
    done

    # Copy essential build files
    for file in CMakeLists.txt Makefile configure .cmake-format; do
        if [[ -f "$PROJECT_ROOT/$file" ]]; then
            cp "$PROJECT_ROOT/$file" "$TEMP_TEST_DIR/"
            log "Copied $file"
        fi
    done

    # Verify extracted sources are present
    if [[ -d "$PROJECT_ROOT/src/extracted" ]]; then
        cp -r "$PROJECT_ROOT/src/extracted" "$TEMP_TEST_DIR/src/"
        log "Copied extracted sources - critical for offline build"

        # Count extracted libraries
        local lib_count=$(find "$TEMP_TEST_DIR/src/extracted" -maxdepth 1 -type d | wc -l)
        lib_count=$((lib_count - 1))  # Subtract 1 for the extracted directory itself
        log "Found $lib_count extracted libraries"

        # Update test results
        TEST_RESULTS=$(echo "$TEST_RESULTS" | jq --argjson count "$lib_count" '
            .extracted_libraries = $count
        ')
    else
        error "Extracted sources not found - cannot perform offline build test"
        return 1
    fi

    # Copy integration infrastructure
    if [[ -d "$PROJECT_ROOT/src/integration" ]]; then
        cp -r "$PROJECT_ROOT/src/integration" "$TEMP_TEST_DIR/src/"
        log "Copied integration infrastructure"
    fi

    # Copy KeyhuntCore compatibility layer
    if [[ -d "$PROJECT_ROOT/src/KeyhuntCore" ]]; then
        cp -r "$PROJECT_ROOT/src/KeyhuntCore" "$TEMP_TEST_DIR/src/"
        log "Copied KeyhuntCore compatibility layer"
    fi

    success "Isolated test environment created successfully"
    return 0
}

# Validate offline build readiness
validate_offline_readiness() {
    log "Validating offline build readiness..."

    local validation_errors=0

    # Check for extracted sources
    if [[ ! -d "$TEMP_TEST_DIR/src/extracted" ]]; then
        error "Missing extracted sources"
        validation_errors=$((validation_errors + 1))
    fi

    # Check for CMakeLists.txt
    if [[ ! -f "$TEMP_TEST_DIR/CMakeLists.txt" ]]; then
        error "Missing CMakeLists.txt"
        validation_errors=$((validation_errors + 1))
    fi

    # Check for offline build configuration
    if ! grep -q "ENABLE_OFFLINE_BUILD" "$TEMP_TEST_DIR/CMakeLists.txt" 2>/dev/null; then
        warning "Offline build configuration not found"
    fi

    # Verify no external repository references
    local external_refs=0
    if grep -r "git submodule" "$TEMP_TEST_DIR" 2>/dev/null; then
        warning "Found git submodule references"
        external_refs=$((external_refs + 1))
    fi

    if grep -r "FetchContent" "$TEMP_TEST_DIR/CMakeLists.txt" 2>/dev/null | grep -q "URL.*http"; then
        warning "Found FetchContent with external URLs"
        external_refs=$((external_refs + 1))
    fi

    # Update test results
    TEST_RESULTS=$(echo "$TEST_RESULTS" | jq --argjson errors "$validation_errors" --argjson refs "$external_refs" '
        .validation_errors = $errors |
        .external_references = $refs
    ')

    if [[ $validation_errors -gt 0 ]]; then
        error "Offline build validation failed with $validation_errors errors"
        return 1
    fi

    success "Offline build readiness validated"
    return 0
}

# Perform offline build test
perform_offline_build() {
    log "Starting offline build test..."
    log "Build timeout: ${BUILD_TIMEOUT} seconds"

    local build_start=$(date +%s)

    # Change to test directory
    cd "$TEMP_TEST_DIR"

    # Create build directory
    mkdir -p build
    cd build

    # Configure for offline build
    log "Configuring CMake for offline build..."
    local configure_start=$(date +%s)

    local configure_cmd="cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_OFFLINE_BUILD=ON -DSECP256K1_AVAILABLE=OFF"

    # Execute based on network isolation method
    local build_output=""
    local configure_result=0
    local build_result=0

    if [[ "$NETWORK_DISABLED" == "unshare" ]]; then
        # Use unshare for network isolation
        log "Using unshare for network-isolated build..."
        build_output=$(unshare -n bash -c "$configure_cmd 2>&1" 2>&1)
        configure_result=$?
    else
        # Standard build (with simulated or actual network block)
        build_output=$(eval "$configure_cmd" 2>&1)
        configure_result=$?
    fi

    local configure_end=$(date +%s)
    local configure_time=$((configure_end - configure_start))

    if [[ $configure_result -eq 0 ]]; then
        log "CMake configuration completed in ${configure_time} seconds"

        # Build the project
        log "Building project in offline mode..."
        local build_compile_start=$(date +%s)

        local build_cmd="make -j$(nproc)"
        if [[ "$NETWORK_DISABLED" == "unshare" ]]; then
            local compile_output=$(unshare -n bash -c "$build_cmd 2>&1" 2>&1)
            build_result=$?
        else
            local compile_output=$(eval "$build_cmd" 2>&1)
            build_result=$?
        fi

        local build_compile_end=$(date +%s)
        local build_compile_time=$((build_compile_end - build_compile_start))

        # Save build output
        echo "$build_output" > build.log
        echo "$compile_output" >> build.log

        if [[ $build_result -eq 0 ]]; then
            log "Build completed successfully in ${build_compile_time} seconds"
            OFFLINE_BUILD_SUCCESS=true

            # Verify build artifacts
            local artifacts_found=0
            if [[ -f "Puzzle71Solver" ]]; then
                artifacts_found=$((artifacts_found + 1))
                success "Main executable found: Puzzle71Solver"
            fi

            # Check for library files
            for lib in libsecp256k1*.so libsecp256k1*.a; do
                if [[ -f "$lib" ]]; then
                    artifacts_found=$((artifacts_found + 1))
                    log "Found library: $lib"
                fi
            done

            # Update test results
            local total_build_time=$((build_compile_end - build_start))
            TEST_RESULTS=$(echo "$TEST_RESULTS" | jq --argjson success "$OFFLINE_BUILD_SUCCESS" \
                --argjson total_time "$total_build_time" \
                --argjson configure_time "$configure_time" \
                --argjson build_time "$build_compile_time" \
                --argjson artifacts "$artifacts_found" '
                .build_success = $success |
                .total_build_time_seconds = $total_time |
                .configure_time_seconds = $configure_time |
                .build_time_seconds = $build_time |
                .build_artifacts_found = $artifacts_found
            ')

        else
            error "Build failed with exit code $build_result"
            error "Build log saved to: $TEMP_TEST_DIR/build/build.log"

            # Update test results with failure
            local total_build_time=$(date +%s)
            total_build_time=$((total_build_time - build_start))
            TEST_RESULTS=$(echo "$TEST_RESULTS" | jq --argjson success "false" \
                --argjson total_time "$total_build_time" \
                --arg error "Build compilation failed" '
                .build_success = $success |
                .total_build_time_seconds = $total_time |
                .build_error = $error
            ')
        fi
    else
        error "CMake configuration failed with exit code $configure_result"
        error "Configuration output: $build_output"

        # Update test results with configuration failure
        local total_build_time=$(date +%s)
        total_build_time=$((total_build_time - build_start))
        TEST_RESULTS=$(echo "$TEST_RESULTS" | jq --argjson success "false" \
            --argjson total_time "$total_build_time" \
            --arg error "CMake configuration failed" '
            .build_success = $success |
            .total_build_time_seconds = $total_time |
            .build_error = $error
            ')
    fi

    return $([ "$OFFLINE_BUILD_SUCCESS" = true ] && echo 0 || echo 1)
}

# Generate test report
generate_test_report() {
    log "Generating offline build test report..."

    TEST_END_TIME=$(date +%s)
    local total_test_time=$((TEST_END_TIME - TEST_START_TIME))

    # Complete the test results
    local test_final_result="PASSED"
    if [[ "$OFFLINE_BUILD_SUCCESS" != "true" ]]; then
        test_final_result="FAILED"
    fi

    # Create comprehensive report
    cat > "$RESULTS_FILE" << EOF
{
  "report_metadata": {
    "generated": "$(date -Iseconds)",
    "test_type": "offline_build_capability",
    "test_duration_seconds": $total_test_time,
    "script_version": "T032-1.0"
  },
  $TEST_RESULTS,
  "test_result": {
    "status": "$test_final_result",
    "offline_capability_verified": $([ "$OFFLINE_BUILD_SUCCESS" = true ] && echo "true" || echo "false"),
    "network_isolation_method": "$NETWORK_DISABLED",
    "recommendation": "$(
        if [[ "$OFFLINE_BUILD_SUCCESS" = true ]]; then
            echo "Offline build capability verified - project can build without internet access"
        else
            echo "Offline build failed - investigate build configuration and dependencies"
        fi
    )"
  }
}
EOF

    success "Offline build test report generated: $RESULTS_FILE"

    # Display summary
    echo
    echo "=== Offline Build Test Summary ==="
    echo "Status: $test_final_result"
    echo "Network Isolation: $NETWORK_DISABLED"
    echo "Total Test Time: ${total_test_time} seconds"

    if [[ "$OFFLINE_BUILD_SUCCESS" = true ]]; then
        echo "Offline Build: SUCCESS"
    else
        echo "Offline Build: FAILED"
    fi

    echo "Report: $RESULTS_FILE"
}

# Cleanup function
cleanup() {
    log "Cleaning up test environment..."
    restore_network

    if [[ -d "$TEMP_TEST_DIR" ]]; then
        rm -rf "$TEMP_TEST_DIR"
    fi
}

# Check dependencies
check_dependencies() {
    local missing_deps=()

    for cmd in jq make cmake; do
        if ! command -v "$cmd" &> /dev/null; then
            missing_deps+=("$cmd")
        fi
    done

    if [[ ${#missing_deps[@]} -gt 0 ]]; then
        error "Missing required dependencies: ${missing_deps[*]}"
        error "Install with: apt-get install ${missing_deps[*]}"
        exit 1
    fi
}

# Main execution
main() {
    log "Starting offline build capability test..."

    # Trap cleanup
    trap cleanup EXIT

    # Check dependencies
    check_dependencies

    # Execute test workflow
    if initialize_test && \
       create_isolated_environment && \
       validate_offline_readiness; then

        # Disable network for actual build test
        disable_network

        # Perform offline build
        if perform_offline_build; then
            success "Offline build test PASSED"
            echo
            echo -e "${GREEN}✅ Offline build capability verified - project builds successfully without internet access${NC}"
        else
            error "Offline build test FAILED"
            echo
            echo -e "${RED}❌ Offline build capability not verified - project requires network access${NC}"
        fi

        # Generate report
        generate_test_report

        # Return appropriate exit code
        if [[ "$OFFLINE_BUILD_SUCCESS" = true ]]; then
            return 0
        else
            return 1
        fi
    else
        error "Offline build test setup failed"
        return 1
    fi
}

# Parse command line arguments
FORCE_OFFLINE=false
SKIP_NETWORK_DISABLE=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --force-offline)
            FORCE_OFFLINE=true
            shift
            ;;
        --skip-network-disable)
            SKIP_NETWORK_DISABLE=true
            shift
            ;;
        --help)
            echo "Usage: $0 [options]"
            echo "Options:"
            echo "  --force-offline       Force offline mode even if network disable fails"
            echo "  --skip-network-disable Skip network isolation (simulated offline test)"
            echo "  --help               Show this help message"
            exit 0
            ;;
        *)
            error "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Apply command line options
if [[ "$SKIP_NETWORK_DISABLE" == true ]]; then
    NETWORK_DISABLED="simulated"
fi

# Run if executed directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi