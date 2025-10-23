#!/bin/bash

# 5-Minute Build Benchmark Test
# T029: Verify 5-minute fresh checkout build completion through benchmark testing

set -euo pipefail

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_TARGET_MINUTES=5
BUILD_TARGET_SECONDS=$((BUILD_TARGET_MINUTES * 60))
TEMP_TEST_DIR="/tmp/keycuda-benchmark-$$"
RESULTS_FILE="$PROJECT_ROOT/build-benchmark-results.json"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Metrics collection
START_TIME=""
END_TIME=""
TOTAL_SECONDS=""
BUILD_SUCCESS=false
METRICS=""

# Logging function
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

# Initialize metrics collection
initialize_metrics() {
    START_TIME=$(date +%s)
    METRICS=$(cat <<EOF
{
  "benchmark_type": "fresh_checkout_build",
  "target_build_time_seconds": $BUILD_TARGET_SECONDS,
  "timestamp": "$(date -Iseconds)",
  "project_root": "$PROJECT_ROOT",
  "test_environment": {
    "platform": "$(uname -s)",
    "architecture": "$(uname -m)",
    "cpu_cores": $(nproc),
    "memory_gb": $(free -g | awk '/^Mem:/{print $2}'),
    "disk_space_gb": $(df -BG . | awk 'NR==2{print $4}' | sed 's/G//')
  }
}
EOF
)
}

# Record checkpoint time
record_checkpoint() {
    local checkpoint_name="$1"
    local checkpoint_time=$(date +%s)
    local elapsed=$((checkpoint_time - START_TIME))

    METRICS=$(echo "$METRICS" | jq --arg name "$checkpoint_name" --arg elapsed "$elapsed" '
        .checkpoints += [{name: $name, elapsed_seconds: ($elapsed | tonumber)}]
    ')
}

# Create fresh test environment
create_fresh_environment() {
    log "Creating fresh test environment..."

    # Clean up any existing test directory
    if [[ -d "$TEMP_TEST_DIR" ]]; then
        rm -rf "$TEMP_TEST_DIR"
    fi

    # Create temporary test directory
    mkdir -p "$TEMP_TEST_DIR"

    # Copy project sources without git history and build artifacts
    log "Copying project sources..."

    # Include essential source files
    mkdir -p "$TEMP_TEST_DIR"/{src,scripts,cmake}

    # Copy source directories
    if [[ -d "$PROJECT_ROOT/src" ]]; then
        cp -r "$PROJECT_ROOT/src" "$TEMP_TEST_DIR/"
        log "Copied src directory"
    fi

    if [[ -d "$PROJECT_ROOT/scripts" ]]; then
        cp -r "$PROJECT_ROOT/scripts" "$TEMP_TEST_DIR/"
        log "Copied scripts directory"
    fi

    if [[ -d "$PROJECT_ROOT/cmake" ]]; then
        cp -r "$PROJECT_ROOT/cmake" "$TEMP_TEST_DIR/"
        log "Copied cmake directory"
    fi

    # Copy essential build files
    for file in CMakeLists.txt Makefile configure .cmake-format; do
        if [[ -f "$PROJECT_ROOT/$file" ]]; then
            cp "$PROJECT_ROOT/$file" "$TEMP_TEST_DIR/"
            log "Copied $file"
        fi
    done

    # Copy extracted sources (critical for offline build)
    if [[ -d "$PROJECT_ROOT/src/extracted" ]]; then
        cp -r "$PROJECT_ROOT/src/extracted" "$TEMP_TEST_DIR/src/"
        log "Copied extracted sources - critical for offline build"
    fi

    # Copy integration files
    if [[ -d "$PROJECT_ROOT/src/integration" ]]; then
        cp -r "$PROJECT_ROOT/src/integration" "$TEMP_TEST_DIR/src/"
        log "Copied integration infrastructure"
    fi

    # Copy KeyhuntCore compatibility layer
    if [[ -d "$PROJECT_ROOT/src/KeyhuntCore" ]]; then
        cp -r "$PROJECT_ROOT/src/KeyhuntCore" "$TEMP_TEST_DIR/src/"
        log "Copied KeyhuntCore compatibility layer"
    fi

    # Create minimal git structure (to simulate fresh clone)
    cd "$TEMP_TEST_DIR"
    git init -q
    git config user.email "benchmark-test@localhost"
    git config user.name "Benchmark Test"

    # Add all files
    git add .
    git commit -q -m "Initial commit - fresh checkout simulation"

    record_checkpoint "environment_creation"
    success "Fresh test environment created successfully"
}

# Validate offline build capability
validate_offline_readiness() {
    log "Validating offline build readiness..."

    local validation_errors=0

    # Check for extracted sources
    if [[ ! -d "$TEMP_TEST_DIR/src/extracted" ]]; then
        error "Missing extracted sources - cannot build offline"
        validation_errors=$((validation_errors + 1))
    else
        local extracted_libs=$(find "$TEMP_TEST_DIR/src/extracted" -maxdepth 1 -type d | wc -l)
        log "Found $((extracted_libs - 1)) extracted libraries"
    fi

    # Check for CMakeLists.txt
    if [[ ! -f "$TEMP_TEST_DIR/CMakeLists.txt" ]]; then
        error "Missing CMakeLists.txt"
        validation_errors=$((validation_errors + 1))
    fi

    # Check for offline build configuration
    if ! grep -q "ENABLE_OFFLINE_BUILD" "$TEMP_TEST_DIR/CMakeLists.txt" 2>/dev/null; then
        warning "Offline build configuration not found in CMakeLists.txt"
    fi

    # Check for integration infrastructure
    if [[ ! -d "$TEMP_TEST_DIR/src/integration" ]]; then
        warning "Integration infrastructure missing"
    fi

    if [[ $validation_errors -gt 0 ]]; then
        error "Offline build validation failed with $validation_errors errors"
        return 1
    fi

    success "Offline build readiness validated"
    record_checkpoint "offline_validation"
    return 0
}

# Perform timed build
perform_timed_build() {
    log "Starting timed build process..."
    log "Target: Complete build within ${BUILD_TARGET_MINUTES} minutes (${BUILD_TARGET_SECONDS} seconds)"

    local build_start=$(date +%s)

    # Create build directory
    mkdir -p "$TEMP_TEST_DIR/build"
    cd "$TEMP_TEST_DIR/build"

    record_checkpoint "build_setup"

    # Configure with offline mode
    log "Configuring CMake with offline build mode..."
    local configure_start=$(date +%s)

    if cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_OFFLINE_BUILD=ON \
        -DSECP256K1_AVAILABLE=OFF 2>&1 | tee cmake-configure.log; then
        local configure_end=$(date +%s)
        local configure_time=$((configure_end - configure_start))
        log "CMake configuration completed in ${configure_time} seconds"
        record_checkpoint "cmake_configuration"
    else
        error "CMake configuration failed"
        return 1
    fi

    # Build with timing
    log "Starting build process..."
    local build_compile_start=$(date +%s)

    # Use parallel build with optimal job count
    local job_count=$(nproc)
    log "Using $job_count parallel jobs"

    if make -j"$job_count" 2>&1 | tee make-build.log; then
        local build_compile_end=$(date +%s)
        local build_compile_time=$((build_compile_end - build_compile_start))
        log "Build compilation completed in ${build_compile_time} seconds"
        record_checkpoint "build_compilation"
    else
        error "Build compilation failed"
        return 1
    fi

    # Verify build artifacts
    log "Verifying build artifacts..."
    local artifact_count=0

    if [[ -f "Puzzle71Solver" ]]; then
        artifact_count=$((artifact_count + 1))
        success "Main executable found: Puzzle71Solver"
    fi

    # Check for library files
    for lib in libsecp256k1*.so libsecp256k1*.a; do
        if [[ -f "$lib" ]]; then
            artifact_count=$((artifact_count + 1))
            log "Found library: $lib"
        fi
    done

    if [[ $artifact_count -eq 0 ]]; then
        error "No build artifacts found"
        return 1
    fi

    local build_end=$(date +%s)
    TOTAL_SECONDS=$((build_end - build_start))
    BUILD_SUCCESS=true

    success "Build completed successfully in ${TOTAL_SECONDS} seconds"
    record_checkpoint "build_completion"

    return 0
}

# Analyze build performance
analyze_performance() {
    log "Analyzing build performance..."

    # Calculate performance metrics
    local target_met=false
    local performance_ratio=0
    local time_variance=0
    local target_met_str="false"

    if [[ $BUILD_SUCCESS == true ]]; then
        if [[ $TOTAL_SECONDS -le $BUILD_TARGET_SECONDS ]]; then
            target_met=true
            target_met_str="true"
            success "Build target met: ${TOTAL_SECONDS}s <= ${BUILD_TARGET_SECONDS}s"
        else
            local overtime=$((TOTAL_SECONDS - BUILD_TARGET_SECONDS))
            warning "Build target missed: ${TOTAL_SECONDS}s exceeds target by ${overtime}s"
        fi

        performance_ratio=$(echo "scale=2; $BUILD_TARGET_SECONDS / $TOTAL_SECONDS" | bc -l 2>/dev/null || echo "0")
        time_variance=$(echo "scale=2; ($TOTAL_SECONDS - $BUILD_TARGET_SECONDS) / $BUILD_TARGET_SECONDS * 100" | bc -l 2>/dev/null || echo "0")
    else
        error "Build failed - cannot analyze performance"
        return 1
    fi

    # Update metrics with performance data
    METRICS=$(echo "$METRICS" | jq --arg success "$BUILD_SUCCESS" \
        --arg total_seconds "$TOTAL_SECONDS" \
        --arg target_met "$target_met_str" \
        --arg performance_ratio "$performance_ratio" \
        --arg time_variance "$time_variance" '
        . += {
            build_success: ($success | test("true")),
            total_build_time_seconds: ($total_seconds | tonumber),
            target_met: ($target_met | test("true")),
            performance_ratio: ($performance_ratio | tonumber),
            time_variance_percent: ($time_variance | tonumber)
        }
    ')

    # Analyze checkpoint times
    log "Checkpoint analysis:"
    echo "$METRICS" | jq -r '.checkpoints[] | "  - \(.name): \(.elapsed_seconds)s"' 2>/dev/null || true

    return 0
}

# Generate performance report
generate_report() {
    log "Generating performance report..."

    local report_file="$RESULTS_FILE"
    local report_date=$(date '+%Y-%m-%d %H:%M:%S')

    # Create comprehensive report
    cat > "$report_file" << EOF
{
  "report_metadata": {
    "generated": "$report_date",
    "benchmark_type": "fresh_checkout_build",
    "script_version": "T029-1.0"
  },
  $METRICS,
  "analysis": {
    "summary": "$([ "$BUILD_SUCCESS" = true ] && echo "BUILD_SUCCESSFUL" || echo "BUILD_FAILED")",
    "recommendation": "$(
        if [[ "$BUILD_SUCCESS" == true ]]; then
            if [[ $TOTAL_SECONDS -le $BUILD_TARGET_SECONDS ]]; then
                echo "Build performance meets target - ready for production"
            else
                echo "Build successful but exceeds target - consider optimization"
            fi
        else
            echo "Build failed - investigate build configuration and dependencies"
        fi
    )"
  }
}
EOF

    success "Performance report generated: $report_file"

    # Display summary
    echo
    echo "=== Build Benchmark Summary ==="
    echo "Status: $([ "$BUILD_SUCCESS" = true ] && echo "SUCCESS" || echo "FAILED")"
    echo "Total Time: ${TOTAL_SECONDS:-0} seconds"
    echo "Target: ${BUILD_TARGET_SECONDS} seconds"
    echo "Target Met: ${target_met_str:-false}"
    echo "Performance Ratio: ${performance_ratio:-0}"
    echo "Time Variance: ${time_variance:-0}%"
    echo "Report: $report_file"
}

# Cleanup function
cleanup() {
    if [[ -d "$TEMP_TEST_DIR" ]]; then
        log "Cleaning up test environment..."
        rm -rf "$TEMP_TEST_DIR"
    fi
}

# Main execution
main() {
    log "Starting 5-minute build benchmark test..."

    # Trap cleanup
    trap cleanup EXIT

    # Initialize
    initialize_metrics

    # Execute benchmark steps
    if create_fresh_environment && \
       validate_offline_readiness && \
       perform_timed_build; then

        # Analyze and report
        analyze_performance
        generate_report

        if [[ $BUILD_SUCCESS == true && $TOTAL_SECONDS -le $BUILD_TARGET_SECONDS ]]; then
            success "5-minute build benchmark PASSED"
            echo
            echo -e "${GREEN}✅ Target achieved: Fresh checkout build completed in ${TOTAL_SECONDS}s (target: ${BUILD_TARGET_SECONDS}s)${NC}"
            return 0
        else
            warning "5-minute build benchmark completed but target not met"
            return 1
        fi
    else
        error "5-minute build benchmark FAILED"

        # Generate failure report
        METRICS=$(echo "$METRICS" | jq --arg success "false" --arg total_seconds "0" '
            . += {
                build_success: false,
                total_build_time_seconds: 0,
                target_met: false,
                error: "Build process failed during execution"
            }
        ')

        generate_report
        return 1
    fi
}

# Check dependencies
check_dependencies() {
    local missing_deps=()

    for cmd in jq bc git cmake make; do
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

# Run if executed directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    check_dependencies
    main "$@"
fi