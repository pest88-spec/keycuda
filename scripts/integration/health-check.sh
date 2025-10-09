#!/usr/bin/env bash
# Integration Health Check and Monitoring
#
# Provides comprehensive health monitoring for third-party library integrations,
# including periodic checks, anomaly detection, and preventive maintenance.
#
# @author       Puzzle71Solver Team
# @created      2025-10-09
# @license      MIT

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
INTEGRATION_ROOT="${REPO_ROOT}/src/extracted"
BUILD_DIR="${REPO_ROOT}/build"
HEALTH_REPORT_DIR="${BUILD_DIR}/health-reports"
MONITORING_DATA="${BUILD_DIR}/monitoring-data.json"

# Source error handling utilities
source "${SCRIPT_DIR}/error-handler.sh"

# Health check configuration
readonly HEALTH_CHECK_INTERVAL=3600  # 1 hour
readonly CRITICAL_THRESHOLD=0.9
readonly WARNING_THRESHOLD=0.7
readonly MAX_BUILD_TIME_MINUTES=30
readonly MAX_DISK_USAGE_PERCENT=85

# Health status levels
readonly STATUS_CRITICAL="CRITICAL"
readonly STATUS_WARNING="WARNING"
readonly STATUS_HEALTHY="HEALTHY"
readonly STATUS_UNKNOWN="UNKNOWN"

# Color codes for output
readonly RED='\033[0;31m'
readonly YELLOW='\033[1;33m'
readonly GREEN='\033[0;32m'
readonly BLUE='\033[0;34m'
readonly NC='\033[0m' # No Color

# Logging functions
log_health() {
    local level="$1"
    local message="$2"
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')

    case "$level" in
        "CRITICAL")
            echo -e "${RED}[CRITICAL]${NC} ${timestamp} - ${message}"
            ;;
        "WARNING")
            echo -e "${YELLOW}[WARNING]${NC} ${timestamp} - ${message}"
            ;;
        "HEALTHY")
            echo -e "${GREEN}[HEALTHY]${NC} ${timestamp} - ${message}"
            ;;
        *)
            echo -e "${BLUE}[INFO]${NC} ${timestamp} - ${message}"
            ;;
    esac
}

# Check disk space usage
check_disk_space() {
    local disk_usage=$(df "${REPO_ROOT}" | awk 'NR==2 {print $5}' | sed 's/%//')
    local available_space=$(df "${REPO_ROOT}" | awk 'NR==2 {print $4}')
    local total_space=$(df "${REPO_ROOT}" | awk 'NR==2 {print $2}')

    local status=$STATUS_HEALTHY
    local message="Disk usage: ${disk_usage}% (${available_space}KB available)"

    if [[ $disk_usage -gt $MAX_DISK_USAGE_PERCENT ]]; then
        status=$STATUS_CRITICAL
        message="Disk usage critical: ${disk_usage}% (threshold: ${MAX_DISK_USAGE_PERCENT}%)"
    elif [[ $disk_usage -gt $((MAX_DISK_USAGE_PERCENT - 10)) ]]; then
        status=$STATUS_WARNING
        message="Disk usage warning: ${disk_usage}% (threshold: ${MAX_DISK_USAGE_PERCENT}%)"
    fi

    echo "$status:$message:$disk_usage:$available_space:$total_space"
}

# Check integration directory integrity
check_integration_integrity() {
    local library_name="$1"
    local library_path="${INTEGRATION_ROOT}/${library_name}"

    if [[ ! -d "$library_path" ]]; then
        echo "${STATUS_CRITICAL}:Library directory missing: $library_path"
        return
    fi

    local issues=()
    local total_checks=0
    local passed_checks=0

    # Check for essential directories
    for dir in "src" "include" "attribution_headers"; do
        ((total_checks++))
        if [[ -d "$library_path/$dir" ]]; then
            ((passed_checks++))
        else
            issues+=("Missing directory: $dir")
        fi
    done

    # Check for source files
    ((total_checks++))
    if ls "$library_path"/src/*.{c,cpp,h,hpp} 1> /dev/null 2>&1; then
        ((passed_checks++))
    else
        issues+=("No source files found")
    fi

    # Check attribution headers
    ((total_checks++))
    local attribution_ok=true
    while IFS= read -r -d '' file; do
        if ! grep -q "@origin" "$file" 2>/dev/null; then
            attribution_ok=false
            break
        fi
    done < <(find "$library_path" -type f \( -name "*.c" -o -name "*.h" -o -name "*.cpp" -o -name "*.hpp" \) -print0 2>/dev/null || true)

    if $attribution_ok; then
        ((passed_checks++))
    else
        issues+=("Missing attribution headers")
    fi

    # Check CMakeLists.txt if present
    if [[ -f "$library_path/CMakeLists.txt" ]]; then
        ((total_checks++))
        if cmake --check-system "$library_path" >/dev/null 2>&1; then
            ((passed_checks++))
        else
            issues+=("CMakeLists.txt has syntax errors")
        fi
    fi

    local health_score=0
    if [[ $total_checks -gt 0 ]]; then
        health_score=$(echo "scale=2; $passed_checks / $total_checks" | bc -l)
    fi

    local status=$STATUS_HEALTHY
    local message="Integration integrity: $passed_checks/$total_checks checks passed"

    if [[ $(echo "$health_score < $CRITICAL_THRESHOLD" | bc -l) -eq 1 ]]; then
        status=$STATUS_CRITICAL
        message="Critical integration issues: ${issues[*]}"
    elif [[ $(echo "$health_score < $WARNING_THRESHOLD" | bc -l) -eq 1 ]]; then
        status=$STATUS_WARNING
        message="Integration issues detected: ${issues[*]}"
    fi

    echo "$status:$message:$health_score:$passed_checks:$total_checks"
}

# Check build system health
check_build_health() {
    local library_name="$1"
    local build_dir="${BUILD_DIR}/health-check-${library_name}"

    mkdir -p "$build_dir"

    local start_time=$(date +%s)
    local build_output

    # Try to configure
    if ! build_output=$(cmake -S "$REPO_ROOT" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release 2>&1); then
        echo "${STATUS_CRITICAL}:CMake configuration failed: $library_name"
        return
    fi

    # Try to build (with timeout)
    local timeout_duration=$((MAX_BUILD_TIME_MINUTES * 60))
    if timeout "$timeout_duration" cmake --build "$build_dir" --parallel $(nproc) >/dev/null 2>&1; then
        local end_time=$(date +%s)
        local build_duration=$((end_time - start_time))
        local build_duration_min=$(echo "scale=1; $build_duration / 60" | bc -l)

        local status=$STATUS_HEALTHY
        local message="Build successful in ${build_duration_min} minutes"

        if [[ $build_duration -gt $((MAX_BUILD_TIME_MINUTES * 60 / 2)) ]]; then
            status=$STATUS_WARNING
            message="Build slow: ${build_duration_min} minutes (threshold: ${MAX_BUILD_TIME_MINUTES} minutes)"
        fi

        echo "$status:$message:$build_duration"
    else
        echo "${STATUS_CRITICAL}:Build failed or timed out for $library_name"
    fi

    # Cleanup
    rm -rf "$build_dir"
}

# Check dependency health
check_dependency_health() {
    local library_name="$1"

    # This would use the integration manager to check dependencies
    # For now, implement basic checks

    local missing_deps=()
    local total_deps=0
    local satisfied_deps=0

    # Check for common external dependencies
    local common_deps=("cmake" "make" "gcc" "nvcc")
    for dep in "${common_deps[@]}"; do
        ((total_deps++))
        if command -v "$dep" >/dev/null 2>&1; then
            ((satisfied_deps++))
        else
            missing_deps+=("$dep")
        fi
    done

    local dep_health_score=0
    if [[ $total_deps -gt 0 ]]; then
        dep_health_score=$(echo "scale=2; $satisfied_deps / $total_deps" | bc -l)
    fi

    local status=$STATUS_HEALTHY
    local message="Dependencies: $satisfied_deps/$total_deps satisfied"

    if [[ ${#missing_deps[@]} -gt 0 ]]; then
        status=$STATUS_WARNING
        message="Missing dependencies: ${missing_deps[*]}"
    fi

    echo "$status:$message:$dep_health_score:$satisfied_deps:$total_deps"
}

# Check performance metrics
check_performance_metrics() {
    local library_name="$1"

    # Check integration time trends
    local integration_log="${BUILD_DIR}/integration-metrics.log"
    local recent_performance=""

    if [[ -f "$integration_log" ]]; then
        recent_performance=$(tail -10 "$integration_log" | grep "$library_name" || echo "")
    fi

    if [[ -n "$recent_performance" ]]; then
        local avg_time=$(echo "$recent_performance" | awk '{sum+=$NF; count++} END {if(count>0) print sum/count; else print 0}')
        local status=$STATUS_HEALTHY
        local message="Performance stable (avg: ${avg_time}s)"

        # Check for performance degradation
        local latest_time=$(echo "$recent_performance" | tail -1 | awk '{print $NF}')
        if [[ $(echo "$latest_time > $avg_time * 1.5" | bc -l) -eq 1 ]]; then
            status=$STATUS_WARNING
            message="Performance degradation detected (latest: ${latest_time}s, avg: ${avg_time}s)"
        fi

        echo "$status:$message:$latest_time:$avg_time"
    else
        echo "${STATUS_UNKNOWN}:No performance data available for $library_name"
    fi
}

# Check for anomalies
detect_anomalies() {
    local library_name="$1"

    local anomalies=()

    # Check for unusual file sizes
    local library_path="${INTEGRATION_ROOT}/${library_name}"
    if [[ -d "$library_path" ]]; then
        local total_size=$(du -sb "$library_path" | cut -f1)
        local file_count=$(find "$library_path" -type f | wc -l)

        # Anomaly: very large library
        if [[ $total_size -gt $((500 * 1024 * 1024)) ]]; then  # > 500MB
            anomalies+=("Large library size: $((total_size / 1024 / 1024))MB")
        fi

        # Anomaly: very few files
        if [[ $file_count -lt 5 ]]; then
            anomalies+=("Very few files: $file_count")
        fi

        # Anomaly: unusual file extensions
        local unusual_files=$(find "$library_path" -type f ! -name "*.c" ! -name "*.h" ! -name "*.cpp" ! -name "*.hpp" ! -name "*.txt" ! -name "*.md" | head -5)
        if [[ -n "$unusual_files" ]]; then
            anomalies+=("Unusual file types found")
        fi
    fi

    if [[ ${#anomalies[@]} -gt 0 ]]; then
        echo "${STATUS_WARNING}:Anomalies detected: ${anomalies[*]}"
    else
        echo "${STATUS_HEALTHY}:No anomalies detected"
    fi
}

# Generate comprehensive health report
generate_health_report() {
    local library_name="${1:-}"
    local output_file="${HEALTH_REPORT_DIR}/health-report-$(date '+%Y%m%d_%H%M%S').json"

    mkdir -p "$HEALTH_REPORT_DIR"

    log_health "INFO" "Generating health report${library_name:+ for $library_name}"

    local report="{
        \"timestamp\": \"$(date -Iseconds)\",
        \"repo_root\": \"$REPO_ROOT\",
        \"libraries\": ["

    local first_library=true
    local libraries=()

    if [[ -n "$library_name" ]]; then
        libraries=("$library_name")
    else
        # Get all integrated libraries
        for lib_dir in "$INTEGRATION_ROOT"/*; do
            if [[ -d "$lib_dir" ]]; then
                libraries+=("$(basename "$lib_dir")")
            fi
        done
    fi

    for lib in "${libraries[@]}"; do
        if [[ "$first_library" == "false" ]]; then
            report+=","
        fi
        first_library=false

        log_health "INFO" "Checking health for: $lib"

        # Run all health checks
        local integrity_check=$(check_integration_integrity "$lib")
        local build_check=$(check_build_health "$lib")
        local dependency_check=$(check_dependency_health "$lib")
        local performance_check=$(check_performance_metrics "$lib")
        local anomaly_check=$(detect_anomalies "$lib")

        # Parse check results
        local integrity_status=$(echo "$integrity_check" | cut -d':' -f1)
        local integrity_message=$(echo "$integrity_check" | cut -d':' -f2-)
        local integrity_score=$(echo "$integrity_check" | cut -d':' -f3)

        local build_status=$(echo "$build_check" | cut -d':' -f1)
        local build_message=$(echo "$build_check" | cut -d':' -f2-)

        local dep_status=$(echo "$dependency_check" | cut -d':' -f1)
        local dep_message=$(echo "$dependency_check" | cut -d':' -f2-)

        local perf_status=$(echo "$performance_check" | cut -d':' -f1)
        local perf_message=$(echo "$performance_check" | cut -d':' -f2-)

        local anomaly_status=$(echo "$anomaly_check" | cut -d':' -f1)
        local anomaly_message=$(echo "$anomaly_check" | cut -d':' -f2-)

        # Determine overall status
        local overall_status=$STATUS_HEALTHY
        if [[ "$integrity_status" == "$STATUS_CRITICAL" || "$build_status" == "$STATUS_CRITICAL" || "$dep_status" == "$STATUS_CRITICAL" ]]; then
            overall_status=$STATUS_CRITICAL
        elif [[ "$integrity_status" == "$STATUS_WARNING" || "$build_status" == "$STATUS_WARNING" || "$dep_status" == "$STATUS_WARNING" || "$anomaly_status" == "$STATUS_WARNING" ]]; then
            overall_status=$STATUS_WARNING
        fi

        # Add library report
        report+="
            {
                \"name\": \"$lib\",
                \"overall_status\": \"$overall_status\",
                \"integrity\": {
                    \"status\": \"$integrity_status\",
                    \"message\": \"$integrity_message\",
                    \"score\": $integrity_score
                },
                \"build\": {
                    \"status\": \"$build_status\",
                    \"message\": \"$build_message\"
                },
                \"dependencies\": {
                    \"status\": \"$dep_status\",
                    \"message\": \"$dep_message\"
                },
                \"performance\": {
                    \"status\": \"$perf_status\",
                    \"message\": \"$perf_message\"
                },
                \"anomalies\": {
                    \"status\": \"$anomaly_status\",
                    \"message\": \"$anomaly_message\"
                }
            }"

        # Log status
        log_health "$overall_status" "$lib: $integrity_message | $build_message | $dep_message"
    done

    # Add system health checks
    local disk_check=$(check_disk_space)
    local disk_status=$(echo "$disk_check" | cut -d':' -f1)
    local disk_message=$(echo "$disk_check" | cut -d':' -f2)
    local disk_usage=$(echo "$disk_check" | cut -d':' -f3)

    report+="
        ],
        \"system\": {
            \"disk_space\": {
                \"status\": \"$disk_status\",
                \"message\": \"$disk_message\",
                \"usage_percent\": $disk_usage
            }
        }
    }"

    # Write report
    echo "$report" > "$output_file"

    log_health "INFO" "Health report generated: $output_file"
    echo "$output_file"
}

# Monitor health continuously
monitor_health() {
    local interval="${1:-$HEALTH_CHECK_INTERVAL}"
    local library_name="${2:-}"

    log_health "INFO" "Starting health monitoring (interval: ${interval}s)"

    while true; do
        generate_health_report "$library_name" >/dev/null

        # Check for critical issues and send alerts if needed
        local latest_report=$(ls -t "$HEALTH_REPORT_DIR"/health-report-*.json | head -1)
        if [[ -f "$latest_report" ]]; then
            local critical_issues=$(jq -r '.libraries[] | select(.overall_status == "CRITICAL") | .name' "$latest_report" || echo "")
            if [[ -n "$critical_issues" ]]; then
                log_health "CRITICAL" "Critical issues detected: $critical_issues"
                # Would send alert/notification here
            fi
        fi

        sleep "$interval"
    done
}

# Show health summary
show_health_summary() {
    local library_name="${1:-}"
    local latest_report=$(ls -t "$HEALTH_REPORT_DIR"/health-report-*.json 2>/dev/null | head -1)

    if [[ ! -f "$latest_report" ]]; then
        log_health "WARNING" "No health reports found. Run generate-report first."
        return 1
    fi

    echo "Integration Health Summary"
    echo "========================="
    echo "Report: $(basename "$latest_report")"
    echo "Generated: $(jq -r '.timestamp' "$latest_report")"
    echo ""

    if [[ -n "$library_name" ]]; then
        # Show specific library
        local lib_info=$(jq -r ".libraries[] | select(.name == \"$library_name\")" "$latest_report")
        if [[ -n "$lib_info" ]]; then
            echo "Library: $library_name"
            echo "Status: $(jq -r '.overall_status' <<< "$lib_info")"
            echo "Integrity: $(jq -r '.integrity.message' <<< "$lib_info")"
            echo "Build: $(jq -r '.build.message' <<< "$lib_info")"
            echo "Dependencies: $(jq -r '.dependencies.message' <<< "$lib_info")"
            echo "Performance: $(jq -r '.performance.message' <<< "$lib_info")"
            echo "Anomalies: $(jq -r '.anomalies.message' <<< "$lib_info")"
        else
            echo "Library not found in report: $library_name"
        fi
    else
        # Show all libraries
        echo "System Status: $(jq -r '.system.disk_space.message' "$latest_report")"
        echo ""
        echo "Libraries:"
        jq -r '.libraries[] | "  \(.name): \(.overall_status) - \(.integrity.message)"' "$latest_report"
    fi
}

# Main execution function
main() {
    # Parse command line arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            -h|--help)
                cat << EOF
Integration Health Check and Monitoring

Usage: $0 [OPTIONS] COMMAND [ARGS]

COMMANDS:
    generate-report [library]
        Generate comprehensive health report for all or specific library

    monitor [interval] [library]
        Start continuous health monitoring

    check-integrity <library>
        Check integration integrity for specific library

    check-build <library>
        Check build health for specific library

    check-dependencies <library>
        Check dependency health for specific library

    check-disk
        Check disk space usage

    detect-anomalies <library>
        Detect anomalies for specific library

    show-summary [library]
        Show health summary from latest report

OPTIONS:
    -h, --help      Show this help message
    -v, --verbose   Enable verbose logging

EXAMPLES:
    $0 generate-report
    $0 generate-report secp256k1-zkp
    $0 monitor 3600
    $0 check-integrity secp256k1-zkp
    $0 show-summary

EOF
                exit 0
                ;;
            -v|--verbose)
                set -x
                shift
                ;;
            *)
                break
                ;;
        esac
    done

    # Execute command
    case "${1:-}" in
        "generate-report")
            generate_health_report "${2:-}"
            ;;
        "monitor")
            monitor "${2:-$HEALTH_CHECK_INTERVAL}" "${3:-}"
            ;;
        "check-integrity")
            if [[ $# -ne 2 ]]; then
                log_health "ERROR" "check-integrity requires 1 argument: library"
                exit 1
            fi
            check_integration_integrity "$2"
            ;;
        "check-build")
            if [[ $# -ne 2 ]]; then
                log_health "ERROR" "check-build requires 1 argument: library"
                exit 1
            fi
            check_build_health "$2"
            ;;
        "check-dependencies")
            if [[ $# -ne 2 ]]; then
                log_health "ERROR" "check-dependencies requires 1 argument: library"
                exit 1
            fi
            check_dependency_health "$2"
            ;;
        "check-disk")
            check_disk_space
            ;;
        "detect-anomalies")
            if [[ $# -ne 2 ]]; then
                log_health "ERROR" "detect-anomalies requires 1 argument: library"
                exit 1
            fi
            detect_anomalies "$2"
            ;;
        "show-summary")
            show_health_summary "${2:-}"
            ;;
        *)
            log_health "ERROR" "Unknown command: ${1:-}"
            exit 1
            ;;
    esac
}

# Run main function if script is executed directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi