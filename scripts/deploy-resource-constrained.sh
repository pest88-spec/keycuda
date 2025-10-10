#!/bin/bash

# Puzzle71Solver - Resource-Constrained Deployment Script
#
# Provides automatic deployment optimization for environments with limited resources
# including memory, CPU, disk space, and network bandwidth constraints.
#
# Author: Puzzle71Solver Team
# Created: 2025-10-10
# License: MIT

set -euo pipefail

# Script constants
readonly SCRIPT_NAME="$(basename "$0")"
readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Default resource constraints
DEFAULT_MEMORY_MB=2048          # 2GB minimum memory
DEFAULT_CPU_CORES=2             # 2 CPU cores minimum
DEFAULT_DISK_SPACE_MB=10240     # 10GB minimum disk space
DEFAULT_NETWORK_KBPS=1000       # 1 Mbps minimum network bandwidth

# Deployment modes
MODE_MINIMAL="minimal"          # Smallest possible footprint
MODE_STANDARD="standard"        # Balanced performance
MODE_COMPRESSED="compressed"    # Optimized for network transfer

# Exit codes
readonly EXIT_SUCCESS=0
readonly EXIT_INVALID_ARGS=1
readonly EXIT_INSUFFICIENT_RESOURCES=2
readonly EXIT_DEPLOYMENT_FAILED=3
readonly EXIT_VALIDATION_FAILED=4

# Color output
readonly RED='\033[0;31m'
readonly GREEN='\033[0;32m'
readonly YELLOW='\033[1;33m'
readonly BLUE='\033[0;34m'
readonly CYAN='\033[0;36m'
readonly NC='\033[0m'

# Logging functions
log_info() {
    echo -e "${GREEN}[INFO]${NC} $*" >&2
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $*" >&2
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $*" >&2
}

log_debug() {
    if [[ "${DEBUG:-false}" == "true" ]]; then
        echo -e "${BLUE}[DEBUG]${NC} $*" >&2
    fi
}

log_resource() {
    echo -e "${CYAN}[RESOURCE]${NC} $*" >&2
}

# Show usage information
show_usage() {
    cat << EOF
Usage: $SCRIPT_NAME [OPTIONS] DEPLOYMENT_PACKAGE

Deploy Puzzle71Solver in resource-constrained environments with automatic optimization.

DEPLOYMENT_PACKAGE:
    Path to the deployment package file (tar.gz, zip, or self-extracting installer)

OPTIONS:
    --mode MODE              Deployment mode: minimal, standard, compressed (default: auto-detect)
    --memory-limit MB        Available memory in MB (default: $DEFAULT_MEMORY_MB)
    --cpu-cores COUNT        Available CPU cores (default: $DEFAULT_CPU_CORES)
    --disk-limit MB          Available disk space in MB (default: $DEFAULT_DISK_SPACE_MB)
    --network-limit KBPS     Network bandwidth limit in KB/s (default: $DEFAULT_NETWORK_KBPS)
    --target-dir DIR         Target installation directory (default: ./puzzle71-deployment)
    --force-overwrite        Overwrite existing installation
    --no-verification        Skip package verification (not recommended)
    --dry-run                Show what would be deployed without actually deploying
    --verbose                Enable verbose logging
    --debug                  Enable debug output
    --help                   Show this help message

RESOURCE MODES:
    minimal    - Smallest footprint, minimal dependencies, lowest resource usage
    standard   - Balanced performance with reasonable resource usage
    compressed - Optimized for network transfer, may require more CPU during extraction

ENVIRONMENT PROFILES:
    --iot-profile           Optimized for IoT devices (memory: 512MB, CPU: 1 core)
    --embedded-profile      Optimized for embedded systems (memory: 256MB, CPU: 1 core)
    --cloud-profile         Optimized for cloud instances (memory: 4096MB, CPU: 4 cores)
    --edge-profile          Optimized for edge computing (memory: 1024MB, CPU: 2 cores)

EXAMPLES:
    $SCRIPT_NAME --mode minimal --memory-limit 1024 puzzle71solver-package.tar.gz
    $SCRIPT_NAME --iot-profile deployment-package.zip
    $SCRIPT_NAME --dry-run --verbose puzzle71solver-v0.1.0-installer.run

EOF
}

# Parse command line arguments
parse_args() {
    DEPLOYMENT_PACKAGE=""
    DEPLOYMENT_MODE=""
    MEMORY_LIMIT_MB="$DEFAULT_MEMORY_MB"
    CPU_CORES="$DEFAULT_CPU_CORES"
    DISK_LIMIT_MB="$DEFAULT_DISK_SPACE_MB"
    NETWORK_LIMIT_KBPS="$DEFAULT_NETWORK_KBPS"
    TARGET_DIR="./puzzle71-deployment"
    FORCE_OVERWRITE="false"
    NO_VERIFICATION="false"
    DRY_RUN="false"
    VERBOSE="false"
    DEBUG="false"

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --mode)
                DEPLOYMENT_MODE="$2"
                shift 2
                ;;
            --memory-limit)
                MEMORY_LIMIT_MB="$2"
                shift 2
                ;;
            --cpu-cores)
                CPU_CORES="$2"
                shift 2
                ;;
            --disk-limit)
                DISK_LIMIT_MB="$2"
                shift 2
                ;;
            --network-limit)
                NETWORK_LIMIT_KBPS="$2"
                shift 2
                ;;
            --target-dir)
                TARGET_DIR="$2"
                shift 2
                ;;
            --force-overwrite)
                FORCE_OVERWRITE="true"
                shift
                ;;
            --no-verification)
                NO_VERIFICATION="true"
                shift
                ;;
            --dry-run)
                DRY_RUN="true"
                shift
                ;;
            --verbose)
                VERBOSE="true"
                shift
                ;;
            --debug)
                DEBUG="true"
                shift
                ;;
            --iot-profile)
                MEMORY_LIMIT_MB=512
                CPU_CORES=1
                DISK_LIMIT_MB=2048
                DEPLOYMENT_MODE="$MODE_MINIMAL"
                shift
                ;;
            --embedded-profile)
                MEMORY_LIMIT_MB=256
                CPU_CORES=1
                DISK_LIMIT_MB=1024
                DEPLOYMENT_MODE="$MODE_MINIMAL"
                shift
                ;;
            --cloud-profile)
                MEMORY_LIMIT_MB=4096
                CPU_CORES=4
                DISK_LIMIT_MB=20480
                DEPLOYMENT_MODE="$MODE_STANDARD"
                shift
                ;;
            --edge-profile)
                MEMORY_LIMIT_MB=1024
                CPU_CORES=2
                DISK_LIMIT_MB=4096
                DEPLOYMENT_MODE="$MODE_STANDARD"
                shift
                ;;
            --help)
                show_usage
                exit $EXIT_SUCCESS
                ;;
            -*)
                log_error "Unknown option: $1"
                show_usage
                exit $EXIT_INVALID_ARGS
                ;;
            *)
                if [[ -z "$DEPLOYMENT_PACKAGE" ]]; then
                    DEPLOYMENT_PACKAGE="$1"
                else
                    log_error "Multiple deployment packages specified"
                    show_usage
                    exit $EXIT_INVALID_ARGS
                fi
                shift
                ;;
        esac
    done

    # Validate arguments
    if [[ -z "$DEPLOYMENT_PACKAGE" ]]; then
        log_error "No deployment package specified"
        show_usage
        exit $EXIT_INVALID_ARGS
    fi

    if [[ ! "$DEPLOYMENT_MODE" =~ ^(minimal|standard|compressed|)$ ]]; then
        log_error "Invalid deployment mode: $DEPLOYMENT_MODE. Supported modes: minimal, standard, compressed"
        exit $EXIT_INVALID_ARGS
    fi

    # Validate numeric arguments
    if ! [[ "$MEMORY_LIMIT_MB" =~ ^[0-9]+$ ]] || [[ "$MEMORY_LIMIT_MB" -lt 128 ]]; then
        log_error "Invalid memory limit: $MEMORY_LIMIT_MB MB (minimum: 128)"
        exit $EXIT_INVALID_ARGS
    fi

    if ! [[ "$CPU_CORES" =~ ^[0-9]+$ ]] || [[ "$CPU_CORES" -lt 1 ]]; then
        log_error "Invalid CPU cores: $CPU_CORES (minimum: 1)"
        exit $EXIT_INVALID_ARGS
    fi

    if ! [[ "$DISK_LIMIT_MB" =~ ^[0-9]+$ ]] || [[ "$DISK_LIMIT_MB" -lt 512 ]]; then
        log_error "Invalid disk limit: $DISK_LIMIT_MB MB (minimum: 512)"
        exit $EXIT_INVALID_ARGS
    fi

    # Convert to absolute paths
    DEPLOYMENT_PACKAGE="$(realpath "$DEPLOYMENT_PACKAGE")"
    TARGET_DIR="$(realpath "$TARGET_DIR" 2>/dev/null || echo "$TARGET_DIR")"

    # Auto-detect deployment mode if not specified
    if [[ -z "$DEPLOYMENT_MODE" ]]; then
        detect_deployment_mode
    fi

    log_debug "Deployment package: $DEPLOYMENT_PACKAGE"
    log_debug "Deployment mode: $DEPLOYMENT_MODE"
    log_debug "Memory limit: ${MEMORY_LIMIT_MB}MB"
    log_debug "CPU cores: $CPU_CORES"
    log_debug "Disk limit: ${DISK_LIMIT_MB}MB"
    log_debug "Network limit: ${NETWORK_LIMIT_KBPS}KB/s"
    log_debug "Target directory: $TARGET_DIR"
}

# Auto-detect optimal deployment mode based on resources
detect_deployment_mode() {
    log_resource "Auto-detecting deployment mode based on available resources..."

    local memory_score=$((MEMORY_LIMIT_MB / 1024))  # GB
    local cpu_score=$CPU_CORES
    local disk_score=$((DISK_LIMIT_MB / 1024))     # GB

    log_debug "Resource scores - Memory: $memory_score, CPU: $cpu_score, Disk: $disk_score"

    if [[ $memory_score -le 1 ]] && [[ $cpu_score -le 2 ]] && [[ $disk_score -le 2 ]]; then
        DEPLOYMENT_MODE="$MODE_MINIMAL"
        log_resource "Detected minimal resource environment - using minimal mode"
    elif [[ $memory_score -ge 4 ]] && [[ $cpu_score -ge 4 ]] && [[ $disk_score -ge 10 ]]; then
        DEPLOYMENT_MODE="$MODE_STANDARD"
        log_resource "Detected adequate resources - using standard mode"
    else
        DEPLOYMENT_MODE="$MODE_COMPRESSED"
        log_resource "Detected constrained network or storage - using compressed mode"
    fi
}

# Check prerequisites and environment
check_prerequisites() {
    log_info "Checking deployment prerequisites..."

    local missing_tools=()

    # Check for required tools
    for tool in tar gzip unzip find; do
        if ! command -v "$tool" >/dev/null 2>&1; then
            missing_tools+=("$tool")
        fi
    done

    # Check for optional tools based on package format
    if [[ "$DEPLOYMENT_PACKAGE" =~ \.zip$ ]] && ! command -v unzip >/dev/null 2>&1; then
        missing_tools+=("unzip")
    fi

    if [[ ${#missing_tools[@]} -gt 0 ]]; then
        log_error "Missing required tools: ${missing_tools[*]}"
        exit $EXIT_INSUFFICIENT_RESOURCES
    fi

    # Check deployment package
    if [[ ! -f "$DEPLOYMENT_PACKAGE" ]]; then
        log_error "Deployment package not found: $DEPLOYMENT_PACKAGE"
        exit $EXIT_DEPLOYMENT_FAILED
    fi

    # Check available disk space
    local available_space=$(df -BG . | awk 'NR==2 {print $4}' | sed 's/G//')
    local package_size=$(du -MB "$DEPLOYMENT_PACKAGE" | cut -dM -f1)
    local required_space=$((package_size * 3))  # 3x package size for extraction

    log_debug "Available disk space: ${available_space}GB"
    log_debug "Package size: ${package_size}MB"
    log_debug "Required space: ${required_space}MB"

    if [[ $available_space -lt $((required_space / 1024 + 1)) ]]; then
        log_error "Insufficient disk space: ${available_space}GB available, ${required_space}MB required"
        exit $EXIT_INSUFFICIENT_RESOURCES
    fi

    log_debug "Prerequisites check passed"
}

# Assess system resources
assess_system_resources() {
    log_info "Assessing system resources..."

    # Check memory
    local total_memory=$(free -m | awk 'NR==2{print $2}')
    local available_memory=$(free -m | awk 'NR==2{print $7}')

    log_resource "Memory: ${available_memory}MB available / ${total_memory}MB total (limit: ${MEMORY_LIMIT_MB}MB)"

    if [[ $available_memory -lt $MEMORY_LIMIT_MB ]]; then
        log_warn "Available memory (${available_memory}MB) is below specified limit (${MEMORY_LIMIT_MB}MB)"
        log_warn "Deployment may be slow or fail"
    fi

    # Check CPU cores
    local cpu_count=$(nproc)
    log_resource "CPU: $cpu_count cores available (limit: $CPU_CORES cores)"

    if [[ $cpu_count -lt $CPU_CORES ]]; then
        log_warn "Available CPU cores ($cpu_count) is below specified limit ($CPU_CORES)"
        log_warn "Deployment will be slower than optimal"
    fi

    # Check network connectivity (optional)
    if command -v ping >/dev/null 2>&1; then
        if ping -c 1 -W 1 8.8.8.8 >/dev/null 2>&1; then
            log_resource "Network: Connected to internet"
        else
            log_resource "Network: No internet connectivity (offline deployment)"
        fi
    fi

    # Create resource assessment report
    cat > "/tmp/puzzle71-resources.json" << EOF
{
  "timestamp": "$(date -u +"%Y-%m-%d %H:%M:%S UTC")",
  "system": {
    "memory": {
      "total_mb": $total_memory,
      "available_mb": $available_memory,
      "limit_mb": $MEMORY_LIMIT_MB,
      "sufficient": $([ $available_memory -ge $MEMORY_LIMIT_MB ] && echo "true" || echo "false")
    },
    "cpu": {
      "available_cores": $cpu_count,
      "limit_cores": $CPU_CORES,
      "sufficient": $([ $cpu_count -ge $CPU_CORES ] && echo "true" || echo "false")
    },
    "disk": {
      "limit_mb": $DISK_LIMIT_MB,
      "package_size_mb": $(du -MB "$DEPLOYMENT_PACKAGE" | cut -dM -f1)
    },
    "deployment": {
      "mode": "$DEPLOYMENT_MODE",
      "target_dir": "$TARGET_DIR",
      "package": "$DEPLOYMENT_PACKAGE"
    }
  }
EOF

    log_debug "Resource assessment saved to /tmp/puzzle71-resources.json"
}

# Optimize deployment configuration based on resources
optimize_deployment_config() {
    log_info "Optimizing deployment configuration for $DEPLOYMENT_MODE mode..."

    # Default optimization parameters
    PARALLEL_JOBS=$CPU_CORES
    MEMORY_LIMIT_MB_ADJUSTED=$MEMORY_LIMIT_MB
    COMPRESSION_LEVEL=6
    ENABLE_GPU_ACCELERATION="true"
    STRIP_BINARIES="false"
    REDUCE_LOGGING="false"

    case "$DEPLOYMENT_MODE" in
        "$MODE_MINIMAL")
            PARALLEL_JOBS=1
            MEMORY_LIMIT_MB_ADJUSTED=$((MEMORY_LIMIT_MB * 80 / 100))  # Use 80% of available memory
            COMPRESSION_LEVEL=1
            ENABLE_GPU_ACCELERATION="false"
            STRIP_BINARIES="true"
            REDUCE_LOGGING="true"
            log_resource "Minimal mode: Single-threaded, reduced memory, stripped binaries"
            ;;
        "$MODE_STANDARD")
            PARALLEL_JOBS=$((CPU_CORES > 4 ? 4 : CPU_CORES))
            MEMORY_LIMIT_MB_ADJUSTED=$((MEMORY_LIMIT_MB * 70 / 100))  # Use 70% of available memory
            COMPRESSION_LEVEL=6
            ENABLE_GPU_ACCELERATION="true"
            STRIP_BINARIES="false"
            REDUCE_LOGGING="false"
            log_resource "Standard mode: Balanced performance with moderate resource usage"
            ;;
        "$MODE_COMPRESSED")
            PARALLEL_JOBS=1  # Single-threaded for network transfer optimization
            MEMORY_LIMIT_MB_ADJUSTED=$((MEMORY_LIMIT_MB * 60 / 100))  # Use 60% of available memory
            COMPRESSION_LEVEL=9
            ENABLE_GPU_ACCELERATION="false"
            STRIP_BINARIES="true"
            REDUCE_LOGGING="true"
            log_resource "Compressed mode: Optimized for network transfer, single-threaded"
            ;;
    esac

    # Create deployment configuration file
    cat > "/tmp/puzzle71-deployment-config.sh" << EOF
#!/bin/bash
# Auto-generated deployment configuration for resource-constrained environments

export DEPLOYMENT_MODE="$DEPLOYMENT_MODE"
export PARALLEL_JOBS=$PARALLEL_JOBS
export MEMORY_LIMIT_MB=$MEMORY_LIMIT_MB_ADJUSTED
export COMPRESSION_LEVEL=$COMPRESSION_LEVEL
export ENABLE_GPU_ACCELERATION=$ENABLE_GPU_ACCELERATION
export STRIP_BINARIES=$STRIP_BINARIES
export REDUCE_LOGGING=$REDUCE_LOGGING
export TARGET_DIR="$TARGET_DIR"
export NETWORK_LIMIT_KBPS=$NETWORK_LIMIT_KBPS

# Performance tuning
if [[ "\$DEPLOYMENT_MODE" == "minimal" ]]; then
    export OMP_NUM_THREADS=1
    export CUDA_VISIBLE_DEVICES=""  # Disable GPU for minimal mode
elif [[ "\$DEPLOYMENT_MODE" == "compressed" ]]; then
    export OMP_NUM_THREADS=1
    export GZIP_COMPRESSION_LEVEL=$COMPRESSION_LEVEL
else
    export OMP_NUM_THREADS=$PARALLEL_JOBS
fi

EOF

    log_debug "Deployment configuration saved to /tmp/puzzle71-deployment-config.sh"
}

# Verify deployment package integrity
verify_deployment_package() {
    if [[ "$NO_VERIFICATION" == "true" ]]; then
        log_warn "Skipping package verification (not recommended)"
        return
    fi

    log_info "Verifying deployment package integrity..."

    local package_name=$(basename "$DEPLOYMENT_PACKAGE")
    local package_size=$(stat -c%s "$DEPLOYMENT_PACKAGE" 2>/dev/null || stat -f%z "$DEPLOYMENT_PACKAGE")

    # Check if verification script exists in the package
    if [[ "$package_name" =~ \.tar\.gz$ ]]; then
        if tar -tzf "$DEPLOYMENT_PACKAGE" | grep -q "scripts/verify-deployment.sh"; then
            log_info "Running built-in package verification..."

            # Extract verification script temporarily
            local temp_dir=$(mktemp -d)
            trap "rm -rf $temp_dir" EXIT

            tar -xzf "$DEPLOYMENT_PACKAGE" -C "$temp_dir" scripts/verify-deployment.sh 2>/dev/null || true

            if [[ -f "$temp_dir/scripts/verify-deployment.sh" ]]; then
                chmod +x "$temp_dir/scripts/verify-deployment.sh"
                "$temp_dir/scripts/verify-deployment.sh" --package "$DEPLOYMENT_PACKAGE"
                log_info "Package verification completed successfully"
            else
                log_warn "Verification script not found in package"
            fi
        else
            log_warn "No verification script found in package, performing basic checks"
        fi
    fi

    # Basic integrity checks
    if [[ $package_size -lt 1024 ]]; then
        log_error "Package too small ($package_size bytes) - likely corrupted"
        exit $EXIT_VALIDATION_FAILED
    fi

    log_debug "Package verification completed"
}

# Extract deployment package with resource optimization
extract_deployment_package() {
    log_info "Extracting deployment package (mode: $DEPLOYMENT_MODE)..."

    if [[ "$DRY_RUN" == "true" ]]; then
        log_info "[DRY RUN] Would extract package to: $TARGET_DIR"
        return
    fi

    # Prepare target directory
    if [[ -d "$TARGET_DIR" ]]; then
        if [[ "$FORCE_OVERWRITE" == "true" ]]; then
            log_warn "Removing existing installation directory: $TARGET_DIR"
            rm -rf "$TARGET_DIR"
        else
            log_error "Target directory already exists: $TARGET_DIR"
            log_error "Use --force-overwrite to replace existing installation"
            exit $EXIT_DEPLOYMENT_FAILED
        fi
    fi

    mkdir -p "$TARGET_DIR"

    local package_name=$(basename "$DEPLOYMENT_PACKAGE")
    local start_time=$(date +%s)

    case "$package_name" in
        *.tar.gz|*.tgz)
            extract_with_optimization "$DEPLOYMENT_PACKAGE" "$TARGET_DIR"
            ;;
        *.zip)
            extract_zip_with_optimization "$DEPLOYMENT_PACKAGE" "$TARGET_DIR"
            ;;
        *.run)
            extract_installer_with_optimization "$DEPLOYMENT_PACKAGE" "$TARGET_DIR"
            ;;
        *)
            log_error "Unsupported package format: $package_name"
            exit $EXIT_DEPLOYMENT_FAILED
            ;;
    esac

    local end_time=$(date +%s)
    local extraction_time=$((end_time - start_time))

    log_info "Package extraction completed in ${extraction_time}s"
    log_resource "Extracted to: $TARGET_DIR"
}

# Extract tar.gz package with resource optimization
extract_with_optimization() {
    local package_file="$1"
    local target_dir="$2"

    log_debug "Extracting tar.gz package with optimizations..."

    # Set environment variables for resource optimization
    source /tmp/puzzle71-deployment-config.sh

    local tar_options="--extract --gzip --file=$package_file --directory=$target_dir"

    # Add optimization options based on mode
    if [[ "$DEPLOYMENT_MODE" == "$MODE_COMPRESSED" ]]; then
        export GZIP="-$COMPRESSION_LEVEL"
    fi

    # Extract with resource constraints
    if command -v pv >/dev/null 2>&1; then
        # Use pv for progress monitoring if available
        pv "$package_file" | tar $tar_options
    else
        tar $tar_options
    fi

    # Post-extraction optimizations
    post_extract_optimizations "$target_dir"
}

# Extract ZIP package with resource optimization
extract_zip_with_optimization() {
    local package_file="$1"
    local target_dir="$2"

    log_debug "Extracting ZIP package with optimizations..."

    source /tmp/puzzle71-deployment-config.sh

    local unzip_options="-q -d $target_dir"

    if [[ "$VERBOSE" == "true" ]]; then
        unzip_options="-d $target_dir"
    fi

    unzip $unzip_options "$package_file"

    post_extract_optimizations "$target_dir"
}

