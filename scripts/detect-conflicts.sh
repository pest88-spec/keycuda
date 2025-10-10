#!/usr/bin/env bash
# Dependency Conflict Detection Script
#
# Provides automated detection and resolution of dependency conflicts
# that arise during third-party library integration.
#
# @author       Puzzle71Solver Team
# @created      2025-10-09
# @license      MIT

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
INTEGRATION_ROOT="${REPO_ROOT}/src/extracted"
BUILD_DIR="${REPO_ROOT}/build"
CONFLICT_REPORTS_DIR="${BUILD_DIR}/conflict-reports"

# Exit codes
readonly EXIT_SUCCESS=0
readonly EXIT_CONFLICTS_DETECTED=1
readonly EXIT_MISSING_DEPENDENCIES=2
readonly EXIT_CONFIG_ERROR=3
readonly EXIT_BUILD_FAILED=4
readonly EXIT_TIMEOUT=5

# Color codes for output
readonly GREEN='\033[0;32m'
readonly RED='\033[0;31m'
readonly YELLOW='\033[1;33m'
readonly BLUE='\033[0;34m'
readonly NC='\033[0m' # No Color

# Conflict detection configuration
ENABLE_VERSION_CHECK=${ENABLE_VERSION_CHECK:-true}
ENABLE_LICENSE_CHECK=${ENABLE_LICENSE_CHECK:-true}
ENABLE_SYMBOL_SCANNING=${ENABLE_SYMBOL_SCANNING:-true}
ENABLE_BUILD_ANALYSIS=${ENABLE_BUILD_ANALYSIS:-true}
SCAN_TIMEOUT=${SCAN_TIMEOUT:-300}  # 5 minutes
AUTO_RESOLVE=${AUTO_RESOLVE:-false}

# Global state
TOTAL_CONFLICTS=0
RESOLVED_CONFLICTS=0
BLOCKING_CONFLICTS=0
WARNING_CONFLICTS=0

# Logging functions
log_conflict() {
    local severity="$1"
    local message="$2"
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')

    case "$severity" in
        "ERROR")
            echo -e "${RED}[CONFLICT-ERROR]${NC} ${timestamp} - ${message}"
            ;;
        "WARNING")
            echo -e "${YELLOW}[CONFLICT-WARN]${NC} ${timestamp} - ${message}"
            ;;
        "INFO")
            echo -e "${BLUE}[CONFLICT-INFO]${NC} ${timestamp} - ${message}"
            ;;
        "SUCCESS")
            echo -e "${GREEN}[CONFLICT-RESOLVED]${NC} ${timestamp} - ${message}"
            ;;
        *)
            echo -e "${BLUE}[CONFLICT-${severity}]${NC} ${timestamp} - ${message}"
            ;;
    esac
}

