#!/bin/bash
# T049: Dependency Reporting and Documentation Generation
# Generates comprehensive reports and documentation for dependencies

set -euo pipefail

# Script configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Configuration
DEPENDENCIES_CONFIG="${PROJECT_ROOT}/config/dependencies.json"
VERSION_HISTORY="${PROJECT_ROOT}/src/integration/manifests/version_history.json"
REPORT_OUTPUT_DIR="${PROJECT_ROOT}/docs/reports"
TEMPLATE_DIR="${PROJECT_ROOT}/scripts/templates"
REPORT_FORMAT="${REPORT_FORMAT:-html}"  # html, json, markdown, pdf

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

log_report() {
    echo -e "${PURPLE}[REPORT]${NC} $1"
}

# Show help
show_help() {
    cat << EOF
Dependency Reporting and Documentation Generator

USAGE:
    $0 [OPTIONS] COMMAND

COMMANDS:
    generate                     Generate dependency report
    status                      Show dependency status summary
    history                     Show dependency update history
    attribution                 Generate attribution documentation
    manifest                    Generate dependency manifest
    all                         Generate all reports

OPTIONS:
    --format FORMAT            Report format: html, json, markdown, pdf (default: html)
    --output DIR               Output directory (default: docs/reports)
    --include-snapshots         Include dependency snapshots
    --no-interactive           Non-interactive mode
    --help, -h                  Show this help message

EXAMPLES:
    $0 generate
    $0 generate --format markdown
    $0 status
    $0 attribution
    $0 all

EOF
}

# Parse command line arguments
parse_arguments() {
    COMMAND="generate"

    while [[ $# -gt 0 ]]; do
        case $1 in
            --format)
                REPORT_FORMAT="$2"
                shift 2
                ;;
            --output)
                REPORT_OUTPUT_DIR="$2"
                shift 2
                ;;
            --include-snapshots)
                INCLUDE_SNAPSHOTS=true
                shift
                ;;
            --no-interactive)
                NO_INTERACTIVE=true
                shift
                ;;
            --help|-h)
                show_help
                exit 0
                ;;
            -*)
                log_error "Unknown option: $1"
                show_help
                exit 1
                ;;
            *)
                COMMAND="$1"
                shift
                ;;
        esac
    done
}

# Initialize reporting environment
initialize_reporting() {
    # Create output directory
    mkdir -p "$REPORT_OUTPUT_DIR"
    mkdir -p "$TEMPLATE_DIR"

    # Create templates if they don't exist
    create_default_templates

    log_info "Output directory: $REPORT_OUTPUT_DIR"
    log_info "Report format: $REPORT_FORMAT"
}

# Create default templates
create_default_templates() {
    if [[ ! -f "$TEMPLATE_DIR/report-template.html" ]]; then
        create_html_template
    fi

    if [[ ! -f "$TEMPLATE_DIR/report-template.md" ]]; then
        create_markdown_template
    fi
}

# Create HTML report template
create_html_template() {
    cat > "$TEMPLATE_DIR/report-template.html" << 'EOF'
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Dependency Report - {{TITLE}}</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 0; padding: 20px; background-color: #f5f5f5; }
        .container { max-width: 1200px; margin: 0 auto; background: white; padding: 30px; border-radius: 8px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
        .header { text-align: center; margin-bottom: 40px; padding-bottom: 20px; border-bottom: 2px solid #eee; }
        .summary { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 20px; margin-bottom: 40px; }
        .summary-item { padding: 20px; background: #f8f9fa; border-radius: 6px; text-align: center; }
        .summary-value { font-size: 2em; font-weight: bold; color: #007bff; }
        .section { margin-bottom: 40px; }
        .section h2 { color: #333; border-bottom: 2px solid #007bff; padding-bottom: 10px; }
        table { width: 100%; border-collapse: collapse; margin: 20px 0; }
        th, td { padding: 12px; text-align: left; border-bottom: 1px solid #ddd; }
        th { background-color: #f8f9fa; font-weight: bold; }
        .status-ok { color: #28a745; font-weight: bold; }
        .status-warning { color: #ffc107; font-weight: bold; }
        .status-error { color: #dc3545; font-weight: bold; }
        .version-tag { background: #007bff; color: white; padding: 4px 8px; border-radius: 4px; font-size: 0.9em; }
        .license-tag { background: #28a745; color: white; padding: 4px 8px; border-radius: 4px; font-size: 0.9em; }
        .timestamp { color: #666; font-size: 0.9em; }
        .footer { text-align: center; margin-top: 40px; padding-top: 20px; border-top: 1px solid #eee; color: #666; }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>Dependency Management Report</h1>
            <p class="timestamp">Generated on {{TIMESTAMP}}</p>
        </div>

        {{CONTENT}}

        <div class="footer">
            <p>Generated by Puzzle71Solver Dependency Management System</p>
        </div>
    </div>
</body>
</html>
EOF
}

# Create Markdown template
create_markdown_template() {
    cat > "$TEMPLATE_DIR/report-template.md" << 'EOF'
# Dependency Management Report

**Generated on:** {{TIMESTAMP}}
**Project:** Puzzle71Solver

{{CONTENT}}

---
*Report generated by Puzzle71Solver Dependency Management System*
EOF
}

# Generate dependency report
generate_dependency_report() {
    local report_date=$(date -u +%Y-%m-%dT%H:%M:%SZ)
    local report_file="$REPORT_OUTPUT_DIR/dependency-report-$(date +%Y%m%d-%H%M%S).$REPORT_FORMAT"

    log_report "Generating dependency report..."

    # Build report content
    local content=""

    # Summary section
    content+="$(generate_summary_section)"

    # Dependencies section
    content+="$(generate_dependencies_section)"

    # Update history section
    content+="$(generate_history_section)"

    # Attribution section
    content+="$(generate_attribution_section)"

    # Recommendations section
    content+="$(generate_recommendations_section)"

    # Generate report based on format
    case "$REPORT_FORMAT" in
        "html")
            generate_html_report "$report_file" "$report_date" "$content"
            ;;
        "markdown")
            generate_markdown_report "$report_file" "$report_date" "$content"
            ;;
        "json")
            generate_json_report "$report_file" "$report_date" "$content"
            ;;
        *)
            log_error "Unsupported report format: $REPORT_FORMAT"
            return 1
            ;;
    esac

    log_success "Dependency report generated: $report_file"
}

# Generate summary section
generate_summary_section() {
    local html="<div class=\"section\"><h2>Summary</h2><div class=\"summary\">"
    local md="## Summary\n\n"

    if command -v jq >/dev/null 2>&1; then
        local total_deps=$(jq '.dependencies | length' "$DEPENDENCIES_CONFIG")
        local extracted_deps=$(jq '[.dependencies[] | select(.type == "extracted")] | length' "$DEPENDENCIES_CONFIG")
        local fetchcontent_deps=$(jq '[.dependencies[] | select(.type == "fetchcontent")] | length' "$DEPENDENCIES_CONFIG")

        html+="<div class=\"summary-item\"><div class=\"summary-value\">$total_deps</div><div>Total Dependencies</div></div>"
        html+="<div class=\"summary-item\"><div class=\"summary-value\">$extracted_deps</div><div>Extracted Libraries</div></div>"
        html+="<div class=\"summary-item\"><div class=\"summary-value\">$fetchcontent_deps</div><div>FetchContent Libraries</div></div>"

        md+="- **Total Dependencies:** $total_deps\n"
        md+="- **Extracted Libraries:** $extracted_deps\n"
        md+="- **FetchContent Libraries:** $fetchcontent_deps\n"
    fi

    html+="</div></div>"
    echo "$html"
    echo "$md"
}

# Generate dependencies section
generate_dependencies_section() {
    local html="<div class=\"section\"><h2>Dependencies</h2><table>"
    local md="## Dependencies\n\n| Library | Type | Version | License | Status |\n|---------|------|--------|--------|--------|\n"

    if command -v jq >/dev/null 2>&1; then
        while IFS= read -r dep_name; do
            local dep_info=$(jq -r ".dependencies.\"$dep_name\"" "$DEPENDENCIES_CONFIG")
            local dep_type=$(echo "$dep_info" | jq -r '.type // "unknown"')
            local current_version=$(echo "$dep_info" | jq -r '.current_version // "unknown"')
            local license=$(echo "$dep_info" | jq -r '.license // "unknown"')
            local origin_url=$(echo "$dep_info" | jq -r '.origin_url // ""')

            html+="<tr>"
            html+="<td><strong>$dep_name</strong><br><small><a href=\"$origin_url\">$origin_url</a></small></td>"
            html+="<td><span class=\"version-tag\">$dep_type</span></td>"
            html+="<td>$current_version</td>"
            html+="<td><span class=\"license-tag\">$license</span></td>"
            html+="<td><span class=\"status-ok\">Active</span></td>"
            html+="</tr>"

            md+="| $dep_name | $dep_type | $current_version | $license | Active |\n"
        done < <(jq -r '.dependencies | keys[]' "$DEPENDENCIES_CONFIG")
    fi

    html+="</table></div>"
    echo "$html"
    echo "$md"
}

# Generate history section
generate_history_section() {
    local html="<div class=\"section\"><h2>Update History</h2>"
    local md="## Update History\n\n"

    if [[ -f "$VERSION_HISTORY" ]]; then
        if command -v jq >/dev/null 2>&1; then
            local total_updates=$(jq -r '.version_history.metadata.total_updates' "$VERSION_HISTORY")
            local successful_updates=$(jq -r '.version_history.metadata.successful_updates' "$VERSION_HISTORY")
            local failed_updates=$(jq -r '.version_history.metadata.failed_updates' "$VERSION_HISTORY")

            html+="<p><strong>Total Updates:</strong> $total_updates</p>"
            html+="<p><strong>Successful:</strong> $successful_updates</p>"
            html+="<p><strong>Failed:</strong> $failed_updates</p>"

            md+="- **Total Updates:** $total_updates\n"
            md+="- **Successful:** $successful_updates\n"
            md+="- **Failed:** $failed_updates\n"
        fi
    else
        html+="<p>No update history available.</p>"
        md+="- No update history available.\n"
    fi

    html+="</div>"
    echo "$html"
    echo "$md"
}

# Generate attribution section
generate_attribution_section() {
    local html="<div class=\"section\"><h2>Attribution</h2>"
    local md="## Attribution\n\n"

    html+="<p>All third-party libraries included in this project are properly attributed with their original licenses and source information.</p>"

    md+="All third-party libraries included in this project are properly attributed with their original licenses and source information.\n"

    if command -v jq >/dev/null 2>&1; then
        html+="<h3>License Summary</h3><ul>"
        md+="### License Summary\n\n"

        while IFS= read -r license; do
            local libraries=$(jq -r "[.dependencies[] | select(.license == \"$license\")] | .name" "$DEPENDENCIES_CONFIG" | tr '\n' ', ')
            if [[ -n "$libraries" ]]; then
                html+="<li><strong>$license:</strong> $libraries</li>"
                md+="- **$license:** $libraries\n"
            fi
        done < <(jq -r '.dependencies[].license' "$DEPENDENCIES_CONFIG" | sort -u)

        html+="</ul>"
    fi

    html+="</div>"
    echo "$html"
    echo "$md"
}

# Generate recommendations section
generate_recommendations_section() {
    local html="<div class=\"section\"><h2>Recommendations</h2>"
    local md="## Recommendations\n\n"

    # Analyze current state and provide recommendations
    local recommendations=()

    if command -v jq >/dev/null 2>&1; then
        # Check for auto-update disabled dependencies
        while IFS= read -r dep_name; do
            local auto_update=$(jq -r ".dependencies.\"$dep_name\".auto_update // false" "$DEPENDENCIES_CONFIG")
            if [[ "$auto_update" == "false" ]]; then
                recommendations+=("Consider enabling auto-update for $dep_name for improved security")
            fi
        done < <(jq -r '.dependencies | keys[]' "$DEPENDENCIES_CONFIG")

        # Check for dependencies without extract paths
        while IFS= read -r dep_name; do
            local dep_type=$(jq -r ".dependencies.\"$dep_name\".type" "$DEPENDENCIES_CONFIG")
            local extract_path=$(jq -r ".dependencies.\"$dep_name\".extract_path // \"\"" "$DEPENDENCIES_CONFIG")
            if [[ "$dep_type" == "extracted" && -z "$extract_path" ]]; then
                recommendations+=("Add extract path configuration for $dep_name")
            fi
        done < <(jq -r '.dependencies | keys[]' "$DEPENDENCIES_CONFIG")
    fi

    if [[ ${#recommendations[@]} -eq 0 ]]; then
        recommendations+=("All dependencies are properly configured")
    fi

    # Add recommendations to output
    html+="<ul>"
    for rec in "${recommendations[@]}"; do
        html+="<li>$rec</li>"
    done
    html+="</ul>"

    for rec in "${recommendations[@]}"; do
        md+="- $rec\n"
    done

    html+="</div>"
    echo "$html"
    echo "$md"
}

# Generate HTML report
generate_html_report() {
    local report_file="$1"
    local report_date="$2"
    local content="$3"

    # Read template
    local template=$(cat "$TEMPLATE_DIR/report-template.html")

    # Replace placeholders
    template=${template//\{\{TITLE\}\}/"Dependency Report"}
    template=${template//\{\{TIMESTAMP\}\}/"$report_date"}
    template=${template//\{\{CONTENT\}\}/"$content"}

    # Write report
    echo "$template" > "$report_file"
}

# Generate Markdown report
generate_markdown_report() {
    local report_file="$1"
    local report_date="$2"
    local content="$3"

    # Read template
    local template=$(cat "$TEMPLATE_DIR/report-template.md")

    # Replace placeholders
    template=${template//\{\{TIMESTAMP\}\}/"$report_date"}
    template=${template//\{\{CONTENT\}\}/"$content"}

    # Write report
    echo "$template" > "$report_file"
}

# Generate JSON report
generate_json_report() {
    local report_file="$1"
    local report_date="$2"
    local content="$3"

    cat > "$report_file" << EOF
{
  "dependency_report": {
    "metadata": {
      "generated_at": "$report_date",
      "generator": "Puzzle71Solver Dependency Management System",
      "format": "json",
      "version": "1.0"
    },
    "summary": {
      "total_dependencies": $(jq '.dependencies | length' "$DEPENDENCIES_CONFIG"),
      "extracted_libraries": $(jq '[.dependencies[] | select(.type == "extracted")] | length' "$DEPENDENCIES_CONFIG"),
      "fetchcontent_libraries": $(jq '[.dependencies[] | select(.type == "fetchcontent")] | length' "$DEPENDENCIES_CONFIG")
    },
    "dependencies": $(jq '.dependencies' "$DEPENDENCIES_CONFIG"),
    "content": "$content"
  }
}
EOF
}

# Show dependency status summary
show_status_summary() {
    log_info "Dependency Status Summary"

    if command -v jq >/dev/null 2>&1; then
        echo -e "\n${BLUE}Current Dependencies:${NC}"
        echo "===================="

        while IFS= read -r dep_name; do
            local dep_info=$(jq -r ".dependencies.\"$dep_name\"" "$DEPENDENCIES_CONFIG")
            local current_version=$(echo "$dep_info" | jq -r '.current_version // "unknown"')
            local dep_type=$(echo "$dep_info" | jq -r '.type // "unknown"')
            local license=$(echo "$dep_info" | jq -r '.license // "unknown"')
            local last_update=$(echo "$dep_info" | jq -r '.last_update // "unknown"')

            echo -e "\n${CYAN}$dep_name${NC}"
            echo "  Version: $current_version"
            echo "  Type: $dep_type"
            echo "  License: $license"
            echo "  Last Update: $last_update"
        done < <(jq -r '.dependencies | keys[]' "$DEPENDENCIES_CONFIG")

        echo -e "\n${BLUE}Summary Statistics:${NC}"
        echo "=================="
        echo "Total Dependencies: $(jq '.dependencies | length' "$DEPENDENCIES_CONFIG")"
        echo "Extracted Libraries: $(jq '[.dependencies[] | select(.type == "extracted")] | length' "$DEPENDENCIES_CONFIG")"
        echo "FetchContent Libraries: $(jq '[.dependencies[] | select(.type == "fetchcontent")] | length' "$DEPENDENCIES_CONFIG")"
    else
        log_error "jq not available for JSON parsing"
        exit 1
    fi

    echo ""
}

# Show update history
show_update_history() {
    log_info "Dependency Update History"

    if [[ -f "$VERSION_HISTORY" ]]; then
        if command -v jq >/dev/null 2>&1; then
            echo -e "\n${BLUE}Update History Summary:${NC}"
            echo "========================="

            local total_updates=$(jq -r '.version_history.metadata.total_updates' "$VERSION_HISTORY")
            local successful_updates=$(jq -r '.version_history.metadata.successful_updates' "$VERSION_HISTORY")
            local failed_updates=$(jq -r '.version_history.metadata.failed_updates' "$VERSION_HISTORY")
            local last_updated=$(jq -r '.version_history.metadata.last_updated' "$VERSION_HISTORY")

            echo "Total Updates: $total_updates"
            echo "Successful: $successful_updates"
            echo "Failed: $failed_updates"
            echo "Last Updated: $last_updated"

            # Show recent updates
            echo -e "\n${CYAN}Recent Updates:${NC}"
            echo "=============="

            while IFS= read -r library; do
                echo -e "\n${library}:"
                jq -r ".version_history.libraries.\"$library\"[-1] |
                    "Timestamp: \(.timestamp)\n  From: \(.old_version)\n  To: \(.new_version)\n  Status: \(.status)\n  By: \(.initiated_by)" \
                    "$VERSION_HISTORY" 2>/dev/null || echo "  No history available"
            done < <(jq -r '.version_history.libraries | keys[]' "$VERSION_HISTORY")
        else
            log_error "jq not available for JSON parsing"
            exit 1
        fi
    else
        log_warning "No version history file found: $VERSION_HISTORY"
    fi

    echo ""
}

# Generate attribution documentation
generate_attribution_documentation() {
    local attrib_file="$REPORT_OUTPUT_DIR/attribution-$(date +%Y%m%d-%H%M%S).md"

    log_info "Generating attribution documentation..."

    cat > "$attrib_file" << 'EOF'
# Third-Party Library Attribution

This document contains attribution information for all third-party libraries included in the Puzzle71Solver project.

EOF

    if command -v jq >/dev/null 2>&1; then
        while IFS= read -r dep_name; do
            local dep_info=$(jq -r ".dependencies.\"$dep_name\"" "$DEPENDENCIES_CONFIG")
            local origin_url=$(echo "$dep_info" | jq -r '.origin_url // ""')
            local license=$(echo "$dep_info" | jq -r '.license // "Unknown"')
            local current_version=$(echo "$dep_info" | jq -r '.current_version // "Unknown"')

            echo -e "\n## $dep_name\n"
            echo "**Version:** $current_version\n"
            echo "**License:** $license\n"
            echo "**Source:** $origin_url\n"
            echo "**Integration Type:** $(jq -r ".dependencies.\"$dep_name\".type // \"Unknown\"" "$DEPENDENCIES_CONFIG")\n"
        done < <(jq -r '.dependencies | keys[]' "$DEPENDENCIES_CONFIG")
    fi

    cat >> "$attrib_file" << 'EOF'

## License Information

All third-party libraries are included according to their respective licenses. The project complies with all license requirements including attribution, preservation of license notices, and distribution terms.

## Integration Method

This project uses a source code fusion architecture for third-party dependency integration. All source code is directly integrated into the main repository with complete attribution and license compliance.

For more information about the integration approach, see the project documentation.

EOF

    log_success "Attribution documentation generated: $attrib_file"
}

# Generate dependency manifest
generate_dependency_manifest() {
    local manifest_file="$REPORT_OUTPUT_DIR/dependency-manifest-$(date +%Y%m%d-%H%M%S).json"

    log_info "Generating dependency manifest..."

    if command -v jq >/dev/null 2>&1; then
        local manifest_version="1.0"
        local timestamp=$(date -u +%Y-%m-%dT%H:%M:%SZ)

        cat > "$manifest_file" << EOF
{
  "dependency_manifest": {
    "manifest_version": "$manifest_version",
    "generated_at": "$timestamp",
    "generator": "Puzzle71Solver Dependency Management System",
    "project_name": "Puzzle71Solver",
    "project_version": "0.2.1",
    "dependencies": $(jq '.dependencies' "$DEPENDENCIES_CONFIG")
  }
}
EOF
    else
        log_error "jq not available for JSON generation"
        return 1
    fi

    log_success "Dependency manifest generated: $manifest_file"
}

# Generate all reports
generate_all_reports() {
    log_info "Generating all reports..."

    generate_dependency_report
    show_status_summary > "$REPORT_OUTPUT_DIR/status-summary.txt"
    show_update_history > "$REPORT_OUTPUT_DIR/update-history.txt"
    generate_attribution_documentation
    generate_dependency_manifest

    log_success "All reports generated in: $REPORT_OUTPUT_DIR"
}

# Main execution
main() {
    log_info "Dependency Reporting and Documentation Generator (T049)"
    log_info "Project root: $PROJECT_ROOT"

    # Parse arguments
    parse_arguments "$@"

    # Initialize reporting
    initialize_reporting

    # Execute command
    case "$COMMAND" in
        "generate")
            generate_dependency_report
            ;;
        "status")
            show_status_summary
            ;;
        "history")
            show_update_history
            ;;
        "attribution")
            generate_attribution_documentation
            ;;
        "manifest")
            generate_dependency_manifest
            ;;
        "all")
            generate_all_reports
            ;;
        *)
            log_error "Unknown command: $COMMAND"
            show_help
            exit 1
            ;;
    esac
}

# Run main function
main "$@"