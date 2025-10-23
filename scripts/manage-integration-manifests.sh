#!/bin/bash

# Integration Manifest Management Script
# T019: Integration Manifest Implementation for Build Configuration Management
#
# This script provides command-line interface for managing integration manifests,
# including creation, validation, export, and CMake configuration generation.

set -euo pipefail

# Script configuration
SCRIPT_NAME="$(basename "$0")"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
MANIFESTS_DIR="${MANIFESTS_DIR:-$PROJECT_ROOT/build/integration-manifests}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

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

# Help function
show_help() {
    cat << EOF
$SCRIPT_NAME - Integration Manifest Management Script

USAGE:
    $SCRIPT_NAME <COMMAND> [OPTIONS]

COMMANDS:
    create <library> <version> <source_path>    Create a new library manifest
    list                                      List all manifests
    show <manifest_id>                        Show manifest details
    validate <manifest_id>                    Validate a manifest
    export <format>                           Export all manifests (json, cmake)
    check-conflicts                           Check for manifest conflicts
    generate-cmake                            Generate CMake configuration
    import <file> <format>                    Import manifest from file
    help                                      Show this help message

OPTIONS:
    --manifests-dir DIR                       Set manifests directory (default: $MANIFESTS_DIR)
    --verbose                                 Enable verbose output

EXAMPLES:
    $SCRIPT_NAME create secp256k1-zkp 1.0.0 src/extracted/secp256k1-zkp
    $SCRIPT_NAME list
    $SCRIPT_NAME validate library_secp256k1-zkp_1234567890
    $SCRIPT_NAME export cmake > integration.cmake
    $SCRIPT_NAME check-conflicts

DESCRIPTION:
    This script manages integration manifests for third-party dependencies.
    It handles library manifests, dependency manifests, and build configuration
    management with validation and conflict detection.

EOF
}

# Ensure manifests directory exists
ensure_manifests_dir() {
    mkdir -p "$MANIFESTS_DIR"
}

# Create a new library manifest
create_manifest() {
    local library_name="$1"
    local library_version="$2"
    local source_path="$3"

    if [[ -z "$library_name" || -z "$library_version" || -z "$source_path" ]]; then
        log_error "Usage: create <library_name> <library_version> <source_path>"
        return 1
    fi

    if [[ ! -d "$source_path" ]]; then
        log_error "Source path does not exist: $source_path"
        return 1
    fi

    ensure_manifests_dir

    # Generate manifest ID
    local timestamp=$(date +%s)
    local manifest_id="library_${library_name}_${timestamp}"

    # Create manifest JSON
    cat > "$MANIFESTS_DIR/${manifest_id}.json" << EOF
{
    "manifest_id": "$manifest_id",
    "manifest_type": "LIBRARY_MANIFEST",
    "schema_version": "1.0",
    "created_at": "$(date -u +"%Y-%m-%dT%H:%M:%SZ")",
    "created_by": "manage-integration-manifests.sh",
    "description": "Manifest for integrated library: $library_name",
    "libraries": [
        {
            "library_name": "$library_name",
            "library_version": "$library_version",
            "origin_url": "Unknown",
            "commit_hash": "Unknown",
            "source_path": "$source_path",
            "build_path": "build/$library_name",
            "cmake_targets": ["$library_name"],
            "dependencies": [],
            "build_options": {
                "BUILD_SHARED_LIBS": "OFF",
                "CMAKE_POSITION_INDEPENDENT_CODE": "ON",
                "BUILD_TESTING": "OFF"
            },
            "include_directories": ["include"],
            "link_libraries": [],
            "is_enabled": true,
            "last_updated": "$(date -u +"%Y-%m-%dT%H:%M:%SZ")",
            "metadata": {
                "auto_detected": false,
                "integration_date": "$(date +%Y-%m-%d)"
            }
        }
    ],
    "build_config": {
        "build_type": "RelWithDebInfo",
        "toolchain_version": "GCC 11+ / CUDA 12.0+",
        "compiler_flags": ["-Wall", "-Wextra", "-O3"],
        "cmake_definitions": [
            "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
            "-DCMAKE_CUDA_ARCHITECTURES=75;86;89;90"
        ],
        "environment_variables": {},
        "install_prefix": "/usr/local",
        "build_dependencies": [],
        "build_options": {},
        "is_offline_build": false,
        "cuda_architecture": "75;86;89;90",
        "required_tools": ["cmake", "make", "gcc", "g++", "nvcc"]
    },
    "version_constraints": [],
    "metadata": {
        "created_by_script": true,
        "script_version": "1.0"
    },
    "tags": ["managed", "library"],
    "manifest_version": "1.0"
}
EOF

    log_success "Created manifest: $manifest_id"
    log_info "Library: $library_name"
    log_info "Version: $library_version"
    log_info "Source: $source_path"
}

# List all manifests
list_manifests() {
    ensure_manifests_dir

    if [[ ! -d "$MANIFESTS_DIR" ]] || [[ -z "$(ls -A "$MANIFESTS_DIR" 2>/dev/null)" ]]; then
        log_warning "No manifests found in $MANIFESTS_DIR"
        return 0
    fi

    echo "Integration Manifests:"
    echo "======================"

    for manifest_file in "$MANIFESTS_DIR"/*.json; do
        if [[ -f "$manifest_file" ]]; then
            local manifest_id=$(basename "$manifest_file" .json)
            local library_name=$(jq -r '.libraries[0].library_name // "Unknown"' "$manifest_file" 2>/dev/null || echo "Unknown")
            local library_version=$(jq -r '.libraries[0].library_version // "Unknown"' "$manifest_file" 2>/dev/null || echo "Unknown")
            local created_at=$(jq -r '.created_at // "Unknown"' "$manifest_file" 2>/dev/null || echo "Unknown")

            printf "%-30s %-15s %-10s %s\n" "$manifest_id" "$library_name" "$library_version" "$created_at"
        fi
    done
}

# Show manifest details
show_manifest() {
    local manifest_id="$1"

    if [[ -z "$manifest_id" ]]; then
        log_error "Usage: show <manifest_id>"
        return 1
    fi

    local manifest_file="$MANIFESTS_DIR/${manifest_id}.json"

    if [[ ! -f "$manifest_file" ]]; then
        log_error "Manifest not found: $manifest_id"
        return 1
    fi

    echo "Manifest Details:"
    echo "=================="

    if command -v jq >/dev/null 2>&1; then
        jq '.' "$manifest_file"
    else
        cat "$manifest_file"
    fi
}

# Validate manifest
validate_manifest() {
    local manifest_id="$1"

    if [[ -z "$manifest_id" ]]; then
        log_error "Usage: validate <manifest_id>"
        return 1
    fi

    local manifest_file="$MANIFESTS_DIR/${manifest_id}.json"

    if [[ ! -f "$manifest_file" ]]; then
        log_error "Manifest not found: $manifest_id"
        return 1
    fi

    log_info "Validating manifest: $manifest_id"

    # Basic validation
    local validation_errors=0
    local validation_warnings=0

    # Check JSON syntax
    if ! jq empty "$manifest_file" 2>/dev/null; then
        log_error "Invalid JSON syntax"
        ((validation_errors++))
    fi

    # Check required fields
    local required_fields=("manifest_id" "manifest_type" "schema_version" "created_at" "libraries")
    for field in "${required_fields[@]}"; do
        if ! jq -e ".$field" "$manifest_file" >/dev/null 2>&1; then
            log_error "Missing required field: $field"
            ((validation_errors++))
        fi
    done

    # Check library information
    local library_name=$(jq -r '.libraries[0].library_name // empty' "$manifest_file" 2>/dev/null)
    local source_path=$(jq -r '.libraries[0].source_path // empty' "$manifest_file" 2>/dev/null)

    if [[ -z "$library_name" ]]; then
        log_error "Library name is required"
        ((validation_errors++))
    fi

    if [[ -z "$source_path" ]]; then
        log_error "Source path is required"
        ((validation_errors++))
    elif [[ ! -d "$source_path" ]]; then
        log_warning "Source path does not exist: $source_path"
        ((validation_warnings++))
    fi

    # Report results
    echo
    if [[ $validation_errors -eq 0 ]]; then
        log_success "Manifest validation passed"
        if [[ $validation_warnings -gt 0 ]]; then
            log_warning "$validation_warnings warning(s) found"
        fi
        return 0
    else
        log_error "Manifest validation failed with $validation_errors error(s)"
        if [[ $validation_warnings -gt 0 ]]; then
            log_warning "$validation_warnings warning(s) found"
        fi
        return 1
    fi
}

# Export manifests
export_manifests() {
    local format="$1"

    if [[ -z "$format" ]]; then
        log_error "Usage: export <format>"
        log_error "Supported formats: json, cmake"
        return 1
    fi

    case "$format" in
        json)
            ensure_manifests_dir
            echo "["
            local first=true
            for manifest_file in "$MANIFESTS_DIR"/*.json; do
                if [[ -f "$manifest_file" ]]; then
                    if [[ "$first" == "false" ]]; then
                        echo ","
                    fi
                    cat "$manifest_file"
                    first=false
                fi
            done
            echo "]"
            ;;
        cmake)
            generate_cmake_configuration
            ;;
        *)
            log_error "Unsupported export format: $format"
            log_error "Supported formats: json, cmake"
            return 1
            ;;
    esac
}

# Check for conflicts
check_conflicts() {
    ensure_manifests_dir

    if [[ ! -d "$MANIFESTS_DIR" ]] || [[ -z "$(ls -A "$MANIFESTS_DIR" 2>/dev/null)" ]]; then
        log_warning "No manifests found for conflict checking"
        return 0
    fi

    log_info "Checking for manifest conflicts..."

    local conflicts_found=0

    # Check for duplicate library names
    local library_names=()
    for manifest_file in "$MANIFESTS_DIR"/*.json; do
        if [[ -f "$manifest_file" ]]; then
            local library_name=$(jq -r '.libraries[0].library_name // empty' "$manifest_file" 2>/dev/null)
            if [[ -n "$library_name" ]]; then
                if [[ " ${library_names[*]} " =~ " $library_name " ]]; then
                    log_error "Duplicate library name found: $library_name"
                    ((conflicts_found++))
                else
                    library_names+=("$library_name")
                fi
            fi
        fi
    done

    # Check for duplicate source paths
    local source_paths=()
    for manifest_file in "$MANIFESTS_DIR"/*.json; do
        if [[ -f "$manifest_file" ]]; then
            local source_path=$(jq -r '.libraries[0].source_path // empty' "$manifest_file" 2>/dev/null)
            if [[ -n "$source_path" ]]; then
                if [[ " ${source_paths[*]} " =~ " $source_path " ]]; then
                    log_error "Duplicate source path found: $source_path"
                    ((conflicts_found++))
                else
                    source_paths+=("$source_path")
                fi
            fi
        fi
    done

    # Check for duplicate CMake targets
    local cmake_targets=()
    for manifest_file in "$MANIFESTS_DIR"/*.json; do
        if [[ -f "$manifest_file" ]]; then
            local targets=$(jq -r '.libraries[0].cmake_targets[]? // empty' "$manifest_file" 2>/dev/null)
            for target in $targets; do
                if [[ -n "$target" ]]; then
                    if [[ " ${cmake_targets[*]} " =~ " $target " ]]; then
                        log_error "Duplicate CMake target found: $target"
                        ((conflicts_found++))
                    else
                        cmake_targets+=("$target")
                    fi
                fi
            done
        fi
    done

    echo
    if [[ $conflicts_found -eq 0 ]]; then
        log_success "No conflicts detected"
    else
        log_error "$conflicts_found conflict(s) detected"
        return 1
    fi
}

# Generate CMake configuration
generate_cmake_configuration() {
    ensure_manifests_dir

    cat << 'EOF'
# Auto-generated CMake configuration from integration manifests
# Generated on: $(date)

# Build Configuration
set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type")
set(CMAKE_CUDA_ARCHITECTURES 75;86;89;90 CACHE STRING "CUDA architectures")

# Extracted Libraries
EOF

    if [[ ! -d "$MANIFESTS_DIR" ]] || [[ -z "$(ls -A "$MANIFESTS_DIR" 2>/dev/null)" ]]; then
        echo "# No integrated libraries found"
        return 0
    fi

    for manifest_file in "$MANIFESTS_DIR"/*.json; do
        if [[ -f "$manifest_file" ]]; then
            local library_name=$(jq -r '.libraries[0].library_name // empty' "$manifest_file" 2>/dev/null)
            local source_path=$(jq -r '.libraries[0].source_path // empty' "$manifest_file" 2>/dev/null)
            local include_dirs=$(jq -r '.libraries[0].include_directories[]? // empty' "$manifest_file" 2>/dev/null)

            if [[ -n "$library_name" && -n "$source_path" ]]; then
                echo
                echo "# Library: $library_name"
                echo "set(${library_name}_SOURCES"
                if [[ -d "$source_path" ]]; then
                    find "$source_path" -name "*.c" -o -name "*.cpp" -o -name "*.cxx" 2>/dev/null | while read -r file; do
                        echo "    $file"
                    done
                fi
                echo ")"

                if [[ -n "$include_dirs" ]]; then
                    echo "set(${library_name}_INCLUDE_DIRS"
                    for include_dir in $include_dirs; do
                        echo "    $source_path/$include_dir"
                    done
                    echo ")"
                fi

                echo "add_library($library_name \${${library_name}_SOURCES})"
                if [[ -n "$include_dirs" ]]; then
                    echo "target_include_directories($library_name PRIVATE \${${library_name}_INCLUDE_DIRS})"
                fi
            fi
        fi
    done

    echo
    echo "# Integration Manifest Configuration Complete"
}

# Import manifest from file
import_manifest() {
    local file="$1"
    local format="$2"

    if [[ -z "$file" || -z "$format" ]]; then
        log_error "Usage: import <file> <format>"
        return 1
    fi

    if [[ ! -f "$file" ]]; then
        log_error "File not found: $file"
        return 1
    fi

    case "$format" in
        json)
            ensure_manifests_dir
            local manifest_id=$(jq -r '.manifest_id // empty' "$file" 2>/dev/null)
            if [[ -z "$manifest_id" ]]; then
                log_error "Invalid manifest file or missing manifest_id"
                return 1
            fi
            cp "$file" "$MANIFESTS_DIR/${manifest_id}.json"
            log_success "Imported manifest: $manifest_id"
            ;;
        *)
            log_error "Unsupported import format: $format"
            log_error "Supported formats: json"
            return 1
            ;;
    esac
}

# Main script execution
main() {
    local command="${1:-}"

    case "$command" in
        create)
            create_manifest "$2" "$3" "$4"
            ;;
        list)
            list_manifests
            ;;
        show)
            show_manifest "$2"
            ;;
        validate)
            validate_manifest "$2"
            ;;
        export)
            export_manifests "$2"
            ;;
        check-conflicts)
            check_conflicts
            ;;
        generate-cmake)
            generate_cmake_configuration
            ;;
        import)
            import_manifest "$2" "$3"
            ;;
        help|--help|-h)
            show_help
            ;;
        *)
            log_error "Unknown command: $command"
            echo
            show_help
            exit 1
            ;;
    esac
}

# Check for required commands
if ! command -v jq >/dev/null 2>&1; then
    log_warning "jq is not installed. Some features may not work correctly."
fi

# Script execution
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi