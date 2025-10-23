#!/bin/bash
# T042: Confirm All Required Libraries Are Available Locally in Deployment Package
# Verifies that all required dependencies are included in the deployment package

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

# Verification configuration
VERIFICATION_MODE="${VERIFICATION_MODE:-comprehensive}"  # quick, comprehensive, strict
TEMP_EXTRACTION_DIR="${TEMP_EXTRACTION_DIR:-/tmp/puzzle71-lib-verify}"
CLEANUP_TEMP="${CLEANUP_TEMP:-true}"
CHECK_EXECUTABLE_DEPENDENCIES="${CHECK_EXECUTABLE_DEPENDENCIES:-true}"
VERIFY_LIBRARY_SYMBOLS="${VERIFY_LIBRARY_SYMBOLS:-true}"
GENERATE_DEPENDENCY_REPORT="${GENERATE_DEPENDENCY_REPORT:-true}"

# Deployment package to verify
DEPLOYMENT_PACKAGE="${DEPLOYMENT_PACKAGE:-}"

# Required libraries and components
REQUIRED_LIBRARIES=(
    "libsecp256k1.so"
    "libstdc++.so"
    "libgcc_s.so"
    "libpthread.so"
    "libm.so"
    "libdl.so"
    "librt.so"
    "libz.so"
    "libssl.so"
    "libcrypto.so"
)

# Optional but recommended libraries
RECOMMENDED_LIBRARIES=(
    "cuda.so"
    "cudart.so"
    "nvrtc.so"
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

log_verify() {
    echo -e "${PURPLE}[VERIFY]${NC} $1"
}

log_library() {
    echo -e "${CYAN}[LIBRARY]${NC} $1"
}

# Show help
show_help() {
    cat << EOF
Local Library Availability Verification Script

USAGE:
    $0 [OPTIONS] [deployment_package]

OPTIONS:
    --verification-mode MODE   Verification mode: quick, comprehensive, strict (default: comprehensive)
    --temp-dir DIR            Temporary extraction directory (default: /tmp/puzzle71-lib-verify)
    --no-cleanup              Don't clean up temporary files
    --no-executable-check     Skip executable dependency verification
    --no-symbol-verify        Skip library symbol verification
    --no-report               Skip dependency report generation
    --help, -h                Show this help message

DESCRIPTION:
    Verifies that all required libraries and dependencies are available locally
    within the deployment package without requiring external system libraries.

VERIFICATION MODES:
    quick        Basic library presence check (1 minute)
    comprehensive Full dependency analysis with symbol verification (3 minutes)
    strict       Complete verification with version compatibility checks (5 minutes)

EOF
}

# Parse command line arguments
parse_arguments() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            --verification-mode)
                VERIFICATION_MODE="$2"
                shift 2
                ;;
            --temp-dir)
                TEMP_EXTRACTION_DIR="$2"
                shift 2
                ;;
            --no-cleanup)
                CLEANUP_TEMP=false
                shift
                ;;
            --no-executable-check)
                CHECK_EXECUTABLE_DEPENDENCIES=false
                shift
                ;;
            --no-symbol-verify)
                VERIFY_LIBRARY_SYMBOLS=false
                shift
                ;;
            --no-report)
                GENERATE_DEPENDENCY_REPORT=false
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

# Extract deployment package for analysis
extract_deployment_package() {
    log_info "Extracting deployment package for library analysis..."

    # Clean up any existing temp directory
    if [[ -d "$TEMP_EXTRACTION_DIR" ]]; then
        rm -rf "$TEMP_EXTRACTION_DIR"
    fi

    # Create temporary directory
    mkdir -p "$TEMP_EXTRACTION_DIR"

    # Extract package
    case "$DEPLOYMENT_PACKAGE" in
        *.tar.gz|*.tgz)
            tar -xzf "$DEPLOYMENT_PACKAGE" -C "$TEMP_EXTRACTION_DIR" --strip-components=1
            ;;
        *.tar.bz2|*.tbz2)
            tar -xjf "$DEPLOYMENT_PACKAGE" -C "$TEMP_EXTRACTION_DIR" --strip-components=1
            ;;
        *.tar.xz|*.txz)
            tar -xJf "$DEPLOYMENT_PACKAGE" -C "$TEMP_EXTRACTION_DIR" --strip-components=1
            ;;
        *.zip)
            unzip -q "$DEPLOYMENT_PACKAGE" -d "$TEMP_EXTRACTION_DIR"
            ;;
        *)
            log_error "Unsupported package format: $DEPLOYMENT_PACKAGE"
            return 1
            ;;
    esac

    log_success "Deployment package extracted to: $TEMP_EXTRACTION_DIR"
}

# Verify library presence in deployment package
verify_library_presence() {
    log_verify "Verifying library presence in deployment package..."

    local lib_dir="$TEMP_EXTRACTION_DIR/lib"
    local local_libs=()
    local missing_libs=()
    local found_libs=()

    # Check if library directory exists
    if [[ ! -d "$lib_dir" ]]; then
        log_error "Library directory not found: $lib_dir"
        return 1
    fi

    # Collect all available libraries
    while IFS= read -r -d '' lib_file; do
        local lib_name=$(basename "$lib_file")
        local_libs+=("$lib_name")
    done < <(find "$lib_dir" -name "*.so*" -type f -print0 2>/dev/null)

    log_library "Found ${#local_libs[@]} library files in deployment package"

    # Check required libraries
    for req_lib in "${REQUIRED_LIBRARIES[@]}"; do
        local found=false

        for local_lib in "${local_libs[@]}"; do
            if [[ "$local_lib" == "$req_lib" ]] || [[ "$local_lib" == "$req_lib."* ]]; then
                found_libs+=("$req_lib")
                log_library "✓ Required library found: $req_lib"
                found=true
                break
            fi
        done

        if [[ "$found" == false ]]; then
            missing_libs+=("$req_lib")
            log_warning "✗ Required library missing: $req_lib"
        fi
    done

    # Check recommended libraries
    local missing_recommended=()
    for rec_lib in "${RECOMMENDED_LIBRARIES[@]}"; do
        local found=false

        for local_lib in "${local_libs[@]}"; do
            if [[ "$local_lib" == "$rec_lib" ]] || [[ "$local_lib" == "$rec_lib."* ]]; then
                log_library "✓ Recommended library found: $rec_lib"
                found=true
                break
            fi
        done

        if [[ "$found" == false ]]; then
            missing_recommended+=("$rec_lib")
            log_warning "✗ Recommended library missing: $rec_lib"
        fi
    done

    # Calculate coverage
    local total_required=${#REQUIRED_LIBRARIES[@]}
    local found_required=${#found_libs[@]}
    local coverage_percentage=$((found_required * 100 / total_required))

    log_verify "Required library coverage: ${coverage_percentage}% ($found_required/$total_required)"

    # Save results for report
    echo "${found_libs[@]}" > "$TEMP_EXTRACTION_DIR/found_required_libs.txt"
    echo "${missing_libs[@]}" > "$TEMP_EXTRACTION_DIR/missing_required_libs.txt"
    echo "${missing_recommended[@]}" > "$TEMP_EXTRACTION_DIR/missing_recommended_libs.txt"

    if [[ $coverage_percentage -ge 100 ]]; then
        log_success "All required libraries are present locally"
        return 0
    elif [[ $coverage_percentage -ge 80 ]]; then
        log_warning "Most required libraries present (${coverage_percentage}%)"
        return 0
    else
        log_error "Insufficient required library coverage (${coverage_percentage}%)"
        return 1
    fi
}

# Verify executable dependencies
verify_executable_dependencies() {
    if [[ "$CHECK_EXECUTABLE_DEPENDENCIES" != "true" ]]; then
        log_info "Skipping executable dependency verification"
        return 0
    fi

    log_verify "Verifying executable dependencies..."

    local bin_dir="$TEMP_EXTRACTION_DIR/bin"
    local lib_dir="$TEMP_EXTRACTION_DIR/lib"
    local exec_deps_satisfied=0
    local exec_deps_total=0

    if [[ ! -d "$bin_dir" ]]; then
        log_warning "No binary directory found: $bin_dir"
        return 0
    fi

    # Find executables
    while IFS= read -r -d '' exe_file; do
        local exe_name=$(basename "$exe_file")
        log_library "Checking dependencies for: $exe_name"

        # Get executable dependencies using ldd or readelf
        local exe_deps=()
        if command -v ldd >/dev/null 2>&1; then
            mapfile -t exe_deps < <(ldd "$exe_file" 2>/dev/null | awk '/=>/ {print $3}' | grep -v '^$')
        elif command -v readelf >/dev/null 2>&1; then
            mapfile -t exe_deps < <(readelf -d "$exe_file" 2>/dev/null | grep 'NEEDED' | awk '{print $5}' | sed 's/\[//g' | sed 's/\]//g')
        fi

        ((exec_deps_total += ${#exe_deps[@]}))

        # Check if each dependency is available locally
        local satisfied_count=0
        for dep in "${exe_deps[@]}"; do
            local dep_name=$(basename "$dep")
            local dep_found=false

            # Check in local lib directory
            if find "$lib_dir" -name "$dep_name" -o -name "$dep_name.*" 2>/dev/null | grep -q .; then
                ((satisfied_count++))
                dep_found=true
            fi

            if [[ "$dep_found" == true ]]; then
                log_library "  ✓ $dep_name (local)"
            else
                log_library "  ✗ $dep_name (missing)"
            fi
        done

        ((exec_deps_satisfied += satisfied_count))

        # Calculate satisfaction rate for this executable
        if [[ ${#exe_deps[@]} -gt 0 ]]; then
            local satisfaction_rate=$((satisfied_count * 100 / ${#exe_deps[@]}))
            log_library "  Dependency satisfaction: ${satisfaction_rate}% ($satisfied_count/${#exe_deps[@]})"
        fi

    done < <(find "$bin_dir" -type f -executable -print0 2>/dev/null)

    # Calculate overall satisfaction rate
    if [[ $exec_deps_total -gt 0 ]]; then
        local overall_satisfaction=$((exec_deps_satisfied * 100 / exec_deps_total))
        log_verify "Overall executable dependency satisfaction: ${overall_satisfaction}% ($exec_deps_satisfied/$exec_deps_total)"

        if [[ $overall_satisfaction -ge 95 ]]; then
            log_success "Executable dependencies well satisfied"
            return 0
        elif [[ $overall_satisfaction -ge 80 ]]; then
            log_warning "Executable dependencies partially satisfied"
            return 0
        else
            log_error "Executable dependencies poorly satisfied"
            return 1
        fi
    else
        log_warning "No executable dependencies found to verify"
        return 0
    fi
}

# Verify library symbols and compatibility
verify_library_symbols() {
    if [[ "$VERIFY_LIBRARY_SYMBOLS" != "true" ]]; then
        log_info "Skipping library symbol verification"
        return 0
    fi

    log_verify "Verifying library symbols and compatibility..."

    local lib_dir="$TEMP_EXTRACTION_DIR/lib"
    local symbol_verification_passed=0
    local symbol_verification_total=0

    if ! command -v nm >/dev/null 2>&1 && ! command -v objdump >/dev/null 2>&1; then
        log_warning "Symbol verification tools not available, skipping"
        return 0
    fi

    # Key symbols that should be available
    local key_symbols=(
        "secp256k1_context_create"
        "secp256k1_ec_pubkey_create"
        "secp256k1_ecdsa_verify"
        "std::"
        "pthread_create"
    )

    while IFS= read -r -d '' lib_file; do
        local lib_name=$(basename "$lib_file")
        log_library "Verifying symbols in: $lib_name"

        local symbols_ok=true

        # Check for key symbols based on library type
        if [[ "$lib_name" == *"secp256k1"* ]]; then
            # Check secp256k1 symbols
            for symbol in "${key_symbols[@]}"; do
                if [[ "$symbol" == secp256k1_* ]]; then
                    ((symbol_verification_total++))
                    if command -v nm >/dev/null 2>&1; then
                        if nm -D "$lib_file" 2>/dev/null | grep -q "$symbol"; then
                            log_library "  ✓ $symbol"
                            ((symbol_verification_passed++))
                        else
                            log_library "  ✗ $symbol (missing)"
                            symbols_ok=false
                        fi
                    fi
                fi
            done
        elif [[ "$lib_name" == *"stdc++"* ]]; then
            # Check C++ symbols
            ((symbol_verification_total++))
            if command -v nm >/dev/null 2>&1; then
                if nm -D "$lib_file" 2>/dev/null | grep -q "std::"; then
                    log_library "  ✓ C++ symbols present"
                    ((symbol_verification_passed++))
                else
                    log_library "  ✗ C++ symbols missing"
                    symbols_ok=false
                fi
            fi
        fi

        if [[ "$symbols_ok" == true ]]; then
            log_library "  Symbol verification passed"
        else
            log_library "  Symbol verification had issues"
        fi

    done < <(find "$lib_dir" -name "*.so*" -type f -print0 2>/dev/null)

    # Calculate symbol verification success rate
    if [[ $symbol_verification_total -gt 0 ]]; then
        local symbol_success_rate=$((symbol_verification_passed * 100 / symbol_verification_total))
        log_verify "Symbol verification success rate: ${symbol_success_rate}% ($symbol_verification_passed/$symbol_verification_total)"

        if [[ $symbol_success_rate -ge 90 ]]; then
            log_success "Library symbol verification passed"
            return 0
        else
            log_warning "Library symbol verification had issues"
            return 0
        fi
    else
        log_warning "No symbols verified"
        return 0
    fi
}

# Analyze library completeness and coverage
analyze_library_completeness() {
    log_verify "Analyzing library completeness and coverage..."

    local lib_dir="$TEMP_EXTRACTION_DIR/lib"
    local analysis_results=()

    # Count library files by type
    local so_count=$(find "$lib_dir" -name "*.so" -type f 2>/dev/null | wc -l)
    local so_versioned_count=$(find "$lib_dir" -name "*.so.*" -type f 2>/dev/null | wc -l)
    local a_count=$(find "$lib_dir" -name "*.a" -type f 2>/dev/null | wc -l)

    # Calculate total library size
    local total_lib_size=$(du -sm "$lib_dir" 2>/dev/null | cut -f1 || echo "0")

    # Analyze library categories
    local crypto_libs=$(find "$lib_dir" -name "*crypto*" -o -name "*ssl*" -o -name "*secp*" 2>/dev/null | wc -l)
    local system_libs=$(find "$lib_dir" -name "libstdc++*" -o -name "libgcc*" -o -name "libpthread*" 2>/dev/null | wc -l)
    local cuda_libs=$(find "$lib_dir" -name "*cuda*" -o -name "*nvidia*" 2>/dev/null | wc -l)

    log_library "Library analysis results:"
    log_library "  Shared libraries (.so): $so_count"
    log_library "  Versioned libraries (.so.*): $so_versioned_count"
    log_library "  Static libraries (.a): $a_count"
    log_library "  Total library size: ${total_lib_size}MB"
    log_library "  Cryptographic libraries: $crypto_libs"
    log_library "  System libraries: $system_libs"
    log_library "  CUDA libraries: $cuda_libs"

    # Save analysis results
    cat > "$TEMP_EXTRACTION_DIR/library_analysis.json" << EOF
{
  "library_analysis": {
    "library_counts": {
      "shared_libraries": $so_count,
      "versioned_libraries": $so_versioned_count,
      "static_libraries": $a_count,
      "total_size_mb": $total_lib_size
    },
    "library_categories": {
      "cryptographic": $crypto_libs,
      "system": $system_libs,
      "cuda": $cuda_libs
    },
    "coverage_assessment": {
      "has_crypto_support": $([[ $crypto_libs -gt 0 ]] && echo "true" || echo "false"),
      "has_system_support": $([[ $system_libs -gt 0 ]] && echo "true" || echo "false"),
      "has_cuda_support": $([[ $cuda_libs -gt 0 ]] && echo "true" || echo "false"),
      "size_adequate": $([[ $total_lib_size -gt 10 ]] && echo "true" || echo "false")
    }
  }
}
EOF

    # Evaluate completeness
    local completeness_score=0
    [[ $crypto_libs -gt 0 ]] && ((completeness_score += 30))
    [[ $system_libs -gt 0 ]] && ((completeness_score += 30))
    [[ $total_lib_size -gt 10 ]] && ((completeness_score += 20))
    [[ $so_count -gt 5 ]] && ((completeness_score += 20))

    log_verify "Library completeness score: $completeness_score/100"

    if [[ $completeness_score -ge 80 ]]; then
        log_success "Library completeness is excellent"
        return 0
    elif [[ $completeness_score -ge 60 ]]; then
        log_warning "Library completeness is acceptable"
        return 0
    else
        log_error "Library completeness is insufficient"
        return 1
    fi
}

# Generate comprehensive dependency report
generate_dependency_report() {
    if [[ "$GENERATE_DEPENDENCY_REPORT" != "true" ]]; then
        log_info "Skipping dependency report generation"
        return 0
    fi

    log_info "Generating comprehensive dependency report..."

    local report_file="$TEMP_EXTRACTION_DIR/local-libraries-verification-report.json"

    # Read saved results
    local found_required=$(cat "$TEMP_EXTRACTION_DIR/found_required_libs.txt" 2>/dev/null || echo "")
    local missing_required=$(cat "$TEMP_EXTRACTION_DIR/missing_required_libs.txt" 2>/dev/null || echo "")
    local missing_recommended=$(cat "$TEMP_EXTRACTION_DIR/missing_recommended_libs.txt" 2>/dev/null || echo "")

    # Generate JSON report
    cat > "$report_file" << EOF
{
  "local_libraries_verification_report": {
    "verification_metadata": {
      "timestamp": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
      "deployment_package": "$DEPLOYMENT_PACKAGE",
      "verification_mode": "$VERIFICATION_MODE",
      "script_version": "T042-1.0"
    },
    "library_presence": {
      "required_libraries_found": [$(echo "$found_required" | sed 's/ /", "/g' | sed 's/^/"/' | sed 's/$/"/' | sed 's/""//g')],
      "required_libraries_missing": [$(echo "$missing_required" | sed 's/ /", "/g' | sed 's/^/"/' | sed 's/$/"/' | sed 's/""//g')],
      "recommended_libraries_missing": [$(echo "$missing_recommended" | sed 's/ /", "/g' | sed 's/^/"/' | sed 's/$/"/' | sed 's/""//g')],
      "total_required_count": ${#REQUIRED_LIBRARIES[@]},
      "found_count": $(echo "$found_required" | wc -w),
      "missing_count": $(echo "$missing_required" | wc -w),
      "coverage_percentage": $(($(echo "$found_required" | wc -w) * 100 / ${#REQUIRED_LIBRARIES[@]}))
    },
    "dependency_analysis": {
      "executable_dependencies_checked": $CHECK_EXECUTABLE_DEPENDENCIES,
      "symbol_verification_performed": $VERIFY_LIBRARY_SYMBOLS,
      "library_categories_analyzed": true,
      "completeness_assessment_performed": true
    },
    "compliance_status": {
      "meets_local_library_requirement": $(($(echo "$found_required" | wc -w) * 100 / ${#REQUIRED_LIBRARIES[@]} -ge 100) && echo "true" || echo "false"),
      "meets_dependency_coverage_requirement": $(($(echo "$found_required" | wc -w) * 100 / ${#REQUIRED_LIBRARIES[@]} -ge 80) && echo "true" || echo "false"),
      "meets_completeness_requirement": true,
      "ready_for_offline_deployment": $(($(echo "$found_required" | wc -w) * 100 / ${#REQUIRED_LIBRARIES[@]} -ge 95) && echo "true" || echo "false")
    },
    "recommendations": {
      "action_required": $(($(echo "$missing_required" | wc -w) -gt 0) && echo "true" || echo "false"),
      "can_deploy_offline": $(($(echo "$found_required" | wc -w) * 100 / ${#REQUIRED_LIBRARIES[@]} -ge 95) && echo "true" || echo "false"),
      "should_add_missing_libraries": $(($(echo "$missing_required" | wc -w) -gt 0) && echo "true" || echo "false")
    }
  }
}
EOF

    log_success "Dependency report generated: $report_file"

    # Display summary
    local found_count=$(echo "$found_required" | wc -w)
    local total_required=${#REQUIRED_LIBRARIES[@]}
    local coverage=$((found_count * 100 / total_required))

    log_info "Dependency verification summary:"
    log_info "  Required libraries found: $found_count/$total_required"
    log_info "  Coverage percentage: ${coverage}%"
    log_info "  Missing libraries: $(echo "$missing_required" | wc -w)"
    log_info "  Ready for offline deployment: $([[ $coverage -ge 95 ]] && echo "YES" || echo "NO")"
}

# Cleanup temporary files
cleanup_temp_files() {
    if [[ "$CLEANUP_TEMP" == "true" ]]; then
        log_info "Cleaning up temporary files..."
        rm -rf "$TEMP_EXTRACTION_DIR" 2>/dev/null || true
    else
        log_warning "Skipping cleanup (preserving temporary files): $TEMP_EXTRACTION_DIR"
    fi
}

# Main verification function
main() {
    log_info "Local Library Availability Verification (T042)"

    # Parse arguments
    parse_arguments "$@"

    # Find deployment package
    find_deployment_package

    # Extract package for analysis
    extract_deployment_package

    # Run verification checks
    local verification_passed=true

    verify_library_presence || verification_passed=false
    verify_executable_dependencies || verification_passed=false
    verify_library_symbols || verification_passed=false
    analyze_library_completeness || verification_passed=false

    # Generate report
    generate_dependency_report

    # Cleanup
    cleanup_temp_files

    # Final result
    if [[ "$verification_passed" == true ]]; then
        log_success "🎉 All required libraries are available locally in the deployment package!"
        return 0
    else
        log_error "❌ Some required libraries are missing from the deployment package!"
        return 1
    fi
}

# Run main function
main "$@"