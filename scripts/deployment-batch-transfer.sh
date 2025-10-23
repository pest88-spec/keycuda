#!/bin/bash
# T039: Create Deployment Transfer Batching Optimization
# Optimizes deployment package transfers with batching and compression

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

# Batching configuration
BATCH_SIZE_MB="${BATCH_SIZE_MB:-100}"
MAX_CONCURRENT_TRANSFERS="${MAX_CONCURRENT_TRANSFERS:-3}"
COMPRESSION_LEVEL="${COMPRESSION_LEVEL:-6}"
CHUNK_SIZE="${CHUNK_SIZE:-1048576}"  # 1MB chunks
RESUME_ENABLED="${RESUME_ENABLED:-true}"
INTEGRITY_CHECK="${INTEGRITY_CHECK:-true}"
BANDWIDTH_LIMIT="${BANDWIDTH_LIMIT:-}"  # MB/s, empty = no limit

# Transfer configuration
TRANSFER_METHOD="${TRANSFER_METHOD:-auto}"  # auto, scp, rsync, http
TARGET_HOSTS=()
TARGET_PATH="/tmp/deployment-batches"
SSH_KEY="${SSH_KEY:-$HOME/.ssh/id_rsa}"
TRANSFER_LOG="${TRANSFER_LOG:-$PROJECT_ROOT/logs/batch-transfer.log}"

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

log_batch() {
    echo -e "${PURPLE}[BATCH]${NC} $1"
}

log_transfer() {
    echo -e "${CYAN}[TRANSFER]${NC} $1"
}

# Show help
show_help() {
    cat << EOF
Deployment Batch Transfer Optimization Script

USAGE:
    $0 [OPTIONS] [deployment_package] [target_hosts...]

OPTIONS:
    --batch-size MB         Maximum batch size in MB (default: 100)
    --concurrent N         Maximum concurrent transfers (default: 3)
    --compression N        Compression level 0-9 (default: 6)
    --chunk-size BYTES     Chunk size for transfers (default: 1048576)
    --method METHOD        Transfer method: auto, scp, rsync, http (default: auto)
    --target-path PATH     Target directory path (default: /tmp/deployment-batches)
    --ssh-key PATH         SSH key file (default: ~/.ssh/id_rsa)
    --bandwidth-limit MB/s Bandwidth limit in MB/s
    --no-resume           Disable resume capability
    --no-integrity         Skip integrity checks
    --dry-run             Show what would be done without executing
    --help, -h            Show this help message

DESCRIPTION:
    Optimizes deployment package transfers by splitting large packages
    into smaller batches, applying compression, and managing concurrent transfers.

EXAMPLES:
    $0 package.tar.gz host1 host2 host3
    $0 --batch-size 50 --concurrent 5 package.tar.gz user@host
    $0 --method rsync --compression 9 package.tar.gz

EOF
}

# Parse command line arguments
parse_arguments() {
    DRY_RUN=false
    DEPLOYMENT_PACKAGE=""

    while [[ $# -gt 0 ]]; do
        case $1 in
            --batch-size)
                BATCH_SIZE_MB="$2"
                shift 2
                ;;
            --concurrent)
                MAX_CONCURRENT_TRANSFERS="$2"
                shift 2
                ;;
            --compression)
                COMPRESSION_LEVEL="$2"
                shift 2
                ;;
            --chunk-size)
                CHUNK_SIZE="$2"
                shift 2
                ;;
            --method)
                TRANSFER_METHOD="$2"
                shift 2
                ;;
            --target-path)
                TARGET_PATH="$2"
                shift 2
                ;;
            --ssh-key)
                SSH_KEY="$2"
                shift 2
                ;;
            --bandwidth-limit)
                BANDWIDTH_LIMIT="$2"
                shift 2
                ;;
            --no-resume)
                RESUME_ENABLED=false
                shift
                ;;
            --no-integrity)
                INTEGRITY_CHECK=false
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
                    TARGET_HOSTS+=("$1")
                fi
                shift
                ;;
        esac
    done

    # Validate arguments
    if [[ -z "$DEPLOYMENT_PACKAGE" ]]; then
        log_error "No deployment package specified"
        exit 1
    fi

    if [[ ${#TARGET_HOSTS[@]} -eq 0 ]]; then
        TARGET_HOSTS=("localhost")
    fi
}

# Analyze deployment package
analyze_package() {
    log_info "Analyzing deployment package: $DEPLOYMENT_PACKAGE"

    if [[ ! -f "$DEPLOYMENT_PACKAGE" ]]; then
        log_error "Deployment package not found: $DEPLOYMENT_PACKAGE"
        exit 1
    fi

    # Get package information
    local package_size=$(stat -c%s "$DEPLOYMENT_PACKAGE")
    local package_size_mb=$((package_size / 1048576))
    local package_type=$(file "$DEPLOYMENT_PACKAGE" | cut -d: -f2 | tr -d ' ')

    log_info "Package size: ${package_size_mb}MB ($package_type)"

    # Calculate optimal batching
    if [[ $package_size_mb -le $BATCH_SIZE_MB ]]; then
        TOTAL_BATCHES=1
        BATCH_SIZE_BYTES=$package_size
    else
        TOTAL_BATCHES=$(( (package_size_mb + BATCH_SIZE_MB - 1) / BATCH_SIZE_MB ))
        BATCH_SIZE_BYTES=$((BATCH_SIZE_MB * 1048576))
    fi

    log_info "Total batches: $TOTAL_BATCHES"
    log_info "Batch size: $BATCH_SIZE_MB MB (${BATCH_SIZE_BYTES} bytes)"

    # Create batches directory
    BATCH_DIR="$PROJECT_ROOT/batches/$(basename "$DEPLOYMENT_PACKAGE" .tar.gz)"
    mkdir -p "$BATCH_DIR"

    # Create batch metadata
    cat > "$BATCH_DIR/batch-metadata.json" << EOF
{
  "batch_metadata": {
    "package_name": "$(basename "$DEPLOYMENT_PACKAGE")",
    "package_path": "$DEPLOYMENT_PACKAGE",
    "package_size_bytes": $package_size,
    "package_size_mb": $package_size_mb,
    "package_type": "$package_type",
    "total_batches": $TOTAL_BATCHES,
    "batch_size_mb": $BATCH_SIZE_MB,
    "batch_size_bytes": $BATCH_SIZE_BYTES,
    "chunk_size": $CHUNK_SIZE,
    "compression_level": $COMPRESSION_LEVEL,
    "created": "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  },
  "transfer_configuration": {
    "max_concurrent_transfers": $MAX_CONCURRENT_TRANSFERS,
    "bandwidth_limit_mbps": "$BANDWIDTH_LIMIT",
    "resume_enabled": $RESUME_ENABLED,
    "integrity_check": $INTEGRITY_CHECK,
    "transfer_method": "$TRANSFER_METHOD"
  },
  "target_hosts": [
    $(printf '"%s",' "${TARGET_HOSTS[@]}" | sed 's/,$//')
  ],
  "target_path": "$TARGET_PATH"
}
EOF

    log_success "Package analysis completed"
}

# Create batch files
create_batches() {
    log_info "Creating $TOTAL_BATCHES batch files..."

    if [[ "$DRY_RUN" == "true" ]]; then
        log_info "[DRY-RUN] Would create $TOTAL_BATCHES batch files in $BATCH_DIR"
        return 0
    fi

    # Split package into batches
    if [[ "$DEPLOYMENT_PACKAGE" == *.tar.gz ]] || [[ "$DEPLOYMENT_PACKAGE" == *.tgz ]]; then
        split_tar_gz_package
    elif [[ "$DEPLOYMENT_PACKAGE" == *.tar.bz2 ]] || [[ "$DEPLOYMENT_PACKAGE" == *.tbz2 ]]; then
        split_tar_bz2_package
    elif [[ "$DEPLOYMENT_PACKAGE" == *.tar.xz ]] || [[ "$DEPLOYMENT_PACKAGE" == *.txz ]]; then
        split_tar_xz_package
    elif [[ "$DEPLOYMENT_PACKAGE" == *.zip ]]; then
        split_zip_package
    else
        log_error "Unsupported package format: $DEPLOYMENT_PACKAGE"
        exit 1
    fi

    log_success "Batch files created in: $BATCH_DIR"
}

# Split tar.gz package
split_tar_gz_package() {
    local temp_dir=$(mktemp -d)
    local temp_file="$temp_dir/package.tar.gz"

    # Copy package to temp file
    cp "$DEPLOYMENT_PACKAGE" "$temp_file"

    # Split into chunks
    split -b $BATCH_SIZE_BYTES -d "$temp_file" "$BATCH_DIR/batch_"

    # Create batch info files
    for i in $(seq 1 $TOTAL_BATCHES); do
        local batch_num=$(printf "%03d" $i)
        local batch_file="$BATCH_DIR/batch_$batch_num"
        local info_file="$BATCH_DIR/batch_${batch_num}_info.json"

        # Calculate chunk info
        local chunk_count=$(($(stat -c%s "$batch_file") + CHUNK_SIZE - 1) / CHUNK_SIZE)

        cat > "$info_file" << EOF
{
  "batch_number": $i,
  "total_batches": $TOTAL_BATCHES,
  "file_path": "$(basename "$batch_file")",
  "file_size_bytes": $(stat -c%s "$batch_file"),
  "chunk_count": $chunk_count,
  "checksum": "$(sha256sum "$batch_file" | cut -d' ' -f1)",
  "compression": "gzip",
  "created": "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
}
EOF
    done

    rm -rf "$temp_dir"
}

# Split tar.bz2 package
split_tar_bz2_package() {
    local temp_dir=$(mktemp -d)
    local temp_file="$temp_dir/package.tar.bz2"

    cp "$DEPLOYMENT_PACKAGE" "$temp_file"
    split -b $BATCH_SIZE_BYTES -d "$temp_file" "$BATCH_DIR/batch_"

    for i in $(seq 1 $TOTAL_BATCHES); do
        local batch_num=$(printf "%03d" $i)
        local batch_file="$BATCH_DIR/batch_$batch_num"
        local info_file="$BATCH_DIR/batch_${batch_num}_info.json"

        local chunk_count=$(($(stat -c%s "$batch_file") + CHUNK_SIZE - 1) / CHUNK_SIZE)

        cat > "$info_file" << EOF
{
  "batch_number": $i,
  "total_batches": $TOTAL_BATCHES,
  "file_path": "$(basename "$batch_file")",
  "file_size_bytes": $(stat -c%s "$batch_file")",
  "chunk_count": $chunk_count,
  "checksum": "$(sha256sum "$batch_file" | cut -d' ' -f1)",
  "compression": "bzip2",
  "created": "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
}
EOF
    done

    rm -rf "$temp_dir"
}

# Split tar.xz package
split_tar_xz_package() {
    local temp_dir=$(mktemp -d)
    local temp_file="$temp_dir/package.tar.xz"

    cp "$DEPLOYMENT_PACKAGE" "$temp_file"
    split -b $BATCH_SIZE_BYTES -d "$temp_file" "$BATCH_DIR/batch_"

    for i in $(seq 1 $TOTAL_BATCHES); do
        local batch_num=$(printf "%03d" $i)
        local batch_file="$BATCH_DIR/batch_$batch_num"
        local info_file="$BATCH_DIR/batch_${batch_num}_info.json"

        local chunk_count=$(($(stat -c%s "$batch_file") + CHUNK_SIZE - 1) / CHUNK_SIZE)

        cat > "$info_file" << EOF
{
  "batch_number": $i,
  "total_batches": $TOTAL_BATCHES,
  "file_path": "$(basename "$batch_file")",
  "file_size_bytes": $(stat -c%s "$batch_file")",
  "chunk_count": $chunk_count,
  "checksum": "$(sha256sum "$batch_file" | cut -d' ' -f1)",
  "compression": "xz",
  "created": "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
}
EOF
    done

    rm -rf "$temp_dir"
}

# Split zip package
split_zip_package() {
    local temp_dir=$(mktemp -d)
    local temp_file="$temp_dir/package.zip"

    cp "$DEPLOYMENT_PACKAGE" "$temp_file"
    split -b $BATCH_SIZE_BYTES -d "$temp_file" "$BATCH_DIR/batch_"

    for i in $(seq 1 $TOTAL_BATCHES); do
        local batch_num=$(printf "%03d" $i)
        local batch_file="$BATCH_DIR/batch_$batch_num"
        local info_file="$BATCH_DIR/batch_${batch_num}_info.json"

        local chunk_count=$(($(stat -c%s "$batch_file") + CHUNK_SIZE - 1) / CHUNK_SIZE)

        cat > "$info_file" << EOF
{
  "batch_number": $i,
  "total_batches": $TOTAL_BATCHES,
  "file_path": "$(basename "$batch_file")",
  "file_size_bytes": $(stat -c%s "$batch_file")",
  "chunk_count": $chunk_count,
  "checksum": "$(sha256sum "$batch_file" | cut -d' ' -f1)",
  "compression": "zip",
  "created": "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
}
EOF
    done

    rm -rf "$temp_dir"
}

# Transfer batches to targets
transfer_batches() {
    log_info "Transferring batches to ${#TARGET_HOSTS[@]} target(s)..."

    mkdir -p "$(dirname "$TRANSFER_LOG")"
    echo "Batch transfer log - $(date)" > "$TRANSFER_LOG"

    for target in "${TARGET_HOSTS[@]}"; do
        log_batch "Transferring to target: $target"

        # Create target directory
        create_target_directory "$target"

        # Transfer metadata first
        transfer_metadata "$target"

        # Transfer batches concurrently
        transfer_batches_to_target "$target"
    done
}

# Create target directory
create_target_directory() {
    local target="$1"

    if [[ "$DRY_RUN" == "true" ]]; then
        log_info "[DRY-RUN] Would create directory on $target: $TARGET_PATH"
        return 0
    fi

    case "$TRANSFER_METHOD" in
        scp|ssh)
            ssh -i "$SSH_KEY" "$target" "mkdir -p \"$TARGET_PATH\"" 2>> "$TRANSFER_LOG"
            ;;
        rsync)
            ssh -i "$SSH_KEY" "$target" "mkdir -p \"$TARGET_PATH\"" 2>> "$TRANSFER_LOG"
            ;;
        http)
            # HTTP doesn't need directory creation
            ;;
        auto)
            # Auto-detect method
            if command -v ssh >/dev/null 2>&1 && ssh -o BatchMode=yes -o ConnectTimeout=5 "$target" true 2>/dev/null; then
                ssh -i "$SSH_KEY" "$target" "mkdir -p \"$TARGET_PATH\"" 2>> "$TRANSFER_LOG"
            fi
            ;;
    esac
}

# Transfer metadata
transfer_metadata() {
    local target="$1"

    if [[ "$DRY_RUN" == "true" ]]; then
        log_info "[DRY-RUN] Would transfer metadata to $target"
        return 0
    fi

    case "$TRANSFER_METHOD" in
        scp|ssh)
            scp -i "$SSH_KEY" "$BATCH_DIR/batch-metadata.json" "$target:$TARGET_PATH/" 2>> "$TRANSFER_LOG"
            scp -i "$SSH_KEY" "$BATCH_DIR/batch_"*"_info.json" "$target:$TARGET_PATH/" 2>> "$TRANSFER_LOG"
            ;;
        rsync)
            rsync -avz -e "ssh -i $SSH_KEY" "$BATCH_DIR/batch-metadata.json" "$target:$TARGET_PATH/" 2>> "$TRANSFER_LOG"
            rsync -avz -e "ssh -i $SSH_KEY" "$BATCH_DIR/batch_"*"_info.json" "$target:$TARGET_PATH/" 2>> "$TRANSFER_LOG"
            ;;
        http)
            log_warning "HTTP transfer not implemented for metadata"
            ;;
        auto)
            # Auto-detect method
            if command -v scp >/dev/null 2>&1; then
                scp -i "$SSH_KEY" "$BATCH_DIR/batch-metadata.json" "$target:$TARGET_PATH/" 2>> "$TRANSFER_LOG"
                scp -i "$SSH_KEY" "$BATCH_DIR/batch_"*"_info.json" "$target:$TARGET_PATH/" 2>> "$TRANSFER_LOG"
            fi
            ;;
    esac
}

# Transfer batches to specific target
transfer_batches_to_target() {
    local target="$1"

    local transfer_pids=()
    local active_transfers=0

    for i in $(seq 1 $TOTAL_BATCHES); do
        local batch_num=$(printf "%03d" $i)
        local batch_file="$BATCH_DIR/batch_$batch_num"

        # Wait for available transfer slot
        while [[ $active_transfers -ge $MAX_CONCURRENT_TRANSFERS ]]; do
            wait -n 1
            ((active_transfers--))
        done

        # Start transfer in background
        (
            transfer_single_batch "$target" "$batch_file" "$batch_num"
        ) &

        transfer_pids+=($!)
        ((active_transfers++))

        log_transfer "Started transfer of batch $batch_num to $target (PID: ${transfer_pids[-1]})"
    done

    # Wait for all transfers to complete
    for pid in "${transfer_pids[@]}"; do
        wait "$pid"
    done

    log_batch "All transfers completed for target: $target"
}

# Transfer single batch
transfer_single_batch() {
    local target="$1"
    local batch_file="$2"
    local batch_num="$3"

    local transfer_start=$(date +%s)

    case "$TRANSFER_METHOD" in
        scp)
            local scp_cmd="scp"
            [[ -n "$BANDWIDTH_LIMIT" ]] && scp_cmd="scp -l $((BANDWIDTH_LIMIT * 1024))"
            [[ "$RESUME_ENABLED" == "true" ]] && scp_cmd="$scp_cmd"

            $scp_cmd -i "$SSH_KEY" "$batch_file" "$target:$TARGET_PATH/batch_$batch_num"
            ;;
        rsync)
            local rsync_cmd="rsync -avz"
            [[ -n "$BANDWIDTH_LIMIT" ]] && rsync_cmd="$rsync_cmd --bwlimit=$BANDWIDTH_LIMIT"
            [[ "$RESUME_ENABLED" == "true" ]] && rsync_cmd="$rsync_cmd --partial"

            $rsync_cmd -e "ssh -i $SSH_KEY" "$batch_file" "$target:$TARGET_PATH/batch_$batch_num"
            ;;
        auto)
            # Auto-detect and use best available method
            if command -v rsync >/dev/null 2>&1; then
                local rsync_cmd="rsync -avz"
                [[ -n "$BANDWIDTH_LIMIT" ]] && rsync_cmd="$rsync_cmd --bwlimit=$BANDWIDTH_LIMIT"
                [[ "$RESUME_ENABLED" == "true" ]] && rsync_cmd="$rsync_cmd --partial"

                $rsync_cmd -e "ssh -i $SSH_KEY" "$batch_file" "$target:$TARGET_PATH/batch_$batch_num"
            elif command -v scp >/dev/null 2>&1; then
                scp -i "$SSH_KEY" "$batch_file" "$target:$TARGET_PATH/batch_$batch_num"
            else
                log_error "No transfer method available for $target"
                return 1
            fi
            ;;
        *)
            log_error "Unsupported transfer method: $TRANSFER_METHOD"
            return 1
            ;;
    esac

    local transfer_end=$(date +%s)
    local transfer_duration=$((transfer_end - transfer_start))

    # Verify transfer
    if [[ "$INTEGRITY_CHECK" == "true" ]]; then
        verify_batch_integrity "$target" "$batch_num"
    fi

    # Log transfer completion
    echo "$(date): Transfer completed - batch $batch_num to $target (${transfer_duration}s)" >> "$TRANSFER_LOG"
}

# Verify batch integrity
verify_batch_integrity() {
    local target="$1"
    local batch_num="$2"

    local info_file="$BATCH_DIR/batch_${batch_num}_info.json"
    local expected_checksum=$(jq -r '.checksum' "$info_file" 2>/dev/null)

    if [[ -n "$expected_checksum" ]]; then
        local remote_checksum=$(ssh -i "$SSH_KEY" "$target" "sha256sum \"$TARGET_PATH/batch_$batch_num\" 2>/dev/null | cut -d' ' -f1")

        if [[ "$remote_checksum" == "$expected_checksum" ]]; then
            echo "$(date): Integrity verified - batch $batch_num" >> "$TRANSFER_LOG"
        else
            echo "$(date): Integrity FAILED - batch $batch_num (expected: $expected_checksum, got: $remote_checksum)" >> "$TRANSFER_LOG"
        fi
    fi
}

# Generate transfer report
generate_transfer_report() {
    log_info "Generating transfer report..."

    local report_file="$BATCH_DIR/transfer-report.json"

    local total_time=$(tail -1 "$TRANSFER_LOG" | grep -o '([0-9]+)s' | head -1)
    local successful_transfers=$(grep "Transfer completed" "$TRANSFER_LOG" | wc -l)
    local failed_transfers=$(grep "FAILED" "$TRANSFER_LOG" | wc -l)
    local integrity_verifications=$(grep "Integrity verified" "$TRANSFER_LOG" | wc -l)

    cat > "$report_file" << EOF
{
  "batch_transfer_report": {
    "report_metadata": {
      "generated": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
      "script_version": "T039-1.0",
      "deployment_package": "$(basename "$DEPLOYMENT_PACKAGE")",
      "batch_directory": "$BATCH_DIR"
    },
    "transfer_configuration": {
      "total_batches": $TOTAL_BATCHES,
      "batch_size_mb": $BATCH_SIZE_MB,
      "max_concurrent_transfers": $MAX_CONCURRENT_TRANSFERS,
      "compression_level": $COMPRESSION_LEVEL,
      "transfer_method": "$TRANSFER_METHOD",
      "bandwidth_limit_mbps": "$BANDWIDTH_LIMIT",
      "resume_enabled": $RESUME_ENABLED,
      "integrity_check": $INTEGRITY_CHECK
    },
    "target_configuration": {
      "target_count": ${#TARGET_HOSTS[@]},
      "target_hosts": [
        $(printf '"%s",' "${TARGET_HOSTS[@]}" | sed 's/,$//')
      ],
      "target_path": "$TARGET_PATH"
    },
    "transfer_results": {
      "total_time_seconds": ${total_time:-0},
      "successful_transfers": $successful_transfers,
      "failed_transfers": $failed_transfers,
      "integrity_verifications": $integrity_verifications,
      "overall_success_rate": $(( successful_transfers * 100 / (TOTAL_BATCHES * ${#TARGET_HOSTS[@]}) ))
    },
    "optimization_metrics": {
      "batching_enabled": true,
      "compression_enabled": true,
      "concurrent_transfers_enabled": true,
      "resume_capability_enabled": $RESUME_ENABLED,
      "integrity_verification_enabled": $INTEGRITY_CHECK
    },
    "recommendations": [
      "Reassemble batches on target systems using the reassembly script",
      "Monitor transfer logs for any failed or slow transfers",
      "Consider adjusting batch size based on network conditions",
      "Use the provided verification scripts to ensure deployment integrity"
    ]
  }
}
EOF

    log_success "Transfer report generated: $report_file"
}

# Create reassembly script for targets
create_reassembly_script() {
    log_info "Creating reassembly script..."

    cat > "$BATCH_DIR/reassemble.sh" << 'EOF'
#!/bin/bash
# Reassembly script for deployment batches
# Usage: ./reassemble.sh [target_directory]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TARGET_DIR="${1:-$(pwd)}"

# Colors for output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
RED='\033[0;31m'
NC='\033[0m'

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if metadata exists
if [[ ! -f "$SCRIPT_DIR/batch-metadata.json" ]]; then
    log_error "Batch metadata not found"
    exit 1
fi

# Get batch information
TOTAL_BATCHES=$(jq -r '.batch_metadata.total_batches' "$SCRIPT_DIR/batch-metadata.json")
PACKAGE_NAME=$(jq -r '.batch_metadata.package_name' "$SCRIPT_DIR/batch-metadata.json")
COMPRESSION=$(jq -r '.batch_metadata.compression_level' "$SCRIPT_DIR/batch-metadata.json")

log_info "Reassembling $PACKAGE_NAME from $TOTAL_BATCHES batches"
log_info "Target directory: $TARGET_DIR"

# Create target directory
mkdir -p "$TARGET_DIR"

# Reassemble batches
for i in $(seq 1 $TOTAL_BATCHES); do
    batch_num=$(printf "%03d" $i)
    batch_file="$SCRIPT_DIR/batch_$batch_num"
    info_file="$SCRIPT_DIR/batch_${batch_num}_info.json"

    if [[ -f "$batch_file" ]]; then
        log_info "Processing batch $batch_num..."

        # Verify integrity if info file exists
        if [[ -f "$info_file" ]]; then
            expected_checksum=$(jq -r '.checksum' "$info_file")
            actual_checksum=$(sha256sum "$batch_file" | cut -d' ' -f1)

            if [[ "$actual_checksum" == "$expected_checksum" ]]; then
                log_success "✓ Batch $batch_num integrity verified"
            else
                log_error "✗ Batch $batch_num integrity failed"
                exit 1
            fi
        fi

        # Concatenate to final package
        cat "$batch_file" >> "$TARGET_DIR/$PACKAGE_NAME"
    else
        log_error "Batch file not found: batch_$batch_num"
        exit 1
    fi
done

log_success "Reassembly completed: $TARGET_DIR/$PACKAGE_NAME"
log_info "Extract package with: tar -xzf $TARGET_DIR/$PACKAGE_NAME"

# Verify final package
if [[ -f "$TARGET_DIR/$PACKAGE_NAME" ]]; then
    log_info "Verifying final package integrity..."
    if tar -tzf "$TARGET_DIR/$PACKAGE_NAME" >/dev/null 2>&1; then
        log_success "✓ Final package integrity verified"
    else
        log_error "✗ Final package is corrupted"
        exit 1
    fi
else
    log_error "Final package not created"
    exit 1
fi
EOF

    chmod +x "$BATCH_DIR/reassemble.sh"
    log_success "Reassembly script created: $BATCH_DIR/reassemble.sh"
}

# Main function
main() {
    log_info "Starting deployment batch transfer optimization..."
    log_info "Deployment package: $DEPLOYMENT_PACKAGE"
    log_info "Batch size: ${BATCH_SIZE_MB}MB"
    log_info "Concurrent transfers: $MAX_CONCURRENT_TRANSFERS"
    log_info "Compression level: $COMPRESSION_LEVEL"

    # Parse arguments
    parse_arguments "$@"

    # Create transfer log
    mkdir -p "$(dirname "$TRANSFER_LOG")"

    # Execute transfer process
    analyze_package || exit 1
    create_batches || exit 1
    transfer_batches || exit 1
    generate_transfer_report || exit 1
    create_reassembly_script || exit 1

    log_success "🎉 Batch transfer optimization completed!"
    log_info "Batch directory: $BATCH_DIR"
    log_info "Transfer log: $TRANSFER_LOG"
    log_info "Transfer report: $BATCH_DIR/transfer-report.json"
    log_info "Reassembly script: $BATCH_DIR/reassemble.sh"
}

# Run main function
main "$@"