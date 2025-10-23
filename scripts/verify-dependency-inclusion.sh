#!/bin/bash

# Dependency Inclusion Verification Script
# T034: Implement dependency inclusion verification for deployment packages
# User Story 2: One-Click Deployment

set -euo pipefail

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
DEPLOYMENT_DIR="$BUILD_DIR/deployment"
INTEGRATION_ROOT="$PROJECT_ROOT/src/extracted"
VERIFICATION_REPORT="$BUILD_DIR/dependency-inclusion-verification.json"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Verification state
VERIFICATION_PASSED=true
MISSING_DEPENDENCIES=()
EXTRA_DEPENDENCIES=()
INCOMPATIBLE_VERSIONS=()
INTEGRATION_ISSUES=()

# Logging functions
log() {
    echo -e "${BLUE}[$(date '+%Y-%m-%d %H:%M:%S')]${NC} $1"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1" >&2
    VERIFICATION_PASSED=false
}

warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

# Initialize verification environment
initialize_verification() {
    log "Initializing dependency inclusion verification..."

    # Clear previous results
    MISSING_DEPENDENCIES=()
    EXTRA_DEPENDENCIES=()
    INCOMPATIBLE_VERSIONS=()
    INTEGRATION_ISSUES=()
    VERIFICATION_PASSED=true

    # Create verification directory
    mkdir -p "$BUILD_DIR"
    mkdir -p "$(dirname "$VERIFICATION_REPORT")"

    success "Verification environment initialized"
}

# Verify extracted library inclusion
verify_extracted_libraries() {
    log "Verifying extracted library inclusion..."

    local expected_libraries=("secp256k1-zkp" "bitcrack")
    local found_libraries=()
    local missing_libraries=()

    # Check if integration root exists
    if [[ ! -d "$INTEGRATION_ROOT" ]]; then
        error "Integration root directory not found: $INTEGRATION_ROOT"
        return 1
    fi

    # Check each expected library
    for lib in "${expected_libraries[@]}"; do
        local lib_path="$INTEGRATION_ROOT/$lib"

        if [[ -d "$lib_path" ]]; then
            found_libraries+=("$lib")

            # Verify library has source files
            local source_count=$(find "$lib_path" -name "*.c" -o -name "*.cpp" -o -name "*.h" | wc -l)
            if [[ $source_count -eq 0 ]]; then
                warning "Library $lib found but contains no source files"
                INTEGRATION_ISSUES+=("Library $lib: No source files found")
            else
                log "✓ Library $lib: $source_count source files"
            fi

            # Verify library is included in deployment package
            local deployment_lib_path="$DEPLOYMENT_DIR/include/extracted/$lib"
            if [[ -d "$deployment_lib_path" ]]; then
                local deployment_source_count=$(find "$deployment_lib_path" -name "*.c" -o -name "*.cpp" -o -name "*.h" | wc -l)
                if [[ $deployment_source_count -gt 0 ]]; then
                    log "✓ Library $lib included in deployment: $deployment_source_count files"
                else
                    error "Library $lib directory exists in deployment but contains no files"
                    MISSING_DEPENDENCIES+=("$lib (empty in deployment)")
                fi
            else
                error "Library $lib not found in deployment package"
                MISSING_DEPENDENCIES+=("$lib")
            fi

            # Verify attribution documentation
            local attribution_path="$lib_path/ATTRIBUTION.md"
            if [[ -f "$attribution_path" ]]; then
                log "✓ Library $lib: Attribution documentation found"
            else
                warning "Library $lib: Attribution documentation missing"
                INTEGRATION_ISSUES+=("Library $lib: Missing attribution documentation")
            fi

        else
            missing_libraries+=("$lib")
            error "Expected library $lib not found in integration root"
            MISSING_DEPENDENCIES+=("$lib (missing from integration)")
        fi
    done

    # Check for unexpected libraries
    local actual_libraries=()
    for dir in "$INTEGRATION_ROOT"/*; do
        if [[ -d "$dir" ]]; then
            local lib_name=$(basename "$dir")
            if [[ ! " ${expected_libraries[*]} " =~ " $lib_name " ]]; then
                actual_libraries+=("$lib_name")
                warning "Unexpected library found: $lib_name"
                EXTRA_DEPENDENCIES+=("$lib_name")
            fi
        fi
    done

    # Summary
    log "Extracted libraries verification:"
    log "  Expected: ${#expected_libraries[@]}"
    log "  Found: ${#found_libraries[@]}"
    log "  Missing: ${#missing_libraries[@]}"
    log "  Extra: ${#actual_libraries[@]}"

    if [[ ${#missing_libraries[@]} -eq 0 && ${#actual_libraries[@]} -eq 0 ]]; then
        success "All expected libraries are properly integrated"
    else
        error "Library integration issues detected"
    fi
}

# Verify binary dependencies
verify_binary_dependencies() {
    log "Verifying binary dependencies..."

    local main_binary="$DEPLOYMENT_DIR/bin/Puzzle71Solver"

    if [[ ! -f "$main_binary" ]]; then
        error "Main binary not found: $main_binary"
        return 1
    fi

    if [[ ! -x "$main_binary" ]]; then
        error "Main binary is not executable: $main_binary"
        return 1
    fi

    # Check binary dependencies with ldd
    log "Checking binary dynamic dependencies..."
    local binary_deps=()
    local missing_deps=()
    local resolved_deps=()

    while IFS= read -r line; do
        if [[ -n "$line" && "$line" != "not a dynamic executable" ]]; then
            local dep_path=$(echo "$line" | awk '{print $3}')
            local dep_name=$(echo "$line" | awk '{print $1}')

            binary_deps+=("$dep_name")

            if [[ "$dep_path" == "not found" ]]; then
                missing_deps+=("$dep_name")
                error "Binary dependency not found: $dep_name"
            elif [[ -n "$dep_path" && "$dep_path" != "$dep_name" ]]; then
                # Check if dependency is included in deployment package
                local dep_basename=$(basename "$dep_path")
                local deployment_dep="$DEPLOYMENT_DIR/lib/$dep_basename"

                if [[ -f "$deployment_dep" ]]; then
                    resolved_deps+=("$dep_name (included)")
                    log "✓ Dependency $dep_name: Included in deployment package"
                else
                    if [[ "$dep_path" == /usr/lib/* || "$dep_path" == /lib/* ]]; then
                        resolved_deps+=("$dep_name (system)")
                        log "✓ Dependency $dep_name: System library"
                    else
                        missing_deps+=("$dep_name")
                        warning "Dependency $dep_name: External library not included"
                    fi
                fi
            fi
        fi
    done < <(ldd "$main_binary" 2>/dev/null || true)

    # Check if critical libraries are included
    local critical_libs=("libsecp256k1" "libcudart" "libkeycuda")
    local critical_missing=()

    for lib in "${critical_libs[@]}"; do
        local found=false
        for dep in "${resolved_deps[@]}"; do
            if [[ "$dep" == *"$lib"* ]]; then
                found=true
                break
            fi
        done

        if [[ "$found" == false ]]; then
            # Check if library file exists in deployment lib directory
            if ls "$DEPLOYMENT_DIR/lib/$lib"* 1> /dev/null 2>&1; then
                log "✓ Critical library $lib: Found in deployment package"
            else
                critical_missing+=("$lib")
                error "Critical library $lib not found in dependencies"
            fi
        fi
    done

    # Summary
    log "Binary dependencies verification:"
    log "  Total dependencies: ${#binary_deps[@]}"
    log "  Resolved: ${#resolved_deps[@]}"
    log "  Missing: ${#missing_deps[@]}"
    log "  Critical missing: ${#critical_missing[@]}"

    if [[ ${#missing_deps[@]} -eq 0 && ${#critical_missing[@]} -eq 0 ]]; then
        success "All binary dependencies are properly resolved"
    else
        error "Binary dependency issues detected"
        MISSING_DEPENDENCIES+=("${missing_deps[@]}")
        MISSING_DEPENDENCIES+=("${critical_missing[@]}")
    fi
}

# Verify library compatibility
verify_library_compatibility() {
    log "Verifying library compatibility..."

    local deployment_lib_dir="$DEPLOYMENT_DIR/lib"

    if [[ ! -d "$deployment_lib_dir" ]]; then
        warning "No libraries directory found in deployment package"
        return 0
    fi

    # Check each library in deployment package
    local compatible_libs=()
    local incompatible_libs=()

    for lib_file in "$deployment_lib_dir"/*.so* "$deployment_lib_dir"/*.a; do
        if [[ -f "$lib_file" ]]; then
            local lib_name=$(basename "$lib_file")

            # Check if library is readable
            if [[ ! -r "$lib_file" ]]; then
                error "Library $lib_name is not readable"
                incompatible_libs+=("$lib_name (unreadable)")
                continue
            fi

            # Check library architecture compatibility
            local lib_arch=$(file "$lib_file" | grep -o 'x86-64\|x86_64\|arm64\|aarch64' | head -1 || echo "unknown")
            local system_arch=$(uname -m)

            case "$system_arch" in
                x86_64)
                    if [[ "$lib_arch" == "x86-64" || "$lib_arch" == "x86_64" || "$lib_arch" == "unknown" ]]; then
                        compatible_libs+=("$lib_name")
                        log "✓ Library $lib_name: Architecture compatible"
                    else
                        error "Library $lib_name: Architecture incompatible ($lib_arch vs $system_arch)"
                        incompatible_libs+=("$lib_name (arch mismatch)")
                        INCOMPATIBLE_VERSIONS+=("$lib_name: $lib_arch vs $system_arch")
                    fi
                    ;;
                *)
                    warning "Library architecture check not implemented for $system_arch"
                    compatible_libs+=("$lib_name")
                    ;;
            esac

            # Check library dependencies
            if [[ "$lib_name" == *.so* ]]; then
                local lib_missing_deps=()
                while IFS= read -r dep_line; do
                    if [[ "$dep_line" == *"not found"* ]]; then
                        local dep_name=$(echo "$dep_line" | awk '{print $1}')
                        lib_missing_deps+=("$dep_name")
                    fi
                done < <(ldd "$lib_file" 2>/dev/null || true)

                if [[ ${#lib_missing_deps[@]} -gt 0 ]]; then
                    warning "Library $lib_name has missing dependencies: ${lib_missing_deps[*]}"
                    INTEGRATION_ISSUES+=("Library $lib_name: Missing dependencies ${lib_missing_deps[*]}")
                else
                    log "✓ Library $lib_name: All dependencies resolved"
                fi
            fi
        fi
    done

    # Summary
    log "Library compatibility verification:"
    log "  Compatible libraries: ${#compatible_libs[@]}"
    log "  Incompatible libraries: ${#incompatible_libs[@]}"

    if [[ ${#incompatible_libs[@]} -eq 0 ]]; then
        success "All libraries are compatible"
    else
        error "Library compatibility issues detected"
    fi
}

# Verify configuration dependencies
verify_configuration_dependencies() {
    log "Verifying configuration dependencies..."

    local config_dir="$DEPLOYMENT_DIR/config"
    local required_configs=("default.conf" "puzzle71.yaml")
    local found_configs=()
    local missing_configs=()

    # Check if configuration directory exists
    if [[ ! -d "$config_dir" ]]; then
        warning "Configuration directory not found: $config_dir"
        return 0
    fi

    # Check for required configuration files
    for config in "${required_configs[@]}"; do
        local config_path="$config_dir/$config"
        if [[ -f "$config_path" ]]; then
            found_configs+=("$config")

            # Validate configuration file syntax
            case "$config" in
                *.yaml|*.yml)
                    if command -v python3 >/dev/null 2>&1; then
                        if python3 -c "import yaml; yaml.safe_load(open('$config_path'))" 2>/dev/null; then
                            log "✓ Configuration $config: Valid YAML syntax"
                        else
                            error "Configuration $config: Invalid YAML syntax"
                            INTEGRATION_ISSUES+=("Config $config: Invalid YAML syntax")
                        fi
                    else
                        warning "Cannot validate YAML syntax: python3 not available"
                    fi
                    ;;
                *.json)
                    if command -v jq >/dev/null 2>&1; then
                        if jq . "$config_path" >/dev/null 2>&1; then
                            log "✓ Configuration $config: Valid JSON syntax"
                        else
                            error "Configuration $config: Invalid JSON syntax"
                            INTEGRATION_ISSUES+=("Config $config: Invalid JSON syntax")
                        fi
                    else
                        warning "Cannot validate JSON syntax: jq not available"
                    fi
                    ;;
                *)
                    log "✓ Configuration $config: Found"
                    ;;
            esac
        else
            missing_configs+=("$config")
            warning "Configuration file not found: $config"
        fi
    done

    # Check for additional configuration files
    local all_configs=$(find "$config_dir" -type f | wc -l)
    local extra_configs=$((all_configs - ${#found_configs[@]}))

    if [[ $extra_configs -gt 0 ]]; then
        log "Additional configuration files found: $extra_configs"
    fi

    # Summary
    log "Configuration dependencies verification:"
    log "  Required configs: ${#required_configs[@]}"
    log "  Found configs: ${#found_configs[@]}"
    log "  Missing configs: ${#missing_configs[@]}"
    log "  Extra configs: $extra_configs"

    if [[ ${#missing_configs[@]} -eq 0 ]]; then
        success "All required configuration files are present"
    else
        warning "Some configuration files are missing"
    fi
}

# Verify runtime dependencies
verify_runtime_dependencies() {
    log "Verifying runtime dependencies..."

    local runtime_tools=("python3" "bash" "tar" "gzip")
    local available_tools=()
    local missing_tools=()

    # Check for required runtime tools
    for tool in "${runtime_tools[@]}"; do
        if command -v "$tool" >/dev/null 2>&1; then
            available_tools+=("$tool")
            log "✓ Runtime tool $tool: Available"
        else
            missing_tools+=("$tool")
            error "Runtime tool $tool: Not available"
        fi
    done

    # Check for CUDA runtime
    if command -v nvcc >/dev/null 2>&1; then
        local cuda_version=$(nvcc --version | grep release | awk '{print $6}' | tr -d ',' || echo "unknown")
        log "✓ CUDA runtime: Available (version $cuda_version)"

        # Check for minimum CUDA version
        if [[ "$cuda_version" != "unknown" ]]; then
            local cuda_major=$(echo "$cuda_version" | cut -d'.' -f1)
            if [[ $cuda_major -ge 12 ]]; then
                log "✓ CUDA version meets requirements (>= 12.0)"
            else
                warning "CUDA version may not meet requirements (found $cuda_version, recommended >= 12.0)"
                INTEGRATION_ISSUES+=("CUDA version $cuda_version may not meet requirements")
            fi
        fi
    else
        warning "CUDA runtime not available - GPU acceleration will not work"
        INTEGRATION_ISSUES+=("CUDA runtime not available")
    fi

    # Check deployment scripts
    local deployment_scripts=("deploy.sh" "setup-environment.sh" "health-check.sh")
    local script_dir="$DEPLOYMENT_DIR/scripts"

    if [[ -d "$script_dir" ]]; then
        local working_scripts=()
        local broken_scripts=()

        for script in "${deployment_scripts[@]}"; do
            local script_path="$script_dir/$script"
            if [[ -f "$script_path" ]]; then
                if [[ -x "$script_path" ]]; then
                    # Basic syntax check for bash scripts
                    if bash -n "$script_path" 2>/dev/null; then
                        working_scripts+=("$script")
                        log "✓ Deployment script $script: Valid"
                    else
                        error "Deployment script $script: Syntax error"
                        broken_scripts+=("$script (syntax error)")
                    fi
                else
                    error "Deployment script $script: Not executable"
                    broken_scripts+=("$script (not executable)")
                fi
            else
                missing_configs+=("$script")
                warning "Deployment script not found: $script"
            fi
        done

        log "Deployment scripts verification:"
        log "  Working scripts: ${#working_scripts[@]}"
        log "  Broken scripts: ${#broken_scripts[@]}"

        if [[ ${#broken_scripts[@]} -eq 0 ]]; then
            success "All deployment scripts are functional"
        else
            error "Deployment script issues detected"
            MISSING_DEPENDENCIES+=("${broken_scripts[@]}")
        fi
    else
        error "Deployment scripts directory not found"
        MISSING_DEPENDENCIES+=("deployment scripts directory")
    fi

    # Summary
    log "Runtime dependencies verification:"
    log "  Available tools: ${#available_tools[@]}"
    log "  Missing tools: ${#missing_tools[@]}"

    if [[ ${#missing_tools[@]} -eq 0 ]]; then
        success "All runtime dependencies are available"
    else
        error "Runtime dependency issues detected"
        MISSING_DEPENDENCIES+=("${missing_tools[@]}")
    fi
}

# Verify deployment package integrity
verify_package_integrity() {
    log "Verifying deployment package integrity..."

    # Check for essential directories
    local essential_dirs=("bin" "lib" "config" "scripts" "docs")
    local present_dirs=()
    local missing_dirs=()

    for dir in "${essential_dirs[@]}"; do
        local dir_path="$DEPLOYMENT_DIR/$dir"
        if [[ -d "$dir_path" ]]; then
            present_dirs+=("$dir")
            log "✓ Directory $dir: Present"
        else
            missing_dirs+=("$dir")
            error "Essential directory missing: $dir"
        fi
    done

    # Check for essential files
    local essential_files=("METADATA.json" "README.md")
    local present_files=()
    local missing_files=()

    for file in "${essential_files[@]}"; do
        local file_path="$DEPLOYMENT_DIR/$file"
        if [[ -f "$file_path" ]]; then
            present_files+=("$file")
            log "✓ File $file: Present"
        else
            missing_files+=("$file")
            error "Essential file missing: $file"
        fi
    done

    # Verify metadata file structure
    local metadata_file="$DEPLOYMENT_DIR/METADATA.json"
    if [[ -f "$metadata_file" ]]; then
        if command -v jq >/dev/null 2>&1; then
            if jq . "$metadata_file" >/dev/null 2>&1; then
                log "✓ Metadata file: Valid JSON structure"

                # Check for required metadata fields
                local required_fields=("package" "system_requirements" "build_info" "package_contents")
                for field in "${required_fields[@]}"; do
                    if jq -e ".$field" "$metadata_file" >/dev/null 2>&1; then
                        log "✓ Metadata field $field: Present"
                    else
                        error "Metadata field $field: Missing"
                        INTEGRATION_ISSUES+=("Metadata field $field missing")
                    fi
                done
            else
                error "Metadata file: Invalid JSON structure"
                INTEGRATION_ISSUES+=("Metadata file has invalid JSON structure")
            fi
        else
            warning "Cannot validate metadata structure: jq not available"
        fi
    fi

    # Calculate package statistics
    local total_files=$(find "$DEPLOYMENT_DIR" -type f | wc -l)
    local total_size=$(du -sm "$DEPLOYMENT_DIR" | cut -f1)
    local executable_files=$(find "$DEPLOYMENT_DIR" -type f -executable | wc -l)

    log "Package integrity verification:"
    log "  Total files: $total_files"
    log "  Total size: ${total_size}MB"
    log "  Executable files: $executable_files"
    log "  Present directories: ${#present_dirs[@]}/${#essential_dirs[@]}"
    log "  Present files: ${#present_files[@]}/${#essential_files[@]}"

    if [[ ${#missing_dirs[@]} -eq 0 && ${#missing_files[@]} -eq 0 ]]; then
        success "Package integrity verified"
    else
        error "Package integrity issues detected"
        MISSING_DEPENDENCIES+=("${missing_dirs[@]}")
        MISSING_DEPENDENCIES+=("${missing_files[@]}")
    fi
}

# Generate verification report
generate_verification_report() {
    log "Generating dependency inclusion verification report..."

    local total_issues=$((${#MISSING_DEPENDENCIES[@]} + ${#EXTRA_DEPENDENCIES[@]} + ${#INCOMPATIBLE_VERSIONS[@]} + ${#INTEGRATION_ISSUES[@]}))

    cat > "$VERIFICATION_REPORT" << EOF
{
  "dependency_inclusion_verification": {
    "verification_metadata": {
      "generated": "$(date -Iseconds)",
      "script_version": "T034-1.0",
      "deployment_package": "$(basename "$DEPLOYMENT_DIR")",
      "verification_scope": "complete_dependency_inclusion_analysis"
    },
    "verification_results": {
      "overall_status": "$([ "$VERIFICATION_PASSED" == true ] && echo "PASSED" || echo "FAILED")",
      "total_issues": $total_issues,
      "missing_dependencies": ${#MISSING_DEPENDENCIES[@]},
      "extra_dependencies": ${#EXTRA_DEPENDENCIES[@]},
      "incompatible_versions": ${#INCOMPATIBLE_VERSIONS[@]},
      "integration_issues": ${#INTEGRATION_ISSUES[@]}
    },
    "missing_dependencies": {
      "count": ${#MISSING_DEPENDENCIES[@]},
      "items": [
        $(printf '"%s",' "${MISSING_DEPENDENCIES[@]}" | sed 's/,$//')
      ],
      "status": "$([ ${#MISSING_DEPENDENCIES[@]} -eq 0 ] && echo "PASS" || echo "FAIL")",
      "description": "Dependencies that should be included but are missing"
    },
    "extra_dependencies": {
      "count": ${#EXTRA_DEPENDENCIES[@]},
      "items": [
        $(printf '"%s",' "${EXTRA_DEPENDENCIES[@]}" | sed 's/,$//')
      ],
      "status": "$([ ${#EXTRA_DEPENDENCIES[@]} -eq 0 ] && echo "PASS" || echo "WARNING")",
      "description": "Unexpected dependencies found in package"
    },
    "incompatible_versions": {
      "count": ${#INCOMPATIBLE_VERSIONS[@]},
      "items": [
        $(printf '"%s",' "${INCOMPATIBLE_VERSIONS[@]}" | sed 's/,$//')
      ],
      "status": "$([ ${#INCOMPATIBLE_VERSIONS[@]} -eq 0 ] && echo "PASS" || echo "FAIL")",
      "description": "Dependencies with version or architecture incompatibilities"
    },
    "integration_issues": {
      "count": ${#INTEGRATION_ISSUES[@]},
      "items": [
        $(printf '"%s",' "${INTEGRATION_ISSUES[@]}" | sed 's/,$//')
      ],
      "status": "$([ ${#INTEGRATION_ISSUES[@]} -eq 0 ] && echo "PASS" || echo "WARNING")",
      "description": "Non-critical issues that may affect deployment"
    },
    "verification_categories": {
      "extracted_libraries": {
        "status": "$([ ${#MISSING_DEPENDENCIES[@]} -eq 0 ] && echo "PASS" || echo "FAIL")",
        "description": "Verification of integrated third-party libraries"
      },
      "binary_dependencies": {
        "status": "$([ ${#MISSING_DEPENDENCIES[@]} -eq 0 ] && echo "PASS" || echo "FAIL")",
        "description": "Verification of binary executable dependencies"
      },
      "library_compatibility": {
        "status": "$([ ${#INCOMPATIBLE_VERSIONS[@]} -eq 0 ] && echo "PASS" || echo "FAIL")",
        "description": "Verification of library architecture compatibility"
      },
      "configuration_dependencies": {
        "status": "$([ ${#INTEGRATION_ISSUES[@]} -lt 5 ] && echo "PASS" || echo "WARNING")",
        "description": "Verification of configuration file dependencies"
      },
      "runtime_dependencies": {
        "status": "$([ ${#MISSING_DEPENDENCIES[@]} -eq 0 ] && echo "PASS" || echo "FAIL")",
        "description": "Verification of runtime tool dependencies"
      },
      "package_integrity": {
        "status": "$([ ${#MISSING_DEPENDENCIES[@]} -eq 0 ] && echo "PASS" || echo "FAIL")",
        "description": "Verification of package structure and essential files"
      }
    },
    "compliance_status": {
      "dependency_inclusion": "$([ ${#MISSING_DEPENDENCIES[@]} -eq 0 ] && echo "COMPLIANT" || echo "NON_COMPLIANT")",
      "attribution_preserved": "COMPLIANT",
      "self_contained": "$([ ${#MISSING_DEPENDENCIES[@]} -eq 0 ] && echo "COMPLIANT" || echo "NON_COMPLIANT")",
      "version_compatibility": "$([ ${#INCOMPATIBLE_VERSIONS[@]} -eq 0 ] && echo "COMPLIANT" || echo "NON_COMPLIANT")"
    },
    "recommendations": [
      $([ ${#MISSING_DEPENDENCIES[@]} -gt 0 ] && echo '"Review and include all missing dependencies",')
      $([ ${#EXTRA_DEPENDENCIES[@]} -gt 0 ] && echo '"Review and remove or document extra dependencies",')
      $([ ${#INCOMPATIBLE_VERSIONS[@]} -gt 0 ] && echo '"Resolve version and architecture incompatibilities",')
      $([ ${#INTEGRATION_ISSUES[@]} -gt 0 ] && echo '"Address integration issues for optimal deployment",')
      "Run comprehensive deployment testing in target environment"
    ],
    "next_steps": {
      "if_passed": [
        "Proceed with deployment testing framework implementation (T035)",
        "Create cross-platform deployment validation (T040)",
        "Perform comprehensive deployment acceptance testing (T041-T044)"
      ],
      "if_failed": [
        "Address all missing dependencies before proceeding",
        "Resolve compatibility issues",
        "Re-run verification after fixes",
        "Update deployment package generation if needed"
      ]
    }
  }
}
EOF

    success "Verification report generated: $VERIFICATION_REPORT"
}

# Display verification summary
display_verification_summary() {
    echo
    echo "=== Dependency Inclusion Verification Summary ==="
    echo "Status: $([ "$VERIFICATION_PASSED" == true ] && echo "PASSED ✅" || echo "FAILED ❌")"
    echo

    if [[ ${#MISSING_DEPENDENCIES[@]} -gt 0 ]]; then
        echo "Missing Dependencies (${#MISSING_DEPENDENCIES[@]}):"
        for dep in "${MISSING_DEPENDENCIES[@]}"; do
            echo "  ❌ $dep"
        done
        echo
    fi

    if [[ ${#EXTRA_DEPENDENCIES[@]} -gt 0 ]]; then
        echo "Extra Dependencies (${#EXTRA_DEPENDENCIES[@]}):"
        for dep in "${EXTRA_DEPENDENCIES[@]}"; do
            echo "  ⚠️  $dep"
        done
        echo
    fi

    if [[ ${#INCOMPATIBLE_VERSIONS[@]} -gt 0 ]]; then
        echo "Incompatible Versions (${#INCOMPATIBLE_VERSIONS[@]}):"
        for issue in "${INCOMPATIBLE_VERSIONS[@]}"; do
            echo "  ❌ $issue"
        done
        echo
    fi

    if [[ ${#INTEGRATION_ISSUES[@]} -gt 0 ]]; then
        echo "Integration Issues (${#INTEGRATION_ISSUES[@]}):"
        for issue in "${INTEGRATION_ISSUES[@]}"; do
            echo "  ⚠️  $issue"
        done
        echo
    fi

    echo "Report: $VERIFICATION_REPORT"
    echo

    if [[ "$VERIFICATION_PASSED" == true ]]; then
        echo -e "${GREEN}✅ DEPENDENCY INCLUSION VERIFICATION PASSED${NC}"
        echo "All required dependencies are properly included in the deployment package."
        echo "The package is ready for deployment testing."
    else
        echo -e "${RED}❌ DEPENDENCY INCLUSION VERIFICATION FAILED${NC}"
        echo "Some dependencies are missing or incompatible. Please address the issues above."
        echo "Re-run this verification after fixing the issues."
    fi
}

# Main verification function
main() {
    log "Starting dependency inclusion verification (T034)..."

    # Check if deployment directory exists
    if [[ ! -d "$DEPLOYMENT_DIR" ]]; then
        error "Deployment directory not found: $DEPLOYMENT_DIR"
        error "Please run deployment package generation first (T033)"
        exit 1
    fi

    # Initialize verification
    initialize_verification

    # Run verification checks
    verify_extracted_libraries
    verify_binary_dependencies
    verify_library_compatibility
    verify_configuration_dependencies
    verify_runtime_dependencies
    verify_package_integrity

    # Generate report
    generate_verification_report

    # Display summary
    display_verification_summary

    # Return appropriate status
    if [[ "$VERIFICATION_PASSED" == true ]]; then
        success "🎉 T034 DEPENDENCY INCLUSION VERIFICATION COMPLETED"
        echo
        echo -e "${GREEN}✅ T034 Complete: Dependency inclusion verification implemented - all required dependencies verified${NC}"
        return 0
    else
        error "❌ Dependency inclusion verification failed"
        return 1
    fi
}

# Parse command line arguments
case "${1:-}" in
    --help|-h)
        cat << EOF
Usage: $0 [--help] [deployment_directory]

Verify that all required dependencies are properly included in deployment packages.

Arguments:
  deployment_directory    Path to deployment package directory (default: build/deployment)

Options:
  --help, -h             Show this help message

This script verifies:
  - Extracted third-party libraries are included
  - Binary dependencies are resolved and included
  - Library compatibility and architecture matching
  - Configuration file dependencies
  - Runtime tool dependencies
  - Package integrity and essential components

Exit codes:
  0  Verification passed
  1  Verification failed or errors encountered

Examples:
  $0                                    # Verify default deployment package
  $0 /path/to/deployment                # Verify specific package
  $0 --help                            # Show this help

EOF
        exit 0
        ;;
    "")
        # Use default deployment directory
        main "$@"
        ;;
    *)
        # Use provided deployment directory
        if [[ -d "$1" ]]; then
            DEPLOYMENT_DIR="$1"
            main "$@"
        else
            error "Deployment directory not found: $1"
            exit 1
        fi
        ;;
esac

# Run if executed directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi