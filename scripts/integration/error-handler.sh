#!/usr/bin/env bash
# Integration Error Handling and Recovery Mechanisms
#
# Provides comprehensive error handling, recovery, and rollback capabilities
# for third-party library integration operations.
#
# @author       Puzzle71Solver Team
# @created      2025-10-09
# @license      MIT

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
INTEGRATION_ROOT="${REPO_ROOT}/src/extracted"
LOG_FILE="${REPO_ROOT}/build/integration-errors.log"
BACKUP_DIR="${REPO_ROOT}/.integration-backups"
STATE_FILE="${REPO_ROOT}/build/.integration-state"

# Color codes for output
readonly RED='\033[0;31m'
readonly GREEN='\033[0;32m'
readonly YELLOW='\033[1;33m'
readonly BLUE='\033[0;34m'
readonly NC='\033[0m' # No Color

# Error types
readonly ERROR_TYPES=(
    "EXTRACTION_FAILED"
    "ATTRIBUTION_FAILED"
    "BUILD_INTEGRATION_FAILED"
    "DEPENDENCY_CONFLICT"
    "LICENSE_INCOMPATIBLE"
    "DISK_SPACE_INSUFFICIENT"
    "NETWORK_ERROR"
    "VALIDATION_FAILED"
    "ROLLBACK_FAILED"
    "RECOVERY_FAILED"
)

# Recovery strategies
readonly RECOVERY_STRATEGIES=(
    "retry"
    "fallback_to_previous"
    "skip_component"
    "use_alternative_source"
    "partial_integration"
    "manual_intervention_required"
)

# Logging functions
log_error() {
    local message="$1"
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    echo -e "${RED}[ERROR]${NC} ${timestamp} - ${message}" | tee -a "$LOG_FILE"
}

log_warning() {
    local message="$1"
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    echo -e "${YELLOW}[WARNING]${NC} ${timestamp} - ${message}" | tee -a "$LOG_FILE"
}

log_info() {
    local message="$1"
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    echo -e "${BLUE}[INFO]${NC} ${timestamp} - ${message}" | tee -a "$LOG_FILE"
}

log_success() {
    local message="$1"
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    echo -e "${GREEN}[SUCCESS]${NC} ${timestamp} - ${message}" | tee -a "$LOG_FILE"
}

# State management functions
save_state() {
    local operation="$1"
    local library="$2"
    local status="$3"
    local details="$4"

    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    local entry="{\"timestamp\":\"$timestamp\",\"operation\":\"$operation\",\"library\":\"$library\",\"status\":\"$status\",\"details\":\"$details\"}"

    mkdir -p "$(dirname "$STATE_FILE")"
    echo "$entry" >> "$STATE_FILE"

    log_info "State saved: $operation on $library - $status"
}

get_last_state() {
    local library="$1"
    if [[ -f "$STATE_FILE" ]]; then
        grep "\"library\":\"$library\"" "$STATE_FILE" | tail -1 || echo ""
    fi
}

# Backup and restore functions
create_backup() {
    local library_name="$1"
    local backup_path="${BACKUP_DIR}/${library_name}/$(date '+%Y%m%d_%H%M%S')"

    log_info "Creating backup for $library_name"

    mkdir -p "$backup_path"

    # Backup source files if they exist
    if [[ -d "${INTEGRATION_ROOT}/${library_name}" ]]; then
        cp -r "${INTEGRATION_ROOT}/${library_name}" "$backup_path/"
    fi

    # Backup CMakeLists.txt
    if [[ -f "${REPO_ROOT}/CMakeLists.txt" ]]; then
        cp "${REPO_ROOT}/CMakeLists.txt" "$backup_path/"
    fi

    # Backup integration manager state
    if [[ -f "${REPO_ROOT}/build/.integration-manager-state.json" ]]; then
        cp "${REPO_ROOT}/build/.integration-manager-state.json" "$backup_path/"
    fi

    log_success "Backup created: $backup_path"
    echo "$backup_path"
}

restore_from_backup() {
    local backup_path="$1"

    if [[ ! -d "$backup_path" ]]; then
        log_error "Backup path does not exist: $backup_path"
        return 1
    fi

    log_info "Restoring from backup: $backup_path"

    # Restore source files
    if [[ -d "$backup_path" ]]; then
        for item in "$backup_path"/*; do
            if [[ -d "$item" ]]; then
                cp -r "$item" "$INTEGRATION_ROOT/"
            elif [[ -f "$item" ]]; then
                cp "$item" "$REPO_ROOT/"
            fi
        done
    fi

    log_success "Restore completed from backup: $backup_path"
    return 0
}

# Error detection functions
detect_extraction_error() {
    local library_name="$1"
    local source_path="$2"

    if [[ ! -d "$source_path" ]] || [[ -z "$(ls -A "$source_path" 2>/dev/null)" ]]; then
        echo "EXTRACTION_FAILED"
        return 0
    fi

    # Check for essential files
    local essential_files=("*.c" "*.h" "*.cpp" "*.hpp")
    for pattern in "${essential_files[@]}"; do
        if ! ls "$source_path"/$pattern 1> /dev/null 2>&1; then
            echo "EXTRACTION_INCOMPLETE"
            return 0
        fi
    done

    return 1
}

detect_attribution_error() {
    local library_path="$1"

    # Check for attribution headers
    while IFS= read -r -d '' file; do
        if ! grep -q "@origin" "$file" 2>/dev/null; then
            echo "ATTRIBUTION_FAILED"
            return 0
        fi
    done < <(find "$library_path" -type f \( -name "*.c" -o -name "*.h" -o -name "*.cpp" -o -name "*.hpp" \) -print0)

    return 1
}

detect_build_error() {
    local library_name="$1"

    # Try to configure with CMake
    local build_dir="${REPO_ROOT}/build/test-${library_name}"
    mkdir -p "$build_dir"

    if ! cmake -S "$REPO_ROOT" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release >/dev/null 2>&1; then
        echo "BUILD_INTEGRATION_FAILED"
        return 0
    fi

    return 1
}

detect_dependency_conflicts() {
    local library_name="$1"

    # This would use the integration manager to detect conflicts
    # For now, just return no conflicts
    return 1
}

# Recovery functions
retry_operation() {
    local operation="$1"
    local max_attempts="${2:-3}"
    local delay="${3:-5}"

    local attempt=1
    while [[ $attempt -le $max_attempts ]]; do
        log_info "Retry attempt $attempt/$max_attempts for: $operation"

        if eval "$operation"; then
            log_success "Operation succeeded on attempt $attempt: $operation"
            return 0
        fi

        if [[ $attempt -lt $max_attempts ]]; then
            log_warning "Operation failed, waiting ${delay}s before retry..."
            sleep "$delay"
        fi

        ((attempt++))
    done

    log_error "Operation failed after $max_attempts attempts: $operation"
    return 1
}

fallback_to_previous_version() {
    local library_name="$1"

    log_info "Attempting fallback to previous version for $library_name"

    # Find most recent backup
    local latest_backup=$(ls -t "${BACKUP_DIR}/${library_name}" 2>/dev/null | head -1)

    if [[ -z "$latest_backup" ]]; then
        log_error "No previous version found for $library_name"
        return 1
    fi

    local backup_path="${BACKUP_DIR}/${library_name}/${latest_backup}"

    if restore_from_backup "$backup_path"; then
        save_state "FALLBACK" "$library_name" "SUCCESS" "Restored from $backup_path"
        return 0
    else
        save_state "FALLBACK" "$library_name" "FAILED" "Could not restore from $backup_path"
        return 1
    fi
}

skip_problematic_component() {
    local library_name="$1"
    local component="$2"

    log_info "Skipping problematic component: $component for $library_name"

    # Update component exclusion list
    # This would integrate with the integration manager
    echo "$component" >> "${REPO_ROOT}/build/.excluded-components-${library_name}.txt"

    save_state "SKIP_COMPONENT" "$library_name" "SUCCESS" "Skipped component: $component"
    return 0
}

use_alternative_source() {
    local library_name="$1"
    local alternative_url="$2"

    log_info "Attempting to use alternative source for $library_name: $alternative_url"

    # This would implement alternative source download and integration
    # For now, just log the attempt
    save_state "ALTERNATIVE_SOURCE" "$library_name" "ATTEMPTED" "Alternative source: $alternative_url"
    return 1
}

# Main error handling function
handle_integration_error() {
    local library_name="$1"
    local operation="$2"
    local error_type="$3"
    local error_details="$4"

    log_error "Integration error occurred: $error_type for $library_name during $operation"
    log_error "Error details: $error_details"

    # Save error state
    save_state "ERROR" "$library_name" "$error_type" "$error_details"

    # Create backup before attempting recovery
    local backup_path=$(create_backup "$library_name")

    case "$error_type" in
        "EXTRACTION_FAILED"|"EXTRACTION_INCOMPLETE")
            log_info "Attempting recovery for extraction error"

            # Retry extraction
            if retry_operation "extract_library $library_name" 3 10; then
                return 0
            fi

            # Try alternative source if available
            if [[ -n "${ALTERNATIVE_SOURCE_URL:-}" ]]; then
                if use_alternative_source "$library_name" "$ALTERNATIVE_SOURCE_URL"; then
                    return 0
                fi
            fi

            log_error "Extraction recovery failed for $library_name"
            return 1
            ;;

        "ATTRIBUTION_FAILED")
            log_info "Attempting recovery for attribution error"

            # Regenerate attribution headers
            if retry_operation "generate_attribution $library_name" 2 5; then
                return 0
            fi

            log_error "Attribution recovery failed for $library_name"
            return 1
            ;;

        "BUILD_INTEGRATION_FAILED")
            log_info "Attempting recovery for build integration error"

            # Detect and resolve conflicts
            if retry_operation "resolve_build_conflicts $library_name" 2 5; then
                return 0
            fi

            # Skip problematic components
            if retry_operation "identify_and_skip_problematic_components $library_name" 1 0; then
                return 0
            fi

            log_error "Build integration recovery failed for $library_name"
            return 1
            ;;

        "DEPENDENCY_CONFLICT")
            log_info "Attempting recovery for dependency conflict"

            # Try to resolve conflicts automatically
            if retry_operation "resolve_dependency_conflicts $library_name" 2 5; then
                return 0
            fi

            log_error "Dependency conflict resolution failed for $library_name"
            return 1
            ;;

        "LICENSE_INCOMPATIBLE")
            log_warning "License incompatibility detected - requires manual review"
            save_state "MANUAL_REVIEW_REQUIRED" "$library_name" "LICENSE_INCOMPATIBLE" "$error_details"
            return 1
            ;;

        "DISK_SPACE_INSUFFICIENT")
            log_info "Attempting recovery for insufficient disk space"

            # Clean up temporary files
            if cleanup_temp_files; then
                # Retry operation
                if retry_operation "$operation" 1 0; then
                    return 0
                fi
            fi

            log_error "Disk space recovery failed for $library_name"
            return 1
            ;;

        *)
            log_error "Unknown error type: $error_type"
            return 1
            ;;
    esac
}

# Utility functions
cleanup_temp_files() {
    log_info "Cleaning up temporary files"

    # Clean build directories
    find "${REPO_ROOT}/build" -name "test-*" -type d -exec rm -rf {} + 2>/dev/null || true

    # Clean temporary extraction files
    find "/tmp" -name "*integration-*" -type d -exec rm -rf {} + 2>/dev/null || true

    log_success "Temporary files cleanup completed"
    return 0
}

validate_integration_state() {
    local library_name="$1"

    log_info "Validating integration state for $library_name"

    # Check if library directory exists
    if [[ ! -d "${INTEGRATION_ROOT}/${library_name}" ]]; then
        echo "MISSING_LIBRARY_DIRECTORY"
        return 1
    fi

    # Check for source files
    if ! ls "${INTEGRATION_ROOT}/${library_name}"/src/* 1> /dev/null 2>&1; then
        echo "MISSING_SOURCE_FILES"
        return 1
    fi

    # Check for attribution
    if detect_attribution_error "${INTEGRATION_ROOT}/${library_name}"; then
        echo "MISSING_ATTRIBUTION"
        return 1
    fi

    echo "VALID"
    return 0
}

# Main execution functions
perform_integration_with_recovery() {
    local library_name="$1"
    local operation="$2"

    log_info "Starting integration with recovery: $operation for $library_name"

    # Create backup before starting
    create_backup "$library_name"

    # Attempt the operation
    if eval "$operation"; then
        log_success "Integration operation completed successfully: $operation"
        save_state "INTEGRATION" "$library_name" "SUCCESS" "$operation"
        return 0
    fi

    # Operation failed, attempt error detection and recovery
    local error_type
    error_type=$(detect_extraction_error "$library_name" "${INTEGRATION_ROOT}/${library_name}") || \
    error_type=$(detect_attribution_error "${INTEGRATION_ROOT}/${library_name}") || \
    error_type=$(detect_build_error "$library_name") || \
    error_type=$(detect_dependency_conflicts "$library_name") || \
    error_type="UNKNOWN_ERROR"

    # Handle the error
    if handle_integration_error "$library_name" "$operation" "$error_type" "Operation failed: $operation"; then
        log_success "Recovery successful for $library_name"
        return 0
    else
        log_error "Recovery failed for $library_name"

        # Attempt fallback to previous version
        if fallback_to_previous_version "$library_name"; then
            log_success "Fallback recovery successful for $library_name"
            return 0
        fi

        log_error "All recovery attempts failed for $library_name"
        return 1
    fi
}

# Help function
show_help() {
    cat << EOF
Integration Error Handling and Recovery Mechanisms

Usage: $0 [OPTIONS] COMMAND [ARGS]

COMMANDS:
    handle-error <library> <operation> <error_type> <details>
        Handle a specific integration error

    perform-integration <library> <operation>
        Perform integration with automatic error handling and recovery

    create-backup <library>
        Create a backup of the current integration state

    restore-backup <backup_path>
        Restore from a specific backup

    validate-state <library>
        Validate the current integration state

    cleanup-temp
        Clean up temporary files and build artifacts

    show-state <library>
        Show the last known state for a library

OPTIONS:
    -h, --help      Show this help message
    -v, --verbose   Enable verbose logging
    -q, --quiet     Suppress non-error output

EXAMPLES:
    $0 perform-integration secp256k1-zkp "extract_library secp256k1-zkp"
    $0 handle-error secp256k1-zkp extraction EXTRACTION_FAILED "Source directory empty"
    $0 create-backup secp256k1-zkp
    $0 validate-state secp256k1-zkp

EOF
}

# Main script execution
main() {
    # Parse command line arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            -h|--help)
                show_help
                exit 0
                ;;
            -v|--verbose)
                set -x
                shift
                ;;
            -q|--quiet)
                exec 1>/dev/null
                shift
                ;;
            *)
                break
                ;;
        esac
    done

    # Create necessary directories
    mkdir -p "$BACKUP_DIR" "$(dirname "$LOG_FILE")" "$(dirname "$STATE_FILE")"

    # Execute command
    case "${1:-}" in
        "handle-error")
            if [[ $# -ne 5 ]]; then
                log_error "handle-error requires 4 arguments: library operation error_type details"
                exit 1
            fi
            handle_integration_error "$2" "$3" "$4" "$5"
            ;;
        "perform-integration")
            if [[ $# -ne 3 ]]; then
                log_error "perform-integration requires 2 arguments: library operation"
                exit 1
            fi
            perform_integration_with_recovery "$2" "$3"
            ;;
        "create-backup")
            if [[ $# -ne 2 ]]; then
                log_error "create-backup requires 1 argument: library"
                exit 1
            fi
            create_backup "$2"
            ;;
        "restore-backup")
            if [[ $# -ne 2 ]]; then
                log_error "restore-backup requires 1 argument: backup_path"
                exit 1
            fi
            restore_from_backup "$2"
            ;;
        "validate-state")
            if [[ $# -ne 2 ]]; then
                log_error "validate-state requires 1 argument: library"
                exit 1
            fi
            validate_integration_state "$2"
            ;;
        "cleanup-temp")
            cleanup_temp_files
            ;;
        "show-state")
            if [[ $# -ne 2 ]]; then
                log_error "show-state requires 1 argument: library"
                exit 1
            fi
            get_last_state "$2"
            ;;
        *)
            log_error "Unknown command: ${1:-}"
            show_help
            exit 1
            ;;
    esac
}

# Run main function if script is executed directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi