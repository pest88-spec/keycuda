#!/bin/bash

# Integration Monitoring and Alerting System
# Implements T062: Real-time metrics dashboard for integration operations

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
MONITOR_DIR="$PROJECT_ROOT/monitoring"
LOG_DIR="$PROJECT_ROOT/logs/monitoring"
METRICS_DIR="$MONITOR_DIR/metrics"
ALERTS_DIR="$MONITOR_DIR/alerts"
DASHBOARD_DIR="$MONITOR_DIR/dashboard"

# Configuration
readonly MONITOR_INTERVAL=30  # seconds
readonly METRICS_RETENTION_DAYS=30
readonly ALERT_THRESHOLD_CPU=80.0
readonly ALERT_THRESHOLD_MEMORY=85.0
readonly ALERT_THRESHOLD_DISK=90.0
readonly ALERT_THRESHOLD_BUILD_FAILURE=5.0  # failure rate %
readonly INTEGRATION_TIMEOUT=600  # 10 minutes

# Ensure directories exist
mkdir -p "$MONITOR_DIR" "$LOG_DIR" "$METRICS_DIR" "$ALERTS_DIR" "$DASHBOARD_DIR"

# Color codes
readonly RED='\033[0;31m'
readonly GREEN='\033[0;32m'
readonly YELLOW='\033[1;33m'
readonly BLUE='\033[0;34m'
readonly NC='\033[0m'

# Logging
log_info() {
    echo -e "${BLUE}[INFO]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1" | tee -a "$LOG_DIR/monitoring.log"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1" | tee -a "$LOG_DIR/monitoring.log"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1" | tee -a "$LOG_DIR/monitoring.log"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1" | tee -a "$LOG_DIR/monitoring.log"
}

# System metrics collection
collect_system_metrics() {
    local timestamp=$(date -Iseconds)
    local metrics_file="$METRICS_DIR/system_metrics_$(date +%Y%m%d).json"

    # Get system metrics
    local cpu_usage=$(top -bn1 | grep "Cpu(s)" | awk '{print $2}' | sed 's/%us,//' || echo "0.0")
    local memory_usage=$(free | grep Mem | awk '{printf "%.1f", $3/$2 * 100.0}' || echo "0.0")
    local disk_usage=$(df -h / | awk 'NR==2 {print $5}' | sed 's/%//' || echo "0.0")
    local load_avg=$(uptime | awk -F'load average:' '{print $2}' | awk '{print $1}' | sed 's/,//' || echo "0.0")

    # Get GPU metrics if available
    local gpu_usage="0.0"
    local gpu_memory="0.0"
    local gpu_temp="0.0"

    if command -v nvidia-smi &> /dev/null; then
        gpu_usage=$(nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits | head -1 || echo "0.0")
        gpu_memory=$(nvidia-smi --query-gpu=utilization.memory --format=csv,noheader,nounits | head -1 || echo "0.0")
        gpu_temp=$(nvidia-smi --query-gpu=temperature.gpu --format=csv,noheader,nounits | head -1 || echo "0.0")
    fi

    # Create metrics JSON
    local metrics=$(cat << EOF
{
    "timestamp": "$timestamp",
    "system": {
        "cpu_usage_percent": $cpu_usage,
        "memory_usage_percent": $memory_usage,
        "disk_usage_percent": $disk_usage,
        "load_average": $load_avg
    },
    "gpu": {
        "usage_percent": $gpu_usage,
        "memory_usage_percent": $gpu_memory,
        "temperature_celsius": $gpu_temp
    }
}
EOF
)

    # Append to metrics file
    echo "$metrics" >> "$metrics_file"

    # Check alert thresholds
    check_system_alerts "$cpu_usage" "$memory_usage" "$disk_usage" "$gpu_usage" "$gpu_temp"
}

# Integration metrics collection
collect_integration_metrics() {
    local timestamp=$(date -Iseconds)
    local metrics_file="$METRICS_DIR/integration_metrics_$(date +%Y%m%d).json"

    # Count integration files
    local integration_files=$(find "$PROJECT_ROOT/src/extracted" -type f -name "*.cpp" -o -name "*.h" | wc -l)
    local integration_size=$(du -sb "$PROJECT_ROOT/src/extracted" | cut -f1 || echo "0")

    # Check build status
    local build_status="unknown"
    local build_time=0
    local build_success_rate=100.0

    if [[ -f "$PROJECT_ROOT/build/CMakeCache.txt" ]]; then
        # Get recent build results
        local recent_builds=$(find "$PROJECT_ROOT/logs" -name "*build*.log" -mtime -1 | wc -l)
        local successful_builds=$(grep -l "BUILD SUCCESSFUL" "$PROJECT_ROOT/logs"/*build*.log 2>/dev/null | wc -l || echo "0")

        if [[ $recent_builds -gt 0 ]]; then
            build_success_rate=$(echo "scale=2; $successful_builds * 100 / $recent_builds" | bc -l)
            build_status="success"
        else
            build_status="no_recent_builds"
        fi
    fi

    # Check integration health
    local integration_health="healthy"
    local health_issues=()

    # Check if extracted libraries are properly integrated
    if [[ ! -d "$PROJECT_ROOT/src/extracted/secp256k1-zkp" ]]; then
        integration_health="unhealthy"
        health_issues+=("secp256k1-zkp not extracted")
    fi

    # Check attribution coverage
    local attribution_files=$(find "$PROJECT_ROOT/src/extracted" -name "attribution*" | wc -l)
    if [[ $attribution_files -eq 0 ]]; then
        integration_health="degraded"
        health_issues+=("missing attribution files")
    fi

    # Create metrics JSON
    local metrics=$(cat << EOF
{
    "timestamp": "$timestamp",
    "integration": {
        "total_files": $integration_files,
        "total_size_bytes": $integration_size,
        "build_status": "$build_status",
        "build_success_rate": $build_success_rate,
        "health_status": "$integration_health",
        "health_issues": $(printf '%s\n' "${health_issues[@]}" | jq -R . | jq -s .)
    }
}
EOF
)

    # Append to metrics file
    echo "$metrics" >> "$metrics_file"

    # Check integration alerts
    check_integration_alerts "$build_success_rate" "$integration_health" "${health_issues[@]}"
}

# Alert system
check_system_alerts() {
    local cpu_usage="$1"
    local memory_usage="$2"
    local disk_usage="$3"
    local gpu_usage="$4"
    local gpu_temp="$5"

    local alerts=()

    # CPU alerts
    if (( $(echo "$cpu_usage > $ALERT_THRESHOLD_CPU" | bc -l) )); then
        alerts+=("CRITICAL: High CPU usage: ${cpu_usage}%")
    fi

    # Memory alerts
    if (( $(echo "$memory_usage > $ALERT_THRESHOLD_MEMORY" | bc -l) )); then
        alerts+=("CRITICAL: High memory usage: ${memory_usage}%")
    fi

    # Disk alerts
    if (( $(echo "$disk_usage > $ALERT_THRESHOLD_DISK" | bc -l) )); then
        alerts+=("CRITICAL: High disk usage: ${disk_usage}%")
    fi

    # GPU alerts
    if (( $(echo "$gpu_temp > 85" | bc -l) )); then
        alerts+=("WARNING: High GPU temperature: ${gpu_temp}°C")
    fi

    # Send alerts if any
    if [[ ${#alerts[@]} -gt 0 ]]; then
        send_alert "system" "${alerts[@]}"
    fi
}

check_integration_alerts() {
    local build_success_rate="$1"
    local integration_health="$2"
    shift 2
    local health_issues=("$@")

    local alerts=()

    # Build success rate alerts
    if (( $(echo "$build_success_rate < $ALERT_THRESHOLD_BUILD_FAILURE" | bc -l) )); then
        alerts+=("CRITICAL: Low build success rate: ${build_success_rate}%")
    fi

    # Integration health alerts
    if [[ "$integration_health" != "healthy" ]]; then
        alerts+=("WARNING: Integration health: $integration_health")
        for issue in "${health_issues[@]}"; do
            alerts+=("INFO: Health issue: $issue")
        done
    fi

    # Send alerts if any
    if [[ ${#alerts[@]} -gt 0 ]]; then
        send_alert "integration" "${alerts[@]}"
    fi
}

send_alert() {
    local alert_type="$1"
    shift
    local messages=("$@")

    local timestamp=$(date -Iseconds)
    local alert_file="$ALERTS_DIR/alert_$(date +%Y%m%d_%H%M%S).json"

    for message in "${messages[@]}"; do
        log_warning "ALERT [$alert_type]: $message"

        # Create alert JSON
        cat >> "$alert_file" << EOF
{
    "timestamp": "$timestamp",
    "type": "$alert_type",
    "severity": "$(echo "$message" | awk '{print $1}')",
    "message": "$(echo "$message" | cut -d' ' -f2-)",
    "source": "integration_monitoring",
    "acknowledged": false
}
EOF

        # Add comma for multiple alerts (except last one)
        if [[ ${#messages[@]} -gt 1 && "$message" != "${messages[-1]}" ]]; then
            echo "," >> "$alert_file"
        fi
    done

    # Could add email, Slack, or other notification integrations here
}

# Real-time dashboard generation
generate_dashboard() {
    local dashboard_file="$DASHBOARD_DIR/index.html"
    local current_time=$(date '+%Y-%m-%d %H:%M:%S')

    # Get latest metrics
    local latest_system_metrics=$(get_latest_metrics "system")
    local latest_integration_metrics=$(get_latest_metrics "integration")

    # Generate HTML dashboard
    cat > "$dashboard_file" << EOF
<!DOCTYPE html>
<html>
<head>
    <title>Integration Monitoring Dashboard</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <script src="https://cdn.jsdelivr.net/npm/moment@2.29.4/moment.min.js"></script>
    <style>
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            margin: 0;
            padding: 20px;
            background-color: #f5f5f5;
        }
        .header {
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            padding: 20px;
            border-radius: 10px;
            margin-bottom: 20px;
            box-shadow: 0 4px 6px rgba(0,0,0,0.1);
        }
        .metric-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
            gap: 20px;
            margin-bottom: 20px;
        }
        .metric-card {
            background: white;
            padding: 20px;
            border-radius: 10px;
            box-shadow: 0 2px 4px rgba(0,0,0,0.1);
            transition: transform 0.2s;
        }
        .metric-card:hover {
            transform: translateY(-2px);
            box-shadow: 0 4px 8px rgba(0,0,0,0.15);
        }
        .metric-value {
            font-size: 2em;
            font-weight: bold;
            color: #333;
        }
        .metric-label {
            color: #666;
            margin-top: 5px;
        }
        .status-good { color: #27ae60; }
        .status-warning { color: #f39c12; }
        .status-critical { color: #e74c3c; }
        .chart-container {
            background: white;
            padding: 20px;
            border-radius: 10px;
            margin-bottom: 20px;
            box-shadow: 0 2px 4px rgba(0,0,0,0.1);
        }
        .alert-list {
            background: white;
            padding: 20px;
            border-radius: 10px;
            box-shadow: 0 2px 4px rgba(0,0,0,0.1);
        }
        .alert-item {
            padding: 10px;
            margin: 5px 0;
            border-left: 4px solid #ddd;
            border-radius: 4px;
        }
        .alert-critical {
            border-left-color: #e74c3c;
            background-color: #fdf2f2;
        }
        .alert-warning {
            border-left-color: #f39c12;
            background-color: #fef9e7;
        }
        .alert-info {
            border-left-color: #3498db;
            background-color: #ebf3fd;
        }
        .refresh-info {
            text-align: center;
            color: #666;
            margin-top: 20px;
        }
        h2 {
            color: #333;
            margin-top: 0;
        }
    </style>
</head>
<body>
    <div class="header">
        <h1>🔧 Integration Monitoring Dashboard</h1>
        <p>Real-time monitoring of third-party dependencies integration</p>
        <p>Last updated: $current_time</p>
    </div>

    <div class="metric-grid">
        <div class="metric-card">
            <h3>System Status</h3>
            <div class="metric-value status-good" id="system-status">Healthy</div>
            <div class="metric-label">Overall system health</div>
        </div>
        <div class="metric-card">
            <h3>CPU Usage</h3>
            <div class="metric-value" id="cpu-usage">0%</div>
            <div class="metric-label">Processor utilization</div>
        </div>
        <div class="metric-card">
            <h3>Memory Usage</h3>
            <div class="metric-value" id="memory-usage">0%</div>
            <div class="metric-label">RAM utilization</div>
        </div>
        <div class="metric-card">
            <h3>Disk Usage</h3>
            <div class="metric-value" id="disk-usage">0%</div>
            <div class="metric-label">Storage utilization</div>
        </div>
        <div class="metric-card">
            <h3>Build Success Rate</h3>
            <div class="metric-value status-good" id="build-success-rate">100%</div>
            <div class="metric-label">Recent build success rate</div>
        </div>
        <div class="metric-card">
            <h3>Integration Health</h3>
            <div class="metric-value status-good" id="integration-health">Healthy</div>
            <div class="metric-label">Dependencies integration status</div>
        </div>
    </div>

    <div class="chart-container">
        <h2>System Performance (Last Hour)</h2>
        <canvas id="performanceChart" width="400" height="200"></canvas>
    </div>

    <div class="chart-container">
        <h2>Build Success Rate Trend</h2>
        <canvas id="buildChart" width="400" height="200"></canvas>
    </div>

    <div class="alert-list">
        <h2>Recent Alerts</h2>
        <div id="alerts-container">
            <p>No recent alerts</p>
        </div>
    </div>

    <div class="refresh-info">
        <p>🔄 Auto-refresh every 30 seconds | Page reloads every 5 minutes</p>
    </div>

    <script>
        // Update metrics with latest data
        function updateMetrics() {
            $latest_system_metrics
            $latest_integration_metrics
        }

        // Initialize charts
        const performanceCtx = document.getElementById('performanceChart').getContext('2d');
        const performanceChart = new Chart(performanceCtx, {
            type: 'line',
            data: {
                labels: [],
                datasets: [{
                    label: 'CPU Usage (%)',
                    data: [],
                    borderColor: '#e74c3c',
                    backgroundColor: 'rgba(231, 76, 60, 0.1)',
                    tension: 0.4
                }, {
                    label: 'Memory Usage (%)',
                    data: [],
                    borderColor: '#3498db',
                    backgroundColor: 'rgba(52, 152, 219, 0.1)',
                    tension: 0.4
                }]
            },
            options: {
                responsive: true,
                scales: {
                    y: {
                        beginAtZero: true,
                        max: 100
                    }
                }
            }
        });

        const buildCtx = document.getElementById('buildChart').getContext('2d');
        const buildChart = new Chart(buildCtx, {
            type: 'line',
            data: {
                labels: [],
                datasets: [{
                    label: 'Build Success Rate (%)',
                    data: [],
                    borderColor: '#27ae60',
                    backgroundColor: 'rgba(39, 174, 96, 0.1)',
                    tension: 0.4
                }]
            },
            options: {
                responsive: true,
                scales: {
                    y: {
                        beginAtZero: true,
                        max: 100
                    }
                }
            }
        });

        // Auto-refresh
        setInterval(function() {
            location.reload();
        }, 300000); // 5 minutes

        updateMetrics();
    </script>
</body>
</html>
EOF

    log_success "Dashboard generated: $dashboard_file"
}

get_latest_metrics() {
    local metric_type="$1"
    local today=$(date +%Y%m%d)
    local metrics_file="$METRICS_DIR/${metric_type}_metrics_${today}.json"

    if [[ -f "$metrics_file" ]]; then
        local latest_metric=$(tail -1 "$metrics_file")
        echo "const latest${metric_type^}Metrics = $latest_metric;"
    else
        echo "const latest${metric_type^}Metrics = {};"
    fi
}

# Background monitoring loop
start_monitoring() {
    log_info "Starting integration monitoring daemon"
    log_info "Monitoring interval: ${MONITOR_INTERVAL} seconds"

    while true; do
        collect_system_metrics
        collect_integration_metrics
        generate_dashboard

        # Clean old metrics files
        find "$METRICS_DIR" -name "*.json" -mtime +$METRICS_RETENTION_DAYS -delete 2>/dev/null || true

        sleep $MONITOR_INTERVAL
    done
}

# Check integration status
check_integration_status() {
    log_info "Checking integration status"

    local status="healthy"
    local issues=()

    # Check required directories
    if [[ ! -d "$PROJECT_ROOT/src/extracted" ]]; then
        status="unhealthy"
        issues+=("Missing extracted directory")
    fi

    if [[ ! -d "$PROJECT_ROOT/src/extracted/secp256k1-zkp" ]]; then
        status="degraded"
        issues+=("Missing secp256k1-zkp integration")
    fi

    # Check build system
    if [[ ! -f "$PROJECT_ROOT/CMakeLists.txt" ]]; then
        status="unhealthy"
        issues+=("Missing CMakeLists.txt")
    fi

    # Check attribution files
    local attribution_count=$(find "$PROJECT_ROOT/src/extracted" -name "*attribution*" | wc -l)
    if [[ $attribution_count -eq 0 ]]; then
        status="degraded"
        issues+=("No attribution files found")
    fi

    echo "Integration Status: $status"
    if [[ ${#issues[@]} -gt 0 ]]; then
        echo "Issues:"
        for issue in "${issues[@]}"; do
            echo "  - $issue"
        done
    fi

    return 0
}

# Performance monitoring
monitor_integration_performance() {
    local test_name="$1"
    local start_time=$(date +%s.%N)

    log_info "Monitoring integration performance: $test_name"

    # Monitor during integration operation
    (
        while true; do
            collect_system_metrics
            sleep 5
        done
    ) &
    local monitor_pid=$!

    # Wait for integration to complete (with timeout)
    local timeout_count=0
    while [[ $timeout_count -lt $((INTEGRATION_TIMEOUT / 5)) ]]; do
        if ! kill -0 $monitor_pid 2>/dev/null; then
            break
        fi
        sleep 5
        ((timeout_count++))
    done

    # Stop monitoring
    kill $monitor_pid 2>/dev/null || true

    local end_time=$(date +%s.%N)
    local duration=$(echo "$end_time - $start_time" | bc -l)

    log_info "Integration $test_name completed in ${duration}s"

    # Generate performance report
    local report_file="$LOG_DIR/performance_${test_name}_$(date +%Y%m%d_%H%M%S).json"
    cat > "$report_file" << EOF
{
    "test_name": "$test_name",
    "start_time": "$(date -d@$start_time -Iseconds)",
    "end_time": "$(date -d@$end_time -Iseconds)",
    "duration_seconds": $duration,
    "monitoring_data": "See metrics files for detailed performance data"
}
EOF
}

# Main execution
main() {
    local command="${1:-start}"

    case "$command" in
        "start")
            start_monitoring
            ;;
        "check")
            check_integration_status
            ;;
        "monitor")
            local operation="$2"
            monitor_integration_performance "$operation"
            ;;
        "dashboard")
            generate_dashboard
            echo "Dashboard generated: $DASHBOARD_DIR/index.html"
            ;;
        "collect")
            collect_system_metrics
            collect_integration_metrics
            ;;
        "alerts")
            # Show recent alerts
            find "$ALERTS_DIR" -name "alert_*.json" -mtime -1 -exec cat {} \; | jq '.'
            ;;
        "clean")
            find "$METRICS_DIR" -name "*.json" -mtime +7 -delete 2>/dev/null || true
            find "$ALERTS_DIR" -name "alert_*.json" -mtime +7 -delete 2>/dev/null || true
            log_info "Old monitoring data cleaned up"
            ;;
        "help"|*)
            echo "Usage: $0 {start|check|monitor|dashboard|collect|alerts|clean|help}"
            echo ""
            echo "Commands:"
            echo "  start      - Start monitoring daemon (runs continuously)"
            echo "  check      - Check current integration status"
            echo "  monitor    - Monitor specific integration operation"
            echo "  dashboard  - Generate real-time dashboard"
            echo "  collect    - Collect metrics once"
            echo "  alerts     - Show recent alerts"
            echo "  clean      - Clean old monitoring data"
            echo "  help       - Show this help message"
            exit 0
            ;;
    esac
}

# Execute main function
main "$@"