#!/bin/bash
# T046: Automated Update Scheduler for Library Version Changes
# Provides scheduling and automation capabilities for dependency updates

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
SCHEDULER_CONFIG="${PROJECT_ROOT}/config/update-scheduler.json"
UPDATE_LOG="${PROJECT_ROOT}/logs/scheduled-updates.log"
DEPENDENCY_SCRIPT="${PROJECT_ROOT}/scripts/update-dependencies.sh"
SCHEDULE_MODE="${SCHEDULE_MODE:-manual}"
NOTIFICATION_ENABLED="${NOTIFICATION_ENABLED:-true}"
DRY_RUN="${DRY_RUN:-false}"

# Logging functions
log_info() {
    echo -e "${BLUE}[SCHEDULER]${NC} $1" | tee -a "$UPDATE_LOG"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1" | tee -a "$UPDATE_LOG"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1" | tee -a "$UPDATE_LOG"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1" | tee -a "$UPDATE_LOG"
}

log_schedule() {
    echo -e "${PURPLE}[SCHEDULE]${NC} $1" | tee -a "$UPDATE_LOG"
}

log_auto() {
    echo -e "${CYAN}[AUTO]${NC} $1" | tee -a "$UPDATE_LOG"
}

# Show help
show_help() {
    cat << EOF
Dependency Update Scheduler

USAGE:
    $0 [OPTIONS] COMMAND [ARGS...]

COMMANDS:
    schedule                    Schedule automatic updates
    run                         Run scheduled updates now
    list-schedules              List all scheduled updates
    remove <schedule_id>        Remove a scheduled update
    enable <schedule_id>        Enable a scheduled update
    disable <schedule_id>       Disable a scheduled update
    status                      Show scheduler status
    notify                      Send test notification

OPTIONS:
    --mode MODE                 Schedule mode: cron, systemd, manual (default: manual)
    --dry-run                   Show what would be done without executing
    --no-notify                 Disable notifications
    --config FILE              Custom scheduler config file
    --log FILE                  Custom log file
    --help, -h                  Show this help message

SCHEDULE EXAMPLES:
    $0 schedule --name "weekly-updates" --frequency "weekly" --strategy "compatible"
    $0 schedule --name "daily-checks" --frequency "daily" --check-only
    $0 schedule --name "security-updates" --frequency "daily" --security-only

EOF
}

# Parse command line arguments
parse_arguments() {
    COMMAND=""
    COMMAND_ARGS=()
    SCHEDULE_NAME=""
    FREQUENCY="weekly"
    STRATEGY="compatible"
    CHECK_ONLY=false
    SECURITY_ONLY=false

    while [[ $# -gt 0 ]]; do
        case $1 in
            --mode)
                SCHEDULE_MODE="$2"
                shift 2
                ;;
            --dry-run)
                DRY_RUN=true
                shift
                ;;
            --no-notify)
                NOTIFICATION_ENABLED=false
                shift
                ;;
            --config)
                SCHEDULER_CONFIG="$2"
                shift 2
                ;;
            --log)
                UPDATE_LOG="$2"
                shift 2
                ;;
            --name)
                SCHEDULE_NAME="$2"
                shift 2
                ;;
            --frequency)
                FREQUENCY="$2"
                shift 2
                ;;
            --strategy)
                STRATEGY="$2"
                shift 2
                ;;
            --check-only)
                CHECK_ONLY=true
                shift
                ;;
            --security-only)
                SECURITY_ONLY=true
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
                if [[ -z "$COMMAND" ]]; then
                    COMMAND="$1"
                else
                    COMMAND_ARGS+=("$1")
                fi
                shift
                ;;
        esac
    done

    if [[ -z "$COMMAND" ]]; then
        log_error "No command specified"
        show_help
        exit 1
    fi
}

# Initialize scheduler environment
initialize_scheduler() {
    # Create directories
    mkdir -p "$(dirname "$UPDATE_LOG")"
    mkdir -p "$(dirname "$SCHEDULER_CONFIG")"

    # Initialize default scheduler config if not exists
    if [[ ! -f "$SCHEDULER_CONFIG" ]]; then
        create_default_scheduler_config
    fi

    # Check if dependency script exists
    if [[ ! -f "$DEPENDENCY_SCRIPT" ]]; then
        log_error "Dependency script not found: $DEPENDENCY_SCRIPT"
        exit 1
    fi
}

# Create default scheduler configuration
create_default_scheduler_config() {
    log_info "Creating default scheduler configuration..."

    cat > "$SCHEDULER_CONFIG" << 'EOF'
{
  "scheduler": {
    "enabled": true,
    "mode": "manual",
    "max_parallel_updates": 3,
    "default_strategy": "compatible",
    "retry_failed_updates": true,
    "max_retry_attempts": 3,
    "notification_enabled": true,
    "backup_before_update": true,
    "auto_commit_updates": false
  },
  "schedules": [],
  "global_settings": {
    "timezone": "UTC",
    "maintenance_window": {
      "start": "02:00",
      "end": "04:00"
    },
    "update_timeout": 600,
    "notification_channels": ["email", "log"],
    "security_update_priority": "high"
  },
  "last_run": {
    "timestamp": null,
    "successful_updates": 0,
    "failed_updates": 0,
    "skipped_updates": 0
  }
}
EOF

    log_success "Default scheduler configuration created: $SCHEDULER_CONFIG"
}

# Generate unique schedule ID
generate_schedule_id() {
    echo "schedule-$(date +%Y%m%d-%H%M%S)-$(openssl rand -hex 4 2>/dev/null || echo $$)"
}

# Add new schedule
add_schedule() {
    local schedule_id=$(generate_schedule_id)
    local created_at=$(date -u +%Y-%m-%dT%H:%M:%SZ)

    log_schedule "Creating new schedule: $SCHEDULE_NAME"

    # Validate frequency
    case "$FREQUENCY" in
        "hourly"|"daily"|"weekly"|"monthly"|*"@ "*")
            ;;
        *)
            log_error "Invalid frequency: $FREQUENCY"
            log_info "Valid frequencies: hourly, daily, weekly, monthly, or cron expression"
            return 1
            ;;
    esac

    # Create schedule entry
    local schedule_entry=$(cat << EOF
{
  "id": "$schedule_id",
  "name": "$SCHEDULE_NAME",
  "frequency": "$FREQUENCY",
  "strategy": "$STRATEGY",
  "check_only": $CHECK_ONLY,
  "security_only": $SECURITY_ONLY,
  "enabled": true,
  "created_at": "$created_at",
  "last_run": null,
  "next_run": "$(calculate_next_run "$FREQUENCY")",
  "run_count": 0,
  "success_count": 0,
  "failure_count": 0,
  "last_result": null,
  "options": {
    "auto_backup": true,
    "notify_on_success": true,
    "notify_on_failure": true,
    "parallel_updates": false,
    "timeout_seconds": 600
  }
}
EOF
)

    # Add to configuration
    local temp_config=$(mktemp)
    jq ".schedules += [$schedule_entry]" "$SCHEDULER_CONFIG" > "$temp_config"
    mv "$temp_config" "$SCHEDULER_CONFIG"

    log_success "Schedule created: $schedule_id"
    log_info "Name: $SCHEDULE_NAME"
    log_info "Frequency: $FREQUENCY"
    log_info "Next run: $(calculate_next_run "$FREQUENCY")"

    # Setup cron job if in cron mode
    if [[ "$SCHEDULE_MODE" == "cron" ]]; then
        setup_cron_job "$schedule_id" "$FREQUENCY"
    fi

    # Setup systemd timer if in systemd mode
    if [[ "$SCHEDULE_MODE" == "systemd" ]]; then
        setup_systemd_timer "$schedule_id" "$FREQUENCY"
    fi
}

# Calculate next run time
calculate_next_run() {
    local frequency="$1"
    local next_run=""

    case "$frequency" in
        "hourly")
            next_run=$(date -u -d "+1 hour" +%Y-%m-%dT%H:%M:%SZ)
            ;;
        "daily")
            next_run=$(date -u -d "+1 day" +%Y-%m-%dT%H:%M:%SZ)
            ;;
        "weekly")
            next_run=$(date -u -d "+1 week" +%Y-%m-%dT%H:%M:%SZ)
            ;;
        "monthly")
            next_run=$(date -u -d "+1 month" +%Y-%m-%dT%H:%M:%SZ)
            ;;
        *@*)
            # Cron expression - approximate next run
            next_run=$(date -u -d "+1 hour" +%Y-%m-%dT%H:%M:%SZ)
            ;;
        *)
            next_run=$(date -u -d "+1 week" +%Y-%m-%dT%H:%M:%SZ)
            ;;
    esac

    echo "$next_run"
}