# Check prerequisites
check_prerequisites() {
    log_conflict "INFO" "Checking conflict detection prerequisites"

    local missing_deps=()

    # Check required commands
    for cmd in cmake make find grep sed awk; do
        if ! command -v "$cmd" >/dev/null 2>&1; then
            missing_deps+=("$cmd")
        fi
    done

    if [[ ${#missing_deps[@]} -gt 0 ]]; then
        log_conflict "ERROR" "Missing required dependencies: ${missing_deps[*]}"
        return $EXIT_MISSING_DEPENDENCIES
    fi

    # Check integration root
    if [[ ! -d "$INTEGRATION_ROOT" ]]; then
        log_conflict "WARNING" "Integration root directory not found: $INTEGRATION_ROOT"
        return $EXIT_SUCCESS  # Not an error, just no libraries to check
    fi

    # Create output directory
    mkdir -p "$CONFLICT_REPORTS_DIR"

    log_conflict "INFO" "Prerequisites check completed"
    return $EXIT_SUCCESS
}

# Detect version conflicts
detect_version_conflicts() {
    log_conflict "INFO" "Detecting version conflicts"

    if [[ "$ENABLE_VERSION_CHECK" != "true" ]]; then
        log_conflict "INFO" "Version checking disabled"
        return $EXIT_SUCCESS
    fi

    local conflicts=0

    # Collect all libraries and their versions
    declare -A library_versions
    declare -A library_paths

    while IFS= read -r -d '' lib_dir; do
        local lib_name=$(basename "$lib_dir")
        local version=""

        # Try to find version in common locations
        for version_file in "VERSION" "version.txt" "CMakeLists.txt" "configure.ac"; do
            if [[ -f "$lib_dir/$version_file" ]]; then
                version=$(grep -oE '\b[0-9]+\.[0-9]+\.[0-9]+\b' "$lib_dir/$version_file" | head -1 || echo "")
                if [[ -n "$version" ]]; then
                    break
                fi
            fi
        done

        if [[ -n "$version" ]]; then
            if [[ -n "${library_versions[$lib_name]:-}" ]]; then
                log_conflict "ERROR" "Version conflict for $lib_name: ${library_versions[$lib_name]} vs $version"
                ((conflicts++))
                ((BLOCKING_CONFLICTS++))
            else
                library_versions[$lib_name]="$version"
                library_paths[$lib_name]="$lib_dir"
            fi
        fi
    done < <(find "$INTEGRATION_ROOT" -maxdepth 1 -type d -print0 2>/dev/null | grep -zv '^'"$INTEGRATION_ROOT"'$')

    if [[ $conflicts -eq 0 ]]; then
        log_conflict "SUCCESS" "No version conflicts detected"
    fi

    return $conflicts
}

# Detect license conflicts
detect_license_conflicts() {
    log_conflict "INFO" "Detecting license conflicts"

    if [[ "$ENABLE_LICENSE_CHECK" != "true" ]]; then
        log_conflict "INFO" "License checking disabled"
        return $EXIT_SUCCESS
    fi

    local conflicts=0
    declare -A library_licenses

    while IFS= read -r -d '' lib_dir; do
        local lib_name=$(basename "$lib_dir")
        local license=""

        # Try to find license file
        for license_file in "LICENSE" "LICENSE.txt" "COPYING" "license.md"; do
            if [[ -f "$lib_dir/$license_file" ]]; then
                license=$(determine_license_type "$lib_dir/$license_file")
                if [[ -n "$license" ]]; then
                    break
                fi
            fi
        done

        if [[ -n "$license" ]]; then
            library_licenses[$lib_name]="$license"
        fi
    done < <(find "$INTEGRATION_ROOT" -maxdepth 1 -type d -print0 2>/dev/null | grep -zv '^'"$INTEGRATION_ROOT"'$')

    # Check for incompatible license combinations
    local libs=(${!library_licenses[@]})
    for ((i=0; i<${#libs[@]}; i++)); do
        for ((j=i+1; j<${#libs[@]}; j++)); do
            local lib1=${libs[i]}
            local lib2=${libs[j]}
            local license1=${library_licenses[$lib1]}
            local license2=${library_licenses[$lib2]}

            if ! are_licenses_compatible "$license1" "$license2"; then
                log_conflict "ERROR" "License incompatibility: $lib1 ($license1) vs $lib2 ($license2)"
                ((conflicts++))
                ((BLOCKING_CONFLICTS++))
            fi
        done
    done

    if [[ $conflicts -eq 0 ]]; then
        log_conflict "SUCCESS" "No license conflicts detected"
    fi

    return $conflicts
}

# Detect file conflicts
detect_file_conflicts() {
    log_conflict "INFO" "Detecting file conflicts"

    local conflicts=0
    declare -A file_owners

    while IFS= read -r -d '' file_path; do
        local relative_path=$(realpath --relative-to="$INTEGRATION_ROOT" "$file_path")
        local lib_name=$(basename "$(dirname "$file_path")")

        # Skip certain files
        case "$relative_path" in
            */test/*|*/tests/*|*/doc/*|*/docs/*|*/example/*|*/examples/*)
                continue
                ;;
        esac

        if [[ -n "${file_owners[$relative_path]:-}" ]]; then
            local existing_owner=${file_owners[$relative_path]}
            if [[ "$existing_owner" != "$lib_name" ]]; then
                log_conflict "ERROR" "File conflict: $relative_path exists in both $existing_owner and $lib_name"
                ((conflicts++))
                ((BLOCKING_CONFLICTS++))
            fi
        else
            file_owners[$relative_path]="$lib_name"
        fi
    done < <(find "$INTEGRATION_ROOT" -type f -print0 2>/dev/null)

    if [[ $conflicts -eq 0 ]]; then
        log_conflict "SUCCESS" "No file conflicts detected"
    fi

    return $conflicts
}

# Detect symbol conflicts
detect_symbol_conflicts() {
    log_conflict "INFO" "Detecting symbol conflicts"

    if [[ "$ENABLE_SYMBOL_SCANNING" != "true" ]]; then
        log_conflict "INFO" "Symbol scanning disabled"
        return $EXIT_SUCCESS
    fi

    local conflicts=0
    declare -A symbol_locations

    while IFS= read -r -d '' file_path; do
        # Only scan source files
        case "$file_path" in
            *.c|*.cpp|*.cu|*.h|*.hpp)
                ;;
            *)
                continue
                ;;
        esac

        local lib_name=$(basename "$(dirname "$(dirname "$file_path")")")

        # Extract function names using simple regex
        while IFS= read -r line; do
            # Match function definitions
            if [[ "$line" =~ ^[[:space:]]*([a-zA-Z_][a-zA-Z0-9_]*)[[:space:]]*\(.*\)[[:space:]]*\{ ]]; then
                local symbol="${BASH_REMATCH[1]}"
                if [[ -n "${symbol_locations[$symbol]:-}" ]]; then
                    local existing_location=${symbol_locations[$symbol]}
                    if [[ "$existing_location" != "$lib_name" ]]; then
                        log_conflict "WARNING" "Symbol conflict: $symbol defined in both $existing_location and $lib_name"
                        ((conflicts++))
                        ((WARNING_CONFLICTS++))
                    fi
                else
                    symbol_locations[$symbol]="$lib_name"
                fi
            fi
        done < "$file_path"
    done < <(find "$INTEGRATION_ROOT" -type f \( -name "*.c" -o -name "*.cpp" -o -name "*.cu" -o -name "*.h" -o -name "*.hpp" \) -print0 2>/dev/null)

    if [[ $conflicts -eq 0 ]]; then
        log_conflict "SUCCESS" "No symbol conflicts detected"
    fi

    return $conflicts
}

# Detect build system conflicts
detect_build_conflicts() {
    log_conflict "INFO" "Detecting build system conflicts"

    if [[ "$ENABLE_BUILD_ANALYSIS" != "true" ]]; then
        log_conflict "INFO" "Build analysis disabled"
        return $EXIT_SUCCESS
    fi

    local conflicts=0

    # Check for conflicting CMake configurations
    declare -A cmake_options
    declare -A option_values

    while IFS= read -r -d '' cmake_file; do
        local lib_name=$(basename "$(dirname "$cmake_file")")

        # Extract CMake options that might conflict
        while IFS= read -r line; do
            if [[ "$line" =~ ^[[:space:]]*set[[:space:]]*\([[:space:]]*([a-zA-Z_][a-zA-Z0-9_]*)[[:space:]]+(.+)\) ]]; then
                local option="${BASH_REMATCH[1]}"
                local value="${BASH_REMATCH[2]}"

                # Skip internal options
                case "$option" in
                    *_VERSION|*_DIR|CMAKE_*)
                        continue
                        ;;
                esac

                if [[ -n "${option_values[$option]:-}" ]]; then
                    local existing_value=${option_values[$option]}
                    local existing_lib=${cmake_options[$option]}
                    if [[ "$existing_value" != "$value" ]]; then
                        log_conflict "WARNING" "CMake option conflict: $option = $existing_value ($existing_lib) vs $value ($lib_name)"
                        ((conflicts++))
                        ((WARNING_CONFLICTS++))
                    fi
                else
                    cmake_options[$option]="$lib_name"
                    option_values[$option]="$value"
                fi
            fi
        done < "$cmake_file"
    done < <(find "$INTEGRATION_ROOT" -name "CMakeLists.txt" -print0 2>/dev/null)

    if [[ $conflicts -eq 0 ]]; then
        log_conflict "SUCCESS" "No build system conflicts detected"
    fi

    return $conflicts
}

# Auto-resolve conflicts if enabled
auto_resolve_conflicts() {
    if [[ "$AUTO_RESOLVE" != "true" ]]; then
        return $EXIT_SUCCESS
    fi

    log_conflict "INFO" "Attempting automatic conflict resolution"

    # Auto-resolution strategies would be implemented here
    # For now, just log that auto-resolution is enabled but not implemented
    log_conflict "INFO" "Auto-resolution enabled but not yet implemented"

    return $EXIT_SUCCESS
}

# Generate conflict report
generate_conflict_report() {
    local report_file="${CONFLICT_REPORTS_DIR}/conflict-report-$(date '+%Y%m%d_%H%M%S').json"

    log_conflict "INFO" "Generating conflict report: $report_file"

    cat > "$report_file" << EOF
{
    "scan_timestamp": "$(date -Iseconds)",
    "scan_duration_seconds": "${SCAN_TIMEOUT}",
    "configuration": {
        "enable_version_check": $ENABLE_VERSION_CHECK,
        "enable_license_check": $ENABLE_LICENSE_CHECK,
        "enable_symbol_scanning": $ENABLE_SYMBOL_SCANNING,
        "enable_build_analysis": $ENABLE_BUILD_ANALYSIS,
        "auto_resolve": $AUTO_RESOLVE
    },
    "summary": {
        "total_conflicts": $TOTAL_CONFLICTS,
        "resolved_conflicts": $RESOLVED_CONFLICTS,
        "blocking_conflicts": $BLOCKING_CONFLICTS,
        "warning_conflicts": $WARNING_CONFLICTS
    },
    "recommendations": [
        $(if [[ $BLOCKING_CONFLICTS -gt 0 ]]; then echo '"Resolve blocking conflicts before proceeding",'; fi)
        $(if [[ $WARNING_CONFLICTS -gt 0 ]]; then echo '"Review warning conflicts for potential impact",'; fi)
        "$(if [[ $TOTAL_CONFLICTS -eq 0 ]]; then echo 'No conflicts detected - integration ready'; else echo 'Review conflict resolution options'; fi)"
    ],
    "integration_status": "$(if [[ $BLOCKING_CONFLICTS -gt 0 ]]; then echo 'BLOCKED'; elif [[ $WARNING_CONFLICTS -gt 0 ]]; then echo 'WARNING'; else echo 'READY'; fi)"
}
EOF

    log_conflict "INFO" "Conflict report generated: $report_file"
    echo "$report_file"
}

# Main conflict detection function
run_conflict_detection() {
    local start_time=$(date +%s)

    log_conflict "INFO" "Starting dependency conflict detection"

    # Check prerequisites
    if ! check_prerequisites; then
        return $EXIT_MISSING_DEPENDENCIES
    fi

    # Run different types of conflict detection
    local version_conflicts=0
    local license_conflicts=0
    local file_conflicts=0
    local symbol_conflicts=0
    local build_conflicts=0

    detect_version_conflicts
    version_conflicts=$?
    ((TOTAL_CONFLICTS += version_conflicts))

    detect_license_conflicts
    license_conflicts=$?
    ((TOTAL_CONFLICTS += license_conflicts))

    detect_file_conflicts
    file_conflicts=$?
    ((TOTAL_CONFLICTS += file_conflicts))

    detect_symbol_conflicts
    symbol_conflicts=$?
    ((TOTAL_CONFLICTS += symbol_conflicts))

    detect_build_conflicts
    build_conflicts=$?
    ((TOTAL_CONFLICTS += build_conflicts))

    # Auto-resolve if enabled
    auto_resolve_conflicts

    # Generate report
    generate_conflict_report

    # Calculate duration
    local end_time=$(date +%s)
    local duration=$((end_time - start_time))

    # Display summary
    echo ""
    log_conflict "INFO" "Conflict Detection Summary"
    log_conflict "INFO" "=========================="
    log_conflict "INFO" "Total conflicts detected: $TOTAL_CONFLICTS"
    log_conflict "INFO" "Version conflicts: $version_conflicts"
    log_conflict "INFO" "License conflicts: $license_conflicts"
    log_conflict "INFO" "File conflicts: $file_conflicts"
    log_conflict "INFO" "Symbol conflicts: $symbol_conflicts"
    log_conflict "INFO" "Build conflicts: $build_conflicts"
    log_conflict "INFO" "Blocking conflicts: $BLOCKING_CONFLICTS"
    log_conflict "INFO" "Warning conflicts: $WARNING_CONFLICTS"
    log_conflict "INFO" "Resolved conflicts: $RESOLVED_CONFLICTS"
    log_conflict "INFO" "Duration: ${duration}s"

    # Determine exit code
    if [[ $BLOCKING_CONFLICTS -gt 0 ]]; then
        log_conflict "ERROR" "Blocking conflicts detected - integration cannot proceed"
        return $EXIT_CONFLICTS_DETECTED
    elif [[ $TOTAL_CONFLICTS -gt 0 ]]; then
        log_conflict "WARNING" "Non-blocking conflicts detected - proceed with caution"
        return $EXIT_SUCCESS  # Warnings don't block integration
    else
        log_conflict "SUCCESS" "No conflicts detected - integration ready"
        return $EXIT_SUCCESS
    fi
}

# Helper functions
determine_license_type() {
    local license_file="$1"

    if grep -qi "mit" "$license_file"; then
        echo "MIT"
    elif grep -qi "apache" "$license_file"; then
        echo "Apache-2.0"
    elif grep -qi "bsd" "$license_file"; then
        echo "BSD"
    elif grep -qi "gpl" "$license_file"; then
        if grep -qi "lgpl" "$license_file"; then
            echo "LGPL"
        else
            echo "GPL"
        fi
    elif grep -qi "boost" "$license_file"; then
        echo "Boost"
    else
        echo "Unknown"
    fi
}

are_licenses_compatible() {
    local license1="$1"
    local license2="$2"

    # If they're the same, they're compatible
    if [[ "$license1" == "$license2" ]]; then
        return 0
    fi

    # Define compatibility rules
    case "$license1" in
        "MIT")
            case "$license2" in
                "BSD"|"Apache-2.0"|"Boost")
                    return 0
                    ;;
            esac
            ;;
        "BSD")
            case "$license2" in
                "MIT"|"Apache-2.0"|"Boost")
                    return 0
                    ;;
            esac
            ;;
        "Apache-2.0")
            case "$license2" in
                "MIT"|"BSD"|"Boost")
                    return 0
                    ;;
            esac
            ;;
    esac

    # GPL and LGPL are generally incompatible with permissive licenses
    case "$license1" in
        "GPL"|"LGPL")
            case "$license2" in
                "GPL"|"LGPL")
                    return 0
                    ;;
            esac
            ;;
    esac

    return 1  # Not compatible
}

# Main execution function
main() {
    # Parse command line arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            -h|--help)
                cat << 'EOF'
Dependency Conflict Detection Script

Usage: ./detect-conflicts.sh [OPTIONS]

OPTIONS:
    -h, --help                     Show this help message
    -v, --verbose                  Enable verbose logging
    --no-version-check             Disable version conflict detection
    --no-license-check             Disable license conflict detection
    --no-symbol-scanning           Disable symbol conflict scanning
    --no-build-analysis            Disable build system analysis
    --auto-resolve                 Enable automatic conflict resolution
    --timeout <seconds>            Set scan timeout (default: 300)
    --integration-root <path>      Set integration root directory

EXAMPLES:
    ./detect-conflicts.sh
    ./detect-conflicts.sh --auto-resolve --timeout 600
    ./detect-conflicts.sh --no-license-check --verbose

DESCRIPTION:
    This script provides automated detection of dependency conflicts that arise
    during third-party library integration, including:

    - Version conflicts between different library versions
    - License compatibility issues
    - File name conflicts
    - Symbol and function name clashes
    - Build system configuration conflicts

EOF
                exit $EXIT_SUCCESS
                ;;
            -v|--verbose)
                set -x
                shift
                ;;
            --no-version-check)
                export ENABLE_VERSION_CHECK=false
                shift
                ;;
            --no-license-check)
                export ENABLE_LICENSE_CHECK=false
                shift
                ;;
            --no-symbol-scanning)
                export ENABLE_SYMBOL_SCANNING=false
                shift
                ;;
            --no-build-analysis)
                export ENABLE_BUILD_ANALYSIS=false
                shift
                ;;
            --auto-resolve)
                export AUTO_RESOLVE=true
                shift
                ;;
            --timeout)
                export SCAN_TIMEOUT="$2"
                shift 2
                ;;
            --integration-root)
                export INTEGRATION_ROOT="$2"
                shift 2
                ;;
            *)
                echo "Unknown option: $1" >&2
                exit 3
                ;;
        esac
    done

    # Run conflict detection with timeout
    if command -v timeout >/dev/null 2>&1; then
        timeout "$SCAN_TIMEOUT" bash -c "$(declare -f run_conflict_detection check_prerequisites detect_version_conflicts detect_license_conflicts detect_file_conflicts detect_symbol_conflicts detect_build_conflicts auto_resolve_conflicts generate_conflict_report); run_conflict_detection"
    else
        run_conflict_detection
    fi
}

# Run main function if script is executed directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi