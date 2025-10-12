#!/bin/bash

# Technical Debt Scanner
# Scans codebase for TODO/FIXME/PLACEHOLDER/HACK markers
# Excludes build dependencies and third-party libraries

set -euo pipefail

# Colors for output
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
GREEN='\033[0;32m'
NC='\033[0m' # No Color

# Directories to exclude
EXCLUDE_DIRS=(
    "build"
    "_deps"
    "third_party"
    "src/extracted"
    ".git"
    "CMakeFiles"
)

# File patterns to scan
FILE_PATTERNS=(
    "*.cpp"
    "*.cu"
    "*.cuh"
    "*.h"
    "*.hpp"
    "*.c"
)

# Technical debt markers to search for
MARKERS=(
    "TODO"
    "FIXME"
    "PLACEHOLDER"
    "HACK"
    "XXX"
    "BUG"
    "TEMP"
)

echo -e "${BLUE}Technical Debt Scanner${NC}"
echo "========================"
echo

# Build find command with exclusions
FIND_CMD="find . -type f \\( "
for i in "${!FILE_PATTERNS[@]}"; do
    if [ $i -gt 0 ]; then
        FIND_CMD+=" -o"
    fi
    FIND_CMD+=" -name \"${FILE_PATTERNS[$i]}\""
done
FIND_CMD+=" \\)"

# Add exclusions
for dir in "${EXCLUDE_DIRS[@]}"; do
    FIND_CMD+=" ! -path \"./$dir/*\""
done

echo -e "${YELLOW}Scanning project files...${NC}"
echo

# Counter variables
declare -A marker_counts
declare -A file_counts
total_markers=0

# Initialize counters
for marker in "${MARKERS[@]}"; do
    marker_counts[$marker]=0
done

# Temporary file for results
TEMP_RESULTS=$(mktemp)

# Execute scan
eval "$FIND_CMD" | while read -r file; do
    # Skip if file doesn't exist or isn't readable
    [ ! -f "$file" ] || [ ! -r "$file" ] && continue

    # Check each marker
    for marker in "${MARKERS[@]}"; do
        # Use grep to find matches with line numbers
        matches=$(grep -n -E "\b$marker\b" "$file" 2>/dev/null || true)
        if [ -n "$matches" ]; then
            echo "$file:$marker" >> "$TEMP_RESULTS"
            # Count occurrences
            count=$(echo "$matches" | wc -l)
            echo "$file:$marker:$count" >> "${TEMP_RESULTS}.counts"
        fi
    done
done

# Process results
echo -e "${GREEN}Technical Debt Summary${NC}"
echo "========================="
echo

# Create summary
if [ -f "$TEMP_RESULTS" ]; then
    # Group by marker type
    for marker in "${MARKERS[@]}"; do
        count=0
        if [ -f "${TEMP_RESULTS}.counts" ]; then
            marker_total=$(awk -F: -v marker="$marker" '$2 == marker {sum += $3} END {print sum+0}' "${TEMP_RESULTS}.counts")
            count=$marker_total
        fi
        marker_counts[$marker]=$count
        total_markers=$((total_markers + count))

        if [ $count -gt 0 ]; then
            printf "${RED}%s${NC}: %d occurrences\n" "$marker" "$count"
        fi
    done

    echo
    printf "${BLUE}Total Technical Debt Items: %d${NC}\n" "$total_markers"
    echo

    # Show files with most technical debt
    echo -e "${YELLOW}Files with Most Technical Debt:${NC}"
    echo "-----------------------------------"

    if [ -f "${TEMP_RESULTS}.counts" ]; then
        # Group by file and count total markers
        awk -F: '{
            files[$1] += $3
        } END {
            for (file in files) {
                printf "%d:%s\n", files[file], file
            }
        }' "${TEMP_RESULTS}.counts" | sort -nr | head -10 | while IFS=: read -r count file; do
            printf "${RED}%d${NC} markers in %s\n" "$count" "$file"
        done
    fi

    echo
    echo -e "${YELLOW}Detailed Breakdown by File:${NC}"
    echo "------------------------------"

    # Group by marker and show files
    for marker in "${MARKERS[@]}"; do
        if [ ${marker_counts[$marker]} -gt 0 ]; then
            echo
            printf "${RED}%s:${NC}\n" "$marker"
            grep ":$marker$" "$TEMP_RESULTS" | sed "s/:$marker$//" | while read -r file; do
                # Show actual lines with context
                echo "  $file:"
                grep -n -E "\b$marker\b" "$file" | sed 's/^/    /'
            done
        fi
    done

    echo
    echo -e "${YELLOW}Technical Debt Hotspots:${NC}"
    echo "---------------------------"

    # Identify directories with most technical debt
    if [ -f "${TEMP_RESULTS}.counts" ]; then
        awk -F: '{
            # Extract directory from file path
            match($1, /^(.*\/)/, arr)
            if (arr[1] != "") {
                dirs[arr[1]] += $3
            } else {
                dirs["./"] += $3
            }
        } END {
            for (dir in dirs) {
                printf "%d:%s\n", dirs[dir], dir
            }
        }' "${TEMP_RESULTS}.counts" | sort -nr | head -5 | while IFS=: read -r count dir; do
            printf "${RED}%d${NC} markers in %s\n" "$count" "$dir"
        done
    fi

else
    echo -e "${GREEN}No technical debt markers found!${NC}"
fi

echo
echo -e "${BLUE}Technical Debt Analysis${NC}"
echo "========================="
echo

# Analysis based on current findings
if [ $total_markers -eq 0 ]; then
    echo -e "${GREEN}✅ Excellent: No technical debt markers found in project code${NC}"
    echo "The codebase appears to be free of TODO/FIXME/PLACEHOLDER/HACK markers."
elif [ $total_markers -le 10 ]; then
    echo -e "${YELLOW}⚠️  Low technical debt: $total_markers items found${NC}"
    echo "The codebase has minimal technical debt. Consider addressing these items soon."
elif [ $total_markers -le 50 ]; then
    echo -e "${RED}🔶 Moderate technical debt: $total_markers items found${NC}"
    echo "The codebase has accumulated significant technical debt. Plan remediation."
else
    echo -e "${RED}❌ High technical debt: $total_markers items found${NC}"
    echo "The codebase has extensive technical debt. Immediate attention required."
fi

echo
echo -e "${YELLOW}Recommendations:${NC}"
echo "=================="

if [ $total_markers -gt 0 ]; then
    echo "1. Prioritize FIXME markers (potential bugs)"
    echo "2. Address TODO items in critical paths"
    echo "3. Replace PLACEHOLDER implementations"
    echo "4. Document and review HACK items"
    echo "5. Set up technical debt tracking in project management"
    echo "6. Allocate sprint capacity for debt reduction"
    echo "7. Establish code review policies to prevent new debt"
fi

# Cleanup
rm -f "$TEMP_RESULTS" "${TEMP_RESULTS}.counts"

echo
echo -e "${GREEN}Technical debt scan completed.${NC}"