#!/bin/bash

# Attribution Headers Script
# T021: Create attribution headers for extracted files
#
# This script adds attribution headers to all extracted source files
# based on the research.md template for proper licensing.

set -euo pipefail

# Script configuration
SCRIPT_NAME="$(basename "$0")"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
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
$SCRIPT_NAME - Attribution Headers Script

USAGE:
    $SCRIPT_NAME <library_name> [OPTIONS]

ARGUMENTS:
    library_name          Name of the library (e.g., secp256k1-zkp)

OPTIONS:
    --extracted-dir DIR   Extracted directory (default: $EXTRACTED_DIR)
    --dry-run            Show what would be done without making changes
    --overwrite          Overwrite existing attribution headers
    --verbose            Enable verbose output
    --help               Show this help message

EXAMPLES:
    $SCRIPT_NAME secp256k1-zkp
    $SCRIPT_NAME secp256k1-zkp --dry-run
    $SCRIPT_NAME secp256k1-zkp --overwrite --verbose

DESCRIPTION:
    This script adds attribution headers to extracted source files for
    proper licensing and attribution compliance.

EOF
}

# Attribution template
ATTRIBUTION_TEMPLATE='/*
 * Attribution Information
 *
 * This file is part of the {LIBRARY_NAME} library.
 *
 * Original repository: https://github.com/BlockstreamResearch/secp256k1-zkp
 * Original commit: {COMMIT_HASH}
 * Extraction date: {EXTRACTION_DATE}
 *
 * This file has been extracted and integrated into this project for
 * simplified build setup. No modifications have been made to the
 * source code itself.
 *
 * SPDX-License-Identifier: MIT
 */

'

# Check if file already has attribution header
has_attribution_header() {
    local file="$1"

    # Check first 20 lines for attribution indicators
    if head -20 "$file" | grep -q "SPDX-License-Identifier\|Original repository\|Attribution Information"; then
        return 0  # Has attribution
    else
        return 1  # No attribution
    fi
}

# Add attribution header to file
add_attribution_header() {
    local file="$1"
    local library_name="$2"
    local commit_hash="$3"
    local extraction_date="$4"
    local dry_run="$5"
    local overwrite="$6"

    # Skip if already has attribution and not overwriting
    if has_attribution_header "$file" && [[ "$overwrite" == "false" ]]; then
        log_info "Skipping $file (already has attribution)"
        return 0
    fi

    # Create attribution content
    local attribution_content="$ATTRIBUTION_TEMPLATE"
    attribution_content="${attribution_content//\{LIBRARY_NAME\}/$library_name}"
    attribution_content="${attribution_content//\{COMMIT_HASH\}/$commit_hash}"
    attribution_content="${attribution_content//\{EXTRACTION_DATE\}/$extraction_date}"

    if [[ "$dry_run" == "true" ]]; then
        log_info "Would add attribution header to: $file"
        return 0
    fi

    # Create temporary file with attribution header
    local temp_file=$(mktemp)
    echo -e "$attribution_content" > "$temp_file"
    cat "$file" >> "$temp_file"

    # Move temporary file to original location
    mv "$temp_file" "$file"

    log_success "Added attribution header to: $file"
}

# Process library
process_library() {
    local library_name="$1"
    local dry_run="$2"
    local overwrite="$3"
    local verbose="$4"

    local library_path="$EXTRACTED_DIR/$library_name"

    if [[ ! -d "$library_path" ]]; then
        log_error "Library directory not found: $library_path"
        return 1
    fi

    log_info "Processing library: $library_name"
    log_info "Library path: $library_path"

    # Get commit hash from git if available
    local commit_hash="Unknown"
    if [[ -d "$PROJECT_ROOT/third_party/$library_name/.git" ]]; then
        commit_hash=$(cd "$PROJECT_ROOT/third_party/$library_name" && git rev-parse HEAD 2>/dev/null || echo "Unknown")
    fi

    local extraction_date=$(date +%Y-%m-%d)

    log_info "Commit hash: $commit_hash"
    log_info "Extraction date: $extraction_date"

    if [[ "$dry_run" == "true" ]]; then
        log_info "DRY RUN - No files will be modified"
    fi

    # Find all source files
    local total_files=0
    local processed_files=0
    local skipped_files=0

    while IFS= read -r -d '' file; do
        ((total_files++))
    done < <(find "$library_path" -name "*.c" -o -name "*.cpp" -o -name "*.h" -o -name "*.hpp" -print0)

    log_info "Found $total_files source files"

    # Process each file
    while IFS= read -r -d '' file; do
        local rel_path="${file#$library_path/}"

        if [[ "$verbose" == "true" ]]; then
            log_info "Processing: $rel_path"
        fi

        if has_attribution_header "$file" && [[ "$overwrite" == "false" ]]; then
            ((skipped_files++))
            if [[ "$verbose" == "true" ]]; then
                log_info "  - Already has attribution, skipping"
            fi
        else
            add_attribution_header "$file" "$library_name" "$commit_hash" "$extraction_date" "$dry_run" "$overwrite"
            ((processed_files++))
        fi
    done < <(find "$library_path" -name "*.c" -o -name "*.cpp" -o -name "*.h" -o -name "*.hpp" -print0)

    # Report results
    echo
    log_success "Attribution processing completed!"
    log_info "Total files found: $total_files"
    log_info "Files processed: $processed_files"
    log_info "Files skipped: $skipped_files"
}

# Verify attribution coverage
verify_attribution() {
    local library_name="$1"

    local library_path="$EXTRACTED_DIR/$library_name"

    log_info "Verifying attribution coverage for: $library_name"

    local total_files=0
    local files_with_attribution=0

    while IFS= read -r -d '' file; do
        ((total_files++))
        if has_attribution_header "$file"; then
            ((files_with_attribution++))
        fi
    done < <(find "$library_path" -name "*.c" -o -name "*.cpp" -o -name "*.h" -o -name "*.hpp" -print0)

    local coverage_percentage=0
    if [[ $total_files -gt 0 ]]; then
        coverage_percentage=$((files_with_attribution * 100 / total_files))
    fi

    echo
    log_info "Attribution Coverage Report:"
    log_info "Total source files: $total_files"
    log_info "Files with attribution: $files_with_attribution"
    log_info "Coverage percentage: ${coverage_percentage}%"

    if [[ $coverage_percentage -eq 100 ]]; then
        log_success "✓ 100% attribution coverage achieved!"
    elif [[ $coverage_percentage -ge 90 ]]; then
        log_warning "⚠ Good attribution coverage: ${coverage_percentage}%"
    else
        log_error "✗ Poor attribution coverage: ${coverage_percentage}%"
        return 1
    fi
}

# Main script execution
main() {
    local library_name=""
    local dry_run=false
    local overwrite=false
    local verbose=false

    # Parse command line arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            --extracted-dir)
                EXTRACTED_DIR="$2"
                shift 2
                ;;
            --dry-run)
                dry_run=true
                shift
                ;;
            --overwrite)
                overwrite=true
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
    if [[ ! -d "$EXTRACTED_DIR" ]]; then
        log_error "Extracted directory not found: $EXTRACTED_DIR"
        exit 1
    fi

    # Process library
    process_library "$library_name" "$dry_run" "$overwrite" "$verbose"

    if [[ "$dry_run" == "false" ]]; then
        # Verify attribution coverage
        verify_attribution "$library_name"
    fi
}

# Script execution
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi