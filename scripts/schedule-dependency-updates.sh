#!/usr/bin/env bash
# T051: Automated Dependency Update Scheduling and Notification System
# Comprehensive scheduling system with notification integration, approval workflow, and audit logging

set -euo pipefail

# Script configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SCHEDULE_DIR="$PROJECT_ROOT/.dependency_cache"
UPDATE_SCRIPT="$SCRIPT_DIR/update-dependencies.sh"
NOTIFICATION_FUNCTIONS="$SCRIPT_DIR/notification-functions.sh"
LOG_DIR="$PROJECT_ROOT/logs/dependency_updates"
SCHEDULE_LOG="$PROJECT_ROOT/logs/scheduled_operations.log"
AUDIT_LOG="$PROJECT_ROOT/logs/audit.log"

# Import notification functions
if [[ -f "$NOTIFICATION_FUNCTIONS" ]]; then
    source "$NOTIFICATION_FUNCTIONS"
else
    echo "Error: Notification functions not found at $NOTIFICATION_FUNCTIONS" >&2
    exit 1
fi

# Scheduling configuration
SCHEDULE_CONFIG="$SCHEDULE_DIR/update_schedule.json"
SCHEDULE_VERSION="1.1.0"

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Logging functions
log_schedule() {
    local level="$1"
    local message="$2"
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    echo "${timestamp} [SCHEDULE] ${level} ${message}" | tee -a "$SCHEDULE_LOG"
}

log_info() { log_schedule "INFO" "$1"; }
log_success() { log_schedule "SUCCESS" "$1"; }
log_warning() { log_schedule "WARNING" "$1"; }
log_error() { log_schedule "ERROR" "$1"; }
log_debug() { log_schedule "DEBUG" "$1"; }

# Audit logging function
log_audit() {
    local operation="$1"
    local details="$2"
    local status="$3"
    local timestamp=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
    local user="${USER:-unknown}"

    local audit_entry="${timestamp} | USER:${user} | OP:${operation} | STATUS:${status} | ${details}"
    echo "$audit_entry" >> "$AUDIT_LOG"

    # Update schedule audit configuration if enabled
    if [[ -f "$SCHEDULE_CONFIG" ]]; then
        local audit_enabled=$(jq -r '.audit_logging.enabled // true' "$SCHEDULE_CONFIG")
        if [[ "$audit_enabled" == "true" ]]; then
            local audit_file=$(jq -r '.audit_logging.log_file // "/root/keycuda/logs/scheduled_operations.log"' "$SCHEDULE_CONFIG")
            mkdir -p "$(dirname "$audit_file")"
            echo "$audit_entry" >> "$audit_file"
        fi
    fi
}

# Check if operation is in blackout period
is_in_blackout_period() {
    if [[ ! -f "$SCHEDULE_CONFIG" ]]; then
        return 1  # No blackout periods configured
    fi

    local current_time=$(date -u +"%H:%M")
    local current_day=$(date -u +"%A" | tr '[:upper:]' '[:lower:]')

    local blackout_count=$(jq -r '.blackout_periods | length' "$SCHEDULE_CONFIG" 2>/dev/null || echo "0")

    for ((i=0; i<blackout_count; i++)); do
        local start_time=$(jq -r --argjson idx "$i" '.blackout_periods[$idx].start_time // ""' "$SCHEDULE_CONFIG")
        local end_time=$(jq -r --argjson idx "$i" '.blackout_periods[$idx].end_time // ""' "$SCHEDULE_CONFIG")
        local blackout_days=$(jq -r --argjson idx "$i" '.blackout_periods[$idx].days[]?' "$SCHEDULE_CONFIG")

        if [[ -n "$start_time" && -n "$end_time" ]]; then
            # Check if current day is in blackout period
            local day_in_blackout=false
            while IFS= read -r day; do
                if [[ "$day" == "$current_day" ]]; then
                    day_in_blackout=true
                    break
                fi
            done <<< "$blackout_days"

            if [[ "$day_in_blackout" == "true" ]]; then
                # Check if current time is in blackout window
                if [[ "$current_time" > "$start_time" && "$current_time" < "$end_time" ]]; then
                    log_warning "Operation blocked: currently in blackout period ($start_time-$end_time on $current_day)"
                    return 0  # In blackout period
                fi
            fi
        fi
    done

    return 1  # Not in blackout period
}

