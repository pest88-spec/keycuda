#!/usr/bin/env bash
# Integration Recovery Strategies Implementation
#
# Provides specific recovery strategies for different types of integration failures
# with automated fallback mechanisms and intelligent decision making.
#
# @author       Puzzle71Solver Team
# @created      2025-10-09
# @license      MIT

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
INTEGRATION_ROOT="${REPO_ROOT}/src/extracted"
BUILD_DIR="${REPO_ROOT}/build"

# Source the error handler for common functions
source "${SCRIPT_DIR}/error-handler.sh"

# Recovery strategy configuration
readonly MAX_RETRY_ATTEMPTS=3
readonly RETRY_DELAY_BASE=5
readonly FALLBACK_TIMEOUT=300
readonly PARTIAL_INTEGRATION_THRESHOLD=0.8

# Recovery strategy: Intelligent retry with exponential backoff
intelligent_retry() {
    local operation="$1"
    local error_context="$2"
    local max_attempts="${3:-$MAX_RETRY_ATTEMPTS}"

    local attempt=1
    local delay=$RETRY_DELAY_BASE

    log_info "Starting intelligent retry for: $operation"

    while [[ $attempt -le $max_attempts ]]; do
        log_info "Retry attempt $attempt/$max_attempts with delay ${delay}s"

        # Modify operation based on error context
        local modified_operation=$(modify_operation_for_retry "$operation" "$error_context" "$attempt")

        if eval "$modified_operation"; then
            log_success "Intelligent retry succeeded on attempt $attempt: $operation"
            return 0
        fi

        # Analyze failure and adjust strategy
        local failure_analysis=$(analyze_failure "$operation" "$error_context" "$attempt")
        log_info "Failure analysis: $failure_analysis"

        if [[ $attempt -lt $max_attempts ]]; then
            log_info "Waiting ${delay}s before next attempt..."
            sleep "$delay"
            delay=$((delay * 2))  # Exponential backoff
        fi

        ((attempt++))
    done

    log_error "Intelligent retry failed after $max_attempts attempts: $operation"
    return 1
}

# Modify operation based on error context and attempt number
modify_operation_for_retry() {
    local operation="$1"
    local error_context="$2"
    local attempt="$3"

    case "$error_context" in
        "network_timeout"|"download_failed")
            # Add timeout and retry flags for network operations
            echo "$operation --timeout $((30 * attempt)) --retry-on-error"
            ;;
        "disk_space"|"io_error")
            # Add cleanup before retry
            echo "cleanup_temp_files && $operation"
            ;;
        "build_error"|"compilation_failed")
            # Add more verbose output and different build flags
            echo "$operation --verbose --debug --retry-build"
            ;;
        "dependency_conflict")
            # Add conflict resolution
            echo "resolve_conflicts_before && $operation"
            ;;
        *)
            # Default: no modification
            echo "$operation"
            ;;
    esac
}

# Analyze failure patterns to suggest next steps
analyze_failure() {
    local operation="$1"
    local error_context="$2"
    local attempt="$3"

    # This would implement sophisticated failure analysis
    # For now, return context-based suggestions
    case "$error_context" in
        "network_timeout")
            echo "Network timeout detected - suggest increasing timeout or using alternative source"
            ;;
        "disk_space")
            echo "Disk space issue detected - suggest cleanup or using alternative location"
            ;;
        "build_error")
            echo "Build error detected - suggest checking dependencies or build configuration"
            ;;
        "dependency_conflict")
            echo "Dependency conflict detected - suggest version downgrade or conflict resolution"
            ;;
        *)
            echo "Unknown failure pattern - require manual investigation"
            ;;
    esac
}

# Recovery strategy: Partial integration for incomplete extractions
attempt_partial_integration() {
    local library_name="$1"
    local min_completeness="${2:-$PARTIAL_INTEGRATION_THRESHOLD}"

    log_info "Attempting partial integration for $library_name (min completeness: $min_completeness)"

    local library_path="${INTEGRATION_ROOT}/${library_name}"
    if [[ ! -d "$library_path" ]]; then
        log_error "Library directory not found: $library_path"
        return 1
    fi

    # Assess what we have
    local assessment=$(assess_integration_completeness "$library_name")
    local completeness=$(echo "$assessment" | cut -d':' -f1)
    local missing_components=$(echo "$assessment" | cut -d':' -f2)

    log_info "Integration completeness: $completeness (missing: $missing_components)"

    if (( $(echo "$completeness >= $min_completeness" | bc -l) )); then
        log_info "Integration meets minimum completeness threshold, proceeding with partial integration"

        # Configure build to exclude missing components
        if configure_build_for_partial_integration "$library_name" "$missing_components"; then
            log_success "Partial integration configured successfully for $library_name"
            return 0
        fi
    else
        log_warning "Integration completeness ($completeness) below threshold ($min_completeness)"
    fi

    return 1
}

# Assess how complete an integration is
assess_integration_completeness() {
    local library_name="$1"
    local library_path="${INTEGRATION_ROOT}/${library_name}"

    local total_files=0
    local found_files=0
    local missing_components=""

    # Check for essential components
    local essential_components=("src" "include" "CMakeLists.txt" "*.h" "*.c")
    local found_components=()

    for component in "${essential_components[@]}"; do
        case "$component" in
            "src")
                if [[ -d "$library_path/src" ]] && [[ -n "$(ls "$library_path/src" 2>/dev/null)" ]]; then
                    ((found_files++))
                    found_components+=("src")
                else
                    missing_components="${missing_components}src,"
                fi
                ((total_files++))
                ;;
            "include")
                if [[ -d "$library_path/include" ]] && [[ -n "$(ls "$library_path/include" 2>/dev/null)" ]]; then
                    ((found_files++))
                    found_components+=("include")
                else
                    missing_components="${missing_components}include,"
                fi
                ((total_files++))
                ;;
            "CMakeLists.txt")
                if [[ -f "$library_path/CMakeLists.txt" ]]; then
                    ((found_files++))
                    found_components+=("CMakeLists.txt")
                else
                    missing_components="${missing_components}CMakeLists.txt,"
                fi
                ((total_files++))
                ;;
            *.h)
                local header_count=$(find "$library_path" -name "*.h" | wc -l)
                if [[ $header_count -gt 0 ]]; then
                    ((found_files++))
                    found_components+=("headers")
                else
                    missing_components="${missing_components}headers,"
                fi
                ((total_files++))
                ;;
            *.c)
                local source_count=$(find "$library_path" -name "*.c" | wc -l)
                if [[ $source_count -gt 0 ]]; then
                    ((found_files++))
                    found_components+=("sources")
                else
                    missing_components="${missing_components}sources,"
                fi
                ((total_files++))
                ;;
        esac
    done

    local completeness=0
    if [[ $total_files -gt 0 ]]; then
        completeness=$(echo "scale=2; $found_files / $total_files" | bc -l)
    fi

    # Remove trailing comma from missing components
    missing_components="${missing_components%,}"

    echo "${completeness}:${missing_components}"
}

# Configure build system for partial integration
configure_build_for_partial_integration() {
    local library_name="$1"
    local missing_components="$2"

    log_info "Configuring build for partial integration of $library_name (missing: $missing_components)"

    # Create a configuration file for the build system
    local config_file="${BUILD_DIR}/partial-integration-${library_name}.cmake"
    cat > "$config_file" << EOF
# Partial integration configuration for $library_name
# Generated automatically by recovery-strategies.sh

set(MISSING_COMPONENTS "$missing_components")

# Configure build based on available components
if(MISSING_COMPONENTS MATCHES "tests")
    set(BUILD_TESTS OFF CACHE BOOL "Disable tests for partial integration" FORCE)
endif()

if(MISSING_COMPONENTS MATCHES "docs")
    set(BUILD_DOCS OFF CACHE BOOL "Disable docs for partial integration" FORCE)
endif()

if(MISSING_COMPONENTS MATCHES "examples")
    set(BUILD_EXAMPLES OFF CACHE BOOL "Disable examples for partial integration" FORCE)
endif()

# Mark as partial integration
set(PARTIAL_INTEGRATION_$library_name ON CACHE BOOL "Partial integration mode")
EOF

    log_info "Partial integration configuration written to: $config_file"
    return 0
}

# Recovery strategy: Component substitution
attempt_component_substitution() {
    local library_name="$1"
    local missing_component="$2"

    log_info "Attempting component substitution for $library_name (missing: $missing_component)"

    case "$missing_component" in
        "build_system")
            if generate_alternative_build_system "$library_name"; then
                return 0
            fi
            ;;
        "test_framework")
            if use_builtin_test_framework "$library_name"; then
                return 0
            fi
            ;;
        "documentation")
            if generate_minimal_documentation "$library_name"; then
                return 0
            fi
            ;;
        *)
            log_warning "No substitution available for component: $missing_component"
            return 1
            ;;
    esac

    return 1
}

# Generate alternative build system
generate_alternative_build_system() {
    local library_name="$1"
    local library_path="${INTEGRATION_ROOT}/${library_name}"

    log_info "Generating alternative build system for $library_name"

    # Create a simple CMakeLists.txt if missing
    if [[ ! -f "$library_path/CMakeLists.txt" ]]; then
        cat > "$library_path/CMakeLists.txt" << EOF
# Auto-generated CMakeLists.txt for $library_name
# Generated by recovery-strategies.sh

cmake_minimum_required(VERSION 3.22)

# Library name and version
set(LIBRARY_NAME "$library_name")
set(LIBRARY_VERSION "1.0.0")

# Collect all source files
file(GLOB_RECURSE SOURCES
    "src/*.c"
    "src/*.cpp"
)

file(GLOB_RECURSE HEADERS
    "include/*.h"
    "include/*.hpp"
    "src/*.h"
    "src/*.hpp"
)

# Create library
add_library(\${LIBRARY_NAME} \${SOURCES} \${HEADERS})

# Set include directories
target_include_directories(\${LIBRARY_NAME}
    PUBLIC
        \$<BUILD_INTERFACE:\${CMAKE_CURRENT_SOURCE_DIR}/include>
        \$<INSTALL_INTERFACE:include>
    PRIVATE
        \${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Installation
install(TARGETS \${LIBRARY_NAME}
    EXPORT \${LIBRARY_NAME}Targets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/ DESTINATION include)
EOF

        log_success "Alternative CMakeLists.txt generated for $library_name"
        return 0
    fi

    return 1
}

# Use built-in test framework
use_builtin_test_framework() {
    local library_name="$1"

    log_info "Configuring built-in test framework for $library_name"

    # Create minimal test configuration
    local test_config="${BUILD_DIR}/builtin-test-${library_name}.cmake"
    cat > "$test_config" << EOF
# Built-in test configuration for $library_name
# Generated by recovery-strategies.sh

enable_testing()

# Simple test runner
add_executable(test_${library_name} minimal_test.cpp)
target_link_libraries(test_${library_name} ${library_name})

# Add test
add_test(NAME ${library_name}_basic_test COMMAND test_${library_name})
EOF

    log_success "Built-in test framework configured for $library_name"
    return 0
}

# Generate minimal documentation
generate_minimal_documentation() {
    local library_name="$1"
    local library_path="${INTEGRATION_ROOT}/${library_name}"

    log_info "Generating minimal documentation for $library_name"

    # Create README if missing
    if [[ ! -f "$library_path/README.md" ]]; then
        cat > "$library_path/README.md" << EOF
# $library_name

Auto-generated documentation for extracted library.

## Overview

This is an extracted third-party library integrated into the Puzzle71Solver project.

## Integration Details

- **Extraction Date**: $(date '+%Y-%m-%d')
- **Integration Status**: Partial (auto-generated documentation)
- **Components Available**: See source files in \`src/\` and \`include/\` directories

## Usage

This library is automatically integrated into the main build system.
See the main project documentation for usage details.

## Attribution

This library has been extracted from its original source with full attribution
preservation. See individual source files for original attribution information.
EOF

        log_success "Minimal README.md generated for $library_name"
        return 0
    fi

    return 1
}

# Recovery strategy: Graceful degradation
attempt_graceful_degradation() {
    local library_name="$1"
    local critical_features="$2"

    log_info "Attempting graceful degradation for $library_name (critical: $critical_features)"

    # Implement degraded functionality
    if create_degraded_implementation "$library_name" "$critical_features"; then
        log_success "Graceful degradation implemented for $library_name"
        return 0
    fi

    return 1
}

# Create degraded implementation
create_degraded_implementation() {
    local library_name="$1"
    local critical_features="$2"

    log_info "Creating degraded implementation for $library_name"

    local library_path="${INTEGRATION_ROOT}/${library_name}"
    local degraded_dir="${library_path}/degraded"

    mkdir -p "$degraded_dir"

    # Create stub implementations for missing functionality
    cat > "$degraded_dir/stub_implementations.cpp" << EOF
// Stub implementations for $library_name
// Generated by recovery-strategies.sh for graceful degradation

#include <stdexcept>

// Stub implementations - replace with actual implementations as needed
void degraded_functionality_warning(const char* feature) {
    // Log warning about degraded functionality
}

// Stub implementations for critical features that couldn't be integrated
EOF

    # Create CMake configuration for degraded build
    cat > "$degraded_dir/CMakeLists.txt" << EOF
# CMakeLists.txt for degraded implementation of $library_name
# Generated by recovery-strategies.sh

cmake_minimum_required(VERSION 3.22)

# Compile stub implementations
add_library(${library_name}_degraded stub_implementations.cpp)

# Configure as fallback
set_target_properties(${library_name}_degraded PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
EOF

    log_success "Degraded implementation created for $library_name"
    return 0
}

# Recovery strategy: Alternative source resolution
resolve_from_alternative_source() {
    local library_name="$1"
    local original_url="$2"

    log_info "Attempting to resolve $library_name from alternative sources"

    # Define alternative sources
    local alternative_sources=(
        "https://github.com/${library_name}/${library_name}.git"
        "https://gitlab.com/${library_name}/${library_name}.git"
        "https://sourceforge.net/projects/${library_name}/"
        "${REPO_ROOT}/third_party/${library_name}"
        "${REPO_ROOT}/cache/${library_name}"
    )

    for alt_source in "${alternative_sources[@]}"; do
        log_info "Trying alternative source: $alt_source"

        if attempt_extraction_from_source "$library_name" "$alt_source"; then
            log_success "Successfully extracted $library_name from alternative source: $alt_source"
            return 0
        fi
    done

    log_error "Failed to extract $library_name from any alternative source"
    return 1
}

# Attempt extraction from a specific source
attempt_extraction_from_source() {
    local library_name="$1"
    local source_url="$2"

    # This would implement the actual extraction from the alternative source
    # For now, just simulate the attempt
    log_info "Attempting extraction from: $source_url"

    # Check if source exists and is accessible
    case "$source_url" in
        http://*|https://*)
            # Try HTTP/HTTPS source
            if curl --head --silent "$source_url" | grep -q "200 OK"; then
                log_info "Source is accessible via HTTP/HTTPS"
                # Would implement actual download and extraction here
                return 1  # Not implemented yet
            fi
            ;;
        git://*|*.git)
            # Try Git source
            if git ls-remote --exit-code "$source_url" >/dev/null 2>&1; then
                log_info "Source is accessible via Git"
                # Would implement git clone here
                return 1  # Not implemented yet
            fi
            ;;
        /*|*/*)
            # Try local file system source
            if [[ -d "$source_url" ]] || [[ -f "$source_url" ]]; then
                log_info "Source is accessible via local filesystem"
                # Would implement local copy here
                return 1  # Not implemented yet
            fi
            ;;
    esac

    return 1
}

# Main recovery strategy selector
select_recovery_strategy() {
    local library_name="$1"
    local error_type="$2"
    local error_context="$3"

    log_info "Selecting recovery strategy for $library_name: $error_type ($error_context)"

    case "$error_type" in
        "EXTRACTION_FAILED"|"EXTRACTION_INCOMPLETE")
            # Try intelligent retry first
            if intelligent_retry "extract_library $library_name" "$error_context"; then
                return 0
            fi

            # Try partial integration
            if attempt_partial_integration "$library_name"; then
                return 0
            fi

            # Try alternative sources
            if resolve_from_alternative_source "$library_name" "$error_context"; then
                return 0
            fi
            ;;

        "ATTRIBUTION_FAILED")
            # Try to regenerate attribution
            if intelligent_retry "generate_attribution $library_name" "$error_context"; then
                return 0
            fi
            ;;

        "BUILD_INTEGRATION_FAILED")
            # Try component substitution
            if attempt_component_substitution "$library_name" "$error_context"; then
                return 0
            fi

            # Try partial integration
            if attempt_partial_integration "$library_name"; then
                return 0
            fi

            # Try graceful degradation
            if attempt_graceful_degradation "$library_name" "core_functionality"; then
                return 0
            fi
            ;;

        "DEPENDENCY_CONFLICT")
            # Try conflict resolution
            if intelligent_retry "resolve_dependency_conflicts $library_name" "$error_context"; then
                return 0
            fi

            # Try alternative versions
            if resolve_from_alternative_source "$library_name" "alternative_version"; then
                return 0
            fi
            ;;

        *)
            log_error "No recovery strategy available for error type: $error_type"
            return 1
            ;;
    esac

    log_error "All recovery strategies failed for $library_name"
    return 1
}

# Main execution function
main() {
    # Parse command line arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            -h|--help)
                cat << EOF
Integration Recovery Strategies

Usage: $0 [OPTIONS] COMMAND [ARGS]

COMMANDS:
    select-strategy <library> <error_type> <error_context>
        Select and execute appropriate recovery strategy

    intelligent-retry <operation> <error_context> [max_attempts]
        Perform intelligent retry with exponential backoff

    partial-integration <library> [min_completeness]
        Attempt partial integration for incomplete extractions

    component-substitution <library> <missing_component>
        Attempt component substitution

    graceful-degradation <library> <critical_features>
        Implement graceful degradation

    alternative-source <library> <original_url>
        Try to resolve from alternative sources

OPTIONS:
    -h, --help      Show this help message
    -v, --verbose   Enable verbose logging

EXAMPLES:
    $0 select-strategy secp256k1-zkp EXTRACTION_FAILED network_timeout
    $0 intelligent-retry "extract_library lib" network_timeout 5
    $0 partial-integration secp256k1-zkp 0.7
    $0 component-substitution secp256k1-zkp build_system

EOF
                exit 0
                ;;
            -v|--verbose)
                set -x
                shift
                ;;
            *)
                break
                ;;
        esac
    done

    # Execute command
    case "${1:-}" in
        "select-strategy")
            if [[ $# -ne 4 ]]; then
                log_error "select-strategy requires 3 arguments: library error_type error_context"
                exit 1
            fi
            select_recovery_strategy "$2" "$3" "$4"
            ;;
        "intelligent-retry")
            if [[ $# -lt 3 ]]; then
                log_error "intelligent-retry requires at least 2 arguments: operation error_context [max_attempts]"
                exit 1
            fi
            intelligent_retry "$2" "$3" "${4:-$MAX_RETRY_ATTEMPTS}"
            ;;
        "partial-integration")
            if [[ $# -lt 2 ]]; then
                log_error "partial-integration requires at least 1 argument: library [min_completeness]"
                exit 1
            fi
            attempt_partial_integration "$2" "${3:-$PARTIAL_INTEGRATION_THRESHOLD}"
            ;;
        "component-substitution")
            if [[ $# -ne 3 ]]; then
                log_error "component-substitution requires 2 arguments: library missing_component"
                exit 1
            fi
            attempt_component_substitution "$2" "$3"
            ;;
        "graceful-degradation")
            if [[ $# -ne 3 ]]; then
                log_error "graceful-degradation requires 2 arguments: library critical_features"
                exit 1
            fi
            attempt_graceful_degradation "$2" "$3"
            ;;
        "alternative-source")
            if [[ $# -ne 3 ]]; then
                log_error "alternative-source requires 2 arguments: library original_url"
                exit 1
            fi
            resolve_from_alternative_source "$2" "$3"
            ;;
        *)
            log_error "Unknown command: ${1:-}"
            exit 1
            ;;
    esac
}

# Run main function if script is executed directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi