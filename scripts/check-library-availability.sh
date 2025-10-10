#!/bin/bash

# T042: Confirm all required libraries are available locally in deployment package
# This script verifies that all required libraries are included locally in deployment packages
# and provides comprehensive analysis of library dependencies and availability

set -euo pipefail

# Script configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
VERIFICATION_DIR="$PROJECT_ROOT/logs/verification"
LIBRARY_CHECK_LOG="$VERIFICATION_DIR/library-availability-$(date +%Y%m%d-%H%M%S).log"
JSON_REPORT="$VERIFICATION_DIR/library-availability-report-$(date +%Y%m%d-%H%M%S).json"

# Create verification directory
mkdir -p "$VERIFICATION_DIR"

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Library categories for verification
LIBRARY_CATEGORIES=(
    "system_runtime"
    "crypto_math"
    "compute_gpu"
    "utility_json"
    "test_framework"
    "build_tools"
)

# Known library files for each category
declare -A CATEGORY_LIBRARIES
CATEGORY_LIBRARIES[system_runtime]="libpthread.so librt.so libdl.so libm.so libgcc_s.so libstdc++.so libc.so"
CATEGORY_LIBRARIES[crypto_math]="libsecp256k1.so libcrypto.so"
CATEGORY_LIBRARIES[compute_gpu]="libcudart.so libcublas.so libcusolver.so libcufft.so libcurand.so"
CATEGORY_LIBRARIES[utility_json]="libjson.so"
CATEGORY_LIBRARIES[test_framework]="libgtest.so libgtest_main.so libgmock.so libgmock_main.so"
CATEGORY_LIBRARIES[build_tools]="libcmake.so make ninja"

# Expected library locations in deployment packages
LIBRARY_PATHS=(
    "lib/"
    "lib64/"
    "usr/lib/"
    "usr/lib64/"
    "lib/x86_64-linux-gnu/"
    "usr/lib/x86_64-linux-gnu/"
    "libexec/"
    "bin/"
)

# Logging functions
log_library() {
    local level="$1"
    local message="$2"
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    echo -e "${timestamp} [${level}] ${message}" | tee -a "$LIBRARY_CHECK_LOG"
}

log_info() { log_library "INFO" "$1"; }
log_success() { log_library "SUCCESS" "$1"; }
log_warning() { log_library "WARNING" "$1"; }
log_error() { log_library "ERROR" "$1"; }

# JSON reporting functions
init_json_report() {
    cat > "$JSON_REPORT" << 'EOF'
{
  "verification_type": "library_availability",
  "timestamp": "",
  "deployment_packages": [],
  "summary": {
    "total_packages": 0,
    "total_libraries_checked": 0,
    "libraries_available": 0,
    "libraries_missing": 0,
    "availability_percentage": 0.0,
    "categories_complete": 0,
    "categories_total": 0
  },
  "library_details": {},
  "category_analysis": {},
  "recommendations": []
}
EOF
}

update_json_timestamp() {
    local timestamp=$(date -Iseconds)
    sed -i "s/\"timestamp\": \"\"/\"timestamp\": \"$timestamp\"/" "$JSON_REPORT"
}

add_package_to_json() {
    local package_file="$1"
    local package_name=$(basename "$package_file")
    local temp_json=$(mktemp)

    # Create package entry
    jq --arg name "$package_name" \
       --arg path "$package_file" \
       '.deployment_packages += [{"name": $name, "path": $path, "status": "processing", "libraries_found": [], "libraries_missing": [], "local_coverage": 0.0}]' \
       "$JSON_REPORT" > "$temp_json" && mv "$temp_json" "$JSON_REPORT"
}

update_package_status() {
    local package_name="$1"
    local status="$2"
    local libraries_found="$3"
    local libraries_missing="$4"
    local coverage="$5"
    local temp_json=$(mktemp)

    jq --arg name "$package_name" \
       --arg status "$status" \
       --argjson found "$libraries_found" \
       --argjson missing "$libraries_missing" \
       --argjson coverage "$coverage" \
       '.deployment_packages[] | select(.name == $name) | .status = $status | .libraries_found = $found | .libraries_missing = $missing | .local_coverage = $coverage' \
       "$JSON_REPORT" > "$temp_json" && mv "$temp_json" "$JSON_REPORT"
}

# Utility functions
print_usage() {
    cat << EOF
T042: Library Availability Verification for Deployment Packages

Usage: $0 [OPTIONS] <deployment_package>...

OPTIONS:
    --help, -h              Show this help message
    --verbose, -v           Enable detailed output
    --check-all             Check all library categories (default: required only)
    --strict                Fail if any library is missing
    --extract-dir DIR       Use specified directory for extraction
    --keep-extracted        Keep extracted files after verification
    --output-format FORMAT  Output format: text, json, both (default: both)
    --timeout SECONDS       Timeout for library checks (default: 30)
    --parallel JOBS         Number of parallel jobs (default: 4)

EXAMPLES:
    $0 build/deployment/*.tar.gz
    $0 --verbose --strict package.tar.gz
    $0 --check-all --output-format json deployment-package.tar.bz2
    $0 --extract-dir /tmp/deploy-test --keep-extracted package.tar.gz

DESCRIPTION:
    This script verifies that all required libraries are available locally in deployment
    packages. It performs comprehensive library dependency analysis, validates local
    library coverage, and generates detailed availability reports.

    The verification ensures deployment packages are truly self-contained and can
    run in isolated environments without external dependencies.

LIBRARY CATEGORIES:
    system_runtime    - System runtime libraries (pthread, rt, dl, m, etc.)
    crypto_math       - Cryptographic and mathematical libraries
    compute_gpu       - GPU computing libraries (CUDA, cuBLAS, etc.)
    utility_json      - JSON utility libraries
    test_framework    - Testing framework libraries
    build_tools       - Build and development tools

EOF
}

# Parse command line arguments
VERBOSE=false
CHECK_ALL=false
STRICT=false
EXTRACT_DIR=""
KEEP_EXTRACTED=false
OUTPUT_FORMAT="both"
TIMEOUT=30
PARALLEL_JOBS=4
DEPLOYMENT_PACKAGES=()

while [[ $# -gt 0 ]]; do
    case $1 in
        --help|-h)
            print_usage
            exit 0
            ;;
        --verbose|-v)
            VERBOSE=true
            shift
            ;;
        --check-all)
            CHECK_ALL=true
            shift
            ;;
        --strict)
            STRICT=true
            shift
            ;;
        --extract-dir)
            EXTRACT_DIR="$2"
            shift 2
            ;;
        --keep-extracted)
            KEEP_EXTRACTED=true
            shift
            ;;
        --output-format)
            OUTPUT_FORMAT="$2"
            shift 2
            ;;
        --timeout)
            TIMEOUT="$2"
            shift 2
            ;;
        --parallel)
            PARALLEL_JOBS="$2"
            shift 2
            ;;
        -*)
            echo "Unknown option: $1" >&2
            print_usage >&2
            exit 1
            ;;
        *)
            DEPLOYMENT_PACKAGES+=("$1")
            shift
            ;;
    esac
done

# Validate arguments
if [[ ${#DEPLOYMENT_PACKAGES[@]} -eq 0 ]]; then
    echo "Error: No deployment packages specified" >&2
    print_usage >&2
    exit 1
fi

# Validate output format
case "$OUTPUT_FORMAT" in
    text|json|both) ;;
    *)
        echo "Error: Invalid output format '$OUTPUT_FORMAT'. Use: text, json, both" >&2
        exit 1
        ;;
esac

# Verify deployment packages exist
for package in "${DEPLOYMENT_PACKAGES[@]}"; do
    if [[ ! -f "$package" ]]; then
        echo "Error: Deployment package not found: $package" >&2
        exit 1
    fi
done

# Library detection functions
extract_deployment_package() {
    local package_file="$1"
    local extract_dir="$2"
    local package_name=$(basename "$package_file")

    log_info "Extracting deployment package: $package_name"

    # Create extraction directory
    mkdir -p "$extract_dir"

    # Extract based on file extension
    case "$package_file" in
        *.tar.gz|*.tgz)
            tar -xzf "$package_file" -C "$extract_dir"
            ;;
        *.tar.bz2|*.tbz2)
            tar -xjf "$package_file" -C "$extract_dir"
            ;;
        *.tar.xz|*.txz)
            tar -xJf "$package_file" -C "$extract_dir"
            ;;
        *.tar)
            tar -xf "$package_file" -C "$extract_dir"
            ;;
        *.zip)
            unzip -q "$package_file" -d "$extract_dir"
            ;;
        *)
            log_error "Unsupported package format: $package_file"
            return 1
            ;;
    esac

    log_success "Package extracted successfully: $package_name"
    return 0
}

find_libraries_in_package() {
    local extract_dir="$1"
    local libraries_found=()

    log_info "Scanning for libraries in extracted package"

    # Search for library files in common locations
    for lib_path in "${LIBRARY_PATHS[@]}"; do
        local full_path="$extract_dir/$lib_path"
        if [[ -d "$full_path" ]]; then
            while IFS= read -r -d '' lib_file; do
                local lib_name=$(basename "$lib_file")
                libraries_found+=("$lib_path/$lib_name")

                if [[ "$VERBOSE" == true ]]; then
                    log_info "Found library: $lib_path/$lib_name"
                fi
            done < <(find "$full_path" -name "*.so*" -o -name "*.a" -o -name "*.dll" -o -name "*.dylib" -print0 2>/dev/null)
        fi
    done

    # Also check for libraries in binary directories
    local bin_dirs=("bin/" "sbin/" "usr/bin/" "usr/local/bin/")
    for bin_dir in "${bin_dirs[@]}"; do
        local full_path="$extract_dir/$bin_dir"
        if [[ -d "$full_path" ]]; then
            while IFS= read -r -d '' binary_file; do
                # Check binary dependencies
                if command -v ldd >/dev/null 2>&1; then
                    while IFS= read -r dep_line; do
                        if [[ "$dep_line" =~ ^[[:space:]]*([^[:space:]]+)[[:space:]]*=[[:space:]]*>>[[:space:]]*([^[:space:]]+) ]]; then
                            local lib_name="${BASH_REMATCH[1]}"
                            local lib_path="${BASH_REMATCH[2]}"
                            if [[ "$lib_path" =~ ^"$extract_dir" ]]; then
                                local relative_path="${lib_path#$extract_dir/}"
                                libraries_found+=("$relative_path")
                            fi
                        fi
                    done <<< "$(timeout "$TIMEOUT" ldd "$binary_file" 2>/dev/null || true)"
                fi
            done < <(find "$full_path" -type f -executable -print0 2>/dev/null)
        fi
    done

    printf '%s\n' "${libraries_found[@]}"
}

check_library_availability() {
    local extract_dir="$1"
    local category="$2"
    shift 2
    local required_libs=("$@")

    local found_libs=()
    local missing_libs=()

    # Find all libraries in the package
    local package_libs=()
    mapfile -t package_libs < <(find_libraries_in_package "$extract_dir")

    # Check each required library
    for lib_pattern in "${required_libs[@]}"; do
        local found=false

        # Check for exact matches
        for pkg_lib in "${package_libs[@]}"; do
            local lib_basename=$(basename "$pkg_lib")
            if [[ "$lib_basename" == "$lib_pattern" ]] || [[ "$lib_basename" == "$lib_pattern".* ]]; then
                found_libs+=("$lib_pattern")
                found=true
                if [[ "$VERBOSE" == true ]]; then
                    log_info "Found required library: $lib_pattern (as $pkg_lib)"
                fi
                break
            fi
        done

        # Check for pattern matches (wildcard)
        if [[ "$found" == false && "$lib_pattern" == *".so" ]]; then
            local base_pattern="${lib_pattern%.so}"
            for pkg_lib in "${package_libs[@]}"; do
                local lib_basename=$(basename "$pkg_lib")
                if [[ "$lib_basename" == "$base_pattern".so.* ]] || [[ "$lib_basename" =~ ^$base_pattern\.so[\.0-9]*$ ]]; then
                    found_libs+=("$lib_pattern")
                    found=true
                    if [[ "$VERBOSE" == true ]]; then
                        log_info "Found matching library: $lib_pattern (as $pkg_lib)"
                    fi
                    break
                fi
            done
        fi

        if [[ "$found" == false ]]; then
            missing_libs+=("$lib_pattern")
            if [[ "$VERBOSE" == true ]]; then
                log_warning "Missing required library: $lib_pattern"
            fi
        fi
    done

    # Calculate availability percentage
    local total_libs=${#required_libs[@]}
    local found_count=${#found_libs[@]}
    local availability=0
    if [[ $total_libs -gt 0 ]]; then
        availability=$((found_count * 100 / total_libs))
    fi

    printf '%s|%s|%s|%d\n' "$category" "$availability" "$(printf '%s,' "${found_libs[@]}" | sed 's/,$//')" "$(printf '%s,' "${missing_libs[@]}" | sed 's/,$//')"
}

analyze_library_categories() {
    local extract_dir="$1"
    local categories_to_check=()

    if [[ "$CHECK_ALL" == true ]]; then
        categories_to_check=("${LIBRARY_CATEGORIES[@]}")
    else
        # Check only essential categories
        categories_to_check=("system_runtime" "crypto_math" "compute_gpu")
    fi

    log_info "Analyzing library categories: ${categories_to_check[*]}"

    local total_libraries=0
    local total_available=0
    local categories_complete=0

    declare -A category_results

    for category in "${categories_to_check[@]}"; do
        local category_libs=(${CATEGORY_LIBRARIES[$category]})

        if [[ "$VERBOSE" == true ]]; then
            log_info "Checking category: $category (${category_libs[*]})"
        fi

        local result
        result=$(check_library_availability "$extract_dir" "$category" "${category_libs[@]}")

        local availability=$(echo "$result" | cut -d'|' -f2)
        local found_libs=$(echo "$result" | cut -d'|' -f3)
        local missing_libs=$(echo "$result" | cut -d'|' -f4)

        category_results[$category]="$availability|$found_libs|$missing_libs"

        # Update totals
        local category_total=${#category_libs[@]}
        local category_found=$((${#found_libs} + ${#missing_libs} > 0 ? ${#found_libs} : 0))
        total_libraries=$((total_libraries + category_total))
        total_available=$((total_available + category_found))

        if [[ $availability -eq 100 ]]; then
            categories_complete=$((categories_complete + 1))
        fi

        log_info "Category $category: $availability% ($category_found/$category_total libraries)"
    done

    # Return results
    for category in "${categories_to_check[@]}"; do
        echo "$category|${category_results[$category]}"
    done
}

verify_deployment_package() {
    local package_file="$1"
    local package_name=$(basename "$package_file")
    local temp_extract_dir

    if [[ -n "$EXTRACT_DIR" ]]; then
        temp_extract_dir="$EXTRACT_DIR/$package_name"
    else
        temp_extract_dir=$(mktemp -d)
    fi

    log_info "Verifying library availability for package: $package_name"

    # Add package to JSON report
    add_package_to_json "$package_file"

    # Extract package
    if ! extract_deployment_package "$package_file" "$temp_extract_dir"; then
        log_error "Failed to extract package: $package_name"
        update_package_status "$package_name" "extraction_failed" 0 0 0
        [[ -n "$EXTRACT_DIR" ]] || rm -rf "$temp_extract_dir"
        return 1
    fi

    # Analyze library categories
    local category_results
    mapfile -t category_results < <(analyze_library_categories "$temp_extract_dir")

    # Process results
    local total_found=0
    local total_missing=0
    local categories_with_libraries=0

    declare -A library_details
    declare -A category_analysis

    for result in "${category_results[@]}"; do
        local category=$(echo "$result" | cut -d'|' -f1)
        local availability=$(echo "$result" | cut -d'|' -f2)
        local found_libs=$(echo "$result" | cut -d'|' -f3)
        local missing_libs=$(echo "$result" | cut -d'|' -f4)

        # Count libraries
        if [[ -n "$found_libs" ]]; then
            IFS=',' read -ra found_array <<< "$found_libs"
            total_found=$((total_found + ${#found_array[@]}))
        fi

        if [[ -n "$missing_libs" ]]; then
            IFS=',' read -ra missing_array <<< "$missing_libs"
            total_missing=$((total_missing + ${#missing_array[@]}))
        fi

        if [[ ${#found_array[@]} -gt 0 ]] || [[ ${#missing_array[@]} -gt 0 ]]; then
            categories_with_libraries=$((categories_with_libraries + 1))
        fi

        # Store details
        library_details[$category]="$found_libs|$missing_libs"
        category_analysis[$category]="$availability"

        log_info "Category $category: $availability% availability"
    done

    # Calculate overall coverage
    local total_libraries=$((total_found + total_missing))
    local coverage_percentage=0
    if [[ $total_libraries -gt 0 ]]; then
        coverage_percentage=$((total_found * 100 / total_libraries))
    fi

    # Update JSON report
    update_package_status "$package_name" "completed" "$total_found" "$total_missing" "$coverage_percentage"

    # Generate detailed report
    echo "=== Library Availability Report for $package_name ==="
    echo "Total Libraries Found: $total_found"
    echo "Total Libraries Missing: $total_missing"
    echo "Overall Availability: $coverage_percentage%"
    echo "Categories with Libraries: $categories_with_libraries"
    echo

    for category in "${!category_analysis[@]}"; do
        local availability=${category_analysis[$category]}
        local details=${library_details[$category]}
        local found_libs=$(echo "$details" | cut -d'|' -f1)
        local missing_libs=$(echo "$details" | cut -d'|' -f2)

        echo "Category: $category ($availability%)"

        if [[ -n "$found_libs" ]]; then
            echo "  ✓ Available: $found_libs"
        fi

        if [[ -n "$missing_libs" ]]; then
            echo "  ✗ Missing: $missing_libs"
        fi

        echo
    done

    # Cleanup
    if [[ "$KEEP_EXTRACTED" == false && -z "$EXTRACT_DIR" ]]; then
        rm -rf "$temp_extract_dir"
    fi

    # Check for strict mode failures
    if [[ "$STRICT" == true && $total_missing -gt 0 ]]; then
        log_error "Strict mode: Package $package_name has missing libraries"
        return 1
    fi

    log_success "Library availability verification completed for: $package_name"
    return 0
}

# Main verification function
main() {
    log_info "Starting T042: Library Availability Verification"
    log_info "Processing ${#DEPLOYMENT_PACKAGES[@]} deployment package(s)"

    # Initialize JSON report
    init_json_report
    update_json_timestamp

    local successful_packages=0
    local failed_packages=0
    local total_libraries_checked=0
    local total_libraries_available=0
    local total_categories_complete=0
    local total_categories_checked=0

    # Process packages in parallel if specified
    if [[ $PARALLEL_JOBS -gt 1 && ${#DEPLOYMENT_PACKAGES[@]} -gt 1 ]]; then
        log_info "Processing packages in parallel (jobs: $PARALLEL_JOBS)"

        # Use GNU parallel or xargs for parallel processing
        if command -v parallel >/dev/null 2>&1; then
            printf '%s\n' "${DEPLOYMENT_PACKAGES[@]}" | parallel -j "$PARALLEL_JOBS" "$0" --single-package --output-format json {}
        else
            printf '%s\n' "${DEPLOYMENT_PACKAGES[@]}" | xargs -P "$PARALLEL_JOBS" -I {} "$0" --single-package --output-format json {}
        fi

        # Collect results from parallel processing
        for package in "${DEPLOYMENT_PACKAGES[@]}"; do
            if verify_deployment_package "$package"; then
                ((successful_packages++))
            else
                ((failed_packages++))
            fi
        done
    else
        # Process packages sequentially
        for package in "${DEPLOYMENT_PACKAGES[@]}"; do
            if verify_deployment_package "$package"; then
                ((successful_packages++))
            else
                ((failed_packages++))
            fi
        done
    fi

    # Update summary in JSON report
    local temp_json=$(mktemp)
    jq --argjson total "${#DEPLOYMENT_PACKAGES[@]}" \
       --argjson successful "$successful_packages" \
       --argjson failed "$failed_packages" \
       '.summary.total_packages = $total | .summary.successful_packages = $successful | .summary.failed_packages = $failed' \
       "$JSON_REPORT" > "$temp_json" && mv "$temp_json" "$JSON_REPORT"

    # Generate final report
    echo "=== T042 Library Availability Verification Summary ==="
    echo "Total Packages: ${#DEPLOYMENT_PACKAGES[@]}"
    echo "Successful: $successful_packages"
    echo "Failed: $failed_packages"
    echo "Log File: $LIBRARY_CHECK_LOG"

    if [[ "$OUTPUT_FORMAT" == "json" || "$OUTPUT_FORMAT" == "both" ]]; then
        echo "JSON Report: $JSON_REPORT"
    fi

    if [[ $failed_packages -gt 0 ]]; then
        log_error "Some packages failed library availability verification"
        if [[ "$STRICT" == true ]]; then
            exit 1
        fi
    else
        log_success "All packages passed library availability verification"
    fi

    log_info "T042 library availability verification completed"
}

# Handle single package mode (for parallel processing)
if [[ "${1:-}" == "--single-package" ]]; then
    # Parse only the package file from remaining arguments
    for arg in "${@:2}"; do
        if [[ -f "$arg" && "$arg" != --* ]]; then
            verify_deployment_package "$arg"
            exit $?
        fi
    done
    exit 0
fi

# Run main function
main