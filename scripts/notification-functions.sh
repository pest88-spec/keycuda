#!/usr/bin/env bash
# T051: Notification Functions for Automated Dependency Update System
# Multi-channel notification system with email, Slack, console, and file logging

set -euo pipefail

# Script configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
NOTIFICATION_LOG_DIR="$PROJECT_ROOT/logs/notifications"
NOTIFICATION_CACHE_DIR="$PROJECT_ROOT/.notification_cache"

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Initialize notification system
init_notification_system() {
    mkdir -p "$NOTIFICATION_LOG_DIR" "$NOTIFICATION_CACHE_DIR"

    # Create notification history file
    local history_file="$NOTIFICATION_CACHE_DIR/notification_history.json"
    if [[ ! -f "$history_file" ]]; then
        cat > "$history_file" << 'EOF'
{
  "notification_version": "1.0",
  "created_timestamp": "",
  "last_updated": "",
  "total_notifications": 0,
  "successful_notifications": 0,
  "failed_notifications": 0,
  "notifications_by_type": {
    "email": 0,
    "slack": 0,
    "console": 0,
    "file": 0
  },
  "notifications_by_priority": {
    "low": 0,
    "medium": 0,
    "high": 0,
    "critical": 0
  },
  "recent_notifications": []
}
EOF
    fi

    # Update timestamps
    local timestamp=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
    jq --arg ts "$timestamp" '.created_timestamp = $ts | .last_updated = $ts' "$history_file" > "${history_file}.tmp" && \
    mv "${history_file}.tmp" "$history_file"
}

# Logging function for notifications
log_notification() {
    local level="$1"
    local channel="$2"
    local message="$3"
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    local log_file="$NOTIFICATION_LOG_DIR/notifications_$(date '+%Y-%m-%d').log"

    echo "${timestamp} [NOTIFY] [${level}] [${channel}] ${message}" | tee -a "$log_file"
}

# Record notification in history
record_notification() {
    local channel="$1"
    local priority="$2"
    local subject="$3"
    local status="$4"
    local error_msg="${5:-}"

    local history_file="$NOTIFICATION_CACHE_DIR/notification_history.json"
    local timestamp=$(date -u +"%Y-%m-%dT%H:%M:%SZ")

    # Create notification record
    local notification_record=$(jq -n \
        --arg ts "$timestamp" \
        --arg ch "$channel" \
        --arg pr "$priority" \
        --arg su "$subject" \
        --arg st "$status" \
        --arg err "$error_msg" \
        '{
            "timestamp": $ts,
            "channel": $ch,
            "priority": $pr,
            "subject": $su,
            "status": $st,
            "error_message": $err
        }')

    # Update history
    jq --arg ts "$timestamp" \
        --arg ch "$channel" \
        --arg pr "$priority" \
        --arg st "$status" \
        --arg record "$notification_record" \
        '
        .last_updated = $ts |
        .total_notifications += 1 |
        if $st == "success" then
            .successful_notifications += 1
        else
            .failed_notifications += 1
        end |
        .notifications_by_type[$ch] += 1 |
        .notifications_by_priority[$pr] += 1 |
        .recent_notifications = [.recent_notifications[], ($record | fromjson)] |
        if .recent_notifications | length > 100 then
            .recent_notifications = .recent_notifications[0:100]
        else
            .
        end
        ' "$history_file" > "${history_file}.tmp" && \
    mv "${history_file}.tmp" "$history_file"
}

# Send console notification
send_console_notification() {
    local priority="$1"
    local subject="$2"
    local message="$3"
    local dry_run="${4:-false}"

    if [[ "$dry_run" == "true" ]]; then
        echo "[DRY RUN] Console notification: [$priority] $subject"
        return 0
    fi

    case "$priority" in
        "low")
            echo -e "${CYAN}[INFO]${NC} $subject: $message"
            ;;
        "medium")
            echo -e "${YELLOW}[NOTICE]${NC} $subject: $message"
            ;;
        "high")
            echo -e "${RED}[WARNING]${NC} $subject: $message"
            ;;
        "critical")
            echo -e "${PURPLE}[CRITICAL]${NC} $subject: $message"
            ;;
    esac

    log_notification "INFO" "console" "Console notification sent: [$priority] $subject"
    record_notification "console" "$priority" "$subject" "success"
}

# Send file notification
send_file_notification() {
    local priority="$1"
    local subject="$2"
    local message="$3"
    local config_file="$4"
    local dry_run="${5:-false}"

    if [[ "$dry_run" == "true" ]]; then
        echo "[DRY RUN] File notification: [$priority] $subject"
        return 0
    fi

    # Get log file path from config or use default
    local log_file="$NOTIFICATION_LOG_DIR/dependency_updates_$(date '+%Y-%m-%d').log"
    if [[ -f "$config_file" ]]; then
        local config_log_file=$(jq -r '.notifications.log_file.path // empty' "$config_file" 2>/dev/null || echo "")
        if [[ -n "$config_log_file" ]]; then
            log_file="$config_log_file"
        fi
    fi

    # Ensure log directory exists
    mkdir -p "$(dirname "$log_file")"

    # Write notification to file
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    echo "${timestamp} [DEPENDENCY_UPDATE] [${priority^^}] ${subject}: ${message}" >> "$log_file"

    log_notification "INFO" "file" "File notification sent: [$priority] $subject"
    record_notification "file" "$priority" "$subject" "success"
}

# Send email notification
send_email_notification() {
    local priority="$1"
    local subject="$2"
    local message="$3"
    local config_file="$4"
    local dry_run="${5:-false}"

    if [[ "$dry_run" == "true" ]]; then
        echo "[DRY RUN] Email notification: [$priority] $subject"
        return 0
    fi

    # Check if email is enabled in config
    if [[ ! -f "$config_file" ]]; then
        log_notification "ERROR" "email" "Email config file not found: $config_file"
        record_notification "email" "$priority" "$subject" "failed" "Config file not found"
        return 1
    fi

    local email_enabled=$(jq -r '.notifications.email.enabled // false' "$config_file")
    if [[ "$email_enabled" != "true" ]]; then
        log_notification "INFO" "email" "Email notifications disabled, skipping: [$priority] $subject"
        record_notification "email" "$priority" "$subject" "skipped" "Email notifications disabled"
        return 0
    fi

    # Get email configuration
    local recipients=$(jq -r '.notifications.email.recipients[]?' "$config_file" | tr '\n' ',' | sed 's/,$//')
    local smtp_server=$(jq -r '.notifications.email.smtp_server // ""' "$config_file")
    local smtp_port=$(jq -r '.notifications.email.smtp_port // 587' "$config_file")
    local use_tls=$(jq -r '.notifications.email.use_tls // true' "$config_file")

    if [[ -z "$recipients" || "$recipients" == "" ]]; then
        log_notification "WARNING" "email" "No email recipients configured"
        record_notification "email" "$priority" "$subject" "failed" "No recipients configured"
        return 1
    fi

    # Create email content
    local email_content=$(cat << EOF
Subject: [Puzzle71Solver] $subject

Priority: $priority
Timestamp: $(date -u +"%Y-%m-%dT%H:%M:%SZ")

$message

---
This is an automated notification from the Puzzle71Solver dependency update system.
EOF
)

    # Try to send email (using mail command if available)
    if command -v mail >/dev/null 2>&1; then
        echo "$email_content" | mail -s "[Puzzle71Solver] $subject" "$recipients"
        local exit_code=$?
        if [[ $exit_code -eq 0 ]]; then
            log_notification "INFO" "email" "Email sent successfully to $recipients"
            record_notification "email" "$priority" "$subject" "success"
            return 0
        else
            log_notification "ERROR" "email" "Failed to send email (exit code: $exit_code)"
            record_notification "email" "$priority" "$subject" "failed" "mail command failed"
            return 1
        fi
    else
        # Fallback: write to a file
        local email_file="$NOTIFICATION_CACHE_DIR/email_$(date +%s).txt"
        echo "$email_content" > "$email_file"
        log_notification "WARNING" "email" "mail command not available, saved to $email_file"
        record_notification "email" "$priority" "$subject" "fallback" "mail command not available"
        return 0
    fi
}

# Send Slack notification
send_slack_notification() {
    local priority="$1"
    local subject="$2"
    local message="$3"
    local config_file="$4"
    local dry_run="${5:-false}"

    if [[ "$dry_run" == "true" ]]; then
        echo "[DRY RUN] Slack notification: [$priority] $subject"
        return 0
    fi

    # Check if Slack is enabled in config
    if [[ ! -f "$config_file" ]]; then
        log_notification "ERROR" "slack" "Slack config file not found: $config_file"
        record_notification "slack" "$priority" "$subject" "failed" "Config file not found"
        return 1
    fi

    local slack_enabled=$(jq -r '.notifications.slack.enabled // false' "$config_file")
    if [[ "$slack_enabled" != "true" ]]; then
        log_notification "INFO" "slack" "Slack notifications disabled, skipping: [$priority] $subject"
        record_notification "slack" "$priority" "$subject" "skipped" "Slack notifications disabled"
        return 0
    fi

    # Get Slack configuration
    local webhook_url=$(jq -r '.notifications.slack.webhook_url // ""' "$config_file")
    local channel=$(jq -r '.notifications.slack.channel // "#dependency-updates"' "$config_file")

    if [[ -z "$webhook_url" || "$webhook_url" == "null" ]]; then
        log_notification "WARNING" "slack" "No Slack webhook URL configured"
        record_notification "slack" "$priority" "$subject" "failed" "No webhook URL configured"
        return 1
    fi

    # Create Slack message with color based on priority
    local color="#36a64f"  # green for low
    case "$priority" in
        "medium") color="#ff9500" ;;  # orange
        "high") color="#ff0000" ;;     # red
        "critical") color="#8b0000" ;; # dark red
    esac

    local slack_payload=$(jq -n \
        --arg channel "$channel" \
        --arg subject "$subject" \
        --arg message "$message" \
        --arg color "$color" \
        '{
            "channel": $channel,
            "attachments": [
                {
                    "color": $color,
                    "title": $subject,
                    "text": $message,
                    "footer": "Puzzle71Solver Dependency Updates",
                    "ts": (now | floor)
                }
            ]
        }')

    # Send to Slack
    if command -v curl >/dev/null 2>&1; then
        local response=$(curl -s -X POST -H "Content-Type: application/json" -d "$slack_payload" "$webhook_url" 2>/dev/null || echo "")
        if [[ -z "$response" ]] || echo "$response" | grep -q "ok"; then
            log_notification "INFO" "slack" "Slack notification sent successfully to $channel"
            record_notification "slack" "$priority" "$subject" "success"
            return 0
        else
            log_notification "ERROR" "slack" "Failed to send Slack notification: $response"
            record_notification "slack" "$priority" "$subject" "failed" "Slack API error: $response"
            return 1
        fi
    else
        log_notification "WARNING" "slack" "curl command not available, cannot send Slack notification"
        record_notification "slack" "$priority" "$subject" "failed" "curl command not available"
        return 1
    fi
}

# Send multi-channel notification
send_notification() {
    local priority="$1"
    local subject="$2"
    local message="$3"
    local config_file="$4"
    local dry_run="${5:-false}"

    local notification_sent=false
    local errors=()

    # Always send console notification
    send_console_notification "$priority" "$subject" "$message" "$dry_run"
    notification_sent=true

    # Send file notification
    if send_file_notification "$priority" "$subject" "$message" "$config_file" "$dry_run"; then
        notification_sent=true
    else
        errors+=("file notification failed")
    fi

    # Send email notification
    if send_email_notification "$priority" "$subject" "$message" "$config_file" "$dry_run"; then
        notification_sent=true
    else
        errors+=("email notification failed")
    fi

    # Send Slack notification
    if send_slack_notification "$priority" "$subject" "$message" "$config_file" "$dry_run"; then
        notification_sent=true
    else
        errors+=("slack notification failed")
    fi

    # Return success if at least one notification was sent
    if [[ "$notification_sent" == "true" ]]; then
        if [[ ${#errors[@]} -gt 0 ]]; then
            log_notification "WARNING" "multi" "Some notification channels failed: ${errors[*]}"
            return 1  # Partial success
        else
            log_notification "INFO" "multi" "All notification channels succeeded"
            return 0  # Full success
        fi
    else
        log_notification "ERROR" "multi" "All notification channels failed"
        return 1  # Complete failure
    fi
}

# Send dependency update notification
send_update_notification() {
    local event_type="$1"
    local details="$2"
    local config_file="$3"
    local dry_run="${4:-false}"

    local subject=""
    local message=""
    local priority="medium"

    case "$event_type" in
        "update_available")
            subject="Dependency Updates Available"
            message="New dependency updates are available for review:\n\n$details"
            priority="low"
            ;;
        "update_started")
            subject="Dependency Update Started"
            message="Dependency update process has started:\n\n$details"
            priority="medium"
            ;;
        "update_completed")
            subject="Dependency Update Completed"
            message="Dependency update process completed successfully:\n\n$details"
            priority="low"
            ;;
        "update_failed")
            subject="Dependency Update Failed"
            message="Dependency update process failed:\n\n$details"
            priority="high"
            ;;
        "rollback_performed")
            subject="Dependency Rollback Performed"
            message="Emergency rollback was performed due to compatibility issues:\n\n$details"
            priority="critical"
            ;;
        "security_update")
            subject="Security Update Available"
            message="Critical security updates are available:\n\n$details"
            priority="high"
            ;;
        "compatibility_issue")
            subject="Compatibility Issue Detected"
            message="Dependency compatibility issues detected:\n\n$details"
            priority="high"
            ;;
        *)
            subject="Dependency Update Notification"
            message="General dependency update notification:\n\n$details"
            priority="medium"
            ;;
    esac

    send_notification "$priority" "$subject" "$message" "$config_file" "$dry_run"
}

# Get notification history
get_notification_history() {
    local limit="${1:-10}"
    local history_file="$NOTIFICATION_CACHE_DIR/notification_history.json"

    if [[ ! -f "$history_file" ]]; then
        echo "No notification history found"
        return 1
    fi

    jq --argjson limit "$limit" \
        '{
            summary: {
                total: .total_notifications,
                successful: .successful_notifications,
                failed: .failed_notifications,
                by_type: .notifications_by_type,
                by_priority: .notifications_by_priority
            },
            recent: .recent_notifications[0:$limit]
        }' "$history_file"
}

# Initialize the notification system when this script is sourced
init_notification_system

# Export functions for use in other scripts
export -f init_notification_system
export -f send_console_notification
export -f send_file_notification
export -f send_email_notification
export -f send_slack_notification
export -f send_notification
export -f send_update_notification
export -f get_notification_history