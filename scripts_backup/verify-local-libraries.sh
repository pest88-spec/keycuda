#!/bin/bash

# T042: Local Library Verification Utility
# This script provides focused verification of local library availability
# in deployment packages with integration to the main check-library-availability.sh

set -euo pipefail

# Script configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
MAIN_CHECK_SCRIPT="$SCRIPT_DIR/check-library-availability.sh"

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Logging functions
log_local() {
    local level="$1"
    local message="$2"
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    echo -e "${timestamp} [LOCAL_LIB_CHECK] ${level} ${message}"
}

log_info() { log_local "INFO" "$1"; }
log_success() { log_local "SUCCESS" "$1"; }
log_warning() { log_local "WARNING" "$1"; }
log_error() { log_local "ERROR" "$1"; }

# Print usage
print_usage() {
    cat << EOF
T042: Local Library Verification Utility

Usage: $0 [OPTIONS] <deployment_package>

OPTIONS:
    --help, -h              Show this help message
    --quick                 Quick check (only essential libraries)
    --comprehensive         Comprehensive check (all library categories)
    --report-format FORMAT  Report format: summary, detailed, json
    --output-file FILE      Save report to specified file

EXAMPLES:
    $0 deployment-package.tar.gz
    $0 --quick --report-format summary package.tar.bz2
    $0 --comprehensive --output-file lib-report.json package.tar.gz

DESCRIPTION:
    This utility provides focused verification of local library availability
    in deployment packages. It uses the main check-library-availability.sh
    script but provides a simpler interface for common verification tasks.

EOF
}

# Parse arguments
QUICK_MODE=false
COMPREHENSIVE=false
REPORT_FORMAT="detailed"
OUTPUT_FILE=""
DEPLOYMENT_PACKAGE=""

while [[ $# -gt 0 ]]; do
    case $1 in
        --help|-h)
            print_usage
            exit 0
            ;;
        --quick)
            QUICK_MODE=true
            shift
            ;;
        --comprehensive)
            COMPREHENSIVE=true
            shift
            ;;
        --report-format)
            REPORT_FORMAT="$2"
            shift 2
            ;;
        --output-file)
            OUTPUT_FILE="$2"
            shift 2
            ;;
        -*)
            echo "Unknown option: $1" >&2
            print_usage >&2
            exit 1
            ;;
        *)
            if [[ -z "$DEPLOYMENT_PACKAGE" ]]; then
                DEPLOYMENT_PACKAGE="$1"
            else
                echo "Error: Multiple deployment packages specified" >&2
                exit 1
            fi
            shift
            ;;
    esac
done

# Validate arguments
if [[ -z "$DEPLOYMENT_PACKAGE" ]]; then
    echo "Error: No deployment package specified" >&2
    print_usage >&2
    exit 1
fi

if [[ ! -f "$DEPLOYMENT_PACKAGE" ]]; then
    echo "Error: Deployment package not found: $DEPLOYMENT_PACKAGE" >&2
    exit 1
fi

# Check if main script exists
if [[ ! -f "$MAIN_CHECK_SCRIPT" ]]; then
    log_error "Main library check script not found: $MAIN_CHECK_SCRIPT"
    exit 1
fi

# Build main script arguments
local_args=("$MAIN_CHECK_SCRIPT")

if [[ "$QUICK_MODE" == true ]]; then
    local_args+=("--verbose")
fi

if [[ "$COMPREHENSIVE" == true ]]; then
    local_args+=("--check-all")
fi

if [[ "$REPORT_FORMAT" == "json" ]]; then
    local_args+=("--output-format" "json")
elif [[ "$REPORT_FORMAT" == "summary" ]]; then
    local_args+=("--output-format" "text")
fi

local_args+=("$DEPLOYMENT_PACKAGE")

# Run the main library check script
log_info "Starting local library verification for: $(basename "$DEPLOYMENT_PACKAGE")"
log_info "Using main script: $MAIN_CHECK_SCRIPT"

if "${local_args[@]}"; then
    log_success "Local library verification completed successfully"

    # Generate summary report if requested
    if [[ -n "$OUTPUT_FILE" ]]; then
        local latest_json=$(find "$PROJECT_ROOT/logs/verification" -name "library-availability-report-*.json" -type f -printf '%T@ %p\n' | sort -n | tail -1 | cut -d' ' -f2-)

        if [[ -n "$latest_json" && -f "$latest_json" ]]; then
            if [[ "$REPORT_FORMAT" == "json" ]]; then
                cp "$latest_json" "$OUTPUT_FILE"
            else
                # Generate summary from JSON
                echo "=== Local Library Availability Summary ===" > "$OUTPUT_FILE"
                echo "Package: $(basename "$DEPLOYMENT_PACKAGE")" >> "$OUTPUT_FILE"
                echo "Timestamp: $(jq -r '.timestamp' "$latest_json")" >> "$OUTPUT_FILE"
                echo >> "$OUTPUT_FILE"

                local package_name=$(basename "$DEPLOYMENT_PACKAGE")
                local package_info=$(jq --arg name "$package_name" '.deployment_packages[] | select(.name == $name)' "$latest_json")

                echo "Overall Availability: $(echo "$package_info" | jq -r '.local_coverage')%" >> "$OUTPUT_FILE"
                echo "Libraries Found: $(echo "$package_info" | jq -r '.libraries_found | length')" >> "$OUTPUT_FILE"
                echo "Libraries Missing: $(echo "$package_info" | jq -r '.libraries_missing | length')" >> "$OUTPUT_FILE"
                echo >> "$OUTPUT_FILE"

                echo "=== Library Categories ===" >> "$OUTPUT_FILE"
                jq -r '.library_details | to_entries[] | "\(.key): \(.value | split("|")[0])% availability"' "$latest_json" >> "$OUTPUT_FILE"
            fi

            log_success "Report saved to: $OUTPUT_FILE"
        fi
    fi

    exit 0
else
    log_error "Local library verification failed"
    exit 1
fi