#!/bin/bash

# Automated Compatibility Report Generation Script
# Generates comprehensive compatibility reports for all integrated libraries

set -euo pipefail

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
COMPATIBILITY_DIR="${PROJECT_ROOT}/compatibility"
REPORTS_DIR="${COMPATIBILITY_DIR}/reports"
DATA_DIR="${COMPATIBILITY_DIR}/data"
TEMPLATES_DIR="${COMPATIBILITY_DIR}/templates"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Logging
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

# Report configuration
readonly REPORT_TITLE="Third-Party Dependencies Compatibility Report"
readonly REPORT_VERSION="1.0.0"
readonly REPORT_AUTHOR="Puzzle71Solver Team"

# Initialize directories
init_directories() {
    log_info "Initializing compatibility report directories..."

    mkdir -p "$COMPATIBILITY_DIR"
    mkdir -p "$REPORTS_DIR"
    mkdir -p "$DATA_DIR"
    mkdir -p "$TEMPLATES_DIR"

    log_success "Directories initialized"
}

# Collect library metadata
collect_library_metadata() {
    local metadata_file="${DATA_DIR}/library_metadata.json"

    log_info "Collecting library metadata..."

    {
        echo "{"
        echo "  \"collection_timestamp\": \"$(date -Iseconds)\","
        echo "  \"libraries\": {"

        local first=true
        local extracted_dir="${PROJECT_ROOT}/src/extracted"

        for lib_dir in "$extracted_dir"/*; do
            if [[ -d "$lib_dir" ]]; then
                local lib_name=$(basename "$lib_dir")

                if [[ "$first" == "false" ]]; then
                    echo ","
                fi
                first=false

                echo "    \"$lib_name\": {"
                echo "      \"name\": \"$lib_name\","
                echo "      \"path\": \"$lib_dir\","
                echo "      \"source_files\": $(find "$lib_dir" -name "*.c" -o -name "*.cpp" -o -name "*.h" -o -name "*.hpp" | wc -l),"
                echo "      \"header_files\": $(find "$lib_dir" -name "*.h" -o -name "*.hpp" | wc -l),"
                echo "      \"source_files_list\": ["

                local source_files=($(find "$lib_dir" -name "*.c" -o -name "*.cpp" 2>/dev/null))
                local first_file=true
                for file in "${source_files[@]:0:10}"; do
                    if [[ "$first_file" == "false" ]]; then
                        echo ","
                    fi
                    first_file=false
                    echo "        \"${file#$lib_dir/}\""
                done
                if [[ ${#source_files[@]} -gt 10 ]]; then
                    echo ","
                    echo "        \"... and $(( ${#source_files[@]} - 10 )) more files\""
                fi

                echo "      ],"
                echo "      \"attribution_files\": $(find "$lib_dir" -name "*.c" -o -name "*.cpp" -o -name "*.h" -o -name "*.hpp" -exec grep -l "@origin" {} \; 2>/dev/null | wc -l),"
                echo "      \"attribution_coverage\": $(echo "scale=2; $(find "$lib_dir" -name "*.c" -o -name "*.cpp" -o -name "*.h" -o -name "*.hpp" -exec grep -l "@origin" {} \; 2>/dev/null | wc -l) * 100 / $(find "$lib_dir" -name "*.c" -o -name "*.cpp" -o -name "*.h" -o -name "*.hpp" | wc -l)" | bc -l),"
                echo "      \"last_modified\": \"$(stat -c %Y "$lib_dir" 2>/dev/null || echo "unknown")\""
                echo "    }"
            fi
        done

        echo "  }"
        echo "}"
    } > "$metadata_file"

    log_success "Library metadata collected: $metadata_file"
}

# Run API compatibility analysis
run_api_analysis() {
    local api_data_file="${DATA_DIR}/api_analysis.json"

    log_info "Running API compatibility analysis..."

    {
        echo "{"
        echo "  \"analysis_timestamp\": \"$(date -Iseconds)\","
        echo "  \"api_compatibility_results\": {"

        local first=true
        local extracted_dir="${PROJECT_ROOT}/src/extracted"

        for lib_dir in "$extracted_dir"/*; do
            if [[ -d "$lib_dir" ]]; then
                local lib_name=$(basename "$lib_dir")

                if [[ "$first" == "false" ]]; then
                    echo ","
                fi
                first=false

                echo "    \"$lib_name\": {"

                # Run API compatibility test
                if [[ -x "${SCRIPT_DIR}/test-api-compatibility.sh" ]]; then
                    local test_output="$("${SCRIPT_DIR}/test-api-compatibility.sh" -t signature "$lib_name" 2>/dev/null || true)"
                    local exit_code=$?

                    echo "      \"test_executed\": true,"
                    echo "      \"exit_code\": $exit_code,"

                    if [[ $exit_code -eq 0 ]]; then
                        echo "      \"status\": \"passed\","
                        echo "      \"message\": \"API compatibility test passed\""
                    else
                        echo "      \"status\": \"failed\","
                        echo "      \"message\": \"API compatibility test failed\""
                    fi
                else
                    echo "      \"test_executed\": false,"
                    echo "      \"status\": \"skipped\","
                    echo "      \"message\": \"API compatibility test script not available\""
                fi

                echo "    }"
            fi
        done

        echo "  }"
        echo "}"
    } > "$api_data_file"

    log_success "API analysis completed: $api_data_file"
}

# Run ABI compatibility analysis
run_abi_analysis() {
    local abi_data_file="${DATA_DIR}/abi_analysis.json"

    log_info "Running ABI compatibility analysis..."

    {
        echo "{"
        echo "  \"analysis_timestamp\": \"$(date -Iseconds)\","
        echo "  \"abi_compatibility_results\": {"

        local first=true
        local extracted_dir="${PROJECT_ROOT}/src/extracted"

        for lib_dir in "$extracted_dir"/*; do
            if [[ -d "$lib_dir" ]]; then
                local lib_name=$(basename "$lib_dir")

                if [[ "$first" == "false" ]]; then
                    echo ","
                fi
                first=false

                echo "    \"$lib_name\": {"

                # Run ABI compatibility test
                if [[ -x "${SCRIPT_DIR}/validate-abi-compatibility.sh" ]]; then
                    local test_output="$("${SCRIPT_DIR}/validate-abi-compatibility.sh" -l "$lib_name" 2>/dev/null || true)"
                    local exit_code=$?

                    echo "      \"test_executed\": true,"
                    echo "      \"exit_code\": $exit_code,"

                    if [[ $exit_code -eq 0 ]]; then
                        echo "      \"status\": \"passed\","
                        echo "      \"message\": \"ABI compatibility test passed\""
                    else
                        echo "      \"status\": \"failed\","
                        echo "      \"message\": \"ABI compatibility test failed\""
                    fi
                else
                    echo "      \"test_executed\": false,"
                    echo "      \"status\": \"skipped\","
                    echo "      \"message\": \"ABI compatibility test script not available\""
                fi

                echo "    }"
            fi
        done

        echo "  }"
        echo "}"
    } > "$abi_data_file"

    log_success "ABI analysis completed: $abi_data_file"
}

# Run regression test analysis
run_regression_analysis() {
    local regression_data_file="${DATA_DIR}/regression_analysis.json"

    log_info "Running regression test analysis..."

    {
        echo "{"
        echo "  \"analysis_timestamp\": \"$(date -Iseconds)\","
        echo "  \"regression_test_results\": {"

        # Check if regression test results exist
        local latest_report=$(find "$REPORTS_DIR" -name "*regression_report_*.md" -type f -printf '%T@%p\n' 2>/dev/null | sort -nr | head -1 | cut -d@ -f2- || echo "")

        if [[ -n "$latest_report" && -f "$latest_report" ]]; then
            echo "    \"test_executed\": true,"
            echo "    \"latest_report\": \"$(basename "$latest_report")\","

            # Extract results from report
            local passed=$(grep "Tests Passed:" "$latest_report" | awk '{print $3}' || echo 0)
            local failed=$(grep "Tests Failed:" "$latest_report" | awk '{print $3}' || echo 0)
            local skipped=$(grep "Tests Skipped:" "$latest_report" | awk '{print $3}' || echo 0)

            echo "    \"tests_passed\": $passed,"
            echo "    \"tests_failed\": $failed,"
            echo "    \"tests_skipped\": $skipped,"
            echo "    \"total_tests\": $((passed + failed + skipped)),"

            if [[ $failed -eq 0 ]]; then
                echo "    \"status\": \"passed\","
                echo "    \"message\": \"All regression tests passed\""
            else
                echo "    \"status\": \"failed\","
                echo "    \"message\": \"$failed regression tests failed\""
            fi
        else
            echo "    \"test_executed\": false,"
            echo "    \"status\": \"not_available\","
            echo "    \"message\": \"No regression test results found\""
        fi

        echo "  }"
        echo "}"
    } > "$regression_data_file"

    log_success "Regression analysis completed: $regression_data_file"
}

# Calculate compatibility scores
calculate_compatibility_scores() {
    local scores_file="${DATA_DIR}/compatibility_scores.json"

    log_info "Calculating compatibility scores..."

    {
        echo "{"
        echo "  \"calculation_timestamp\": \"$(date -Iseconds)\","
        echo "  \"overall_scores\": {"

        # Load data files
        local metadata_file="${DATA_DIR}/library_metadata.json"
        local api_file="${DATA_DIR}/api_analysis.json"
        local abi_file="${DATA_DIR}/abi_analysis.json"
        local regression_file="${DATA_DIR}/regression_analysis.json"

        if [[ -f "$metadata_file" && -f "$api_file" && -f "$abi_file" ]]; then
            echo "    \"libraries_analyzed\": true,"

            # Extract library names from metadata
            local libraries=($(jq -r '.libraries | keys[]' "$metadata_file" 2>/dev/null || echo ""))

            echo "    \"library_scores\": {"

            local first=true
            for lib in "${libraries[@]}"; do
                if [[ "$first" == "false" ]]; then
                    echo ","
                fi
                first=false

                echo "      \"$lib\": {"

                # Get library metadata
                local attribution=$(jq -r ".libraries[\"$lib\"].attribution_coverage // 0" "$metadata_file" 2>/dev/null || echo 0)
                local source_files=$(jq -r ".libraries[\"$lib\"].source_files // 0" "$metadata_file" 2>/dev/null || echo 0)

                # Get API test result
                local api_status=$(jq -r ".api_compatibility_results[\"$lib\"].status // \"unknown\"" "$api_file" 2>/dev/null || echo "unknown")

                # Get ABI test result
                local abi_status=$(jq -r ".abi_compatibility_results[\"$lib\"].status // \"unknown\"" "$abi_file" 2>/dev/null || echo "unknown")

                # Calculate individual scores
                local attribution_score=0
                if (( $(echo "$attribution >= 95" | bc -l) )); then
                    attribution_score=100
                else
                    attribution_score=$(echo "scale=0; $attribution" | bc -l)
                fi

                local api_score=0
                [[ "$api_status" == "passed" ]] && api_score=100 || api_score=0

                local abi_score=0
                [[ "$abi_status" == "passed" ]] && abi_score=100 || abi_score=0

                # Calculate overall score (weighted average)
                local overall_score=$(echo "scale=2; ($attribution_score * 0.4 + $api_score * 0.3 + $abi_score * 0.3)" | bc -l)

                echo "        \"attribution_coverage\": $attribution_score,"
                echo "        \"api_compatibility\": $api_score,"
                echo "        \"abi_compatibility\": $abi_score,"
                echo "        \"overall_score\": $overall_score,"
                echo "        \"grade\": \"$(get_grade $overall_score)\","
                echo "        \"source_files\": $source_files"

                echo "      }"
            done

            echo "    },"
        else
            echo "    \"libraries_analyzed\": false,"
            echo "    \"library_scores\": {}"
        fi

        # Overall system scores
        if [[ -f "$regression_file" ]]; then
            local regression_passed=$(jq -r '.regression_test_results.tests_passed // 0' "$regression_file" 2>/dev/null || echo 0)
            local regression_total=$(jq -r '.regression_test_results.total_tests // 1' "$regression_file" 2>/dev/null || echo 1)
            local regression_score=$(echo "scale=2; $regression_passed * 100 / $regression_total" | bc -l)

            echo "    \"regression_score\": $regression_score,"
            echo "    \"regression_grade\": \"$(get_grade $regression_score)\""
        else
            echo "    \"regression_score\": 0,"
            echo "    \"regression_grade\": \"F\""
        fi

        echo "  }"
        echo "}"
    } > "$scores_file"

    log_success "Compatibility scores calculated: $scores_file"
}

# Get grade from score
get_grade() {
    local score="$1"

    if (( $(echo "$score >= 95" | bc -l) )); then
        echo "A"
    elif (( $(echo "$score >= 85" | bc -l) )); then
        echo "B"
    elif (( $(echo "$score >= 75" | bc -l) )); then
        echo "C"
    elif (( $(echo "$score >= 65" | bc -l) )); then
        echo "D"
    else
        echo "F"
    fi
}

# Generate HTML report
generate_html_report() {
    local html_file="${REPORTS_DIR}/compatibility_report_$(date +%Y%m%d_%H%M%S).html"
    local data_file="${DATA_DIR}/compatibility_scores.json"

    log_info "Generating HTML compatibility report..."

    # Check if jq is available for JSON processing
    if ! command -v jq >/dev/null 2>&1; then
        log_warning "jq not available, generating basic HTML report"
        generate_basic_html_report "$html_file"
    else
        generate_detailed_html_report "$html_file" "$data_file"
    fi

    log_success "HTML report generated: $html_file"
}

# Generate basic HTML report (fallback)
generate_basic_html_report() {
    local html_file="$1"

    cat > "$html_file" << 'EOF'
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Compatibility Report</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 40px; line-height: 1.6; }
        .header { background: #f4f4f4; padding: 20px; border-radius: 8px; margin-bottom: 20px; }
        .section { margin-bottom: 30px; padding: 20px; border: 1px solid #ddd; border-radius: 8px; }
        .success { color: #28a745; }
        .warning { color: #ffc107; }
        .error { color: #dc3545; }
        .score { font-size: 2em; font-weight: bold; padding: 10px; border-radius: 5px; text-align: center; margin: 10px 0; }
        .grade-a { background: #d4edda; color: #155724; }
        .grade-b { background: #cce5ff; color: #004085; }
        .grade-c { background: #fff3cd; color: #856404; }
        .grade-d { background: #f8d7da; color: #721c24; }
        .grade-f { background: #f5c6cb; color: #721c24; }
    </style>
</head>
<body>
    <div class="header">
        <h1>Third-Party Dependencies Compatibility Report</h1>
        <p><strong>Generated:</strong> $(date)</p>
        <p><strong>Version:</strong> '$REPORT_VERSION'</p>
        <p><strong>Author:</strong> $REPORT_AUTHOR</p>
    </div>

    <div class="section">
        <h2>Report Summary</h2>
        <p>This compatibility report analyzes the compatibility of integrated third-party libraries in the Puzzle71Solver project.</p>

        <h3>Compatibility Status</h3>
        <div class="score grade-c">
            COMPATIBILITY ANALYSIS COMPLETE
        </div>

        <p><strong>Note:</strong> Detailed JSON reports are available in the reports directory.</p>
    </div>

    <div class="section">
        <h2>Recommendations</h2>
        <ul>
            <li>Review detailed compatibility data in JSON reports</li>
            <li>Address any compatibility issues before library updates</li>
            <li>Maintain comprehensive testing for all library changes</li>
            <li>Monitor compatibility in production deployments</li>
        </ul>
    </div>

    <div class="section">
        <h2>Next Steps</h2>
        <p>1. Review detailed compatibility reports</p>
        <p>2. Address any identified issues</p>
        <p>3. Update integration code as needed</p>
        <p>4. Re-run compatibility tests</p>
    </div>
</body>
</html>
EOF
}

# Generate detailed HTML report with JSON data
generate_detailed_html_report() {
    local html_file="$1"
    local data_file="$2"

    # Extract data using jq
    local overall_scores=$(jq -r '.overall_scores' "$data_file" 2>/dev/null || echo "{}")
    local regression_grade=$(echo "$overall_scores" | jq -r '.regression_grade // "F"' 2>/dev/null || echo "F")
    local regression_score=$(echo "$overall_scores" | jq -r '.regression_score // 0' 2>/dev/null || echo 0)

    cat > "$html_file" << EOF
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>$REPORT_TITLE</title>
    <style>
        body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; margin: 0; padding: 20px; background: #f5f7fa; line-height: 1.6; }
        .container { max-width: 1200px; margin: 0 auto; background: white; border-radius: 12px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); overflow: hidden; }
        .header { background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); color: white; padding: 30px; text-align: center; }
        .header h1 { margin: 0; font-size: 2.5em; font-weight: 300; }
        .header p { margin: 5px 0; opacity: 0.9; }
        .content { padding: 30px; }
        .summary-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(250px, 1fr)); gap: 20px; margin-bottom: 30px; }
        .summary-card { background: #f8f9fa; padding: 20px; border-radius: 8px; border-left: 4px solid #007bff; }
        .summary-card h3 { margin: 0 0 10px 0; color: #333; }
        .summary-card .value { font-size: 2em; font-weight: bold; color: #007bff; }
        .section { margin-bottom: 40px; }
        .section h2 { color: #333; border-bottom: 2px solid #007bff; padding-bottom: 10px; margin-bottom: 20px; }
        .score-display { text-align: center; margin: 20px 0; }
        .score-circle { width: 120px; height: 120px; border-radius: 50%; display: flex; align-items: center; justify-content: center; font-size: 2em; font-weight: bold; margin: 0 auto 20px; }
        .grade-a { background: #28a745; color: white; }
        .grade-b { background: #17a2b8; color: white; }
        .grade-c { background: #ffc107; color: #212529; }
        .grade-d { background: #fd7e14; color: #212529; }
        .grade-f { background: #dc3545; color: white; }
        .library-table { width: 100%; border-collapse: collapse; margin-top: 20px; }
        .library-table th, .library-table td { padding: 12px; text-align: left; border-bottom: 1px solid #dee2e6; }
        .library-table th { background: #f8f9fa; font-weight: 600; color: #495057; }
        .library-table tr:hover { background: #f8f9fa; }
        .score-badge { padding: 4px 8px; border-radius: 4px; font-weight: bold; font-size: 0.9em; }
        .grade-a-badge { background: #d4edda; color: #155724; }
        .grade-b-badge { background: #cce5ff; color: #004085; }
        .grade-c-badge { background: #fff3cd; color: #856404; }
        .grade-d-badge { background: #f8d7da; color: #721c24; }
        .grade-f-badge { background: #f5c6cb; color: #721c24; }
        .recommendations { background: #e9ecef; padding: 20px; border-radius: 8px; margin-top: 30px; }
        .recommendations h3 { margin-top: 0; color: #495057; }
        .footer { text-align: center; padding: 20px; background: #f8f9fa; color: #6c757d; border-top: 1px solid #dee2e6; }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>$REPORT_TITLE</h1>
            <p><strong>Generated:</strong> $(date)</p>
            <p><strong>Version:</strong> $REPORT_VERSION</p>
            <p><strong>Author:</strong> $REPORT_AUTHOR</p>
        </div>

        <div class="content">
            <div class="section">
                <h2>Overall Compatibility Status</h2>
                <div class="summary-grid">
                    <div class="summary-card">
                        <h3>Regression Tests</h3>
                        <div class="value">$regression_score%</div>
                        <div>Grade: <span class="grade-${regression_grade,,grade-f}">$regression_grade</span></div>
                    </div>
                    <div class="summary-card">
                        <h3>Libraries Analyzed</h3>
                        <div class="value">$(jq -r '.overall_scores.library_scores | length' "$data_file" 2>/dev/null || echo 0)</div>
                        <div>Total integrated libraries</div>
                    </div>
                    <div class="summary-card">
                        <h3>Report Generated</h3>
                        <div class="value">$(date +%H:%M)</div>
                        <div>Current time</div>
                    </div>
                </div>

                <div class="score-display">
                    <div class="score-circle grade-${regression_grade,,grade-c}">
                        $regression_score%
                    </div>
                    <h3>Overall Compatibility Grade</h3>
                </div>
            </div>

            <div class="section">
                <h2>Library Compatibility Scores</h2>
                <table class="library-table">
                    <thead>
                        <tr>
                            <th>Library</th>
                            <th>Attribution Coverage</th>
                            <th>API Compatibility</th>
                            <th>ABI Compatibility</th>
                            <th>Overall Score</th>
                            <th>Grade</th>
                        </tr>
                    </thead>
                    <tbody>
EOF

    # Add library rows
    jq -r '.overall_scores.library_scores | to_entries[] | . | @base64' "$data_file" 2>/dev/null | while IFS= read -r encoded; do
        decoded=$(echo "$encoded" | base64 -d 2>/dev/null || echo "")
        lib_name=$(echo "$decoded" | jq -r '.key // "unknown"' 2>/dev/null || echo "unknown")
        attribution=$(echo "$decoded" | jq -r '.value.attribution_coverage // 0' 2>/dev/null || echo 0)
        api_score=$(echo "$decoded" | jq -r '.value.api_compatibility // 0' 2>/dev/null || echo 0)
        abi_score=$(echo "$decoded" | jq -r '.value.abi_compatibility // 0' 2>/dev/null || echo 0)
        overall=$(echo "$decoded" | jq -r '.value.overall_score // 0' 2>/dev/null || echo 0)
        grade=$(echo "$decoded" | jq -r '.value.grade // "F"' 2>/dev/null || echo "F")

        echo "                        <tr>"
        echo "                            <td><strong>$lib_name</strong></td>"
        echo "                            <td>${attribution}%</td>"
        echo "                            <td>${api_score}%</td>"
        echo "                            <td>${abi_score}%</td>"
        echo "                            <td>${overall}%</td>"
        echo "                            <td><span class=\"score-badge grade-${grade,,grade-f}\">$grade</span></td>"
        echo "                        </tr>"
    done >> "$html_file"

    cat >> "$html_file" << 'EOF'
                    </tbody>
                </table>
            </div>

            <div class="recommendations">
                <h3>Recommendations</h3>
                <p><strong>For libraries with compatibility issues:</strong></p>
                <ul>
                    <li>Review detailed compatibility reports</li>
                    <li>Address breaking changes before updating</li>
                    <li>Consider compatibility shims for minor issues</li>
                    <li>Pin library versions if updates cause problems</li>
                </ul>

                <p><strong>For maintaining compatibility:</strong></p>
                <ul>
                    <li>Run compatibility tests before each release</li>
                    <li>Maintain comprehensive test coverage</li>
                    <li>Monitor library deprecation notices</li>
                    <li>Update integration code proactively</li>
                </ul>
            </div>
        </div>

        <div class="footer">
            <p><em>This report was generated automatically by the Puzzle71Solver compatibility reporting system.</em></p>
            <p><strong>Report files:</strong> Detailed JSON data available in $DATA_DIR/</p>
        </div>
    </div>
</body>
</html>
EOF
}

# Generate Markdown report
generate_markdown_report() {
    local md_file="${REPORTS_DIR}/compatibility_report_$(date +%Y%m%d_%H%M%S).md"
    local data_file="${DATA_DIR}/compatibility_scores.json"

    log_info "Generating Markdown compatibility report..."

    {
        echo "# $REPORT_TITLE"
        echo ""
        echo "**Generated**: $(date)"
        echo "**Version**: $REPORT_VERSION"
        echo "**Author**: $REPORT_AUTHOR"
        echo ""
        echo "---"
        echo ""
        echo "## Executive Summary"
        echo ""
        echo "This report provides a comprehensive analysis of third-party library compatibility within the Puzzle71Solver project. The analysis covers API compatibility, ABI compatibility, and regression testing results."
        echo ""

        # Overall scores
        if [[ -f "$data_file" ]]; then
            echo "### Overall Compatibility Status"
            echo ""
            local regression_grade=$(jq -r '.overall_scores.regression_grade // "F"' "$data_file" 2>/dev/null || echo "F")
            local regression_score=$(jq -r '.overall_scores.regression_score // 0' "$data_file" 2>/dev/null || echo 0)

            echo "- **Regression Test Grade**: **$regression_grade** ($regression_score%)"
            echo "- **Libraries Analyzed**: $(jq -r '.overall_scores.library_scores | length' "$data_file" 2>/dev/null || echo 0)"
            echo ""

            echo "### Compatibility Grades"
            echo ""
            echo "| Grade | Score Range | Description |"
            echo "|-------|-------------|-------------|"
            echo "| A | 95-100% | Excellent - No compatibility issues |"
            echo "| B | 85-94%  | Good - Minor issues only |"
            echo "| C | 75-84%  | Acceptable - Some issues requiring attention |"
            echo "| D | 65-74%  | Poor - Significant compatibility issues |"
            echo "| F | 0-64%   | Critical - Major compatibility problems |"
            echo ""

            echo "## Library Compatibility Details"
            echo ""
            echo "| Library | Attribution | API | ABI | Overall | Grade |"
            echo "|---------|------------|-----|-----|--------|-------|"

            jq -r '.overall_scores.library_scores | to_entries[] | . | @base64' "$data_file" 2>/dev/null | while IFS= read -r encoded; do
                decoded=$(echo "$encoded" | base64 -d 2>/dev/null || echo "")
                lib_name=$(echo "$decoded" | jq -r '.key // "unknown"' 2>/dev/null || echo "unknown")
                attribution=$(echo "$decoded" | jq -r '.value.attribution_coverage // 0' 2>/dev/null || echo 0)
                api_score=$(echo "$decoded" | jq -r '.value.api_compatibility // 0' 2>/dev/null || echo 0)
                abi_score=$(echo "$decoded" | jq -r '.value.abi_compatibility // 0' 2>/dev/null || echo 0)
                overall=$(echo "$decoded" | jq -r '.value.overall_score // 0' 2>/dev/null || echo 0)
                grade=$(echo "$decoded" | jq -r '.value.grade // "F"' 2>/dev/null || echo "F")

                echo "| $lib_name | ${attribution}% | ${api_score}% | ${abi_score}% | ${overall}% | **$grade** |"
            done
            echo ""
        fi

        echo "## Test Results Summary"
        echo ""
        echo "### Analysis Components"
        echo "- **API Compatibility Testing**: Validation of API signatures and interface stability"
        echo "- **ABI Compatibility Testing**: Binary compatibility and symbol stability analysis"
        echo "- **Regression Testing**: Comprehensive compatibility regression validation"
        echo "- **Attribution Coverage**: Verification of attribution header completeness"
        echo ""

        echo "## Recommendations"
        echo ""
        echo "### Immediate Actions"
        echo "1. **Review Detailed Reports**: Examine individual library compatibility reports in JSON format"
        echo "2. **Address Failures**: Fix any failing compatibility tests before library updates"
        echo "3. **Update Integration Code**: Modify code to handle breaking changes"
        echo "4. **Re-test Compatibility**: Run compatibility tests after fixes"
        echo ""

        echo "### Ongoing Maintenance"
        echo "1. **Regular Testing**: Run compatibility tests before each release"
        echo "2. **Proactive Monitoring**: Monitor library deprecation notices and updates"
        echo "3. **Version Management**: Consider library version pinning for stability"
        echo "4. **Documentation**: Keep compatibility documentation up to date"
        echo ""

        echo "## Next Steps"
        echo ""
        echo "1. Review detailed JSON reports in \`$DATA_DIR/\`"
        echo "2. Address any identified compatibility issues"
        echo "3. Update integration code as necessary"
        echo "4. Run regression tests to validate fixes"
        echo "5. Monitor compatibility in production deployments"
        echo ""

        echo "---"
        echo ""
        "*This report was generated automatically on $(date) by the Puzzle71Solver compatibility reporting system.*"

    } > "$md_file"

    log_success "Markdown report generated: $md_file"
}

# Main execution function
main() {
    log_info "Starting automated compatibility report generation..."

    # Parse command line arguments
    local format="all"
    local output_dir=""

    while [[ $# -gt 0 ]]; do
        case $1 in
            -h|--help)
                cat << 'EOF
Automated Compatibility Report Generation

Usage: ./generate-compatibility-report.sh [OPTIONS]

OPTIONS:
    -h, --help                     Show this help message
    -f, --format <format>           Report format: html, markdown, all (default: all)
    -o, --output-dir <directory>    Output directory for reports
    -v, --verbose                  Enable verbose output

EXAMPLES:
    ./generate-compatibility-report.sh
    ./generate-compatibility-report.sh -f html
    ./generate-compatibility-report.sh -o /path/to/reports

DESCRIPTION:
    Generates comprehensive compatibility reports for all integrated
    third-party libraries, including API compatibility, ABI compatibility,
    and regression testing results with both HTML and Markdown output formats.

EOF
                exit 0
                ;;
            -f|--format)
                format="$2"
                shift 2
                ;;
            -o|--output-dir)
                output_dir="$2"
                shift 2
                ;;
            -v|--verbose)
                set -x
                shift
                ;;
            -*)
                log_error "Unknown option: $1"
                exit 1
                ;;
            *)
                log_error "Unknown argument: $1"
                exit 1
                ;;
        esac
    done

    # Set output directory if specified
    if [[ -n "$output_dir" ]]; then
        REPORTS_DIR="$output_dir"
        mkdir -p "$REPORTS_DIR"
    fi

    # Initialize
    init_directories

    # Run data collection and analysis
    collect_library_metadata
    run_api_analysis
    run_abi_analysis
    run_regression_analysis
    calculate_compatibility_scores

    # Generate reports
    local generated_files=()

    case "$format" in
        "html")
            generate_html_report
            generated_files+=("HTML")
            ;;
        "markdown"|"md")
            generate_markdown_report
            generated_files+=("Markdown")
            ;;
        "all")
            generate_html_report
            generate_markdown_report
            generated_files+=("HTML and Markdown")
            ;;
        *)
            log_error "Unknown format: $format"
            exit 1
            ;;
    esac

    # Print summary
    echo ""
    log_info "Automated Compatibility Report Generation Complete"
    log_info "================================================="
    echo "Reports generated: ${generated_files[*]}"
    echo "Reports directory: $REPORTS_DIR"
    echo "Data directory: $DATA_DIR"
    echo ""

    # Show latest files
    echo "Latest report files:"
    ls -la "$REPORTS_DIR"/*_$(date +%Y%m%d)* 2>/dev/null | head -5 || echo "No report files found"

    log_success "Compatibility report generation completed successfully!"
}

# Run main function if script is executed directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi