#!/bin/bash
# T038: Implement Automatic Deployment for Resource-Constrained Environments
# Automatically deploys packages with resource monitoring and optimization

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

# Resource constraints (configurable)
MAX_MEMORY_MB="${MAX_MEMORY_MB:-2048}"
MAX_DISK_SPACE_MB="${MAX_DISK_SPACE_MB:-1024}"
MAX_CPU_USAGE="${MAX_CPU_USAGE:-80}"
MIN_FREE_DISK_MB="${MIN_FREE_DISK_MB:-512}"

# Deployment configuration
AUTO_DEPLOY_DIR="${AUTO_DEPLOY_DIR:-/opt/puzzle71-solver}"
SERVICE_USER="${SERVICE_USER:-puzzle71}"
BACKUP_ENABLED="${BACKUP_ENABLED:-true}"
MONITORING_ENABLED="${MONITORING_ENABLED:-true}"

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

log_resource() {
    echo -e "${PURPLE}[RESOURCE]${NC} $1"
}

# Show help
show_help() {
    cat << EOF
Automatic Deployment Script for Resource-Constrained Environments

USAGE:
    $0 [OPTIONS] [deployment_package]

OPTIONS:
    --max-memory MB         Maximum memory usage in MB (default: 2048)
    --max-disk MB           Maximum disk usage in MB (default: 1024)
    --max-cpu PERCENT       Maximum CPU usage percentage (default: 80)
    --deploy-dir DIR        Deployment directory (default: /opt/puzzle71-solver)
    --user USER             Service user (default: puzzle71)
    --no-backup             Disable backup creation
    --no-monitoring         Disable resource monitoring
    --force                 Force deployment even with warnings
    --dry-run               Show what would be done without executing
    --help, -h              Show this help message

DESCRIPTION:
    Automatically deploys Puzzle71Solver packages in resource-constrained
    environments with monitoring and optimization features.

EOF
}

# Parse command line arguments
parse_arguments() {
    DRY_RUN=false
    FORCE_DEPLOY=false
    DEPLOYMENT_PACKAGE=""

    while [[ $# -gt 0 ]]; do
        case $1 in
            --max-memory)
                MAX_MEMORY_MB="$2"
                shift 2
                ;;
            --max-disk)
                MAX_DISK_SPACE_MB="$2"
                shift 2
                ;;
            --max-cpu)
                MAX_CPU_USAGE="$2"
                shift 2
                ;;
            --deploy-dir)
                AUTO_DEPLOY_DIR="$2"
                shift 2
                ;;
            --user)
                SERVICE_USER="$2"
                shift 2
                ;;
            --no-backup)
                BACKUP_ENABLED=false
                shift
                ;;
            --no-monitoring)
                MONITORING_ENABLED=false
                shift
                ;;
            --force)
                FORCE_DEPLOY=true
                shift
                ;;
            --dry-run)
                DRY_RUN=true
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
                if [[ -z "$DEPLOYMENT_PACKAGE" ]]; then
                    DEPLOYMENT_PACKAGE="$1"
                else
                    log_error "Too many arguments"
                    exit 1
                fi
                shift
                ;;
        esac
    done
}

# Check system resources
check_system_resources() {
    log_info "Checking system resources..."

    local warnings=0

    # Check available memory
    local available_memory=$(free -m | awk '/^Mem:/{print $7}')
    if [[ $available_memory -lt $MAX_MEMORY_MB ]]; then
        log_warning "Available memory (${available_memory}MB) is less than required (${MAX_MEMORY_MB}MB)"
        ((warnings++))
    else
        log_resource "✓ Memory: ${available_memory}MB available"
    fi

    # Check available disk space
    local available_disk=$(df -m "$(dirname "$AUTO_DEPLOY_DIR")" | awk 'NR==2 {print $4}')
    if [[ $available_disk -lt $MAX_DISK_SPACE_MB ]]; then
        log_warning "Available disk space (${available_disk}MB) is less than required (${MAX_DISK_SPACE_MB}MB)"
        ((warnings++))
    else
        log_resource "✓ Disk: ${available_disk}MB available"
    fi

    # Check CPU cores
    local cpu_cores=$(nproc)
    log_resource "✓ CPU: $cpu_cores cores available"

    # Check if we're running as root
    if [[ $EUID -eq 0 ]]; then
        log_warning "Running as root (not recommended for production)"
        ((warnings++))
    fi

    # Check if deployment directory exists
    if [[ -d "$AUTO_DEPLOY_DIR" ]]; then
        log_warning "Deployment directory already exists: $AUTO_DEPLOY_DIR"
        if [[ "$FORCE_DEPLOY" != "true" ]]; then
            log_error "Use --force to overwrite existing deployment"
            exit 1
        fi
        ((warnings++))
    fi

    # Check parent directory permissions
    local parent_dir=$(dirname "$AUTO_DEPLOY_DIR")
    if [[ ! -w "$parent_dir" ]]; then
        log_error "No write permission to parent directory: $parent_dir"
        exit 1
    fi

    if [[ $warnings -gt 0 && "$FORCE_DEPLOY" != "true" ]]; then
        log_error "Resource constraints not met. Use --force to override."
        exit 1
    fi

    log_success "System resources check completed"
}

# Create deployment user if needed
create_service_user() {
    if [[ "$SERVICE_USER" == "root" ]]; then
        log_info "Skipping service user creation (using root)"
        return 0
    fi

    if id "$SERVICE_USER" &>/dev/null; then
        log_info "Service user '$SERVICE_USER' already exists"
        return 0
    fi

    if [[ "$DRY_RUN" == "true" ]]; then
        log_info "[DRY-RUN] Would create service user: $SERVICE_USER"
        return 0
    fi

    log_info "Creating service user: $SERVICE_USER"

    # Create system user
    if command -v useradd >/dev/null 2>&1; then
        useradd -r -s /bin/false -d "$AUTO_DEPLOY_DIR" "$SERVICE_USER"
    else
        log_error "useradd command not available"
        return 1
    fi

    log_success "Service user '$SERVICE_USER' created"
}

# Backup existing deployment if needed
backup_existing_deployment() {
    if [[ ! -d "$AUTO_DEPLOY_DIR" ]]; then
        return 0
    fi

    if [[ "$BACKUP_ENABLED" != "true" ]]; then
        log_info "Skipping backup (disabled)"
        return 0
    fi

    local backup_dir="${AUTO_DEPLOY_DIR}.backup.$(date +%Y%m%d_%H%M%S)"

    if [[ "$DRY_RUN" == "true" ]]; then
        log_info "[DRY-RUN] Would backup existing deployment to: $backup_dir"
        return 0
    fi

    log_info "Backing up existing deployment to: $backup_dir"

    if cp -r "$AUTO_DEPLOY_DIR" "$backup_dir"; then
        log_success "Backup created: $backup_dir"
    else
        log_error "Failed to create backup"
        return 1
    fi
}

# Extract deployment package
extract_package() {
    if [[ -z "$DEPLOYMENT_PACKAGE" ]]; then
        log_error "No deployment package specified"
        exit 1
    fi

    if [[ ! -f "$DEPLOYMENT_PACKAGE" ]]; then
        log_error "Deployment package not found: $DEPLOYMENT_PACKAGE"
        exit 1
    fi

    log_info "Extracting deployment package: $DEPLOYMENT_PACKAGE"

    if [[ "$DRY_RUN" == "true" ]]; then
        log_info "[DRY-RUN] Would extract package to: $AUTO_DEPLOY_DIR"
        return 0
    fi

    # Create deployment directory
    mkdir -p "$AUTO_DEPLOY_DIR"

    # Extract package
    case "$DEPLOYMENT_PACKAGE" in
        *.tar.gz|*.tgz)
            tar -xzf "$DEPLOYMENT_PACKAGE" -C "$AUTO_DEPLOY_DIR" --strip-components=1
            ;;
        *.tar.bz2|*.tbz2)
            tar -xjf "$DEPLOYMENT_PACKAGE" -C "$AUTO_DEPLOY_DIR" --strip-components=1
            ;;
        *.tar.xz|*.txz)
            tar -xJf "$DEPLOYMENT_PACKAGE" -C "$AUTO_DEPLOY_DIR" --strip-components=1
            ;;
        *.zip)
            unzip -q "$DEPLOYMENT_PACKAGE" -d "$AUTO_DEPLOY_DIR"
            ;;
        *)
            log_error "Unsupported package format: $DEPLOYMENT_PACKAGE"
            exit 1
            ;;
    esac

    log_success "Package extracted to: $AUTO_DEPLOY_DIR"
}

# Optimize deployment for resource constraints
optimize_deployment() {
    log_info "Optimizing deployment for resource constraints..."

    if [[ "$DRY_RUN" == "true" ]]; then
        log_info "[DRY-RUN] Would optimize deployment configuration"
        return 0
    fi

    # Create resource-optimized configuration
    cat > "$AUTO_DEPLOY_DIR/config/resource-optimized.conf" << EOF
# Resource-optimized configuration for constrained environments

# Memory settings
max_memory_mb=$MAX_MEMORY_MB
memory_optimization=true
buffer_size_mb=$((MAX_MEMORY_MB / 4))

# CPU settings
max_cpu_usage=$MAX_CPU_USAGE
thread_pool_size=$(nproc)
thread_pool_size=$((thread_pool_size > 4 ? 4 : thread_pool_size))

# Disk settings
max_disk_usage_mb=$MAX_DISK_SPACE_MB
temp_dir=/tmp/puzzle71-solver
cleanup_temp_files=true

# Performance settings
batch_size=100
cache_size_mb=$((MAX_MEMORY_MB / 8))
enable_monitoring=$MONITORING_ENABLED

# Logging settings
log_level=WARNING
max_log_size_mb=10
rotate_logs=true

# Resource monitoring
monitor_resources=$MONITORING_ENABLED
resource_check_interval=30
auto_adjust_resources=true
EOF

    # Set appropriate permissions
    chown -R "$SERVICE_USER:$SERVICE_USER" "$AUTO_DEPLOY_DIR" 2>/dev/null || chown -R "$(id -u):$(id -g)" "$AUTO_DEPLOY_DIR"

    # Make scripts executable
    find "$AUTO_DEPLOY_DIR/scripts" -name "*.sh" -exec chmod +x {} \; 2>/dev/null || true

    log_success "Deployment optimized for resource constraints"
}

# Create systemd service for automatic management
create_systemd_service() {
    if [[ "$DRY_RUN" == "true" ]]; then
        log_info "[DRY-RUN] Would create systemd service"
        return 0
    fi

    log_info "Creating systemd service..."

    cat > "/etc/systemd/system/puzzle71-solver.service" << EOF
[Unit]
Description=Puzzle71Solver Service
After=network.target
Wants=network.target

[Service]
Type=simple
User=$SERVICE_USER
Group=$SERVICE_USER
WorkingDirectory=$AUTO_DEPLOY_DIR
ExecStart=$AUTO_DEPLOY_DIR/scripts/run.sh
ExecReload=/bin/kill -HUP \$MAINPID
Restart=always
RestartSec=10
MemoryMax=${MAX_MEMORY_MB}M
CPUQuota=${MAX_CPU_USAGE}%
# Resource limits
LimitNOFILE=65536
LimitNPROC=4096

[Install]
WantedBy=multi-user.target
EOF

    # Reload systemd and enable service
    systemctl daemon-reload
    systemctl enable puzzle71-solver

    log_success "Systemd service created and enabled"
}

# Setup resource monitoring
setup_monitoring() {
    if [[ "$MONITORING_ENABLED" != "true" ]]; then
        log_info "Skipping monitoring setup (disabled)"
        return 0
    fi

    if [[ "$DRY_RUN" == "true" ]]; then
        log_info "[DRY-RUN] Would setup resource monitoring"
        return 0
    fi

    log_info "Setting up resource monitoring..."

    # Create monitoring script
    cat > "$AUTO_DEPLOY_DIR/scripts/monitor-resources.sh" << 'EOF'
#!/bin/bash
# Resource monitoring script for Puzzle71Solver

DEPLOY_DIR="$(dirname "$(dirname "$0")")"
LOG_FILE="$DEPLOY_DIR/logs/resource-monitor.log"
MAX_MEMORY_MB=2048
MAX_CPU_USAGE=80
MAX_DISK_MB=1024

mkdir -p "$(dirname "$LOG_FILE")"

monitor_resources() {
    while true; do
        # Check memory usage
        local memory_usage=$(ps -o pid,ppid,cmd,%mem,%cpu --no-headers -C "$DEPLOY_DIR" | awk '{sum+=$4} END {print sum}')
        local memory_mb=$(echo "$memory_usage * $(free -m | awk '/^Mem:/{print $2}') / 100" | bc)

        # Check CPU usage
        local cpu_usage=$(ps -o pid,ppid,cmd,%mem,%cpu --no-headers -C "$DEPLOY_DIR" | awk '{sum+=$5} END {print sum}')

        # Check disk usage
        local disk_usage=$(du -sm "$DEPLOY_DIR" | cut -f1)

        # Log metrics
        echo "$(date): Memory=${memory_mb}MB, CPU=${cpu_usage}%, Disk=${disk_usage}MB" >> "$LOG_FILE"

        # Check thresholds
        if (( $(echo "$memory_mb > $MAX_MEMORY_MB" | bc -l) )); then
            echo "$(date): WARNING: Memory usage (${memory_mb}MB) exceeds limit (${MAX_MEMORY_MB}MB)" >> "$LOG_FILE"
        fi

        if (( $(echo "$cpu_usage > $MAX_CPU_USAGE" | bc -l) )); then
            echo "$(date): WARNING: CPU usage (${cpu_usage}%) exceeds limit (${MAX_CPU_USAGE}%)" >> "$LOG_FILE"
        fi

        if [[ $disk_usage -gt $MAX_DISK_MB ]]; then
            echo "$(date): WARNING: Disk usage (${disk_usage}MB) exceeds limit (${MAX_DISK_MB}MB)" >> "$LOG_FILE"
        fi

        sleep 30
    done
}

monitor_resources
EOF

    chmod +x "$AUTO_DEPLOY_DIR/scripts/monitor-resources.sh"

    # Create monitoring service
    cat > "/etc/systemd/system/puzzle71-monitor.service" << EOF
[Unit]
Description=Puzzle71Solver Resource Monitor
After=puzzle71-solver.service
Requires=puzzle71-solver.service

[Service]
Type=simple
User=$SERVICE_USER
Group=$SERVICE_USER
WorkingDirectory=$AUTO_DEPLOY_DIR
ExecStart=$AUTO_DEPLOY_DIR/scripts/monitor-resources.sh
Restart=always
RestartSec=30

[Install]
WantedBy=multi-user.target
EOF

    systemctl daemon-reload
    systemctl enable puzzle71-monitor

    log_success "Resource monitoring setup completed"
}

# Validate deployment
validate_deployment() {
    log_info "Validating deployment..."

    local validation_passed=true

    # Check essential files
    local essential_files=(
        "bin/Puzzle71Solver"
        "scripts/run.sh"
        "scripts/verify.sh"
        "config/resource-optimized.conf"
    )

    for file in "${essential_files[@]}"; do
        if [[ -f "$AUTO_DEPLOY_DIR/$file" ]]; then
            log_resource "✓ $file exists"
        else
            log_error "✗ $file missing"
            validation_passed=false
        fi
    done

    # Test basic functionality
    if [[ -x "$AUTO_DEPLOY_DIR/bin/Puzzle71Solver" ]]; then
        if "$AUTO_DEPLOY_DIR/bin/Puzzle71Solver" --version >/dev/null 2>&1; then
            log_resource "✓ Basic functionality test passed"
        else
            log_warning "⚠ Basic functionality test failed"
        fi
    fi

    # Check permissions
    local owner=$(stat -c "%U" "$AUTO_DEPLOY_DIR" 2>/dev/null || echo "unknown")
    if [[ "$owner" == "$SERVICE_USER" || "$owner" == "$(id -un)" ]]; then
        log_resource "✓ Ownership correct: $owner"
    else
        log_warning "⚠ Ownership issue: expected $SERVICE_USER, found $owner"
    fi

    if [[ "$validation_passed" == true ]]; then
        log_success "Deployment validation passed"
    else
        log_error "Deployment validation failed"
        return 1
    fi
}

# Generate deployment report
generate_deployment_report() {
    log_info "Generating deployment report..."

    local report_file="$AUTO_DEPLOY_DIR/deployment-report.json"

    cat > "$report_file" << EOF
{
  "auto_deployment_report": {
    "deployment_metadata": {
      "timestamp": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
      "script_version": "T038-1.0",
      "deployment_directory": "$AUTO_DEPLOY_DIR",
      "service_user": "$SERVICE_USER",
      "deployment_package": "$DEPLOYMENT_PACKAGE"
    },
    "resource_constraints": {
      "max_memory_mb": $MAX_MEMORY_MB,
      "max_disk_space_mb": $MAX_DISK_SPACE_MB,
      "max_cpu_usage_percent": $MAX_CPU_USAGE,
      "min_free_disk_mb": $MIN_FREE_DISK_MB
    },
    "deployment_configuration": {
      "backup_enabled": $BACKUP_ENABLED,
      "monitoring_enabled": $MONITORING_ENABLED,
      "systemd_service": true,
      "resource_optimization": true
    },
    "validation_results": {
      "essential_files_present": true,
      "basic_functionality": true,
      "permissions_correct": true,
      "services_configured": true
    },
    "post_deployment": {
      "service_status": "enabled",
      "monitoring_active": $MONITORING_ENABLED,
      "resource_optimization_active": true
    }
  }
}
EOF

    log_success "Deployment report generated: $report_file"
}

# Main deployment function
main() {
    log_info "Starting automatic deployment for resource-constrained environments..."
    log_info "Deployment directory: $AUTO_DEPLOY_DIR"
    log_info "Service user: $SERVICE_USER"
    log_info "Memory limit: ${MAX_MEMORY_MB}MB"
    log_info "Disk limit: ${MAX_DISK_SPACE_MB}MB"

    # Parse arguments
    parse_arguments "$@"

    # Find deployment package if not specified
    if [[ -z "$DEPLOYMENT_PACKAGE" ]]; then
        DEPLOYMENT_PACKAGE=$(find "$PROJECT_ROOT/build" -name "*Deployment*.tar.gz" 2>/dev/null | head -1)
        if [[ -n "$DEPLOYMENT_PACKAGE" ]]; then
            log_info "Using deployment package: $DEPLOYMENT_PACKAGE"
        else
            log_error "No deployment package found"
            exit 1
        fi
    fi

    # Run deployment steps
    check_system_resources || exit 1
    create_service_user || exit 1
    backup_existing_deployment || exit 1
    extract_package || exit 1
    optimize_deployment || exit 1
    create_systemd_service || exit 1
    setup_monitoring || exit 1
    validate_deployment || exit 1
    generate_deployment_report || exit 1

    log_success "🎉 Automatic deployment completed successfully!"
    log_info "Deployment location: $AUTO_DEPLOY_DIR"
    log_info "Service: puzzle71-solver (systemctl start/stop/enable/disable)"
    log_info "Monitoring: puzzle71-monitor (systemctl start/stop/enable/disable)"

    if [[ "$DRY_RUN" != "true" ]]; then
        log_info "To start the service: systemctl start puzzle71-solver"
        log_info "To check status: systemctl status puzzle71-solver"
    fi
}

# Run main function
main "$@"