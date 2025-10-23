#!/bin/bash

# Library Extraction Script
# T020: Implement secp256k1-zkp extraction
#
# This script extracts source code from third-party libraries into the
# src/extracted directory for integration, with attribution handling.

set -euo pipefail

# Script configuration
SCRIPT_NAME="$(basename "$0")"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SOURCE_DIR="$PROJECT_ROOT/third_party"
EXTRACTED_DIR="$PROJECT_ROOT/src/extracted"

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
$SCRIPT_NAME - Library Extraction Script

USAGE:
    $SCRIPT_NAME <library_name> [OPTIONS]

ARGUMENTS:
    library_name          Name of the library to extract (e.g., secp256k1-zkp)

OPTIONS:
    --source-dir DIR      Source directory (default: $SOURCE_DIR)
    --extracted-dir DIR   Extracted directory (default: $EXTRACTED_DIR)
    --dry-run            Show what would be done without actually extracting
    --clean              Clean extracted directory before extraction
    --verbose            Enable verbose output
    --help               Show this help message

EXAMPLES:
    $SCRIPT_NAME secp256k1-zkp
    $SCRIPT_NAME secp256k1-zkp --clean --verbose
    $SCRIPT_NAME secp256k1-zkp --dry-run

DESCRIPTION:
    This script extracts source code from third-party libraries into the
    src/extracted directory for integration into the main project.
    It handles file filtering, attribution, and build configuration.

EOF
}

# File filtering - exclude patterns
declare -a EXCLUDE_PATTERNS=(
    ".*"                 # Hidden files
    "*.git*"            # Git files and directories
    "*.so"              # Shared libraries
    "*.a"               # Static libraries
    "build*"            # Build directories
    "CMakeFiles*"       # CMake generated files
    "*.o"               # Object files
    "*.exe"             # Executables
    "*.dll"             # Windows DLLs
    "test*"             # Test files
    "tests*"            # Test directories
    "example*"          # Example files
    "examples*"         # Example directories
    "doc*"              # Documentation
    "*.md"              # Markdown files
    "*.txt"             # Text files (except specific ones)
    "Makefile*"         # Makefiles
    "configure*"        # Configure scripts
    "*.sh"              # Shell scripts (except specific ones)
    "*.py"              # Python scripts
    "COPYING*"          # License files (will be handled separately)
    "LICENSE*"          # License files (will be handled separately)
)

# Include patterns - files we definitely want
declare -a INCLUDE_PATTERNS=(
    "*.c"
    "*.cpp"
    "*.cxx"
    "*.h"
    "*.hpp"
    "*.inl"
    "*.inc"
    "CMakeLists.txt"
    "*.cmake"
    "*.h.in"
    "*.c.in"
)

# Check if file should be excluded
should_exclude_file() {
    local file="$1"
    local filename=$(basename "$file")

    # Check exclude patterns
    for pattern in "${EXCLUDE_PATTERNS[@]}"; do
        if [[ "$filename" == $pattern ]]; then
            return 0  # Exclude
        fi
    done

    # Check include patterns
    for pattern in "${INCLUDE_PATTERNS[@]}"; do
        if [[ "$filename" == $pattern ]]; then
            return 1  # Don't exclude (include)
        fi
    done

    # Default to exclude if not in include patterns
    return 0
}

# Extract library
extract_library() {
    local library_name="$1"
    local clean_dir="$2"
    local dry_run="$3"
    local verbose="$4"

    local source_path="$SOURCE_DIR/$library_name"
    local extracted_path="$EXTRACTED_DIR/$library_name"

    if [[ ! -d "$source_path" ]]; then
        log_error "Source directory not found: $source_path"
        return 1
    fi

    log_info "Extracting library: $library_name"
    log_info "Source: $source_path"
    log_info "Target: $extracted_path"

    if [[ "$dry_run" == "true" ]]; then
        log_info "DRY RUN - No files will be copied"
    fi

    # Clean existing directory if requested
    if [[ "$clean_dir" == "true" && -d "$extracted_path" ]]; then
        if [[ "$dry_run" == "false" ]]; then
            log_info "Cleaning existing extracted directory: $extracted_path"
            rm -rf "$extracted_path"
        else
            log_info "Would clean existing extracted directory: $extracted_path"
        fi
    fi

    # Create target directory
    if [[ "$dry_run" == "false" ]]; then
        mkdir -p "$extracted_path"
    fi

    # Count files for progress reporting
    local total_files=0
    local copied_files=0
    local skipped_files=0

    while IFS= read -r -d '' file; do
        ((total_files++))
    done < <(find "$source_path" -type f -print0)

    log_info "Found $total_files files in source directory"

    # Copy files with filtering
    local file_count=0
    while IFS= read -r -d '' file; do
        ((file_count++))

        local rel_path="${file#$source_path/}"
        local target_file="$extracted_path/$rel_path"
        local target_dir=$(dirname "$target_file")

        if should_exclude_file "$file"; then
            ((skipped_files++))
            if [[ "$verbose" == "true" ]]; then
                log_info "Skipping: $rel_path"
            fi
            continue
        fi

        if [[ "$dry_run" == "false" ]]; then
            # Create target directory if needed
            mkdir -p "$target_dir"

            # Copy file
            cp "$file" "$target_file"
            ((copied_files++))

            if [[ "$verbose" == "true" ]]; then
                log_info "Copied: $rel_path"
            fi
        else
            ((copied_files++))
            log_info "Would copy: $rel_path"
        fi

        # Progress reporting
        if (( file_count % 100 == 0 )); then
            log_info "Processed $file_count/$total_files files..."
        fi

    done < <(find "$source_path" -type f -print0)

    # Report results
    echo
    log_success "Extraction completed!"
    log_info "Total files found: $total_files"
    log_info "Files copied: $copied_files"
    log_info "Files skipped: $skipped_files"

    if [[ "$dry_run" == "false" ]]; then
        log_info "Extracted to: $extracted_path"

        # Show extracted directory structure
        echo
        log_info "Extracted directory structure:"
        tree -L 2 "$extracted_path" 2>/dev/null || find "$extracted_path" -type d | head -10
    fi
}