# Setup cron job
setup_cron_job() {
    local schedule_id="$1"
    local frequency="$2"
    local cron_expr=""

    case "$frequency" in
        "hourly")
            cron_expr="0 * * * *"
            ;;
        "daily")
            cron_expr="0 2 * * *"
            ;;
        "weekly")
            cron_expr="0 2 * * 0"
            ;;
        "monthly")
            cron_expr="0 2 1 * *"
            ;;
        *)
            log_warning "Cron mode with complex frequency not fully supported"
            cron_expr="0 2 * * *"
            ;;
    esac

    local cron_job="$cron_expr $PROJECT_ROOT/scripts/schedule-dependency-upsdates.sh run --schedule-id $schedule_id >> $UPDATE_LOG 2>&1"

    # Add to crontab
    (crontab -l 2>/dev/null || true; echo "# Dependency update schedule: $schedule_id"; echo "$cron_job") | crontab -

    log_success "Cron job added for schedule: $schedule_id"
}

# Setup systemd timer
setup_systemd_timer() {
    local schedule_id="$1"
    local frequency="$2"
    local timer_unit="/etc/systemd/system/dependency-update-${schedule_id}.timer"
    local service_unit="/etc/systemd/system/dependency-update-${schedule_id}.service"

    # Create timer unit
    cat > "$timer_unit" << EOF
[Unit]
Description=Dependency Update Schedule - $schedule_id
Requires=dependency-update-${schedule_id}.service

[Timer]
OnCalendar=$frequency
Persistent=true

[Install]
WantedBy=timers.target
EOF

    # Create service unit
    cat > "$service_unit" << EOF
[Unit]
Description=Run Dependency Update - $schedule_id
After=network.target

[Service]
Type=oneshot
ExecStart=$PROJECT_ROOT/scripts/schedule-dependency-updates.sh run --schedule-id $schedule_id
WorkingDirectory=$PROJECT_ROOT
User=$(id -un)
Group=$(id -gn)
EOF

    # Reload systemd and enable timer
    systemctl daemon-reload
    systemctl enable "dependency-update-${schedule_id}.timer"
    systemctl start "dependency-update-${schedule_id}.timer"

    log_success "Systemd timer created and enabled for schedule: $schedule_id"
}

# Run scheduled updates
run_scheduled_updates() {
    local target_schedule_id=""

    # Parse run command arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            --schedule-id)
                target_schedule_id="$2"
                shift 2
                ;;
            *)
                shift
                ;;
        esac
    done

    log_info "Running scheduled updates..."
    log_info "Target schedule ID: ${target_schedule_id:-all}"

    # Get schedules to run
    local schedules=()
    if [[ -n "$target_schedule_id" ]]; then
        # Run specific schedule
        local schedule=$(jq -r ".schedules[] | select(.id == \"$target_schedule_id\")" "$SCHEDULER_CONFIG")
        if [[ -n "$schedule" && "$schedule" != "null" ]]; then
            schedules+=("$schedule")
        else
            log_error "Schedule not found: $target_schedule_id"
            return 1
        fi
    else
        # Run all enabled schedules that are due
        local current_time=$(date -u +%Y-%m-%dT%H:%M:%SZ)
        while IFS= read -r schedule; do
            local enabled=$(echo "$schedule" | jq -r '.enabled // false')
            local next_run=$(echo "$schedule" | jq -r '.next_run // ""')

            if [[ "$enabled" == "true" && "$next_run" < "$current_time" ]]; then
                schedules+=("$schedule")
            fi
        done < <(jq -c '.schedules[]' "$SCHEDULER_CONFIG")
    fi

    if [[ ${#schedules[@]} -eq 0 ]]; then
        log_info "No schedules to run"
        return 0
    fi

    log_info "Found ${#schedules[@]} schedules to execute"

    # Execute each schedule
    local total_success=0
    local total_failure=0
    local total_skipped=0

    for schedule in "${schedules[@]}"; do
        local schedule_id=$(echo "$schedule" | jq -r '.id')
        local schedule_name=$(echo "$schedule" | jq -r '.name')
        local strategy=$(echo "$schedule" | jq -r '.strategy // "compatible"')
        local check_only=$(echo "$schedule" | jq -r '.check_only // false')
        local security_only=$(echo "$schedule" | jq -r '.security_only // false')

        log_schedule "Executing schedule: $schedule_name ($schedule_id)"

        # Build update command
        local update_cmd=("$DEPENDENCY_SCRIPT")

        if [[ "$check_only" == true ]]; then
            update_cmd+=("check-updates")
        elif [[ "$security_only" == true ]]; then
            update_cmd+=("update-all" "--strategy" "security")
        else
            update_cmd+=("update-all" "--strategy" "$strategy")
        fi

        # Add dry run flag if needed
        if [[ "$DRY_RUN" == true ]]; then
            update_cmd+=("--dry-run")
        fi

        # Execute update
        local update_start=$(date +%s)
        local update_result=0

        if "${update_cmd[@]}" 2>&1 | tee -a "$UPDATE_LOG"; then
            update_result=0
            ((total_success++))
            log_success "Schedule completed successfully: $schedule_name"
        else
            update_result=1
            ((total_failure++))
            log_error "Schedule failed: $schedule_name"
        fi

        local update_end=$(date +%s)
        local update_duration=$((update_end - update_start))

        # Update schedule status
        update_schedule_status "$schedule_id" $update_result $update_duration

        # Send notification
        if [[ "$NOTIFICATION_ENABLED" == true ]]; then
            send_notification "$schedule_name" $update_result $update_duration
        fi
    done

    # Update global run status
    update_global_status $total_success $total_failure $total_skipped

    # Display summary
    echo -e "\n${BLUE}Scheduled Updates Summary:${NC}"
    echo "------------------------"
    echo "Successful: $total_success"
    echo "Failed: $total_failure"
    echo "Skipped: $total_skipped"

    if [[ $total_failure -gt 0 ]]; then
        return 1
    else
        log_success "All scheduled updates completed successfully!"
        return 0
    fi
}

# Update schedule status
update_schedule_status() {
    local schedule_id="$1"
    local result="$2"
    local duration="$3"
    local timestamp=$(date -u +%Y-%m-%dT%H:%M:%SZ)

    local temp_config=$(mktemp)
    jq "(.schedules[] | select(.id == \"$schedule_id\")) |= . + {
      \"last_run\": \"$timestamp\",
      \"next_run\": \"$(calculate_next_run "$(jq -r ".schedules[] | select(.id == \"$schedule_id\") | .frequency" "$SCHEDULER_CONFIG")")\",
      \"run_count\": (.run_count + 1),
      \"success_count\": (.success_count + $([[ $result -eq 0 ]] && echo 1 || echo 0)),
      \"failure_count\": (.failure_count + $([[ $result -ne 0 ]] && echo 1 || echo 0)),
      \"last_result\": {
        \"timestamp\": \"$timestamp\",
        \"success\": $([[ $result -eq 0 ]] && echo true || echo false),
        \"duration_seconds\": $duration
      }
    }" "$SCHEDULER_CONFIG" > "$temp_config"
    mv "$temp_config" "$SCHEDULER_CONFIG"
}

# Update global status
update_global_status() {
    local success="$1"
    local failure="$2"
    local skipped="$3"
    local timestamp=$(date -u +%Y-%m-%dT%H:%M:%SZ)

    local temp_config=$(mktemp)
    jq ".last_run = {
      \"timestamp\": \"$timestamp\",
      \"successful_updates\": $success,
      \"failed_updates\": $failure,
      \"skipped_updates\": $skipped
    }" "$SCHEDULER_CONFIG" > "$temp_config"
    mv "$temp_config" "$SCHEDULER_CONFIG"
}

# Send notification
send_notification() {
    local schedule_name="$1"
    local result="$2"
    local duration="$3"

    if [[ "$NOTIFICATION_ENABLED" != true ]]; then
        return 0
    fi

    local status="FAILED"
    local color="$RED"
    if [[ $result -eq 0 ]]; then
        status="SUCCESS"
        color="$GREEN"
    fi

    # Log notification
    log_auto "NOTIFICATION: Schedule '$schedule_name' $status (duration: ${duration}s)"

    # Email notification (if configured)
    # TODO: Implement email notification system

    # Slack notification (if configured)
    # TODO: Implement Slack webhook integration

    # Log notification is always sent
    echo -e "${color}[NOTIFICATION]${NC} Schedule '$schedule_name' $status" | tee -a "$UPDATE_LOG"
}

# List all schedules
list_schedules() {
    log_info "Listing all scheduled updates..."

    if command -v jq >/dev/null 2>&1; then
        echo -e "\n${BLUE}Configured Schedules:${NC}"
        echo "----------------------------------------"

        local schedule_count=$(jq '.schedules | length' "$SCHEDULER_CONFIG")
        if [[ $schedule_count -eq 0 ]]; then
            log_info "No schedules configured"
            return 0
        fi

        jq -r '.schedules[] | [
          .id,
          .name,
          .frequency,
          .strategy,
          (.enabled | if . then "ENABLED" else "DISABLED" end),
          .last_run // "NEVER",
          .next_run,
          .run_count,
          .success_count,
          .failure_count
        ] | @tsv' "$SCHEDULER_CONFIG" | while IFS=$'\t' read -r id name frequency strategy enabled last_run next_run run_count success_count failure_count; do
            echo -e "\n${CYAN}$name${NC}"
            echo "  ID: $id"
            echo "  Frequency: $frequency"
            echo "  Strategy: $strategy"
            echo "  Status: $enabled"
            echo "  Last run: $last_run"
            echo "  Next run: $next_run"
            echo "  Statistics: $run_count runs, $success_count success, $failure_count failures"
        done
    else
        log_error "jq not available for JSON parsing"
        exit 1
    fi

    echo -e "\n"
}

# Show scheduler status
show_status() {
    log_info "Dependency Update Scheduler Status"

    if command -v jq >/dev/null 2>&1; then
        local enabled=$(jq -r '.scheduler.enabled // false' "$SCHEDULER_CONFIG")
        local mode=$(jq -r '.scheduler.mode // "manual"' "$SCHEDULER_CONFIG")
        local total_schedules=$(jq '.schedules | length' "$SCHEDULER_CONFIG")
        local enabled_schedules=$(jq '.schedules | map(select(.enabled == true)) | length' "$SCHEDULER_CONFIG")
        local last_run=$(jq -r '.last_run.timestamp // "Never"' "$SCHEDULER_CONFIG")

        echo -e "\n${BLUE}Status Information:${NC}"
        echo "------------------------"
        echo "Scheduler enabled: $enabled"
        echo "Mode: $mode"
        echo "Total schedules: $total_schedules"
        echo "Enabled schedules: $enabled_schedules"
        echo "Last run: $last_run"

        if [[ -n "$last_run" && "$last_run" != "null" ]]; then
            local successful=$(jq -r '.last_run.successful_updates // 0' "$SCHEDULER_CONFIG")
            local failed=$(jq -r '.last_run.failed_updates // 0' "$SCHEDULER_CONFIG")
            local skipped=$(jq -r '.last_run.skipped_updates // 0' "$SCHEDULER_CONFIG")
            echo "Last run results: $successful successful, $failed failed, $ skipped skipped"
        fi
    else
        log_error "jq not available for JSON parsing"
        exit 1
    fi

    echo -e "\n"
}

# Send test notification
send_test_notification() {
    log_info "Sending test notification..."

    local test_message="Test notification from dependency update scheduler"
    local test_timestamp=$(date -u +%Y-%m-%dT%H:%M:%SZ)

    log_auto "NOTIFICATION: $test_message at $test_timestamp"
    log_success "Test notification sent successfully"
}

# Main execution
main() {
    log_info "Dependency Update Scheduler (T046)"
    log_info "Project root: $PROJECT_ROOT"

    # Parse arguments
    parse_arguments "$@"

    # Initialize scheduler
    initialize_scheduler

    # Execute command
    case "$COMMAND" in
        "schedule")
            if [[ -z "$SCHEDULE_NAME" ]]; then
                log_error "Schedule name required. Use --name option."
                exit 1
            fi
            add_schedule
            ;;
        "run")
            run_scheduled_updates "${COMMAND_ARGS[@]}"
            ;;
        "list-schedules")
            list_schedules
            ;;
        "remove")
            log_error "remove command not implemented yet"
            exit 1
            ;;
        "enable")
            log_error "enable command not implemented yet"
            exit 1
            ;;
        "disable")
            log_error "disable command not implemented yet"
            exit 1
            ;;
        "status")
            show_status
            ;;
        "notify")
            send_test_notification
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