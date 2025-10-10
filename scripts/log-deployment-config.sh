#!/bin/bash

# Puzzle71Solver - Deployment Configuration Logging Script
#
# Provides visibility into deployment configuration decisions by logging
# all deployment-related choices, build options, and system configurations.
#
# Author: Puzzle71Solver Team
# Created: 2025-10-10
# License: MIT

set -euo pipefail

# Script constants
readonly SCRIPT_NAME="$(basename "$0")"
readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Default configuration
DEFAULT_CONFIG_LOG_FILE="${PROJECT_ROOT}/build/deployment-config.log"
DEFAULT_OUTPUT_FORMAT="json"
DEFAULT_LOG_LEVEL="INFO"

# Exit codes
readonly EXIT_SUCCESS=0
readonly EXIT_INVALID_ARGS=1
readonly EXIT_BUILD_FAILED=2
readonly EXIT_FILE_ERROR=3

# Color output
readonly RED='\033[0;31m'
readonly GREEN='\033[0;32m'
readonly YELLOW='\033[1;33m'
readonly BLUE='\033[0;34m'
readonly NC='\033[0m'

# Logging functions
log_info() {
    echo -e "${GREEN}[INFO]${NC} $*" >&2
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $*" >&2
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $*" >&2
}

log_debug() {
    if [[ "${DEBUG:-false}" == "true" ]]; then
        echo -e "${BLUE}[DEBUG]${NC} $*" >&2
    fi
}

# Show usage information
show_usage() {
    cat << EOF
Usage: $SCRIPT_NAME [OPTIONS] [ACTION]

Log deployment configuration decisions and generate visibility reports.

ACTIONS:
    log-cmake               Log current CMake configuration options
    log-features            Log feature flag configurations
    log-dependencies        Log dependency resolution information
    log-environment         Log environment detection information
    log-build               Log build configuration
    log-deployment          Log deployment target configurations
    log-package             Log package configuration
    generate-report         Generate comprehensive configuration report
    clear-history           Clear configuration history
    show-stats              Show configuration statistics

OPTIONS:
    --config-log FILE       Configuration log file path (default: $DEFAULT_CONFIG_LOG_FILE)
    --output-format FORMAT  Output format: json, markdown (default: $DEFAULT_OUTPUT_FORMAT)
    --log-level LEVEL       Log level: DEBUG, INFO, WARN, ERROR (default: $DEFAULT_LOG_LEVEL)
    --build-dir DIR         Build directory path (default: auto-detect)
    --verbose               Enable verbose logging
    --debug                 Enable debug output
    --help                  Show this help message

EXAMPLES:
    $SCRIPT_NAME log-cmake --verbose
    $SCRIPT_NAME log-features --log-level DEBUG
    $SCRIPT_NAME generate-report --output-format markdown
    $SCRIPT_NAME log-environment --build-dir ./build

EOF
}

# Parse command line arguments
parse_args() {
    ACTION=""
    CONFIG_LOG_FILE="$DEFAULT_CONFIG_LOG_FILE"
    OUTPUT_FORMAT="$DEFAULT_OUTPUT_FORMAT"
    LOG_LEVEL="$DEFAULT_LOG_LEVEL"
    BUILD_DIR=""
    VERBOSE="false"
    DEBUG="false"

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --config-log)
                CONFIG_LOG_FILE="$2"
                shift 2
                ;;
            --output-format)
                OUTPUT_FORMAT="$2"
                shift 2
                ;;
            --log-level)
                LOG_LEVEL="$2"
                shift 2
                ;;
            --build-dir)
                BUILD_DIR="$2"
                shift 2
                ;;
            --verbose)
                VERBOSE="true"
                shift
                ;;
            --debug)
                DEBUG="true"
                shift
                ;;
            --help)
                show_usage
                exit $EXIT_SUCCESS
                ;;
            log-cmake|log-features|log-dependencies|log-environment|log-build|log-deployment|log-package|generate-report|clear-history|show-stats)
                ACTION="$1"
                shift
                ;;
            *)
                log_error "Unknown action or option: $1"
                show_usage
                exit $EXIT_INVALID_ARGS
                ;;
        esac
    done

    # Validate arguments
    if [[ -z "$ACTION" ]]; then
        log_error "No action specified"
        show_usage
        exit $EXIT_INVALID_ARGS
    fi

    if [[ ! "$OUTPUT_FORMAT" =~ ^(json|markdown)$ ]]; then
        log_error "Invalid output format: $OUTPUT_FORMAT. Supported formats: json, markdown"
        exit $EXIT_INVALID_ARGS
    fi

    if [[ ! "$LOG_LEVEL" =~ ^(DEBUG|INFO|WARN|ERROR)$ ]]; then
        log_error "Invalid log level: $LOG_LEVEL. Supported levels: DEBUG, INFO, WARN, ERROR"
        exit $EXIT_INVALID_ARGS
    fi

    # Auto-detect build directory if not specified
    if [[ -z "$BUILD_DIR" ]]; then
        for dir in "build" "build-debug" "build-release"; do
            if [[ -d "$PROJECT_ROOT/$dir" ]]; then
                BUILD_DIR="$PROJECT_ROOT/$dir"
                break
            fi
        done
        if [[ -z "$BUILD_DIR" ]]; then
            log_error "Could not auto-detect build directory. Please specify with --build-dir"
            exit $EXIT_INVALID_ARGS
        fi
    fi

    # Convert to absolute paths
    BUILD_DIR="$(realpath "$BUILD_DIR")"
    CONFIG_LOG_FILE="$(realpath "$CONFIG_LOG_FILE" 2>/dev/null || echo "$CONFIG_LOG_FILE")"

    log_debug "Action: $ACTION"
    log_debug "Config log file: $CONFIG_LOG_FILE"
    log_debug "Output format: $OUTPUT_FORMAT"
    log_debug "Log level: $LOG_LEVEL"
    log_debug "Build directory: $BUILD_DIR"
}

# Check prerequisites
check_prerequisites() {
    log_debug "Checking prerequisites..."

    local missing_tools=()

    # Check required tools
    for tool in find date sed awk jq; do
        if ! command -v "$tool" >/dev/null 2>&1; then
            missing_tools+=("$tool")
        fi
    done

    if [[ ${#missing_tools[@]} -gt 0 ]]; then
        log_error "Missing required tools: ${missing_tools[*]}"
        exit $EXIT_MISSING_TOOLS
    fi

    # Check build directory
    if [[ ! -d "$BUILD_DIR" ]]; then
        log_error "Build directory not found: $BUILD_DIR"
        exit $EXIT_BUILD_FAILED
    fi

    # Create log directory if it doesn't exist
    mkdir -p "$(dirname "$CONFIG_LOG_FILE")"

    log_debug "Prerequisites check passed"
}

# Initialize configuration logging
initialize_logging() {
    log_info "Initializing deployment configuration logging..."

    # Create configuration log file with header
    cat > "$CONFIG_LOG_FILE" << EOF
{
  "session_start": "$(date -u +"%Y-%m-%d %H:%M:%S UTC")",
  "build_directory": "$BUILD_DIR",
  "project_root": "$PROJECT_ROOT",
  "log_level": "$LOG_LEVEL",
  "configurations": []
}
EOF

    # Log script initialization
    log_configuration_decision "INIT" "Deployment configuration logging initialized" \
        "script" "$SCRIPT_NAME" \
        "log_file" "$CONFIG_LOG_FILE" \
        "build_dir" "$BUILD_DIR"

    log_debug "Configuration logging initialized"
}

# Log configuration decision
log_configuration_decision() {
    local decision_type="$1"
    local description="$2"
    shift 2

    local timestamp=$(date -u +"%Y-%m-%d %H:%M:%S UTC")
    local timestamp_ms=$(date +%s%3N)

    # Create decision JSON
    local decision_json="{"
    decision_json+="\"type\":\"$decision_type\","
    decision_json+="\"description\":\"$description\","
    decision_json+="\"timestamp\":\"$timestamp\","
    decision_json+="\"timestamp_ms\":$timestamp_ms,"

    # Add context
    decision_json+="\"context\":{"
    local first=true
    while [[ $# -gt 0 ]]; do
        local key="$1"
        local value="$2"
        shift 2
        if [[ "$first" != "true" ]]; then
            decision_json+=","
        fi
        decision_json+="\"$key\":\"$value\""
        first=false
    done
    decision_json+="}"

    decision_json+="}"

    # Append to configuration log file
    if [[ -f "$CONFIG_LOG_FILE" ]] && command -v jq >/dev/null 2>&1; then
        jq --arg decision "$decision_json" '.configurations += [$decision | fromjson]' \
           "$CONFIG_LOG_FILE" > "${CONFIG_LOG_FILE}.tmp" && \
           mv "${CONFIG_LOG_FILE}.tmp" "$CONFIG_LOG_FILE"
    else
        # Fallback: simple append
        echo "$decision_json" >> "$CONFIG_LOG_FILE"
    fi

    if [[ "$VERBOSE" == "true" ]]; then
        echo "[$timestamp] $decision_type: $description" >&2
    fi
}

# Log CMake configuration options
log_cmake_configuration() {
    log_info "Logging CMake configuration options..."

    local cmake_cache="$BUILD_DIR/CMakeCache.txt"
    if [[ ! -f "$cmake_cache" ]]; then
        log_warn "CMakeCache.txt not found in build directory"
        return
    fi

    # Extract CMake options
    while IFS= read -r line; do
        if [[ "$line" =~ ^([^:]+):([^=]+)=(.*)$ ]]; then
            local name="${BASH_REMATCH[1]}"
            local type="${BASH_REMATCH[2]}"
            local value="${BASH_REMATCH[3]}"

            if [[ "$name" =~ ^[A-Z_]+$ ]] && [[ "$type" == "BOOL" ]]; then
                log_configuration_decision "CMAKE_OPTION" "CMake boolean option configuration" \
                    "option_name" "$name" \
                    "option_value" "$value" \
                    "option_type" "$type" \
                    "source" "cache"
            fi
        fi
    done < "$cmake_cache"

    log_configuration_decision "CMAKE_CONFIG_SUMMARY" "CMake configuration logging completed" \
        "total_options" "$(grep -c "^ENABLE_\|^DEPLOYMENT_\|^OFFLINE_\|^STRICT_" "$cmake_cache" 2>/dev/null || echo "0")"

    log_debug "CMake configuration logged"
}

# Log feature flags
log_feature_flags() {
    log_info "Logging feature flag configurations..."

    local features=(
        "ENABLE_DEPLOYMENT_SYSTEM"
        "DEPLOYMENT_SELF_CONTAINED"
        "DEPLOYMENT_INCLUDE_DOCS"
        "DEPLOYMENT_COMPRESS_PACKAGE"
        "ENABLE_INTEGRATION_SYSTEM"
        "OFFLINE_BUILD"
        "STRICT_ATTRIBUTION"
        "SECP256K1_AVAILABLE"
    )

    for feature in "${features[@]}"; do
        local cmake_cache="$BUILD_DIR/CMakeCache.txt"
        if [[ -f "$cmake_cache" ]]; then
            local value=$(grep "^$feature:" "$cmake_cache" | cut -d= -f2 | head -n1)
            if [[ -n "$value" ]]; then
                local enabled="false"
                if [[ "$value" =~ ^(ON|TRUE|1)$ ]]; then
                    enabled="true"
                fi

                log_configuration_decision "FEATURE_FLAG" "Feature flag configuration" \
                    "feature_name" "$feature" \
                    "enabled" "$enabled" \
                    "raw_value" "$value" \
                    "source" "cmake_cache"
            fi
        fi
    done

    log_configuration_decision "FEATURE_FLAGS_SUMMARY" "Feature flag logging completed" \
        "total_features" "${#features[@]}"

    log_debug "Feature flags logged"
}

# Log dependencies
log_dependencies() {
    log_info "Logging dependency resolution information..."

    # Log extracted libraries
    if [[ -d "$PROJECT_ROOT/src/extracted" ]]; then
        local extracted_libs=()
        for lib_dir in "$PROJECT_ROOT/src/extracted"/*; do
            if [[ -d "$lib_dir" ]] && [[ "$(basename "$lib_dir")" != "secp256k1-zkp" ]]; then
                local lib_name=$(basename "$lib_dir")
                local file_count=$(find "$lib_dir" -name "*.cpp" -o -name "*.cu" -o -name "*.h" | wc -l)

                log_configuration_decision "DEPENDENCY" "Extracted library dependency" \
                    "dependency_name" "$lib_name" \
                    "resolution_type" "extracted" \
                    "source_path" "$lib_dir" \
                    "file_count" "$file_count"

                extracted_libs+=("$lib_name")
            fi
        done

        # Special handling for secp256k1-zkp
        if [[ -d "$PROJECT_ROOT/src/extracted/secp256k1-zkp" ]]; then
            local zkp_files=$(find "$PROJECT_ROOT/src/extracted/secp256k1-zkp" -name "*.c" -o -name "*.h" | wc -l)
            log_configuration_decision "DEPENDENCY" "secp256k1-zkp library dependency" \
                "dependency_name" "secp256k1-zkp" \
                "resolution_type" "extracted" \
                "source_path" "$PROJECT_ROOT/src/extracted/secp256k1-zkp" \
                "file_count" "$zkp_files"
        fi
    fi

    # Log system dependencies
    local system_deps=()
    if command -v nvcc >/dev/null 2>&1; then
        local cuda_version=$(nvcc --version | grep release | awk '{print $6}' | sed 's/,//')
        log_configuration_decision "DEPENDENCY" "CUDA Toolkit system dependency" \
            "dependency_name" "cuda" \
            "resolution_type" "system" \
            "version" "$cuda_version" \
            "source_path" "$(which nvcc)"
        system_deps+=("cuda")
    fi

    if pkg-config --exists openssl 2>/dev/null; then
        local openssl_version=$(pkg-config --modversion openssl)
        log_configuration_decision "DEPENDENCY" "OpenSSL system dependency" \
            "dependency_name" "openssl" \
            "resolution_type" "system" \
            "version" "$openssl_version" \
            "source_path" "$(pkg-config --variable=libdir openssl)"
        system_deps+=("openssl")
    fi

    log_configuration_decision "DEPENDENCIES_SUMMARY" "Dependency resolution logging completed" \
        "extracted_libraries" "${#extracted_libs[@]}" \
        "system_dependencies" "${#system_deps[@]}"

    log_debug "Dependencies logged"
}

# Log environment information
log_environment() {
    log_info "Logging environment detection information..."

    # System information
    log_configuration_decision "ENVIRONMENT" "System environment detection" \
        "system_name" "$(uname -s)" \
        "system_version" "$(uname -r)" \
        "system_processor" "$(uname -m)" \
        "hostname" "$(hostname)"

    # Build environment
    if command -v g++ >/dev/null 2>&1; then
        log_configuration_decision "ENVIRONMENT" "C++ compiler environment" \
            "cxx_compiler" "$(g++ --version | head -n1)" \
            "cxx_path" "$(which g++)"
    fi

    if command -v nvcc >/dev/null 2>&1; then
        log_configuration_decision "ENVIRONMENT" "CUDA compiler environment" \
            "cuda_compiler" "$(nvcc --version | grep release | head -n1)" \
            "cuda_path" "$(which nvcc)"
    fi

    # Build directory information
    if [[ -d "$BUILD_DIR" ]]; then
        local build_size=$(du -sh "$BUILD_DIR" | cut -f1)
        local build_files=$(find "$BUILD_DIR" -type f | wc -l)
        log_configuration_decision "ENVIRONMENT" "Build directory information" \
            "build_directory" "$BUILD_DIR" \
            "build_size" "$build_size" \
            "build_files" "$build_files"
    fi

    # Memory information
    if command -v free >/dev/null 2>&1; then
        local total_memory=$(free -h | grep Mem | awk '{print $2}')
        log_configuration_decision "ENVIRONMENT" "Memory environment" \
            "total_memory" "$total_memory" \
            "source" "system"
    fi

    log_configuration_decision "ENVIRONMENT_SUMMARY" "Environment detection logging completed" \
        "detection_timestamp" "$(date -u +"%Y-%m-%d %H:%M:%S UTC")"

    log_debug "Environment information logged"
}

# Log build configuration
log_build_configuration() {
    log_info "Logging build configuration..."

    local build_config_json="$BUILD_DIR/deployment/deployment-config.cmake"
    if [[ -f "$build_config_json" ]]; then
        log_configuration_decision "BUILD_CONFIG" "Deployment configuration file found" \
            "config_file" "$build_config_json" \
            "config_exists" "true"
    else
        log_configuration_decision "BUILD_CONFIG" "Deployment configuration file not found" \
            "config_file" "$build_config_json" \
            "config_exists" "false"
    fi

    # Check for build artifacts
    local main_binary="$BUILD_DIR/Puzzle71Solver"
    if [[ -f "$main_binary" ]]; then
        local binary_size=$(stat -c%s "$main_binary" 2>/dev/null || stat -f%z "$main_binary")
        local binary_mtime=$(date -r "$main_binary" -u +"%Y-%m-%d %H:%M:%S UTC" 2>/dev/null || echo "unknown")
        log_configuration_decision "BUILD_ARTIFACT" "Main build binary found" \
            "binary_path" "$main_binary" \
            "binary_size" "$binary_size" \
            "binary_mtime" "$binary_mtime"
    else
        log_configuration_decision "BUILD_ARTIFACT" "Main build binary not found" \
            "binary_path" "$main_binary" \
            "binary_exists" "false"
    fi

    log_configuration_decision "BUILD_CONFIG_SUMMARY" "Build configuration logging completed" \
        "build_directory" "$BUILD_DIR"

    log_debug "Build configuration logged"
}

# Log deployment targets
log_deployment_targets() {
    log_info "Logging deployment target configurations..."

    # Check for deployment scripts
    local deployment_scripts=(
        "package-deployment.sh"
        "verify-deployment.sh"
        "test-deployment.sh"
    )

    local found_scripts=()
    for script in "${deployment_scripts[@]}"; do
        local script_path="$PROJECT_ROOT/scripts/$script"
        if [[ -f "$script_path" ]]; then
            local script_size=$(stat -c%s "$script_path" 2>/dev/null || stat -f%z "$script_path")
            log_configuration_decision "DEPLOYMENT_TARGET" "Deployment script found" \
                "target_name" "${script%.sh}" \
                "script_path" "$script_path" \
                "script_size" "$script_size" \
                "target_type" "script"
            found_scripts+=("$script")
        fi
    done

    # Check for deployment directory
    local deployment_dir="$BUILD_DIR/deployment"
    if [[ -d "$deployment_dir" ]]; then
        local deployment_size=$(du -sh "$deployment_dir" | cut -f1)
        local deployment_files=$(find "$deployment_dir" -type f | wc -l)
        log_configuration_decision "DEPLOYMENT_TARGET" "Deployment directory found" \
            "target_name" "deployment_directory" \
            "directory_path" "$deployment_dir" \
            "directory_size" "$deployment_size" \
            "directory_files" "$deployment_files" \
            "target_type" "directory"
    fi

    log_configuration_decision "DEPLOYMENT_TARGETS_SUMMARY" "Deployment target logging completed" \
        "found_scripts" "${#found_scripts[@]}" \
        "deployment_directory_exists" "$([[ -d "$deployment_dir" ]] && echo "true" || echo "false")"

    log_debug "Deployment targets logged"
}

# Log package configuration
log_package_configuration() {
    log_info "Logging package configuration..."

    # Check for existing packages
    local packages_dir="$BUILD_DIR/deployment/packages"
    if [[ -d "$packages_dir" ]]; then
        local package_formats=()
        for package_file in "$packages_dir"/*; do
            if [[ -f "$package_file" ]]; then
                local package_name=$(basename "$package_file")
                local package_size=$(stat -c%s "$package_file" 2>/dev/null || stat -f%z "$package_file")
                local package_format="${package_name##*.}"

                log_configuration_decision "PACKAGE_CONFIG" "Deployment package found" \
                    "package_name" "$package_name" \
                    "package_path" "$package_file" \
                    "package_size" "$package_size" \
                    "package_format" "$package_format" \
                    "package_exists" "true"

                package_formats+=("$package_format")
            fi
        done

        log_configuration_decision "PACKAGE_CONFIG_SUMMARY" "Package configuration logging completed" \
            "packages_directory" "$packages_dir" \
            "found_packages" "${#package_formats[@]}" \
            "package_formats" "$(IFS=,; echo "${package_formats[*]}")"
    else
        log_configuration_decision "PACKAGE_CONFIG_SUMMARY" "No packages directory found" \
            "packages_directory" "$packages_dir" \
            "packages_directory_exists" "false"
    fi

    log_debug "Package configuration logged"
}

# Generate configuration report
generate_report() {
    log_info "Generating deployment configuration report..."

    local output_file="$BUILD_DIR/deployment/configuration-report.$OUTPUT_FORMAT"

    mkdir -p "$(dirname "$output_file")"

    if [[ "$OUTPUT_FORMAT" == "json" ]]; then
        # Copy and format the configuration log
        if [[ -f "$CONFIG_LOG_FILE" ]]; then
            cp "$CONFIG_LOG_FILE" "$output_file"
            log_info "JSON configuration report generated: $output_file"
        else
            log_error "Configuration log file not found: $CONFIG_LOG_FILE"
            return $EXIT_FILE_ERROR
        fi
    elif [[ "$OUTPUT_FORMAT" == "markdown" ]]; then
        generate_markdown_report "$output_file"
        log_info "Markdown configuration report generated: $output_file"
    fi
}

# Generate markdown report
generate_markdown_report() {
    local output_file="$1"

    cat > "$output_file" << EOF
# Deployment Configuration Report

**Generated**: $(date -u +"%Y-%m-%d %H:%M:%S UTC")
**Build Directory**: $BUILD_DIR
**Configuration Log**: $CONFIG_LOG_FILE

## Configuration Summary

EOF

    # Add summary from configuration log if it exists and jq is available
    if [[ -f "$CONFIG_LOG_FILE" ]] && command -v jq >/dev/null 2>&1; then
        local total_configs=$(jq '.configurations | length' "$CONFIG_LOG_FILE" 2>/dev/null || echo "0")
        echo "**Total Configurations**: $total_configs" >> "$output_file"
    fi

    echo "" >> "$output_file"

    # Add configuration details
    echo "## Configuration Details" >> "$output_file"
    echo "" >> "$output_file"

    if [[ -f "$CONFIG_LOG_FILE" ]] && command -v jq >/dev/null 2>&1; then
        jq -r '.configurations[] | "### \(.type)\n\n**Description**: \(.description)\n\n**Timestamp**: \(.timestamp)\n\n**Context**:\n\(.context | to_entries | map("- \(.key): \(.value)") | join("\n"))\n"' \
           "$CONFIG_LOG_FILE" >> "$output_file" 2>/dev/null || echo "Error parsing configuration log" >> "$output_file"
    else
        echo "Configuration log not available or jq not installed" >> "$output_file"
    fi

    # Add build information
    echo "" >> "$output_file"
    echo "## Build Information" >> "$output_file"
    echo "" >> "$output_file"
    echo "- **Project Root**: $PROJECT_ROOT" >> "$output_file"
    echo "- **Build Directory**: $BUILD_DIR" >> "$output_file"
    echo "- **Report Generated**: $(date -u +"%Y-%m-%d %H:%M:%S UTC")" >> "$output_file"

    if [[ -f "$BUILD_DIR/CMakeCache.txt" ]]; then
        echo "- **CMake Build Type**: $(grep CMAKE_BUILD_TYPE "$BUILD_DIR/CMakeCache.txt" | cut -d= -f2)" >> "$output_file"
        echo "- **CMake Generator**: $(grep CMAKE_GENERATOR "$BUILD_DIR/CMakeCache.txt" | cut -d= -f2)" >> "$output_file"
    fi
}

# Clear configuration history
clear_history() {
    log_info "Clearing configuration history..."

    if [[ -f "$CONFIG_LOG_FILE" ]]; then
        rm "$CONFIG_LOG_FILE"
        log_configuration_decision "HISTORY_CLEARED" "Configuration history cleared by user action" \
            "action" "clear_history" \
            "user" "$(whoami)"
    fi

    log_info "Configuration history cleared"
}

# Show configuration statistics
show_stats() {
    log_info "Showing configuration statistics..."

    if [[ ! -f "$CONFIG_LOG_FILE" ]]; then
        log_error "Configuration log file not found: $CONFIG_LOG_FILE"
        return $EXIT_FILE_ERROR
    fi

    echo "=== Deployment Configuration Statistics ==="
    echo "Configuration Log: $CONFIG_LOG_FILE"
    echo "Build Directory: $BUILD_DIR"
    echo

    if command -v jq >/dev/null 2>&1; then
        echo "Total Configurations: $(jq '.configurations | length' "$CONFIG_LOG_FILE" 2>/dev/null || echo "0")"
        echo

        echo "Configuration Types:"
        jq -r '.configurations[].type' "$CONFIG_LOG_FILE" | sort | uniq -c | sort -nr
        echo

        echo "Recent Activity:"
        jq -r '.configurations[] | "\(.timestamp): \(.type)"' "$CONFIG_LOG_FILE" | tail -10
    else
        echo "jq not available for detailed analysis"
        echo "Raw log file size: $(stat -c%s "$CONFIG_LOG_FILE" 2>/dev/null || stat -f%z "$CONFIG_LOG_FILE") bytes"
    fi
}

# Main function
main() {
    log_info "Starting deployment configuration logging..."

    parse_args "$@"
    check_prerequisites

    case "$ACTION" in
        log-cmake)
            initialize_logging
            log_cmake_configuration
            ;;
        log-features)
            initialize_logging
            log_feature_flags
            ;;
        log-dependencies)
            initialize_logging
            log_dependencies
            ;;
        log-environment)
            initialize_logging
            log_environment
            ;;
        log-build)
            initialize_logging
            log_build_configuration
            ;;
        log-deployment)
            initialize_logging
            log_deployment_targets
            ;;
        log-package)
            initialize_logging
            log_package_configuration
            ;;
        generate-report)
            generate_report
            ;;
        clear-history)
            clear_history
            ;;
        show-stats)
            show_stats
            ;;
        *)
            log_error "Unknown action: $ACTION"
            exit $EXIT_INVALID_ARGS
            ;;
    esac

    log_info "Configuration logging completed successfully!"
}

# Execute main function
main "$@"