# Create attribution template
create_attribution_template() {
    local library_name="$1"
    local dry_run="$2"

    local extracted_path="$EXTRACTED_DIR/$library_name"
    local attribution_file="$extracted_path/ATTRIBUTION.md"

    if [[ "$dry_run" == "true" ]]; then
        log_info "Would create attribution template: $attribution_file"
        return 0
    fi

    cat > "$attribution_file" << EOF
# Attribution Information

This directory contains extracted source code from the $library_name library.

## Original Library Information

- **Library Name**: $library_name
- **Source Location**: third_party/$library_name
- **Extraction Date**: $(date +%Y-%m-%d)
- **Extraction Script**: $SCRIPT_NAME

## License Information

Please refer to the original library's license files for complete licensing information.

## Modifications

All source files in this directory have been extracted from their original
repository and integrated into this project for simplified build setup.
No modifications have been made to the source code itself.

## Build Integration

This library is integrated into the main project through the CMake build system.
See the main CMakeLists.txt for integration details.

EOF

    log_success "Created attribution template: $attribution_file"
}

# Validate extraction
validate_extraction() {
    local library_name="$1"

    local extracted_path="$EXTRACTED_DIR/$library_name"

    if [[ ! -d "$extracted_path" ]]; then
        log_error "Extracted directory not found: $extracted_path"
        return 1
    fi

    log_info "Validating extraction for: $library_name"

    # Check for source files
    local source_files=$(find "$extracted_path" -name "*.c" -o -name "*.cpp" -o -name "*.h" | wc -l)
    if [[ $source_files -eq 0 ]]; then
        log_error "No source files found in extracted directory"
        return 1
    fi

    log_success "Found $source_files source files"

    # Check for CMakeLists.txt
    local cmake_files=$(find "$extracted_path" -name "CMakeLists.txt" | wc -l)
    if [[ $cmake_files -eq 0 ]]; then
        log_warning "No CMakeLists.txt files found"
    else
        log_success "Found $cmake_files CMakeLists.txt files"
    fi

    # Check for include directories
    local include_dirs=$(find "$extracted_path" -type d -name "include" | wc -l)
    if [[ $include_dirs -eq 0 ]]; then
        log_warning "No include directories found"
    else
        log_success "Found $include_dirs include directories"
    fi

    log_success "Extraction validation completed"
}

# Main script execution
main() {
    local library_name=""
    local dry_run=false
    local clean_dir=false
    local verbose=false

    # Parse command line arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            --source-dir)
                SOURCE_DIR="$2"
                shift 2
                ;;
            --extracted-dir)
                EXTRACTED_DIR="$2"
                shift 2
                ;;
            --dry-run)
                dry_run=true
                shift
                ;;
            --clean)
                clean_dir=true
                shift
                ;;
            --verbose)
                verbose=true
                shift
                ;;
            --help|--help|-h)
                show_help
                exit 0
                ;;
            -*)
                log_error "Unknown option: $1"
                show_help
                exit 1
                ;;
            *)
                if [[ -z "$library_name" ]]; then
                    library_name="$1"
                else
                    log_error "Multiple library names specified"
                    exit 1
                fi
                shift
                ;;
        esac
    done

    # Validate arguments
    if [[ -z "$library_name" ]]; then
        log_error "Library name is required"
        show_help
        exit 1
    fi

    # Validate directories
    if [[ ! -d "$SOURCE_DIR" ]]; then
        log_error "Source directory not found: $SOURCE_DIR"
        exit 1
    fi

    # Perform extraction
    extract_library "$library_name" "$clean_dir" "$dry_run" "$verbose"

    if [[ "$dry_run" == "false" ]]; then
        # Create attribution template
        create_attribution_template "$library_name" "$dry_run"

        # Validate extraction
        validate_extraction "$library_name"
    fi
}

# Script execution
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi