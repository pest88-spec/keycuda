#!/bin/bash

# Attribution Verification Script for Puzzle71Solver
# Validates that all extracted third-party source files have proper attribution headers
# Usage: scripts/verify-attribution.sh [--verbose] [--json]

set -e

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
EXTRACTED_DIR="$PROJECT_ROOT/src/extracted"
MINIMUM_COVERAGE=95

# Output options
VERBOSE=false
JSON_OUTPUT=false

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --verbose)
            VERBOSE=true
            shift
            ;;
        --json)
            JSON_OUTPUT=true
            shift
            ;;
        --help)
            echo "Usage: $0 [--verbose] [--json]"
            echo "  --verbose: Show detailed attribution information for each file"
            echo "  --json:    Output results in JSON format"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Attribution validation function
validate_attribution() {
    local file="$1"
    local required_fields=(
        "@origin"
        "@origin_license"
        "@extracted_date"
        "@extracted_by"
        "@spdx_license_identifier"
    )

    local missing_fields=()
    local present_fields=()

    # Check each required field
    for field in "${required_fields[@]}"; do
        if grep -q "$field" "$file"; then
            present_fields+=("$field")
        else
            missing_fields+=("$field")
        fi
    done

    # Return results as space-separated values: file total missing present
    echo "${file#*$PROJECT_ROOT/} $((${#required_fields[@]} + ${#present_fields[@]})) ${#missing_fields[@]} ${#present_fields[@]}"
    if [ "$VERBOSE" = true ] && [ ${#missing_fields[@]} -gt 0 ]; then
        echo "MISSING: ${missing_fields[*]}" >> "/tmp/attribution_missing_$$.log"
    fi
}

# Function to count files by type
count_files() {
    local dir="$1"
    find "$dir" -type f \( -name "*.cpp" -o -name "*.c" -o -name "*.h" -o -name "*.cuh" -o -name "*.hpp" \) | wc -l
}

# Main validation function
validate_attribution_coverage() {
    local library="$1"
    local library_dir="$EXTRACTED_DIR/$library"

    if [ ! -d "$library_dir" ]; then
        echo "Warning: Library directory $library_dir not found"
        return 1
    fi

    if [ "$JSON_OUTPUT" = false ]; then
        echo "🔍 Validating attribution for $library library..."
    fi

    # Get all source files
    local files=()
    while IFS= read -r -d '' file; do
        files+=("$file")
    done < <(find "$library_dir" -type f \( -name "*.cpp" -o -name "*.c" -o -name "*.h" -o -name "*.cuh" -o -name "*.hpp" \) -print0)

    local total_files=${#files[@]}
    local files_with_attribution=0
    local total_fields=0
    local missing_fields_total=0

    # Clear temporary log
    > "/tmp/attribution_missing_$$.log"

    # Validate each file
    for file in "${files[@]}"; do
        local result
        result=$(validate_attribution "$file")
        local present_fields
        present_fields=$(echo "$result" | awk '{print $4}')

        if [ "$present_fields" -ge 5 ]; then  # At least 5 required fields present
            ((files_with_attribution++))
        fi

        total_fields=$((total_fields + $(echo "$result" | awk '{print $2}')))
        missing_fields_total=$((missing_fields_total + $(echo "$result" | awk '{print $3}')))

        if [ "$VERBOSE" = true ]; then
            local filename
            filename=$(echo "$result" | awk '{print $1}')
            local present
            present=$(echo "$result" | awk '{print $4}')
            local missing
            missing=$(echo "$result" | awk '{print $3}')

            if [ "$missing" -gt 0 ]; then
                echo -e "  ${RED}✗${NC} $filename ($present/$(($present + $missing)) fields)"
                if [ -f "/tmp/attribution_missing_$$.log" ]; then
                    grep "MISSING:" "/tmp/attribution_missing_$$.log" | tail -1 | sed 's/MISSING:/    Missing:/' >&2
                fi
            else
                echo -e "  ${GREEN}✓${NC} $filename ($present fields)"
            fi
        fi
    done

    # Calculate coverage percentage
    local coverage_percentage
    if [ "$total_files" -gt 0 ]; then
        coverage_percentage=$((files_with_attribution * 100 / total_files))
    else
        coverage_percentage=0
    fi

    # Clean up temporary log
    rm -f "/tmp/attribution_missing_$$.log"

    # Output results
    if [ "$JSON_OUTPUT" = true ]; then
        echo "  \"$library\": {"
        echo "    \"total_files\": $total_files,"
        echo "    \"files_with_attribution\": $files_with_attribution,"
        echo "    \"coverage_percentage\": $coverage_percentage,"
        echo "    \"total_fields\": $total_fields,"
        echo "    \"missing_fields\": $missing_fields_total,"
        echo "    \"meets_requirement\": $([ $coverage_percentage -ge $MINIMUM_COVERAGE ] && echo true || echo false)"
        echo "  },"
    else
        echo "  Total files: $total_files"
        echo "  Files with attribution: $files_with_attribution"
        echo "  Coverage: ${coverage_percentage}% (minimum: ${MINIMUM_COVERAGE}%)"
        if [ $coverage_percentage -ge $MINIMUM_COVERAGE ]; then
            echo -e "  Status: ${GREEN}✓ PASS${NC}"
        else
            echo -e "  Status: ${RED}✗ FAIL${NC}"
        fi
        echo ""
    fi

    return $([ $coverage_percentage -ge $MINIMUM_COVERAGE ] && echo 0 || echo 1)
}

# Main execution
main() {
    if [ "$JSON_OUTPUT" = false ]; then
        echo "🎯 Attribution Verification Script"
        echo "=================================="
        echo "Project: $PROJECT_ROOT"
        echo "Minimum coverage required: ${MINIMUM_COVERAGE}%"
        echo ""
    fi

    # Check if extracted directory exists
    if [ ! -d "$EXTRACTED_DIR" ]; then
        if [ "$JSON_OUTPUT" = true ]; then
            echo '{"error": "Extracted directory not found"}'
        else
            echo -e "${RED}Error:${NC} Extracted directory not found: $EXTRACTED_DIR"
        fi
        exit 1
    fi

    # Get all libraries
    local libraries=()
    for dir in "$EXTRACTED_DIR"/*; do
        if [ -d "$dir" ]; then
            libraries+=("$(basename "$dir")")
        fi
    done

    if [ ${#libraries[@]} -eq 0 ]; then
        if [ "$JSON_OUTPUT" = true ]; then
            echo '{"error": "No libraries found in extracted directory"}'
        else
            echo -e "${YELLOW}Warning:${NC} No libraries found in extracted directory"
        fi
        exit 1
    fi

    local overall_success=true

    if [ "$JSON_OUTPUT" = true ]; then
        echo "{"
        echo "  \"timestamp\": \"$(date -Iseconds)\","
        echo "  \"project_root\": \"$PROJECT_ROOT\","
        echo "  \"minimum_coverage\": $MINIMUM_COVERAGE,"
        echo "  \"libraries\": {"
    else
        echo "📚 Libraries found: ${libraries[*]}"
        echo ""
    fi

    # Validate each library
    for library in "${libraries[@]}"; do
        if ! validate_attribution_coverage "$library"; then
            overall_success=false
        fi
    done

    if [ "$JSON_OUTPUT" = true ]; then
        # Remove trailing comma and close JSON
        echo "  },"
        echo "  \"overall_success\": $overall_success"
        echo "}"
    else
        # Overall summary
        echo "📊 Overall Summary"
        echo "=================="
        if [ "$overall_success" = true ]; then
            echo -e "Status: ${GREEN}✓ ALL LIBRARIES PASS ATTRIBUTION REQUIREMENTS${NC}"
            exit 0
        else
            echo -e "Status: ${RED}✗ SOME LIBRARIES FAIL ATTRIBUTION REQUIREMENTS${NC}"
            exit 1
        fi
    fi
}

# Run main function
main "$@"