# Extract self-extracting installer with resource optimization
extract_installer_with_optimization() {
    local package_file="$1"
    local target_dir="$2"

    log_debug "Extracting self-extracting installer with optimizations..."

    source /tmp/puzzle71-deployment-config.sh

    # Run installer with resource constraints
    cd "$target_dir"
    "$package_file" --accept-license --no-interaction --target-dir "$target_dir"

    post_extract_optimizations "$target_dir"
}

# Perform post-extraction optimizations
post_extract_optimizations() {
    local target_dir="$1"

    log_debug "Performing post-extraction optimizations..."

    source /tmp/puzzle71-deployment-config.sh

    # Strip binaries if requested
    if [[ "$STRIP_BINARIES" == "true" ]]; then
        log_resource "Stripping debug symbols from binaries..."
        find "$target_dir" -type f -executable -name "*.so*" -exec strip --strip-unneeded {} \; 2>/dev/null || true
        find "$target_dir" -type f -executable -name "Puzzle71Solver" -exec strip --strip-unneeded {} \; 2>/dev/null || true
    fi

    # Reduce logging verbosity if requested
    if [[ "$REDUCE_LOGGING" == "true" ]]; then
        log_resource "Reducing logging verbosity..."
        find "$target_dir" -name "*.json" -name "*log*" -delete 2>/dev/null || true
    fi

    # Set optimal permissions
    chmod -R u=rwX,go=rX "$target_dir" 2>/dev/null || true
    find "$target_dir" -type f -executable -exec chmod 755 {} \; 2>/dev/null || true

    # Create resource usage configuration
    cat > "$target_dir/.resource-config" << EOF
# Resource-constrained deployment configuration
DEPLOYMENT_MODE=$DEPLOYMENT_MODE
MEMORY_LIMIT_MB=$MEMORY_LIMIT_MB_ADJUSTED
CPU_CORES=$CPU_CORES
PARALLEL_JOBS=$PARALLEL_JOBS
ENABLE_GPU_ACCELERATION=$ENABLE_GPU_ACCELERATION
DEPLOYMENT_TIMESTAMP=$(date -u +"%Y-%m-%d %H:%M:%S UTC")
EOF

    log_debug "Post-extraction optimizations completed"
}

# Configure deployment for resource constraints
configure_deployment() {
    log_info "Configuring deployment for resource constraints..."

    if [[ "$DRY_RUN" == "true" ]]; then
        log_info "[DRY RUN] Would configure deployment in: $TARGET_DIR"
        return
    fi

    source /tmp/puzzle71-deployment-config.sh

    # Create optimized configuration files
    create_optimized_config

    # Set environment variables for runtime
    setup_runtime_environment

    # Create startup scripts with resource constraints
    create_startup_scripts

    log_info "Deployment configuration completed"
}

# Create optimized configuration
create_optimized_config() {
    log_debug "Creating optimized configuration..."

    local config_dir="$TARGET_DIR/config"
    mkdir -p "$config_dir"

    # Main configuration file
    cat > "$config_dir/puzzle71.json" << EOF
{
  "deployment": {
    "mode": "$DEPLOYMENT_MODE",
    "resource_constraints": {
      "memory_mb": $MEMORY_LIMIT_MB_ADJUSTED,
      "cpu_cores": $CPU_CORES,
      "parallel_jobs": $PARALLEL_JOBS,
      "network_limit_kbps": $NETWORK_LIMIT_KBPS
    },
    "optimizations": {
      "gpu_acceleration": $ENABLE_GPU_ACCELERATION,
      "strip_binaries": $STRIP_BINARIES,
      "reduce_logging": $REDUCE_LOGGING
    }
  },
  "performance": {
    "max_memory_usage_mb": $MEMORY_LIMIT_MB_ADJUSTED,
    "thread_pool_size": $PARALLEL_JOBS,
    "batch_size": $([ "$DEPLOYMENT_MODE" == "minimal" ] && echo "64" || echo "256"),
    "gpu_blocks": $([ "$ENABLE_GPU_ACCELERATION" == "true" ] && echo "128" || echo "0"),
    "gpu_threads": $([ "$ENABLE_GPU_ACCELERATION" == "true" ] && echo "128" || echo "0")
  },
  "logging": {
    "level": "$([ "$REDUCE_LOGGING" == "true" ] && echo "WARN" || echo "INFO")",
    "console_output": true,
    "file_output": $([ "$REDUCE_LOGGING" == "false" ] && echo "true" || echo "false"),
    "max_file_size_mb": $([ "$REDUCE_LOGGING" == "true" ] && echo "1" || echo "10")
  }
}
EOF

    log_debug "Optimized configuration created"
}

# Setup runtime environment
setup_runtime_environment() {
    log_debug "Setting up runtime environment..."

    local env_file="$TARGET_DIR/scripts/runtime-env.sh"
    mkdir -p "$(dirname "$env_file")"

    cat > "$env_file" << 'EOF'
#!/bin/bash
# Runtime environment for resource-constrained deployment

# Source deployment configuration
if [[ -f "$(dirname "$0")/../.resource-config" ]]; then
    source "$(dirname "$0")/../.resource-config"
fi

# Set memory limits
if [[ -n "${MEMORY_LIMIT_MB:-}" ]]; then
    ulimit -v $((MEMORY_LIMIT_MB * 1024)) 2>/dev/null || true
fi

# Set CPU affinity if specified
if [[ -n "${PARALLEL_JOBS:-}" ]] && [[ $PARALLEL_JOBS -gt 0 ]]; then
    export OMP_NUM_THREADS=$PARALLEL_JOBS
    export OPENBLAS_NUM_THREADS=$PARALLEL_JOBS
    export MKL_NUM_THREADS=$PARALLEL_JOBS
fi

# Configure GPU usage
if [[ "${ENABLE_GPU_ACCELERATION:-true}" != "true" ]]; then
    export CUDA_VISIBLE_DEVICES=""
fi

# Network throttling (if configured)
if [[ -n "${NETWORK_LIMIT_KBPS:-}" ]] && command -v trickle >/dev/null 2>&1; then
    export NETWORK_THROTTLE="trickle -s -d $NETWORK_LIMIT_KBPS -u $NETWORK_LIMIT_KBPS"
fi
EOF

    chmod +x "$env_file"
    log_debug "Runtime environment setup completed"
}

# Create startup scripts
create_startup_scripts() {
    log_debug "Creating startup scripts..."

    local scripts_dir="$TARGET_DIR/scripts"
    mkdir -p "$scripts_dir"

    # Main startup script
    cat > "$scripts_dir/start.sh" << EOF
#!/bin/bash
# Puzzle71Solver startup script for resource-constrained environments

set -euo pipefail

# Source runtime environment
SCRIPT_DIR="\$(cd "\$(dirname "\${BASH_SOURCE[0]}")" && pwd)"
source "\$SCRIPT_DIR/runtime-env.sh"

# Get deployment directory
DEPLOYMENT_DIR="\$(dirname "\$SCRIPT_DIR")"
cd "\$DEPLOYMENT_DIR"

# Check for GPU availability
if [[ "${ENABLE_GPU_ACCELERATION:-true}" == "true" ]]; then
    if command -v nvidia-smi >/dev/null 2>&1; then
        echo "GPU acceleration enabled"
        export CUDA_VISIBLE_DEVICES=0
    else
        echo "GPU not available, using CPU mode"
        export CUDA_VISIBLE_DEVICES=""
    fi
fi

# Set binary path
BINARY_PATH="\$DEPLOYMENT_DIR/bin/Puzzle71Solver"

# Check if binary exists
if [[ ! -f "\$BINARY_PATH" ]]; then
    echo "Error: Puzzle71Solver binary not found at \$BINARY_PATH"
    exit 1
fi

# Apply network throttling if configured
if [[ -n "\${NETWORK_THROTTLE:-}" ]]; then
    echo "Starting with network throttling (\${NETWORK_LIMIT_KBPS}KB/s)..."
    exec \$NETWORK_THROTTLE "\$BINARY_PATH" "\$@"
else
    echo "Starting Puzzle71Solver..."
    exec "\$BINARY_PATH" "\$@"
fi
EOF

    chmod +x "$scripts_dir/start.sh"

    # Create status script
    cat > "$scripts_dir/status.sh" << EOF
#!/bin/bash
# Status check script for resource-constrained deployment

SCRIPT_DIR="\$(cd "\$(dirname "\${BASH_SOURCE[0]}")" && pwd)"
DEPLOYMENT_DIR="\$(dirname "\$SCRIPT_DIR")"

echo "=== Puzzle71Solver Deployment Status ==="
echo "Deployment Directory: \$DEPLOYMENT_DIR"
echo "Deployment Mode: \${DEPLOYMENT_MODE:-unknown}"
echo

# Check binary
if [[ -f "\$DEPLOYMENT_DIR/bin/Puzzle71Solver" ]]; then
    echo "✓ Binary exists: \$(ls -lh "\$DEPLOYMENT_DIR/bin/Puzzle71Solver" | awk '{print \$5}')"
else
    echo "✗ Binary missing"
fi

# Check configuration
if [[ -f "\$DEPLOYMENT_DIR/config/puzzle71.json" ]]; then
    echo "✓ Configuration file exists"
else
    echo "✗ Configuration file missing"
fi

# Check resource configuration
if [[ -f "\$DEPLOYMENT_DIR/.resource-config" ]]; then
    echo "✓ Resource configuration exists"
    source "\$DEPLOYMENT_DIR/.resource-config"
    echo "  - Memory Limit: \${MEMORY_LIMIT_MB:-unknown}MB"
    echo "  - CPU Cores: \${CPU_CORES:-unknown}"
    echo "  - GPU Acceleration: \${ENABLE_GPU_ACCELERATION:-unknown}"
else
    echo "✗ Resource configuration missing"
fi

# Check GPU status
if command -v nvidia-smi >/dev/null 2>&1; then
    echo "✓ GPU drivers available"
    nvidia-smi --query-gpu=name,memory.total --format=csv,noheader,nounits | head -1
else
    echo "⚠ GPU drivers not available"
fi

echo
echo "Memory Usage:"
free -h

echo
echo "Disk Usage:"
df -h "\$DEPLOYMENT_DIR"
EOF

    chmod +x "$scripts_dir/status.sh"

    log_debug "Startup scripts created"
}

# Validate deployment
validate_deployment() {
    log_info "Validating deployment..."

    if [[ "$DRY_RUN" == "true" ]]; then
        log_info "[DRY RUN] Would validate deployment in: $TARGET_DIR"
        return
    fi

    local validation_errors=0

    # Check essential files
    local essential_files=(
        "$TARGET_DIR/bin/Puzzle71Solver"
        "$TARGET_DIR/config/puzzle71.json"
        "$TARGET_DIR/scripts/start.sh"
        "$TARGET_DIR/scripts/status.sh"
    )

    for file in "${essential_files[@]}"; do
        if [[ -f "$file" ]]; then
            log_debug "✓ Found: $file"
        else
            log_error "✗ Missing: $file"
            validation_errors=$((validation_errors + 1))
        fi
    done

    # Check binary permissions
    if [[ -f "$TARGET_DIR/bin/Puzzle71Solver" ]]; then
        if [[ -x "$TARGET_DIR/bin/Puzzle71Solver" ]]; then
            log_debug "✓ Binary is executable"
        else
            log_error "✗ Binary is not executable"
            validation_errors=$((validation_errors + 1))
        fi
    fi

    # Test configuration loading
    if [[ -f "$TARGET_DIR/config/puzzle71.json" ]]; then
        if command -v jq >/dev/null 2>&1; then
            if jq empty "$TARGET_DIR/config/puzzle71.json" 2>/dev/null; then
                log_debug "✓ Configuration JSON is valid"
            else
                log_error "✗ Configuration JSON is invalid"
                validation_errors=$((validation_errors + 1))
            fi
        else
            log_debug "⚠ jq not available, skipping JSON validation"
        fi
    fi

    if [[ $validation_errors -gt 0 ]]; then
        log_error "Deployment validation failed with $validation_errors errors"
        exit $EXIT_VALIDATION_FAILED
    fi

    log_info "Deployment validation completed successfully"
}

# Generate deployment report
generate_deployment_report() {
    log_info "Generating deployment report..."

    local report_file="$TARGET_DIR/deployment-report.txt"

    cat > "$report_file" << EOF
Puzzle71Solver Resource-Constrained Deployment Report
=====================================================

Deployment Information:
- Package: $(basename "$DEPLOYMENT_PACKAGE")
- Target Directory: $TARGET_DIR
- Deployment Mode: $DEPLOYMENT_MODE
- Deployment Date: $(date -u +"%Y-%m-%d %H:%M:%S UTC")

Resource Configuration:
- Memory Limit: ${MEMORY_LIMIT_MB_ADJUSTED}MB
- CPU Cores: $CPU_CORES
- Parallel Jobs: $PARALLEL_JOBS
- Network Limit: ${NETWORK_LIMIT_KBPS}KB/s
- GPU Acceleration: $ENABLE_GPU_ACCELERATION

Optimizations Applied:
- Strip Binaries: $STRIP_BINARIES
- Reduce Logging: $REDUCE_LOGGING
- Compression Level: $COMPRESSION_LEVEL

System Information:
- OS: $(uname -s) $(uname -r)
- Architecture: $(uname -m)
- Hostname: $(hostname)

Usage Instructions:
1. Start Puzzle71Solver:
   cd $TARGET_DIR
   ./scripts/start.sh --help

2. Check deployment status:
   ./scripts/status.sh

3. View configuration:
   cat config/puzzle71.json

4. Monitor resources:
   watch -n 1 ./scripts/status.sh

Notes:
- This deployment is optimized for $DEPLOYMENT_MODE mode
- Resource limits are automatically enforced
- GPU acceleration is $([ "$ENABLE_GPU_ACCELERATION" == "true" ] && echo "enabled" || echo "disabled")
- Logs are $([ "$REDUCE_LOGGING" == "true" ] && echo "reduced" || echo "enabled")

EOF

    log_info "Deployment report saved to: $report_file"
    log_resource "Deployment completed successfully!"
    log_resource "Start with: cd $TARGET_DIR && ./scripts/start.sh"
}

# Main function
main() {
    log_info "Starting resource-constrained deployment..."

    parse_args "$@"
    check_prerequisites
    assess_system_resources
    optimize_deployment_config
    verify_deployment_package
    extract_deployment_package
    configure_deployment
    validate_deployment
    generate_deployment_report

    log_info "Resource-constrained deployment completed successfully!"
}

# Execute main function
main "$@"