#!/bin/bash

# Comprehensive Integration Verification Framework
# T017: Integrity verification system for third-party dependencies integration
#
# This script provides comprehensive verification of the integration process,
# validating library extraction, attribution management, build system changes,
# and cross-system consistency across the entire project.

set -euo pipefail

# Script configuration
SCRIPT_NAME="$(basename "$0")"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$PROJECT_ROOT/build}"
VERIFICATION_DIR="$BUILD_DIR/verification-reports"
INTEGRATION_MANIFESTS_DIR="$BUILD_DIR/integration-manifests"
METRICS_DIR="$BUILD_DIR/metrics"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Verification exit codes
EXIT_SUCCESS=0
EXIT_VALIDATION_FAILED=1
EXIT_MISSING_DEPENDENCY=2
EXIT_BUILD_SYSTEM_ERROR=3
EXIT_INTEGRITY_ERROR=4
EXIT_CONFIGURATION_ERROR=5

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

log_fatal() {
    echo -e "${RED}[FATAL]${NC} $1" >&2
    exit "${2:-$EXIT_VALIDATION_FAILED}"
}

# Create verification directory
setup_verification_environment() {
    log_info "Setting up verification environment..."

    mkdir -p "$VERIFICATION_DIR"
    mkdir -p "$INTEGRATION_MANIFESTS_DIR"
    mkdir -p "$METRICS_DIR"

    # Create verification timestamp
    VERIFICATION_TIMESTAMP=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
    VERIFICATION_REPORT_FILE="$VERIFICATION_DIR/integration-verification-$VERIFICATION_TIMESTAMP.json"

    log_success "Verification environment ready"
}

# Dependency verification
verify_dependencies() {
    log_info "Verifying required dependencies..."

    local missing_deps=()
    local required_tools=("cmake" "make" "gcc" "g++" "nvcc" "sha256sum" "jq" "find")

    for tool in "${required_tools[@]}"; do
        if ! command -v "$tool" >/dev/null 2>&1; then
            missing_deps+=("$tool")
        fi
    done

    if [[ ${#missing_deps[@]} -gt 0 ]]; then
        log_fatal "Missing required dependencies: ${missing_deps[*]}" $EXIT_MISSING_DEPENDENCY
    fi

    # Verify CUDA toolkit
    if ! nvcc --version >/dev/null 2>&1; then
        log_fatal "CUDA toolkit not properly installed" $EXIT_MISSING_DEPENDENCY
    fi

    # Verify CMake version
    local cmake_version
    cmake_version=$(cmake --version | head -n1 | grep -oE '[0-9]+\.[0-9]+' | head -n1)
    local cmake_major=${cmake_version%.*}
    local cmake_minor=${cmake_version#*.}
    local required_major=3
    local required_minor=22

    if [[ $cmake_major -lt $required_major ]] || [[ $cmake_major -eq $required_major && $cmake_minor -lt $required_minor ]]; then
        log_fatal "CMake version $cmake_version is too old. Minimum required: 3.22" $EXIT_MISSING_DEPENDENCY
    fi

    log_success "All dependencies verified"
}

# Library extraction verification
verify_library_extraction() {
    log_info "Verifying library extraction integrity..."

    local extraction_report=()
    local extraction_failed=false

    # Find all extracted libraries
    local extracted_libs=()
    while IFS= read -r -d '' lib_dir; do
        lib_name=$(basename "$lib_dir")
        extracted_libs+=("$lib_name")
    done < <(find "$PROJECT_ROOT/src/extracted" -mindepth 1 -maxdepth 1 -type d -print0)

    if [[ ${#extracted_libs[@]} -eq 0 ]]; then
        log_warning "No extracted libraries found"
        extraction_report+=("$(jq -n --arg status "warning" --arg message "No extracted libraries found" '{status: $status, message: $message}')")
    else
        for lib_name in "${extracted_libs[@]}"; do
            log_info "Verifying extraction for library: $lib_name"

            local lib_path="$PROJECT_ROOT/src/extracted/$lib_name"
            local verification_result

            # Verify source files exist
            local source_file_count
            source_file_count=$(find "$lib_path" -name "*.c" -o -name "*.cpp" -o -name "*.h" -o -name "*.hpp" | wc -l)

            if [[ $source_file_count -eq 0 ]]; then
                verification_result=$(jq -n \
                    --arg library "$lib_name" \
                    --arg status "failed" \
                    --arg message "No source files found" \
                    '{library: $library, status: $status, message: $message, source_files: 0}')
                extraction_failed=true
            else
                verification_result=$(jq -n \
                    --arg library "$lib_name" \
                    --arg status "success" \
                    --arg message "Extraction verified" \
                    --argjson source_files "$source_file_count" \
                    '{library: $library, status: $status, message: $message, source_files: $source_files}')
            fi

            extraction_report+=("$verification_result")
        done
    fi

    # Generate extraction verification summary
    local extraction_summary="{\"timestamp\": \"$VERIFICATION_TIMESTAMP\", \"extraction_verification\": ["

    if [[ ${#extraction_report[@]} -gt 0 ]]; then
        for i in "${!extraction_report[@]}"; do
            if [[ $i -gt 0 ]]; then
                extraction_summary+=", "
            fi
            extraction_summary+="${extraction_report[$i]}"
        done
    fi

    extraction_summary+="], \"failed\": $extraction_failed}"

    if [[ "$extraction_failed" == "true" ]]; then
        log_error "Library extraction verification failed"
    else
        log_success "Library extraction verification completed"
    fi

    echo "$extraction_summary"
}

# Attribution verification
verify_attribution() {
    log_info "Verifying attribution headers..."

    local attribution_report=()
    local attribution_failed=false

    # Define patterns for attribution headers
    local attribution_patterns=(
        "SPDX-License-Identifier:"
        "Copyright"
        "Original author"
        "Source repository"
    )

    # Check extracted libraries for attribution
    local extracted_libs=()
    while IFS= read -r -d '' lib_dir; do
        lib_name=$(basename "$lib_dir")
        extracted_libs+=("$lib_name")
    done < <(find "$PROJECT_ROOT/src/extracted" -mindepth 1 -maxdepth 1 -type d -print0)

    for lib_name in "${extracted_libs[@]}"; do
        log_info "Checking attribution for library: $lib_name"

        local lib_path="$PROJECT_ROOT/src/extracted/$lib_name"
        local attribution_result
        local files_with_attribution=0
        local total_source_files=0
        local missing_attribution_files=()

        # Find all source files
        while IFS= read -r -d '' source_file; do
            ((total_source_files++))

            local has_attribution=false
            for pattern in "${attribution_patterns[@]}"; do
                if grep -q "$pattern" "$source_file" 2>/dev/null; then
                    has_attribution=true
                    break
                fi
            done

            if [[ "$has_attribution" == "true" ]]; then
                ((files_with_attribution++))
            else
                missing_attribution_files+=("$(realpath --relative-to="$PROJECT_ROOT" "$source_file")")
            fi
        done < <(find "$lib_path" -name "*.c" -o -name "*.cpp" -o -name "*.h" -o -name "*.hpp" -print0)

        # Calculate attribution coverage
        local attribution_coverage=0
        if [[ $total_source_files -gt 0 ]]; then
            attribution_coverage=$((files_with_attribution * 100 / total_source_files))
        fi

        # Determine attribution status
        local attribution_status="success"
        local attribution_message="Attribution verification completed"

        if [[ $attribution_coverage -lt 100 ]]; then
            attribution_status="warning"
            attribution_message="Some files missing attribution"
            if [[ $attribution_coverage -lt 80 ]]; then
                attribution_status="failed"
                attribution_message="Insufficient attribution coverage"
                attribution_failed=true
            fi
        fi

        attribution_result=$(jq -n \
            --arg library "$lib_name" \
            --arg status "$attribution_status" \
            --arg message "$attribution_message" \
            --argjson coverage "$attribution_coverage" \
            --argjson files_with_attribution "$files_with_attribution" \
            --argjson total_files "$total_source_files" \
            --argjson missing_files "$(printf '%s\n' "${missing_attribution_files[@]}" | jq -R . | jq -s .)" \
            '{library: $library, status: $status, message: $message, attribution_coverage: $coverage, files_with_attribution: $files_with_attribution, total_files: $total_files, missing_attribution_files: $missing_files}')

        attribution_report+=("$attribution_result")
    done

    # Generate attribution verification summary
    local attribution_summary="{\"timestamp\": \"$VERIFICATION_TIMESTAMP\", \"attribution_verification\": ["

    if [[ ${#attribution_report[@]} -gt 0 ]]; then
        for i in "${!attribution_report[@]}"; do
            if [[ $i -gt 0 ]]; then
                attribution_summary+=", "
            fi
            attribution_summary+="${attribution_report[$i]}"
        done
    fi

    attribution_summary+="], \"failed\": $attribution_failed}"

    if [[ "$attribution_failed" == "true" ]]; then
        log_error "Attribution verification failed"
    else
        log_success "Attribution verification completed"
    fi

    echo "$attribution_summary"
}

# Build system verification
verify_build_system() {
    log_info "Verifying build system integration..."

    local build_report=()
    local build_failed=false

    # Check if CMakeLists.txt exists and is valid
    local cmake_file="$PROJECT_ROOT/CMakeLists.txt"
    if [[ ! -f "$cmake_file" ]]; then
        log_error "CMakeLists.txt not found"
        build_failed=true
        build_report+=("$(jq -n --arg status "failed" --arg message "CMakeLists.txt not found" '{status: $status, message: $message}')")
    else
        # Verify CMakeLists.txt syntax
        if ! cmake --log-level=ERROR -P "$cmake_file" >/dev/null 2>&1; then
            log_error "CMakeLists.txt syntax error"
            build_failed=true
            build_report+=("$(jq -n --arg status "failed" --arg message "CMakeLists.txt syntax error" '{status: $status, message: $message}')")
        else
            build_report+=("$(jq -n --arg status "success" --arg message "CMakeLists.txt valid" '{status: $status, message: $message}')")
        fi
    fi

    # Check build directory
    if [[ ! -d "$BUILD_DIR" ]]; then
        log_warning "Build directory does not exist, creating..."
        mkdir -p "$BUILD_DIR"
    fi

    # Verify CMake configuration
    log_info "Testing CMake configuration..."
    local cmake_config_result

    if cd "$BUILD_DIR" && cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo >/dev/null 2>&1; then
        cmake_config_result=$(jq -n \
            --arg status "success" \
            --arg message "CMake configuration successful" \
            '{status: $status, message: $message}')
    else
        cmake_config_result=$(jq -n \
            --arg status "failed" \
            --arg message "CMake configuration failed" \
            '{status: $status, message: $message}')
        build_failed=true
    fi

    build_report+=("$cmake_config_result")

    # Verify build targets
    if [[ "$build_failed" == "false" ]]; then
        log_info "Verifying build targets..."
        local make_result

        if make -j2 >/dev/null 2>&1; then
            make_result=$(jq -n \
                --arg status "success" \
                --arg message "Build successful" \
                '{status: $status, message: $message}')
        else
            make_result=$(jq -n \
                --arg status "failed" \
                --arg message "Build failed" \
                '{status: $status, message: $message}')
            build_failed=true
        fi

        build_report+=("$make_result")
    fi

    # Generate build system verification summary
    local build_summary="{\"timestamp\": \"$VERIFICATION_TIMESTAMP\", \"build_system_verification\": ["

    if [[ ${#build_report[@]} -gt 0 ]]; then
        for i in "${!build_report[@]}"; do
            if [[ $i -gt 0 ]]; then
                build_summary+=", "
            fi
            build_summary+="${build_report[$i]}"
        done
    fi

    build_summary+="], \"failed\": $build_failed}"

    if [[ "$build_failed" == "true" ]]; then
        log_error "Build system verification failed"
    else
        log_success "Build system verification completed"
    fi

    echo "$build_summary"
}

# Cross-system consistency verification
verify_cross_system_consistency() {
    log_info "Verifying cross-system consistency..."

    local consistency_report=()
    local consistency_failed=false

    # Verify integration manifests consistency
    if [[ -d "$INTEGRATION_MANIFESTS_DIR" ]]; then
        local manifest_files=("$INTEGRATION_MANIFESTS_DIR"/*.json)

        if [[ -f "${manifest_files[0]}" ]]; then
            for manifest_file in "${manifest_files[@]}"; do
                log_info "Checking manifest: $(basename "$manifest_file")"

                local manifest_result
                if jq empty "$manifest_file" 2>/dev/null; then
                    # Verify manifest schema
                    local manifest_validation
                    manifest_validation=$(jq '
                        if has("manifest_id") and has("manifest_type") and has("schema_version") and has("created_at") then
                            {status: "success", message: "Manifest schema valid"}
                        else
                            {status: "failed", message: "Invalid manifest schema"}
                        end
                    ' "$manifest_file")

                    if [[ $(echo "$manifest_validation" | jq -r '.status') == "failed" ]]; then
                        consistency_failed=true
                    fi

                    manifest_result=$(jq -n \
                        --arg manifest "$(basename "$manifest_file")" \
                        --argjson validation "$manifest_validation" \
                        '{manifest: $manifest} + $validation')
                else
                    manifest_result=$(jq -n \
                        --arg manifest "$(basename "$manifest_file")" \
                        --arg status "failed" \
                        --arg message "Invalid JSON format" \
                        '{manifest: $manifest, status: $status, message: $message}')
                    consistency_failed=true
                fi

                consistency_report+=("$manifest_result")
            done
        else
            consistency_report+=("$(jq -n --arg status "warning" --arg message "No integration manifests found" '{status: $status, message: $message}')")
        fi
    fi

    # Verify extracted libraries vs manifests consistency
    local extracted_libs=()
    while IFS= read -r -d '' lib_dir; do
        extracted_libs+=("$(basename "$lib_dir")")
    done < <(find "$PROJECT_ROOT/src/extracted" -mindepth 1 -maxdepth 1 -type d -print0)

    for lib_name in "${extracted_libs[@]}"; do
        local manifest_found=false
        for manifest_file in "$INTEGRATION_MANIFESTS_DIR"/*.json; do
            if [[ -f "$manifest_file" ]]; then
                local manifest_lib_name
                manifest_lib_name=$(jq -r '.libraries[0].library_name // empty' "$manifest_file" 2>/dev/null)
                if [[ "$manifest_lib_name" == "$lib_name" ]]; then
                    manifest_found=true
                    break
                fi
            fi
        done

        local consistency_result
        if [[ "$manifest_found" == "true" ]]; then
            consistency_result=$(jq -n \
                --arg library "$lib_name" \
                --arg status "success" \
                --arg message "Manifest found for library" \
                '{library: $library, status: $status, message: $message}')
        else
            consistency_result=$(jq -n \
                --arg library "$lib_name" \
                --arg status "warning" \
                --arg message "No manifest found for library" \
                '{library: $library, status: $status, message: $message}')
        fi

        consistency_report+=("$consistency_result")
    done

    # T017 Enhanced: Verify offline build configuration consistency
    log_info "Verifying offline build configuration consistency..."
    local offline_build_config_result

    if grep -q "ENABLE_OFFLINE_BUILD" "$PROJECT_ROOT/CMakeLists.txt"; then
        # Check if offline build validation is present
        if grep -q "Offline build validation passed" "$PROJECT_ROOT/CMakeLists.txt" ||
           grep -q "OFFLINE_BUILD_VALIDATION_PASSED" "$PROJECT_ROOT/CMakeLists.txt"; then
            offline_build_config_result=$(jq -n \
                --arg status "success" \
                --arg message "Offline build configuration properly validated" \
                '{component: "offline_build_config", status: $status, message: $message}')
        else
            offline_build_config_result=$(jq -n \
                --arg status "warning" \
                --arg message "Offline build enabled but validation unclear" \
                '{component: "offline_build_config", status: $status, message: $message}')
        fi
    else
        offline_build_config_result=$(jq -n \
            --arg status "failed" \
            --arg message "Offline build configuration not found" \
            '{component: "offline_build_config", status: $status, message: $message}')
        consistency_failed=true
    fi

    consistency_report+=("$offline_build_config_result")

    # T017 Enhanced: Verify CMakeLists.txt integration consistency
    log_info "Verifying CMakeLists.txt integration consistency..."
    local cmake_integration_result

    # Check for extracted sources references
    local extracted_refs=$(grep -c "src/extracted" "$PROJECT_ROOT/CMakeLists.txt" 2>/dev/null || echo "0")
    local submodule_refs=$(grep -c "git submodule\|FetchContent" "$PROJECT_ROOT/CMakeLists.txt" 2>/dev/null || echo "0")

    if [[ $extracted_refs -gt 0 ]] && [[ $submodule_refs -eq 0 ]]; then
        cmake_integration_result=$(jq -n \
            --arg status "success" \
            --arg message "CMakeLists.txt properly integrated with extracted sources" \
            --argjson extracted_refs "$extracted_refs" \
            --argjson submodule_refs "$submodule_refs" \
            '{component: "cmake_integration", status: $status, message: $message, extracted_references: $extracted_refs, submodule_references: $submodule_refs}')
    elif [[ $extracted_refs -gt 0 ]] && [[ $submodule_refs -gt 0 ]]; then
        cmake_integration_result=$(jq -n \
            --arg status "warning" \
            --arg message "Mixed configuration: both extracted and external references found" \
            --argjson extracted_refs "$extracted_refs" \
            --argjson submodule_refs "$submodule_refs" \
            '{component: "cmake_integration", status: $status, message: $message, extracted_references: $extracted_refs, submodule_references: $submodule_refs}')
    else
        cmake_integration_result=$(jq -n \
            --arg status "failed" \
            --arg message "No extracted sources integration found in CMakeLists.txt" \
            --argjson extracted_refs "$extracted_refs" \
            --argjson submodule_refs "$submodule_refs" \
            '{component: "cmake_integration", status: $status, message: $message, extracted_references: $extracted_refs, submodule_references: $submodule_refs}')
        consistency_failed=true
    fi

    consistency_report+=("$cmake_integration_result")

    # T017 Enhanced: Verify functionality and integration consistency
    log_info "Verifying functionality and integration consistency..."
    local functionality_result

    # Test basic build functionality with extracted sources
    local test_build_dir="$BUILD_DIR/verification_test_build"
    mkdir -p "$test_build_dir"

    if cd "$test_build_dir" && cmake "$PROJECT_ROOT" -DENABLE_OFFLINE_BUILD=ON >/dev/null 2>&1; then
        # Check if configuration succeeded and offline mode is active
        if grep -q "OFFLINE BUILD MODE ENABLED" CMakeCache.txt 2>/dev/null ||
           grep -q "OFFLINE_BUILD_ENABLED=ON" CMakeCache.txt 2>/dev/null; then
            functionality_result=$(jq -n \
                --arg status "success" \
                --arg message "Offline build configuration functional" \
                '{component: "functionality_test", status: $status, message: $message}')
        else
            functionality_result=$(jq -n \
                --arg status "warning" \
                --arg message "Build succeeded but offline mode verification unclear" \
                '{component: "functionality_test", status: $status, message: $message}')
        fi
    else
        functionality_result=$(jq -n \
            --arg status "failed" \
            --arg message "Offline build configuration failed" \
            '{component: "functionality_test", status: $status, message: $message}')
        consistency_failed=true
    fi

    consistency_report+=("$functionality_result")

    # T017 Enhanced: Verify attribution file consistency
    log_info "Verifying attribution file consistency..."
    local attribution_consistency_result

    local attribution_files=0
    local libraries_with_attribution=0
    local total_libraries=0

    # Count libraries and attribution files
    while IFS= read -r -d '' lib_dir; do
        ((total_libraries++))
        lib_name=$(basename "$lib_dir")

        if [[ -f "$lib_dir/ATTRIBUTION.md" ]]; then
            ((attribution_files++))
            ((libraries_with_attribution++))
        fi
    done < <(find "$PROJECT_ROOT/src/extracted" -mindepth 1 -maxdepth 1 -type d -print0)

    if [[ $total_libraries -gt 0 ]]; then
        local attribution_coverage=$((libraries_with_attribution * 100 / total_libraries))

        if [[ $attribution_coverage -eq 100 ]]; then
            attribution_consistency_result=$(jq -n \
                --arg status "success" \
                --arg message "All libraries have proper attribution" \
                --argjson coverage "$attribution_coverage" \
                --argjson libraries "$libraries_with_attribution" \
                --argjson total "$total_libraries" \
                '{component: "attribution_consistency", status: $status, message: $message, coverage_percentage: $coverage, libraries_with_attribution: $libraries, total_libraries: $total}')
        elif [[ $attribution_coverage -ge 80 ]]; then
            attribution_consistency_result=$(jq -n \
                --arg status "warning" \
                --arg message "Most libraries have attribution" \
                --argjson coverage "$attribution_coverage" \
                --argjson libraries "$libraries_with_attribution" \
                --argjson total "$total_libraries" \
                '{component: "attribution_consistency", status: $status, message: $message, coverage_percentage: $coverage, libraries_with_attribution: $libraries, total_libraries: $total}')
        else
            attribution_consistency_result=$(jq -n \
                --arg status "failed" \
                --arg message "Insufficient attribution coverage" \
                --argjson coverage "$attribution_coverage" \
                --argjson libraries "$libraries_with_attribution" \
                --argjson total "$total_libraries" \
                '{component: "attribution_consistency", status: $status, message: $message, coverage_percentage: $coverage, libraries_with_attribution: $libraries, total_libraries: $total}')
            consistency_failed=true
        fi
    else
        attribution_consistency_result=$(jq -n \
            --arg status "warning" \
            --arg message "No libraries found for attribution check" \
            '{component: "attribution_consistency", status: $status, message: $message}')
    fi

    consistency_report+=("$attribution_consistency_result")

    # Generate consistency verification summary
    local consistency_summary="{\"timestamp\": \"$VERIFICATION_TIMESTAMP\", \"consistency_verification\": ["

    if [[ ${#consistency_report[@]} -gt 0 ]]; then
        for i in "${!consistency_report[@]}"; do
            if [[ $i -gt 0 ]]; then
                consistency_summary+=", "
            fi
            consistency_summary+="${consistency_report[$i]}"
        done
    fi

    consistency_summary+="], \"failed\": $consistency_failed}"

    if [[ "$consistency_failed" == "true" ]]; then
        log_error "Cross-system consistency verification failed"
    else
        log_success "Cross-system consistency verification completed"
    fi

    echo "$consistency_summary"
}

# Generate comprehensive verification report
generate_verification_report() {
    local extraction_summary="$1"
    local attribution_summary="$2"
    local build_summary="$3"
    local consistency_summary="$4"

    log_info "Generating comprehensive verification report..."

    # Calculate overall status - simple string matching approach
    local overall_failed=false
    if [[ "$extraction_summary" == *"\"failed\": true"* ]] ||
       [[ "$attribution_summary" == *"\"failed\": true"* ]] ||
       [[ "$build_summary" == *"\"failed\": true"* ]] ||
       [[ "$consistency_summary" == *"\"failed\": true"* ]]; then
        overall_failed=true
    fi

    local overall_status="success"
    local overall_message="All verification checks passed"

    if [[ "$overall_failed" == "true" ]]; then
        overall_status="failed"
        overall_message="Some verification checks failed"
    fi

    # Generate comprehensive report as simple JSON
    local verification_id="verify-$(date +%s)"
    local comprehensive_report="{
        \"verification_id\": \"$verification_id\",
        \"timestamp\": \"$VERIFICATION_TIMESTAMP\",
        \"status\": \"$overall_status\",
        \"message\": \"$overall_message\",
        \"extraction_verification\": $extraction_summary,
        \"attribution_verification\": $attribution_summary,
        \"build_system_verification\": $build_summary,
        \"consistency_verification\": $consistency_summary,
        \"overall_failed\": $overall_failed
    }"

    # Save report to file
    echo "$comprehensive_report" > "$VERIFICATION_REPORT_FILE"

    # Generate human-readable summary
    local human_readable_summary="=== Integration Verification Report ===
Verification ID: $verification_id
Timestamp: $VERIFICATION_TIMESTAMP
Status: $(echo "$overall_status" | tr '[:lower:]' '[:upper:]')
Message: $overall_message

=== Component Status ===
Library Extraction: $([ "$extraction_summary" = *"\"failed\": true"* ] && echo "FAILED" || echo "PASS")
Attribution: $([ "$attribution_summary" = *"\"failed\": true"* ] && echo "FAILED" || echo "PASS")
Build System: $([ "$build_summary" = *"\"failed\": true"* ] && echo "FAILED" || echo "PASS")
Cross-System Consistency: $([ "$consistency_summary" = *"\"failed\": true"* ] && echo "FAILED" || echo "PASS")
"

    echo -e "\n$human_readable_summary"

    if [[ "$overall_failed" == "true" ]]; then
        log_error "Verification completed with failures"
        log_info "Detailed report saved to: $VERIFICATION_REPORT_FILE"
        return $EXIT_VALIDATION_FAILED
    else
        log_success "All verification checks passed"
        log_info "Report saved to: $VERIFICATION_REPORT_FILE"
        return $EXIT_SUCCESS
    fi
}

# Main verification function
run_verification() {
    log_info "Starting comprehensive integrity verification..."
    log_info "Verification ID: verify-$(date +%s)"

    # Setup verification environment
    setup_verification_environment

    # Run verification components
    local extraction_summary attribution_summary build_summary consistency_summary

    extraction_summary=$(verify_library_extraction)
    attribution_summary=$(verify_attribution)
    build_summary=$(verify_build_system)
    consistency_summary=$(verify_cross_system_consistency)

    # Generate comprehensive report
    generate_verification_report "$extraction_summary" "$attribution_summary" "$build_summary" "$consistency_summary"
}

# Quick verification mode
run_quick_verification() {
    log_info "Running quick integrity verification..."

    # Quick checks only
    setup_verification_environment

    local extraction_ok=true
    local attribution_ok=true
    local build_ok=true

    # Quick extraction check
    if [[ ! -d "$PROJECT_ROOT/src/extracted" ]] || [[ -z "$(find "$PROJECT_ROOT/src/extracted" -mindepth 1 -maxdepth 1 -type d)" ]]; then
        log_warning "No extracted libraries found"
        extraction_ok=false
    fi

    # Quick build check
    if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
        log_warning "Build not configured"
        build_ok=false
    fi

    # Quick consistency check
    if [[ ! -d "$INTEGRATION_MANIFESTS_DIR" ]] || [[ -z "$(find "$INTEGRATION_MANIFESTS_DIR" -name "*.json")" ]]; then
        log_warning "No integration manifests found"
    fi

    if [[ "$extraction_ok" == "true" && "$build_ok" == "true" ]]; then
        log_success "Quick verification passed"
        return $EXIT_SUCCESS
    else
        log_warning "Quick verification found issues - run full verification"
        return $EXIT_VALIDATION_FAILED
    fi
}

# Help function
show_help() {
    cat << EOF
$SCRIPT_NAME - Comprehensive Integration Verification Framework

USAGE:
    $SCRIPT_NAME [OPTIONS]

OPTIONS:
    -h, --help          Show this help message
    -q, --quick         Run quick verification only
    -v, --verbose       Enable verbose output
    --build-dir DIR     Set build directory (default: $PROJECT_ROOT/build)
    --reports-dir DIR   Set reports directory (default: \$BUILD_DIR/verification-reports)

VERIFICATION COMPONENTS:
    1. Library Extraction Verification
       - Verifies extracted library integrity
       - Checks source file availability
       - Validates extraction completeness

    2. Attribution Verification
       - Checks attribution headers in source files
       - Verifies SPDX license identifiers
       - Validates attribution coverage

    3. Build System Verification
       - Validates CMakeLists.txt syntax
       - Tests CMake configuration
       - Verifies build targets

    4. Cross-System Consistency Verification
       - Checks integration manifests consistency
       - Validates library-manifest correspondence
       - Verifies system-wide consistency

EXAMPLES:
    $SCRIPT_NAME                    # Run full verification
    $SCRIPT_NAME --quick           # Run quick verification only
    $SCRIPT_NAME --build-dir ./build  # Use custom build directory

EXIT CODES:
    $EXIT_SUCCESS              All verification checks passed
    $EXIT_VALIDATION_FAILED    Some verification checks failed
    $EXIT_MISSING_DEPENDENCY   Required dependencies missing
    $EXIT_BUILD_SYSTEM_ERROR   Build system configuration error
    $EXIT_INTEGRITY_ERROR      Integrity verification error
    $EXIT_CONFIGURATION_ERROR  Configuration error

EOF
}

# Main script execution
main() {
    local quick_mode=false
    local verbose=false

    # Parse command line arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            -h|--help)
                show_help
                exit $EXIT_SUCCESS
                ;;
            -q|--quick)
                quick_mode=true
                shift
                ;;
            -v|--verbose)
                verbose=true
                shift
                ;;
            --build-dir)
                BUILD_DIR="$2"
                shift 2
                ;;
            --reports-dir)
                VERIFICATION_DIR="$2"
                shift 2
                ;;
            *)
                log_error "Unknown option: $1"
                show_help
                exit $EXIT_CONFIGURATION_ERROR
                ;;
        esac
    done

    # Validate project root
    if [[ ! -f "$PROJECT_ROOT/CMakeLists.txt" ]]; then
        log_fatal "Not a valid project root (no CMakeLists.txt found)" $EXIT_CONFIGURATION_ERROR
    fi

    # Set verbose mode
    if [[ "$verbose" == "true" ]]; then
        set -x
    fi

    # Run verification
    if [[ "$quick_mode" == "true" ]]; then
        run_quick_verification
    else
        run_verification
    fi
}

# Script execution
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi