#!/bin/bash

# Attribution Verification Script
# Verifies that all integrated third-party code has proper attribution and licensing
#
# This script checks compliance with constitution VI.2 License Compliance Verification
# ensuring 100% attribution coverage for all integrated third-party code.

set -euo pipefail

# Script configuration
SCRIPT_NAME="$(basename "$0")"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Logging function
log() {
    echo -e "${BLUE}[$SCRIPT_NAME]${NC} $1"
}

warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1" >&2
}

success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

# Function to show usage
show_usage() {
    cat << EOF
Usage: $SCRIPT_NAME [OPTIONS] COMMAND

Attribution Verification Script

COMMANDS:
    check           Check attribution compliance for all integrated code
    report          Generate detailed attribution report
    fix             Attempt to fix missing attribution (where possible)
    list            List all integrated libraries and their attribution status

OPTIONS:
    -h, --help      Show this help message
    -v, --verbose   Enable verbose output
    -q, --quiet     Suppress non-error output
    --json          Output results in JSON format

EOF
}

# Function to check file attribution
check_file_attribution() {
    local file="$1"
    local issues=()

    # Check for SPDX identifier
    if ! grep -q "SPDX-License-Identifier:" "$file"; then
        issues+=("Missing SPDX license identifier")
    fi

    # Check for copyright notice
    if ! grep -qi -E "(copyright|©)" "$file"; then
        issues+=("Missing copyright notice")
    fi

    # Check for origin reference
    if ! grep -q -E "@origin|@sot_ref|source:" "$file"; then
        issues+=("Missing source origin reference")
    fi

    # Output issues
    if [ ${#issues[@]} -gt 0 ]; then
        echo "$file: ${issues[*]}"
        return 1
    fi

    return 0
}

# Function to check directory attribution
check_directory_attribution() {
    local dir="$1"
    local verbose="${2:-false}"
    local total_files=0
    local compliant_files=0
    local non_compliant_files=()

    log "Checking attribution for directory: $dir"

    # Find all source files
    while IFS= read -r -d '' file; do
        ((total_files++))

        if check_file_attribution "$file" >/dev/null 2>&1; then
            ((compliant_files++))
            if [ "$verbose" = true ]; then
                log "  ✓ $file"
            fi
        else
            non_compliant_files+=("$file")
            if [ "$verbose" = true ]; then
                error "  ✗ $file"
            fi
        fi
    done < <(find "$dir" -type f \( -name "*.h" -o -name "*.hpp" -o -name "*.c" -o -name "*.cpp" -o -name "*.cc" \) -print0)

    # Calculate compliance percentage
    local compliance_percentage=0
    if [ "$total_files" -gt 0 ]; then
        compliance_percentage=$(( compliant_files * 100 / total_files ))
    fi

    echo "Directory: $dir"
    echo "  Total files: $total_files"
    echo "  Compliant files: $compliant_files"
    echo "  Compliance: ${compliance_percentage}%"

    if [ ${#non_compliant_files[@]} -gt 0 ]; then
        echo "  Non-compliant files:"
        for file in "${non_compliant_files[@]}"; do
            echo "    - $file: $(check_file_attribution "$file")"
        done
    fi

    echo ""

    # Return success if 100% compliant
    [ "$compliance_percentage" -eq 100 ]
}

# Function to check all integrations
check_all_attribution() {
    local verbose="${1:-false}"
    local overall_compliance=true

    log "Checking attribution compliance for all integrated dependencies..."

    # Check extracted libraries
    local extracted_dirs=()
    if [ -d "src/extracted" ]; then
        while IFS= read -r -d '' dir; do
            extracted_dirs+=("$dir")
        done < <(find "src/extracted" -maxdepth 1 -type d ! -path "src/extracted" -print0)
    fi

    if [ ${#extracted_dirs[@]} -eq 0 ]; then
        warning "No extracted libraries found"
        return 0
    fi

    for dir in "${extracted_dirs[@]}"; do
        if ! check_directory_attribution "$dir" "$verbose"; then
            overall_compliance=false
        fi
    done

    if [ "$overall_compliance" = true ]; then
        success "✓ 100% attribution compliance achieved"
        return 0
    else
        error "✗ Attribution compliance issues found"
        return 1
    fi
}

# Function to generate attribution report
generate_attribution_report() {
    local report_file="attribution-report-$(date +%Y%m%d-%H%M%S).json"

    log "Generating attribution report: $report_file"

    # Create report header
    cat > "$report_file" << EOF
{
  "generated": "$(date -Iseconds)",
  "script": "$SCRIPT_NAME",
  "version": "1.0.0",
  "libraries": []
}
EOF

    # TODO: Implement detailed report generation
    log "Report generation to be fully implemented with US1 tasks"

    success "Attribution report generated: $report_file"
}

# Function to list integrated libraries
list_integrated_libraries() {
    log "Integrated libraries and attribution status:"

    local extracted_dirs=()
    if [ -d "src/extracted" ]; then
        while IFS= read -r -d '' dir; do
            extracted_dirs+=("$(basename "$dir")")
        done < <(find "src/extracted" -maxdepth 1 -type d ! -path "src/extracted" -print0)
    fi

    if [ ${#extracted_dirs[@]} -eq 0 ]; then
        warning "No integrated libraries found"
        return
    fi

    for lib in "${extracted_dirs[@]}"; do
        local lib_path="src/extracted/$lib"
        local total_files=$(find "$lib_path" -type f \( -name "*.h" -o -name "*.cpp" \) | wc -l)
        local compliant_files=$(find "$lib_path" -type f \( -name "*.h" -o -name "*.cpp" \) -exec grep -l "SPDX-License-Identifier:" {} \; | wc -l)

        if [ "$total_files" -gt 0 ]; then
            local compliance=$(( compliant_files * 100 / total_files ))
            printf "  %-20s: %3d files, %3d%% compliant\n" "$lib" "$total_files" "$compliance"
        else
            printf "  %-20s: No source files found\n" "$lib"
        fi
    done
}

# Main script logic
main() {
    local verbose=false
    local quiet=false
    local json_output=false

    # Parse command line arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            -h|--help)
                show_usage
                exit 0
                ;;
            -v|--verbose)
                verbose=true
                shift
                ;;
            -q|--quiet)
                quiet=true
                shift
                ;;
            --json)
                json_output=true
                shift
                ;;
            check|report|fix|list)
                local command="$1"
                shift
                break
                ;;
            *)
                error "Unknown option: $1"
                show_usage
                exit 1
                ;;
        esac
    done

    # Check if command was provided
    if [ -z "${command:-}" ]; then
        error "No command specified"
        show_usage
        exit 1
    fi

    # Welcome message
    if [ "$quiet" = false ]; then
        log "Attribution Verification Script"
    fi

    # Execute command
    case "$command" in
        check)
            check_all_attribution "$verbose"
            ;;
        report)
            generate_attribution_report
            ;;
        fix)
            log "Attempting to fix missing attribution..."
            # TODO: Implement fix functionality
            warning "Fix functionality to be implemented with US1 tasks"
            ;;
        list)
            list_integrated_libraries
            ;;
        *)
            error "Unknown command: $command"
            show_usage
            exit 1
            ;;
    esac
}

# Run main function with all arguments
main "$@"