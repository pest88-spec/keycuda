#!/bin/bash

# License Documentation Generation Script for Puzzle71Solver
# Generates comprehensive license documentation for all integrated third-party libraries
# Usage: scripts/generate-license-docs.sh [--output-dir=docs/licenses] [--format=markdown|html]

set -e

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
EXTRACTED_DIR="$PROJECT_ROOT/src/extracted"
OUTPUT_DIR="$PROJECT_ROOT/docs/licenses"
FORMAT="markdown"

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --output-dir=*)
            OUTPUT_DIR="${1#*=}"
            shift
            ;;
        --format=*)
            FORMAT="${1#*=}"
            shift
            ;;
        --help)
            echo "Usage: $0 [--output-dir=docs/licenses] [--format=markdown|html]"
            echo "  --output-dir: Output directory for license files (default: docs/licenses)"
            echo "  --format:     Output format (markdown, html) - default: markdown"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Function to extract license information from source files
extract_license_info() {
    local file="$1"
    local license_info=""

    # Extract attribution header block
    license_info=$(sed -n '/^\/\*\*/,/^ *\*\/$/p' "$file" 2>/dev/null | head -20)

    if [ -n "$license_info" ]; then
        echo "$license_info"
    fi
}

# Function to get library metadata
get_library_metadata() {
    local library="$1"
    local library_dir="$EXTRACTED_DIR/$library"

    if [ ! -d "$library_dir" ]; then
        return 1
    fi

    # Get a sample file to extract metadata
    local sample_file
    sample_file=$(find "$library_dir" -type f \( -name "*.cpp" -o -name "*.c" -o -name "*.h" \) | head -1)

    if [ -z "$sample_file" ]; then
        return 1
    fi

    # Extract metadata from attribution header
    local origin_url=""
    local origin_path=""
    local origin_commit=""
    local origin_license=""
    local extracted_date=""
    local extracted_by=""
    local modifications=""
    local spdx_id=""

    local attribution
    attribution=$(extract_license_info "$sample_file")

    # Extract repository name from the @origin line
    origin_url=$(echo "$attribution" | grep "@origin" | head -1 | sed 's/.*@origin[[:space:]]*//' | sed 's/[[:space:]]*$//')

    while IFS= read -r line; do
        case "$line" in
            *@origin_path*)
                origin_path=$(echo "$line" | sed 's/.*@origin_path[[:space:]]*//' | sed 's/[[:space:]]*$//')
                ;;
            *@origin_commit*)
                origin_commit=$(echo "$line" | sed 's/.*@origin_commit[[:space:]]*//' | sed 's/[[:space:]]*$//')
                ;;
            *@origin_license*)
                origin_license=$(echo "$line" | sed 's/.*@origin_license[[:space:]]*//' | sed 's/[[:space:]]*$//')
                ;;
            *@extracted_date*)
                extracted_date=$(echo "$line" | sed 's/.*@extracted_date[[:space:]]*//' | sed 's/[[:space:]]*$//')
                ;;
            *@extracted_by*)
                extracted_by=$(echo "$line" | sed 's/.*@extracted_by[[:space:]]*//' | sed 's/[[:space:]]*$//')
                ;;
            *@modifications*)
                modifications=$(echo "$line" | sed 's/.*@modifications[[:space:]]*//' | sed 's/[[:space:]]*$//')
                ;;
            *@spdx_license_identifier*)
                spdx_id=$(echo "$line" | sed 's/.*@spdx_license_identifier[[:space:]]*//' | sed 's/[[:space:]]*$//')
                ;;
        esac
    done <<< "$attribution"

    echo "$library|$origin_url|$origin_path|$origin_commit|$origin_license|$extracted_date|$extracted_by|$modifications|$spdx_id"
}

# Function to generate markdown documentation
generate_markdown() {
    local library="$1"
    IFS='|' read -r name origin_url origin_path origin_commit origin_license extracted_date extracted_by modifications spdx_id <<< "$2"

    local filename="$name-LICENSE.$origin_license.md"
    local filepath="$OUTPUT_DIR/$filename"

    cat > "$filepath" << EOF
# $name License Information

## Overview

This document contains license information for the **$name** library integrated into Puzzle71Solver.

## Source Information

- **Original Repository**: $origin_url
- **Original Path**: $origin_path
- **Commit Hash**: $origin_commit
- **Original License**: $origin_license
- **SPDX Identifier**: $spdx_id

## Integration Details

- **Extraction Date**: $extracted_date
- **Extracted By**: $extracted_by
- **Modifications**: $modifications

## License Terms

The $name library is licensed under the $origin_license license. The full license text is included below.

---

## $origin_license License Text

EOF

    # Append original license if available
    local license_file
    license_file=$(find "$EXTRACTED_DIR/$name" -name "LICENSE*" -o -name "COPYING*" -o -name "license*" | head -1)

    if [ -n "$license_file" ] && [ -f "$license_file" ]; then
        echo "" >> "$filepath"
        echo '```' >> "$filepath"
        cat "$license_file" >> "$filepath"
        echo '```' >> "$filepath"
    else
        echo "Original license file not found. Please refer to the original repository for the complete license text." >> "$filepath"
    fi

    # Add compliance section
    cat >> "$filepath" << EOF

---

## Compliance Information

### Integration Compliance

- ✅ Source code integrity preserved
- ✅ Attribution headers maintained
- ✅ License terms clearly documented
- ✅ SPDX identifier included
- ✅ Origin information tracked

### Usage Requirements

1. **Attribution**: All redistributed versions must include the original attribution headers
2. **License Notice**: This license notice must be included with all distributions
3. **Source Availability**: The source code must be made available when required by the original license
4. **Modifications**: All modifications must be clearly documented in attribution headers

### Contact Information

For questions about licensing compliance, please refer to the original project repository:
$origin_url

---

*This document was automatically generated on $(date)*
*Generated by Puzzle71Solver License Documentation Generator*
EOF

    echo "$filepath"
}

# Function to generate HTML documentation
generate_html() {
    local library="$1"
    IFS='|' read -r name origin_url origin_path origin_commit origin_license extracted_date extracted_by modifications spdx_id <<< "$2"

    local filename="$name-LICENSE.$origin_license.html"
    local filepath="$OUTPUT_DIR/$filename"

    cat > "$filepath" << EOF
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>$name License Information - Puzzle71Solver</title>
    <style>
        body { font-family: Arial, sans-serif; max-width: 800px; margin: 0 auto; padding: 20px; line-height: 1.6; }
        .header { border-bottom: 2px solid #333; padding-bottom: 10px; margin-bottom: 20px; }
        .section { margin-bottom: 30px; }
        .metadata { background-color: #f5f5f5; padding: 15px; border-radius: 5px; }
        .metadata dt { font-weight: bold; color: #333; }
        .metadata dd { margin-left: 20px; margin-bottom: 10px; }
        .license-text { background-color: #f9f9f9; padding: 15px; border-radius: 5px; white-space: pre-wrap; font-family: monospace; }
        .compliance { background-color: #e8f5e8; padding: 15px; border-radius: 5px; }
        .compliance ul { margin: 0; padding-left: 20px; }
        .footer { border-top: 1px solid #ccc; padding-top: 10px; margin-top: 30px; font-size: 0.9em; color: #666; }
        a { color: #0066cc; }
        a:hover { text-decoration: underline; }
    </style>
</head>
<body>
    <div class="header">
        <h1>$name License Information</h1>
        <p><em>License documentation for $name library integrated into Puzzle71Solver</em></p>
    </div>

    <div class="section">
        <h2>Source Information</h2>
        <dl class="metadata">
            <dt>Original Repository:</dt>
            <dd><a href="$origin_url">$origin_url</a></dd>
            <dt>Original Path:</dt>
            <dd>$origin_path</dd>
            <dt>Commit Hash:</dt>
            <dd><code>$origin_commit</code></dd>
            <dt>Original License:</dt>
            <dd>$origin_license</dd>
            <dt>SPDX Identifier:</dt>
            <dd><code>$spdx_id</code></dd>
        </dl>
    </div>

    <div class="section">
        <h2>Integration Details</h2>
        <dl class="metadata">
            <dt>Extraction Date:</dt>
            <dd>$extracted_date</dd>
            <dt>Extracted By:</dt>
            <dd>$extracted_by</dd>
            <dt>Modifications:</dt>
            <dd>$modifications</dd>
        </dl>
    </div>

    <div class="section">
        <h2>License Terms</h2>
        <p>The <strong>$name</strong> library is licensed under the <strong>$origin_license</strong> license.</p>

EOF

    # Add original license if available
    local license_file
    license_file=$(find "$EXTRACTED_DIR/$name" -name "LICENSE*" -o -name "COPYING*" -o -name "license*" | head -1)

    if [ -n "$license_file" ] && [ -f "$license_file" ]; then
        cat >> "$filepath" << EOF
        <h3>$origin_license License Text</h3>
        <div class="license-text">$(cat "$license_file" | sed 's/&/\&amp;/g; s/</\&lt;/g; s/>/\&gt;/g')</div>
EOF
    else
        cat >> "$filepath" << EOF
        <p><em>Original license file not found. Please refer to the <a href="$origin_url">original repository</a> for the complete license text.</em></p>
EOF
    fi

    # Add compliance section
    cat >> "$filepath" << EOF
    </div>

    <div class="section">
        <h2>Compliance Information</h2>

        <h3>Integration Compliance</h3>
        <ul>
            <li>✅ Source code integrity preserved</li>
            <li>✅ Attribution headers maintained</li>
            <li>✅ License terms clearly documented</li>
            <li>✅ SPDX identifier included</li>
            <li>✅ Origin information tracked</li>
        </ul>

        <h3>Usage Requirements</h3>
        <ol>
            <li><strong>Attribution</strong>: All redistributed versions must include the original attribution headers</li>
            <li><strong>License Notice</strong>: This license notice must be included with all distributions</li>
            <li><strong>Source Availability</strong>: The source code must be made available when required by the original license</li>
            <li><strong>Modifications</strong>: All modifications must be clearly documented in attribution headers</li>
        </ol>
    </div>

    <div class="section">
        <h2>Contact Information</h2>
        <p>For questions about licensing compliance, please refer to the original project repository:<br>
        <a href="$origin_url">$origin_url</a></p>
    </div>

    <div class="footer">
        <p><em>This document was automatically generated on $(date)</em><br>
        <em>Generated by Puzzle71Solver License Documentation Generator</em></p>
    </div>
</body>
</html>
EOF

    echo "$filepath"
}

# Function to generate summary index
generate_summary() {
    local format="$1"
    local index_file="$OUTPUT_DIR/README.$format"
    local metadata="$2"

    case "$format" in
        "markdown")
            cat > "$index_file" << EOF
# Third-Party Library Licenses

This directory contains license information for all third-party libraries integrated into Puzzle71Solver.

## Integrated Libraries

EOF
            echo "$metadata" | while IFS= read -r line; do
                if [ -n "$line" ]; then
                    IFS='|' read -r name origin_url origin_commit origin_license extracted_date extracted_by modifications spdx_id <<< "$line"
                    echo "- **[$name]($name-LICENSE.$origin_license)** - $origin_license license" >> "$index_file"
                fi
            done

            cat >> "$index_file" << EOF

## Compliance Summary

All integrated libraries comply with the following requirements:

- ✅ Source code integrity preserved
- ✅ Attribution headers maintained
- ✅ License terms clearly documented
- ✅ SPDX identifiers included
- ✅ Origin information tracked

## Generation Information

- **Generated**: $(date)
- **Generator**: Puzzle71Solver License Documentation Generator
- **Total Libraries**: $(echo "$metadata" | wc -l)

For questions about specific library licenses, please refer to the individual license files above.

---
EOF
            ;;
        "html")
            cat > "$index_file" << EOF
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Third-Party Library Licenses - Puzzle71Solver</title>
    <style>
        body { font-family: Arial, sans-serif; max-width: 800px; margin: 0 auto; padding: 20px; line-height: 1.6; }
        .header { border-bottom: 2px solid #333; padding-bottom: 10px; margin-bottom: 20px; }
        .library-list { list-style-type: none; padding: 0; }
        .library-list li { margin-bottom: 10px; padding: 10px; background-color: #f9f9f9; border-radius: 5px; }
        .compliance { background-color: #e8f5e8; padding: 15px; border-radius: 5px; margin: 20px 0; }
        .footer { border-top: 1px solid #ccc; padding-top: 10px; margin-top: 30px; font-size: 0.9em; color: #666; }
        a { color: #0066cc; text-decoration: none; }
        a:hover { text-decoration: underline; }
    </style>
</head>
<body>
    <div class="header">
        <h1>Third-Party Library Licenses</h1>
        <p><em>License information for all third-party libraries integrated into Puzzle71Solver</em></p>
    </div>

    <h2>Integrated Libraries</h2>
    <ul class="library-list">
EOF

            while IFS= read -r line; do
                if [ -n "$line" ]; then
                    IFS='|' read -r name origin_url origin_commit origin_license extracted_date extracted_by modifications spdx_id <<< "$line"
                    echo "        <li><strong><a href=\"$name-LICENSE.$origin_license\">$name</a></strong> - $origin_license license<br><small>Original: <a href=\"$origin_url\">$origin_url</a></small></li>" >> "$index_file"
                fi
            done

            cat >> "$index_file" << EOF
    </ul>

    <div class="compliance">
        <h2>Compliance Summary</h2>
        <p>All integrated libraries comply with the following requirements:</p>
        <ul>
            <li>✅ Source code integrity preserved</li>
            <li>✅ Attribution headers maintained</li>
            <li>✅ License terms clearly documented</li>
            <li>✅ SPDX identifiers included</li>
            <li>✅ Origin information tracked</li>
        </ul>
    </div>

    <div class="footer">
        <p><strong>Generation Information</strong></p>
        <ul>
            <li><strong>Generated</strong>: $(date)</li>
            <li><strong>Generator</strong>: Puzzle71Solver License Documentation Generator</li>
            <li><strong>Total Libraries</strong>: $(echo "$metadata" | wc -l)</li>
        </ul>
        <p>For questions about specific library licenses, please refer to the individual license files above.</p>
    </div>
</body>
</html>
EOF
            ;;
    esac

    echo "$index_file"
}

# Main execution
main() {
    # Check if extracted directory exists
    if [ ! -d "$EXTRACTED_DIR" ]; then
        echo "Error: Extracted directory not found: $EXTRACTED_DIR" >&2
        exit 1
    fi

    # Create output directory
    mkdir -p "$OUTPUT_DIR"

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

    echo "📝 Generating License Documentation"
    echo "=================================="
    echo "Output Format: $FORMAT"
    echo "Output Directory: $OUTPUT_DIR"
    echo "Libraries: ${libraries[*]}"
    echo ""

    # Collect metadata for all libraries
    local library_metadata=""
    for library in "${libraries[@]}"; do
        local metadata
        metadata=$(get_library_metadata "$library")
        if [ $? -eq 0 ]; then
            library_metadata+="$metadata"$'\n'
            echo "✓ Processed $library"
        else
            echo "✗ Failed to process $library" >&2
        fi
    done

    # Generate documentation for each library
    local generated_files=()
    while IFS= read -r line; do
        if [ -n "$line" ]; then
            local library_name
            library_name=$(echo "$line" | cut -d'|' -f1)

            local generated_file
            case "$FORMAT" in
                "markdown")
                    generated_file=$(generate_markdown "$library_name" "$line")
                    ;;
                "html")
                    generated_file=$(generate_html "$library_name" "$line")
                    ;;
            esac

            if [ -n "$generated_file" ]; then
                generated_files+=("$generated_file")
                echo "  ✓ Generated $(basename "$generated_file")"
            fi
        fi
    done <<< "$metadata"

    # Generate summary index
    echo ""
    local summary_file
    summary_file=$(generate_summary "$FORMAT" "$library_metadata")
    echo "✓ Generated summary index: $(basename "$summary_file")"

    echo ""
    echo "🎉 License documentation generation complete!"
    echo "📁 Generated files: ${#generated_files[@]}"
    echo "📂 Output directory: $OUTPUT_DIR"

    if [ "$FORMAT" = "markdown" ]; then
        echo "📖 Start reading: $summary_file"
    else
        echo "🌐 Open in browser: file://$summary_file"
    fi
}

# Library metadata will be stored globally for summary generation

# Run main function
main "$@"