#!/bin/bash

# Attribution Coverage Report Script for Puzzle71Solver
# Generates comprehensive attribution coverage reports and metrics
# Usage: scripts/attribution-report.sh [--output-format=table|json|csv] [--output-file=filename]

set -e

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
EXTRACTED_DIR="$PROJECT_ROOT/src/extracted"
OUTPUT_FORMAT="table"
OUTPUT_FILE=""

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --output-format=*)
            OUTPUT_FORMAT="${1#*=}"
            shift
            ;;
        --output-file=*)
            OUTPUT_FILE="${1#*=}"
            shift
            ;;
        --help)
            echo "Usage: $0 [--output-format=table|json|csv] [--output-file=filename]"
            echo "  --output-format: Output format (table, json, csv) - default: table"
            echo "  --output-file:    Write output to file instead of stdout"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Function to collect attribution metrics
collect_attribution_metrics() {
    local library="$1"
    local library_dir="$EXTRACTED_DIR/$library"

    if [ ! -d "$library_dir" ]; then
        return 1
    fi

    # Get all source files
    local files=()
    while IFS= read -r -d '' file; do
        files+=("$file")
    done < <(find "$library_dir" -type f \( -name "*.cpp" -o -name "*.c" -o -name "*.h" -o -name "*.cuh" -o -name "*.hpp" \) -print0)

    local total_files=${#files[@]}
    local files_with_attribution=0
    local total_field_count=0
    local spdx_compliant=0
    local origin_tracked=0
    local license_tracked=0
    local date_tracked=0

    # Attribution fields to check
    local required_fields=(
        "@origin"
        "@origin_license"
        "@extracted_date"
        "@extracted_by"
        "@spdx_license_identifier"
    )

    # Analyze each file
    for file in "${files[@]}"; do
        local file_field_count=0
        local has_spdx=false
        local has_origin=false
        local has_license=false
        local has_date=false

        for field in "${required_fields[@]}"; do
            if grep -q "$field" "$file"; then
                ((file_field_count++))
                total_field_count=$((total_field_count + 1))

                case "$field" in
                    "@spdx_license_identifier") has_spdx=true ;;
                    "@origin") has_origin=true ;;
                    "@origin_license") has_license=true ;;
                    "@extracted_date") has_date=true ;;
                esac
            fi
        done

        if [ "$file_field_count" -ge 5 ]; then
            ((files_with_attribution++))
        fi

        if [ "$has_spdx" = true ]; then ((spdx_compliant++)); fi
        if [ "$has_origin" = true ]; then ((origin_tracked++)); fi
        if [ "$has_license" = true ]; then ((license_tracked++)); fi
        if [ "$has_date" = true ]; then ((date_tracked++)); fi
    done

    # Calculate percentages
    local coverage_percentage=0
    if [ "$total_files" -gt 0 ]; then
        coverage_percentage=$((files_with_attribution * 100 / total_files))
    fi

    # Output metrics
    echo "$library $total_files $files_with_attribution $coverage_percentage $total_field_count $spdx_compliant $origin_tracked $license_tracked $date_tracked"
}

# Function to output table format
output_table() {
    echo "📊 Attribution Coverage Report"
    echo "=============================="
    echo "Generated: $(date)"
    echo "Project: $PROJECT_ROOT"
    echo ""

    printf "%-20s %8s %8s %10s %12s %10s %10s %10s %10s\n" \
        "Library" "Files" "Attributed" "Coverage%" "Total Fields" "SPDX" "Origin" "License" "Date"
    printf "%-20s %8s %8s %10s %12s %10s %10s %10s %10s\n" \
        "------" "-----" "---------" "---------" "-----------" "-----" "------" "-------" "----"

    local total_files=0
    local total_attributed=0
    local total_fields=0

    while IFS= read -r line; do
        if [ -n "$line" ]; then
            IFS=' ' read -r library files attributed coverage fields spdx origin license date <<< "$line"
            printf "%-20s %8d %8d %9d%% %11d %9d %9d %9d %9d\n" \
                "$library" "$files" "$attributed" "$coverage" "$fields" "$spdx" "$origin" "$license" "$date"

            total_files=$((total_files + files))
            total_attributed=$((total_attributed + attributed))
            total_fields=$((total_fields + fields))
        fi
    done

    echo ""
    printf "%-20s %8d %8d %9d%% %11d\n" \
        "TOTAL" "$total_files" "$total_attributed" "$((total_attributed * 100 / total_files))" "$total_fields"
}

# Function to output JSON format
output_json() {
    local total_files=0
    local total_attributed=0
    local total_fields=0

    echo "{"
    echo "  \"timestamp\": \"$(date -Iseconds)\","
    echo "  \"project_root\": \"$PROJECT_ROOT\","
    echo "  \"libraries\": {"

    local first=true
    while IFS= read -r line; do
        if [ -n "$line" ]; then
            if [ "$first" = false ]; then
                echo ","
            fi
            first=false

            IFS=' ' read -r library files attributed coverage fields spdx origin license date <<< "$line"
            echo "    \"$library\": {"
            echo "      \"total_files\": $files,"
            echo "      \"files_with_attribution\": $attributed,"
            echo "      \"coverage_percentage\": $coverage,"
            echo "      \"total_attribution_fields\": $fields,"
            echo "      \"spdx_compliant_files\": $spdx,"
            echo "      \"origin_tracked_files\": $origin,"
            echo "      \"license_tracked_files\": $license,"
            echo "      \"date_tracked_files\": $date"
            echo -n "    }"

            total_files=$((total_files + files))
            total_attributed=$((total_attributed + attributed))
            total_fields=$((total_fields + fields))
        fi
    done

    echo ""
    echo "  },"
    echo "  \"summary\": {"
    echo "    \"total_files\": $total_files,"
    echo "    \"total_files_with_attribution\": $total_attributed,"
    echo "    \"overall_coverage_percentage\": $((total_attributed * 100 / total_files)),"
    echo "    \"total_attribution_fields\": $total_fields"
    echo "  }"
    echo "}"
}

# Function to output CSV format
output_csv() {
    echo "library,total_files,files_with_attribution,coverage_percentage,total_attribution_fields,spdx_compliant_files,origin_tracked_files,license_tracked_files,date_tracked_files"

    while IFS= read -r line; do
        if [ -n "$line" ]; then
            echo "$line"
        fi
    done

    # Add summary row
    local total_files=0
    local total_attributed=0
    local total_fields=0

    while IFS= read -r line; do
        if [ -n "$line" ]; then
            IFS=' ' read -r library files attributed coverage fields spdx origin license date <<< "$line"
            total_files=$((total_files + files))
            total_attributed=$((total_attributed + attributed))
            total_fields=$((total_fields + fields))
        fi
    done < <(tail -n +1)  # Read from the same input again

    echo "TOTAL,$total_files,$total_attributed,$((total_attributed * 100 / total_files)),$total_fields,,,"
}

# Main execution
main() {
    # Check if extracted directory exists
    if [ ! -d "$EXTRACTED_DIR" ]; then
        echo "Error: Extracted directory not found: $EXTRACTED_DIR" >&2
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
        echo "Warning: No libraries found in extracted directory" >&2
        exit 1
    fi

    # Collect metrics for all libraries
    local metrics_output=""
    for library in "${libraries[@]}"; do
        local library_metrics
        library_metrics=$(collect_attribution_metrics "$library")
        if [ $? -eq 0 ]; then
            metrics_output+="$library_metrics"$'\n'
        fi
    done

    # Output in requested format
    if [ -n "$OUTPUT_FILE" ]; then
        exec > "$OUTPUT_FILE"
    fi

    case "$OUTPUT_FORMAT" in
        "table")
            echo "$metrics_output" | output_table
            ;;
        "json")
            echo "$metrics_output" | output_json
            ;;
        "csv")
            echo "$metrics_output" | output_csv
            ;;
        *)
            echo "Error: Invalid output format '$OUTPUT_FORMAT'. Use table, json, or csv." >&2
            exit 1
            ;;
    esac

    if [ -n "$OUTPUT_FILE" ]; then
        echo "Report saved to: $OUTPUT_FILE"
    fi
}

# Run main function
main "$@"