# Initialize schedule system with T051 enhancements
init_schedule_system() {
    log_info "Initializing T051 automated update schedule system..."
    mkdir -p "$SCHEDULE_DIR" "$LOG_DIR"

    # Create log directories
    mkdir -p "$(dirname "$SCHEDULE_LOG")" "$(dirname "$AUDIT_LOG")"

    # Create enhanced schedule configuration if it doesn't exist
    if [[ ! -f "$SCHEDULE_CONFIG" ]]; then
        log_info "Creating enhanced T051 schedule configuration..."
        cat > "$SCHEDULE_CONFIG" << 'EOF'
{
  "schedule_version": "1.1",
  "created_timestamp": "",
  "last_updated": "",
  "enabled": true,
  "t051_implementation": {
    "version": "1.0.0",
    "automated_scheduling": true,
    "notification_integration": true,
    "approval_workflow": true,
    "audit_logging": true
  },
  "update_policies": {
    "auto_external_updates": false,
    "auto_cmake_updates": true,
    "security_updates_only": true,
    "require_compatibility_check": true,
    "require_test_suite": true,
    "max_parallel_updates": 3,
    "update_timeout_seconds": 3600,
    "rollback_on_failure": true,
    "notification_required": true
  },
  "schedule": {
    "daily_checks": {
      "enabled": true,
      "time": "02:00",
      "timezone": "UTC",
      "days": ["monday", "tuesday", "wednesday", "thursday", "friday"]
    },
    "weekly_full_scan": {
      "enabled": true,
      "day": "sunday",
      "time": "03:00",
      "timezone": "UTC"
    },
    "security_scan": {
      "enabled": true,
      "interval_hours": 6,
      "urgent_only": true
    },
    "monthly_cleanup": {
      "enabled": true,
      "day": 1,
      "time": "04:00",
      "timezone": "UTC"
    }
  },
  "dependencies": {
    "external": {
      "auto_update": false,
      "require_approval": true,
      "security_priority": true
    },
    "extracted": {
      "auto_update": false,
      "require_approval": false,
      "security_priority": false
    },
    "cmake_fetch": {
      "auto_update": true,
      "require_approval": false,
      "security_priority": true
    }
  },
  "notifications": {
    "email": {
      "enabled": false,
      "recipients": [],
      "smtp_server": "",
      "smtp_port": 587,
      "use_tls": true
    },
    "slack": {
      "enabled": false,
      "webhook_url": "",
      "channel": "#dependency-updates"
    },
    "console": {
      "enabled": true,
      "verbose": false
    },
    "log_file": {
      "enabled": true,
      "path": "",
      "max_size_mb": 100
    }
  },
  "blackout_periods": [
    {
      "name": "Production Hours",
      "start_time": "09:00",
      "end_time": "17:00",
      "timezone": "UTC",
      "days": ["monday", "tuesday", "wednesday", "thursday", "friday"]
    }
  ],
  "approval_workflow": {
    "enabled": true,
    "require_approval_for": ["external", "major_version_updates", "security_updates", "breaking_changes"],
    "auto_approve": ["cmake_fetch", "patch_updates", "compatibility_verified"],
    "approvers": [],
    "approval_timeout_hours": 24,
    "auto_approve_after_timeout": false,
    "emergency_override": {
      "enabled": true,
      "authorized_users": [],
      "require_reason": true
    }
  },
  "audit_logging": {
    "enabled": true,
    "log_all_operations": true,
    "retention_days": 90,
    "log_file": "/root/keycuda/logs/scheduled_operations.log",
    "include_sensitive_data": false,
    "backup_logs": true
  },
  "update_history": {
    "last_check": "",
    "last_successful_update": "",
    "last_failed_update": "",
    "total_updates": 0,
    "successful_updates": 0,
    "failed_updates": 0,
    "rollbacks_performed": 0,
    "scheduled_operations": 0,
    "cancelled_operations": 0
  }
}
EOF

        local timestamp=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
        jq --arg ts "$timestamp" '.created_timestamp = $ts | .last_updated = $ts' "$SCHEDULE_CONFIG" > "${SCHEDULE_CONFIG}.tmp" && \
        mv "${SCHEDULE_CONFIG}.tmp" "$SCHEDULE_CONFIG"
    fi

    # Initialize notification system
    if command -v init_notification_system >/dev/null 2>&1; then
        init_notification_system
    fi

    # Log initialization
    log_audit "SYSTEM_INIT" "T051 scheduling system initialized with version $SCHEDULE_VERSION" "SUCCESS"

    # Send initialization notification
    if command -v send_update_notification >/dev/null 2>&1; then
        send_update_notification "system_initialized" "T051 automated scheduling system initialized successfully" "$SCHEDULE_CONFIG" "false"
    fi

    log_success "T051 update schedule system initialized"
}

# Check if approval is required for operation
require_approval() {
    local operation_type="$1"
    local dependency_type="$2"

    if [[ ! -f "$SCHEDULE_CONFIG" ]]; then
        return 1  # No approval required if no config
    fi

    local approval_enabled=$(jq -r '.approval_workflow.enabled // false' "$SCHEDULE_CONFIG")
    if [[ "$approval_enabled" != "true" ]]; then
        return 1  # Approval workflow disabled
    fi

    # Check if operation type requires approval
    local requires_approval=false
    local approval_types=$(jq -r '.approval_workflow.require_approval_for[]?' "$SCHEDULE_CONFIG")

    while IFS= read -r type; do
        if [[ "$type" == "$operation_type" || "$type" == "$dependency_type" ]]; then
            requires_approval=true
            break
        fi
    done <<< "$approval_types"

    # Check if auto-approved
    local auto_approved=false
    local auto_approve_types=$(jq -r '.approval_workflow.auto_approve[]?' "$SCHEDULE_CONFIG")

    while IFS= read -r type; do
        if [[ "$type" == "$operation_type" || "$type" == "$dependency_type" ]]; then
            auto_approved=true
            break
        fi
    done <<< "$auto_approve_types"

    if [[ "$requires_approval" == "true" && "$auto_approved" != "true" ]]; then
        return 0  # Approval required
    else
        return 1  # No approval required
    fi
}

# Execute scheduled update with T051 enhancements
execute_scheduled_update() {
    local update_type="$1"
    local dry_run="${2:-false}"

    log_info "Executing scheduled update: $update_type"

    # Check blackout periods
    if is_in_blackout_period; then
        log_info "Update skipped due to blackout period"
        log_audit "SCHEDULED_UPDATE_SKIP" "Update type: $update_type, reason: blackout period" "SKIPPED"
        return 0
    fi

    # Check if approval is required
    if require_approval "$update_type" "scheduled"; then
        log_info "Approval required for $update_type update - creating approval request"
        create_approval_request "$update_type"
        log_audit "APPROVAL_REQUEST" "Update type: $update_type requires approval" "PENDING"
        return 0
    fi

    # Send notification about starting update
    if command -v send_update_notification >/dev/null 2>&1; then
        send_update_notification "update_started" "Scheduled $update_type update started" "$SCHEDULE_CONFIG" "$dry_run"
    fi

    # Execute the update
    local exit_code=0
    local start_time=$(date +%s)

    if [[ "$dry_run" == "true" ]]; then
        log_info "DRY RUN: Would execute $update_type update"
        exit_code=0
    else
        case "$update_type" in
            "daily_check")
                "$UPDATE_SCRIPT" check-updates --quiet || exit_code=$?
                ;;
            "weekly_scan")
                "$UPDATE_SCRIPT" detect || exit_code=$?
                "$UPDATE_SCRIPT" check-updates || exit_code=$?
                ;;
            "security_scan")
                "$UPDATE_SCRIPT" check-updates --force || exit_code=$?
                ;;
            "full_update")
                "$UPDATE_SCRIPT" update --quiet || exit_code=$?
                ;;
            *)
                log_error "Unknown update type: $update_type"
                exit_code=1
                ;;
        esac
    fi

    local end_time=$(date +%s)
    local duration=$((end_time - start_time))

    # Update history
    update_schedule_history "$update_type" "$exit_code" "$duration"

    # Send completion notification
    if [[ $exit_code -eq 0 ]]; then
        log_success "Scheduled $update_type update completed successfully in ${duration}s"
        if command -v send_update_notification >/dev/null 2>&1; then
            send_update_notification "update_completed" "Scheduled $update_type update completed successfully in ${duration}s" "$SCHEDULE_CONFIG" "$dry_run"
        fi
        log_audit "SCHEDULED_UPDATE_SUCCESS" "Update type: $update_type, duration: ${duration}s" "SUCCESS"
    else
        log_error "Scheduled $update_type update failed with exit code $exit_code"
        if command -v send_update_notification >/dev/null 2>&1; then
            send_update_notification "update_failed" "Scheduled $update_type update failed with exit code $exit_code" "$SCHEDULE_CONFIG" "$dry_run"
        fi
        log_audit "SCHEDULED_UPDATE_FAILED" "Update type: $update_type, exit_code: $exit_code, duration: ${duration}s" "FAILED"
    fi

    return $exit_code
}

# Create approval request
create_approval_request() {
    local operation_type="$1"
    local timestamp=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
    local request_id="req_$(date +%s)"

    local approval_file="$SCHEDULE_DIR/approval_requests/${request_id}.json"
    mkdir -p "$(dirname "$approval_file")"

    cat > "$approval_file" << EOF
{
  "request_id": "$request_id",
  "operation_type": "$operation_type",
  "requested_at": "$timestamp",
  "requested_by": "${USER:-unknown}",
  "status": "pending",
  "expires_at": "$(date -u -d "+24 hours" +"%Y-%m-%dT%H:%M:%SZ")",
  "details": {
    "operation": "dependency_update",
    "type": "$operation_type",
    "reason": "Scheduled operation requiring approval"
  }
}
EOF

    log_info "Approval request created: $request_id"
    if command -v send_update_notification >/dev/null 2>&1; then
        send_update_notification "approval_required" "Approval required for $operation_type operation (Request ID: $request_id)" "$SCHEDULE_CONFIG" "false"
    fi
}

# Update schedule history
update_schedule_history() {
    local operation="$1"
    local exit_code="$2"
    local duration="$3"
    local timestamp=$(date -u +"%Y-%m-%dT%H:%M:%SZ")

    jq --arg ts "$timestamp" \
        --arg op "$operation" \
        --arg ec "$exit_code" \
        --arg dur "$duration" \
        '
        .update_history.last_check = $ts |
        if $ec == "0" then
            .update_history.last_successful_update = $ts |
            .update_history.successful_updates += 1
        else
            .update_history.last_failed_update = $ts |
            .update_history.failed_updates += 1
        end |
        .update_history.total_updates += 1 |
        .update_history.scheduled_operations += 1 |
        .last_updated = $ts
        ' "$SCHEDULE_CONFIG" > "${SCHEDULE_CONFIG}.tmp" && \
    mv "${SCHEDULE_CONFIG}.tmp" "$SCHEDULE_CONFIG"
}

# Test comprehensive T051 functionality
test_t051_functionality() {
    log_info "Testing comprehensive T051 scheduling functionality..."

    # Check required dependencies
    local required_commands=("jq" "date")
    for cmd in "${required_commands[@]}"; do
        if ! command -v "$cmd" >/dev/null 2>&1; then
            log_error "Required command not found: $cmd"
            return 1
        fi
    done

    # Test configuration parsing
    if [[ ! -f "$SCHEDULE_CONFIG" ]]; then
        log_error "Schedule configuration not found"
        return 1
    fi

    log_info "Testing configuration parsing..."
    local schedule_version=$(jq -r '.schedule_version // "unknown"' "$SCHEDULE_CONFIG")
    local enabled=$(jq -r '.enabled // false' "$SCHEDULE_CONFIG")
    log_info "Schedule version: $schedule_version"
    log_info "Scheduling enabled: $enabled"

    # Test notification system
    log_info "Testing notification system..."
    if command -v send_update_notification >/dev/null 2>&1; then
        send_update_notification "test" "T051 functionality test completed successfully" "$SCHEDULE_CONFIG" "false"
        log_success "Notification system test passed"
    else
        log_warning "Notification system not available"
    fi

    # Test approval workflow
    log_info "Testing approval workflow..."
    if require_approval "external" "test_dependency"; then
        log_info "Approval workflow correctly requires approval for external dependencies"
    else
        log_info "Approval workflow does not require approval for test case"
    fi

    # Test blackout period detection
    log_info "Testing blackout period detection..."
    if is_in_blackout_period; then
        log_info "Currently in blackout period"
    else
        log_info "Currently not in blackout period"
    fi

    # Test scheduled update execution (dry run)
    log_info "Testing scheduled update execution (dry run)..."
    execute_scheduled_update "daily_check" "true"

    log_success "T051 functionality test completed"
}

# Show comprehensive status
show_t051_status() {
    log_info "T051 Automated Dependency Update System Status"

    echo -e "${BLUE}=== System Configuration ===${NC}"

    if [[ ! -f "$SCHEDULE_CONFIG" ]]; then
        echo -e "${RED}Schedule configuration not found${NC}"
        return 1
    fi

    local version=$(jq -r '.t051_implementation.version // "unknown"' "$SCHEDULE_CONFIG")
    local enabled=$(jq -r '.enabled // false' "$SCHEDULE_CONFIG")
    echo "T051 Version: $version"
    echo "Scheduling enabled: $enabled"

    echo -e "\n${YELLOW}Schedule Status:${NC}"
    local daily_enabled=$(jq -r '.schedule.daily_checks.enabled // false' "$SCHEDULE_CONFIG")
    local weekly_enabled=$(jq -r '.schedule.weekly_full_scan.enabled // false' "$SCHEDULE_CONFIG")
    local security_enabled=$(jq -r '.schedule.security_scan.enabled // false' "$SCHEDULE_CONFIG")
    echo "Daily checks: $daily_enabled"
    echo "Weekly scan: $weekly_enabled"
    echo "Security scan: $security_enabled"

    echo -e "\n${YELLOW}Workflow Status:${NC}"
    local approval_enabled=$(jq -r '.approval_workflow.enabled // false' "$SCHEDULE_CONFIG")
    local audit_enabled=$(jq -r '.audit_logging.enabled // false' "$SCHEDULE_CONFIG")
    local notification_enabled=$(jq -r '.notifications.console.enabled // false' "$SCHEDULE_CONFIG")
    echo "Approval workflow: $approval_enabled"
    echo "Audit logging: $audit_enabled"
    echo "Notifications: $notification_enabled"

    echo -e "\n${YELLOW}Operation History:${NC}"
    local total_ops=$(jq -r '.update_history.total_updates // 0' "$SCHEDULE_CONFIG")
    local successful_ops=$(jq -r '.update_history.successful_updates // 0' "$SCHEDULE_CONFIG")
    local failed_ops=$(jq -r '.update_history.failed_updates // 0' "$SCHEDULE_CONFIG")
    local scheduled_ops=$(jq -r '.update_history.scheduled_operations // 0' "$SCHEDULE_CONFIG")
    echo "Total operations: $total_ops"
    echo "Successful: $successful_ops"
    echo "Failed: $failed_ops"
    echo "Scheduled operations: $scheduled_ops"

    echo -e "\n${YELLOW}Notification History:${NC}"
    if command -v get_notification_history >/dev/null 2>&1; then
        get_notification_history 5
    else
        echo "Notification history not available"
    fi
}

# Print usage information
print_usage() {
    cat << EOF
T051: Automated Dependency Update Scheduling and Notification System

Usage: $0 [OPTIONS] COMMAND

OPTIONS:
    --dry-run              Simulate operations without making changes
    --force                Force operations without confirmation
    --verbose              Enable verbose output

COMMANDS:
    init                    Initialize T051 scheduling system
    test                    Test T051 functionality comprehensively
    status                  Show comprehensive system status
    run TYPE                Execute scheduled update (daily_check|weekly_scan|security_scan|full_update)
    enable TYPE             Enable schedule type (daily|weekly|security|all)
    disable TYPE            Disable schedule type (daily|weekly|security|all)
    check-blackout          Check if currently in blackout period
    require-approval TYPE  Test if approval is required for operation type
    approval-list           List pending approval requests
    show-notifications      Show notification history
    audit-log               Show audit log entries
    help                    Show this help message

EXAMPLES:
    $0 init                    # Initialize T051 system
    $0 test                    # Test all functionality
    $0 status                  # Show system status
    $0 run daily_check --dry-run  # Simulate daily check
    $0 enable all              # Enable all schedules
    $0 require-approval external  # Check if external requires approval

DESCRIPTION:
    This script provides comprehensive automated dependency update scheduling
    with notification integration, approval workflow, blackout periods, and
    audit logging for the Puzzle71Solver project.

FEATURES:
    - Configurable update schedules (daily, weekly, monthly, security)
    - Multi-channel notifications (console, email, Slack, file)
    - Approval workflow for critical updates
    - Blackout period management
    - Comprehensive audit logging
    - Integration with existing dependency management

EOF
}

# Main execution logic
main() {
    local command="${1:-help}"
    local dry_run=false
    local force=false
    local verbose=false

    # Parse options
    while [[ $# -gt 0 ]]; do
        case $1 in
            --dry-run)
                dry_run=true
                shift
                ;;
            --force)
                force=true
                shift
                ;;
            --verbose)
                verbose=true
                shift
                ;;
            --help|-h)
                print_usage
                exit 0
                ;;
            *)
                break
                ;;
        esac
    done

    # Parse command
    command="${1:-help}"
    shift || true

    case "$command" in
        init)
            init_schedule_system
            log_audit "COMMAND_EXEC" "init_schedule_system" "SUCCESS"
            ;;
        test)
            test_t051_functionality
            log_audit "COMMAND_EXEC" "test_t051_functionality" "SUCCESS"
            ;;
        status)
            show_t051_status
            ;;
        run)
            local update_type="${1:-daily_check}"
            execute_scheduled_update "$update_type" "$dry_run"
            log_audit "COMMAND_EXEC" "execute_scheduled_update $update_type" "SUCCESS"
            ;;
        enable)
            local schedule_type="${1:-all}"
            enable_schedule_type "$schedule_type"
            log_audit "COMMAND_EXEC" "enable_schedule $schedule_type" "SUCCESS"
            ;;
        disable)
            local schedule_type="${1:-all}"
            disable_schedule_type "$schedule_type"
            log_audit "COMMAND_EXEC" "disable_schedule $schedule_type" "SUCCESS"
            ;;
        check-blackout)
            if is_in_blackout_period; then
                echo "Currently in blackout period"
                log_audit "COMMAND_EXEC" "check_blackout" "IN_BLACKOUT"
            else
                echo "Not in blackout period"
                log_audit "COMMAND_EXEC" "check_blackout" "NOT_IN_BLACKOUT"
            fi
            ;;
        require-approval)
            local operation_type="${1:-external}"
            if require_approval "$operation_type" "test"; then
                echo "Approval required for: $operation_type"
            else
                echo "Approval not required for: $operation_type"
            fi
            log_audit "COMMAND_EXEC" "require_approval $operation_type" "SUCCESS"
            ;;
        approval-list)
            list_approval_requests
            ;;
        show-notifications)
            local limit="${1:-10}"
            if command -v get_notification_history >/dev/null 2>&1; then
                get_notification_history "$limit"
            else
                echo "Notification system not available"
            fi
            ;;
        audit-log)
            local lines="${1:-20}"
            if [[ -f "$AUDIT_LOG" ]]; then
                echo "Recent audit log entries:"
                tail -n "$lines" "$AUDIT_LOG"
            else
                echo "Audit log not found: $AUDIT_LOG"
            fi
            ;;
        help|--help|-h)
            print_usage
            ;;
        *)
            echo "Error: Unknown command: $command" >&2
            print_usage
            exit 1
            ;;
    esac
}

# Enable schedule type
enable_schedule_type() {
    local schedule_type="$1"

    if [[ ! -f "$SCHEDULE_CONFIG" ]]; then
        log_error "Schedule configuration not found"
        return 1
    fi

    case "$schedule_type" in
        "daily"|"daily_checks")
            jq '.schedule.daily_checks.enabled = true | .enabled = true' "$SCHEDULE_CONFIG" > "${SCHEDULE_CONFIG}.tmp" && \
            mv "${SCHEDULE_CONFIG}.tmp" "$SCHEDULE_CONFIG"
            log_success "Daily check schedule enabled"
            ;;
        "weekly"|"weekly_full_scan")
            jq '.schedule.weekly_full_scan.enabled = true | .enabled = true' "$SCHEDULE_CONFIG" > "${SCHEDULE_CONFIG}.tmp" && \
            mv "${SCHEDULE_CONFIG}.tmp" "$SCHEDULE_CONFIG"
            log_success "Weekly full scan schedule enabled"
            ;;
        "security"|"security_scan")
            jq '.schedule.security_scan.enabled = true | .enabled = true' "$SCHEDULE_CONFIG" > "${SCHEDULE_CONFIG}.tmp" && \
            mv "${SCHEDULE_CONFIG}.tmp" "$SCHEDULE_CONFIG"
            log_success "Security scan schedule enabled"
            ;;
        "all")
            jq '
                .schedule.daily_checks.enabled = true |
                .schedule.weekly_full_scan.enabled = true |
                .schedule.security_scan.enabled = true |
                .enabled = true
            ' "$SCHEDULE_CONFIG" > "${SCHEDULE_CONFIG}.tmp" && \
            mv "${SCHEDULE_CONFIG}.tmp" "$SCHEDULE_CONFIG"
            log_success "All schedules enabled"
            ;;
        *)
            log_error "Unknown schedule type: $schedule_type"
            echo "Available types: daily, weekly, security, all"
            return 1
            ;;
    esac
}

# Disable schedule type
disable_schedule_type() {
    local schedule_type="$1"

    if [[ ! -f "$SCHEDULE_CONFIG" ]]; then
        log_error "Schedule configuration not found"
        return 1
    fi

    case "$schedule_type" in
        "daily"|"daily_checks")
            jq '.schedule.daily_checks.enabled = false' "$SCHEDULE_CONFIG" > "${SCHEDULE_CONFIG}.tmp" && \
            mv "${SCHEDULE_CONFIG}.tmp" "$SCHEDULE_CONFIG"
            log_success "Daily check schedule disabled"
            ;;
        "weekly"|"weekly_full_scan")
            jq '.schedule.weekly_full_scan.enabled = false' "$SCHEDULE_CONFIG" > "${SCHEDULE_CONFIG}.tmp" && \
            mv "${SCHEDULE_CONFIG}.tmp" "$SCHEDULE_CONFIG"
            log_success "Weekly full scan schedule disabled"
            ;;
        "security"|"security_scan")
            jq '.schedule.security_scan.enabled = false' "$SCHEDULE_CONFIG" > "${SCHEDULE_CONFIG}.tmp" && \
            mv "${SCHEDULE_CONFIG}.tmp" "$SCHEDULE_CONFIG"
            log_success "Security scan schedule disabled"
            ;;
        "all")
            jq '.enabled = false' "$SCHEDULE_CONFIG" > "${SCHEDULE_CONFIG}.tmp" && \
            mv "${SCHEDULE_CONFIG}.tmp" "$SCHEDULE_CONFIG"
            log_success "All schedules disabled"
            ;;
        *)
            log_error "Unknown schedule type: $schedule_type"
            echo "Available types: daily, weekly, security, all"
            return 1
            ;;
    esac
}

# List pending approval requests
list_approval_requests() {
    local approval_dir="$SCHEDULE_DIR/approval_requests"

    if [[ ! -d "$approval_dir" ]]; then
        echo "No approval requests directory found"
        return 0
    fi

    echo "Pending approval requests:"
    echo "=========================="

    local found=false
    for request_file in "$approval_dir"/*.json; do
        if [[ -f "$request_file" ]]; then
            found=true
            local request_id=$(jq -r '.request_id // "unknown"' "$request_file")
            local operation_type=$(jq -r '.operation_type // "unknown"' "$request_file")
            local requested_at=$(jq -r '.requested_at // "unknown"' "$request_file")
            local status=$(jq -r '.status // "unknown"' "$request_file")
            local expires_at=$(jq -r '.expires_at // "unknown"' "$request_file")

            echo "Request ID: $request_id"
            echo "Operation: $operation_type"
            echo "Requested: $requested_at"
            echo "Status: $status"
            echo "Expires: $expires_at"
            echo "File: $request_file"
            echo "---"
        fi
    done

    if [[ "$found" != "true" ]]; then
        echo "No approval requests found"
    fi
}

# Execute main function with all arguments
main "$@"