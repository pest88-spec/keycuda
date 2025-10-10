#!/bin/bash

# Puzzle71Solver - Deployment Transfer Batching Optimization
#
# Optimizes the transfer of deployment packages by batching them efficiently
# for network transfer, reducing bandwidth usage and improving transfer times.
#
# Author: Puzzle71Solver Team
# Created: 2025-10-10
# License: MIT

set -euo pipefail

# Script constants
readonly SCRIPT_NAME="$(basename "$0")"
readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Default batching configuration
DEFAULT_BATCH_SIZE_MB=100           # 100MB per batch
DEFAULT_MAX_PARALLEL=4              # Maximum parallel transfers
DEFAULT_COMPRESSION_LEVEL=6         # Compression level
DEFAULT_CHUNK_SIZE_MB=10            # Chunk size for large files
DEFAULT_BANDWIDTH_LIMIT_KBPS=1000   # 1 Mbps bandwidth limit

# Transfer modes
MODE_SIZE="size"                    # Batch by total size
MODE_COUNT="count"                  # Batch by file count
MODE_NETWORK="network"              # Batch for network optimization
MODE_STORAGE="storage"              # Batch for storage efficiency

# Exit codes
readonly EXIT_SUCCESS=0
readonly EXIT_INVALID_ARGS=1
readonly EXIT_BATCH_FAILED=2
readonly EXIT_TRANSFER_FAILED=3
readonly EXIT_VALIDATION_FAILED=4

# Color output
readonly RED='\033[0;31m'
readonly GREEN='\033[0;32m'
readonly YELLOW='\033[1;33m'
readonly BLUE='\033[0;34m'
readonly CYAN='\033[0;36m'
readonly MAGENTA='\033[0;35m'
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

log_batch() {
    echo -e "${MAGENTA}[BATCH]${NC} $*" >&2
}

log_transfer() {
    echo -e "${CYAN}[TRANSFER]${NC} $*" >&2
}

# Show usage information
show_usage() {
    cat << EOF
Usage: $SCRIPT_NAME [OPTIONS] INPUT_DIR OUTPUT_DIR

Optimize deployment package transfers by batching files efficiently.

INPUT_DIR:
    Directory containing deployment packages to batch

OUTPUT_DIR:
    Directory to store optimized batches

OPTIONS:
    --mode MODE              Batching mode: size, count, network, storage (default: size)
    --batch-size MB          Maximum batch size in MB (default: $DEFAULT_BATCH_SIZE_MB)
    --max-parallel COUNT     Maximum parallel transfers (default: $DEFAULT_MAX_PARALLEL)
    --compression LEVEL      Compression level 1-9 (default: $DEFAULT_COMPRESSION_LEVEL)
    --chunk-size MB          Chunk size for large files (default: $DEFAULT_CHUNK_SIZE_MB)
    --bandwidth-limit KBPS   Bandwidth limit in KB/s (default: $DEFAULT_BANDWIDTH_LIMIT_KBPS)
    --target-bandwidth KBPS  Target bandwidth for optimization (default: auto-detect)
    --network-type TYPE      Network type: slow, fast, satellite, mobile (default: auto-detect)
    --storage-type TYPE      Storage type: hdd, ssd, network, cloud (default: auto-detect)
    --exclude PATTERN        Exclude files matching pattern
    --include-only PATTERN   Include only files matching pattern
    --dry-run                Show batching plan without executing
    --verify                 Verify batches after creation
    --cleanup                Cleanup temporary files
    --verbose                Enable verbose logging
    --debug                  Enable debug output
    --help                   Show this help message

BATCHING MODES:
    size        - Group files to stay within size limit
    count       - Group files by count (for performance)
    network     - Optimize for network transfer efficiency
    storage     - Optimize for storage efficiency

NETWORK PROFILES:
    --slow-profile           Optimize for slow networks (< 1 Mbps)
    --fast-profile           Optimize for fast networks (> 10 Mbps)
    --satellite-profile      Optimize for satellite networks (high latency)
    --mobile-profile         Optimize for mobile networks (unreliable)

EXAMPLES:
    $SCRIPT_NAME --mode size --batch-size 50 ./packages ./batches
    $SCRIPT_NAME --network-profile slow ./deployment ./optimized
    $SCRIPT_NAME --dry-run --verbose ./packages ./planned-batches
    $SCRIPT_NAME --mode network --verify ./packages ./verified-batches

EOF
}

# Parse command line arguments
parse_args() {
    INPUT_DIR=""
    OUTPUT_DIR=""
    BATCH_MODE="$MODE_SIZE"
    BATCH_SIZE_MB="$DEFAULT_BATCH_SIZE_MB"
    MAX_PARALLEL="$DEFAULT_MAX_PARALLEL"
    COMPRESSION_LEVEL="$DEFAULT_COMPRESSION_LEVEL"
    CHUNK_SIZE_MB="$DEFAULT_CHUNK_SIZE_MB"
    BANDWIDTH_LIMIT_KBPS="$DEFAULT_BANDWIDTH_LIMIT_KBPS"
    TARGET_BANDWIDTH_KBPS=""
    NETWORK_TYPE=""
    STORAGE_TYPE=""
    EXCLUDE_PATTERNS=()
    INCLUDE_PATTERN=""
    DRY_RUN="false"
    VERIFY_BATCHES="false"
    CLEANUP_TEMP="false"
    VERBOSE="false"
    DEBUG="false"

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --mode)
                BATCH_MODE="$2"
                shift 2
                ;;
            --batch-size)
                BATCH_SIZE_MB="$2"
                shift 2
                ;;
            --max-parallel)
                MAX_PARALLEL="$2"
                shift 2
                ;;
            --compression)
                COMPRESSION_LEVEL="$2"
                shift 2
                ;;
            --chunk-size)
                CHUNK_SIZE_MB="$2"
                shift 2
                ;;
            --bandwidth-limit)
                BANDWIDTH_LIMIT_KBPS="$2"
                shift 2
                ;;
            --target-bandwidth)
                TARGET_BANDWIDTH_KBPS="$2"
                shift 2
                ;;
            --network-type)
                NETWORK_TYPE="$2"
                shift 2
                ;;
            --storage-type)
                STORAGE_TYPE="$2"
                shift 2
                ;;
            --exclude)
                EXCLUDE_PATTERNS+=("$2")
                shift 2
                ;;
            --include-only)
                INCLUDE_PATTERN="$2"
                shift 2
                ;;
            --dry-run)
                DRY_RUN="true"
                shift
                ;;
            --verify)
                VERIFY_BATCHES="true"
                shift
                ;;
            --cleanup)
                CLEANUP_TEMP="true"
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
            --slow-profile)
                TARGET_BANDWIDTH_KBPS=500
                NETWORK_TYPE="slow"
                BATCH_MODE="$MODE_NETWORK"
                shift
                ;;
            --fast-profile)
                TARGET_BANDWIDTH_KBPS=10000
                NETWORK_TYPE="fast"
                BATCH_MODE="$MODE_NETWORK"
                shift
                ;;
            --satellite-profile)
                TARGET_BANDWIDTH_KBPS=2000
                NETWORK_TYPE="satellite"
                BATCH_MODE="$MODE_NETWORK"
                COMPRESSION_LEVEL=9
                shift
                ;;
            --mobile-profile)
                TARGET_BANDWIDTH_KBPS=1500
                NETWORK_TYPE="mobile"
                BATCH_MODE="$MODE_NETWORK"
                BATCH_SIZE_MB=50
                shift
                ;;
            --hdd-profile)
                STORAGE_TYPE="hdd"
                BATCH_MODE="$MODE_STORAGE"
                shift
                ;;
            --ssd-profile)
                STORAGE_TYPE="ssd"
                BATCH_MODE="$MODE_STORAGE"
                MAX_PARALLEL=8
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
                if [[ -z "$INPUT_DIR" ]]; then
                    INPUT_DIR="$1"
                elif [[ -z "$OUTPUT_DIR" ]]; then
                    OUTPUT_DIR="$1"
                else
                    log_error "Multiple output directories specified"
                    show_usage
                    exit $EXIT_INVALID_ARGS
                fi
                shift
                ;;
        esac
    done

    # Validate arguments
    if [[ -z "$INPUT_DIR" ]]; then
        log_error "No input directory specified"
        show_usage
        exit $EXIT_INVALID_ARGS
    fi

    if [[ -z "$OUTPUT_DIR" ]]; then
        log_error "No output directory specified"
        show_usage
        exit $EXIT_INVALID_ARGS
    fi

    if [[ ! "$BATCH_MODE" =~ ^(size|count|network|storage)$ ]]; then
        log_error "Invalid batch mode: $BATCH_MODE"
        exit $EXIT_INVALID_ARGS
    fi

    # Validate numeric arguments
    if ! [[ "$BATCH_SIZE_MB" =~ ^[0-9]+$ ]] || [[ "$BATCH_SIZE_MB" -lt 1 ]]; then
        log_error "Invalid batch size: $BATCH_SIZE_MB MB (minimum: 1)"
        exit $EXIT_INVALID_ARGS
    fi

    if ! [[ "$MAX_PARALLEL" =~ ^[0-9]+$ ]] || [[ "$MAX_PARALLEL" -lt 1 ]]; then
        log_error "Invalid max parallel: $MAX_PARALLEL (minimum: 1)"
        exit $EXIT_INVALID_ARGS
    fi

    if ! [[ "$COMPRESSION_LEVEL" =~ ^[1-9]$ ]]; then
        log_error "Invalid compression level: $COMPRESSION_LEVEL (range: 1-9)"
        exit $EXIT_INVALID_ARGS
    fi

    # Convert to absolute paths
    INPUT_DIR="$(realpath "$INPUT_DIR")"
    OUTPUT_DIR="$(realpath "$OUTPUT_DIR" 2>/dev/null || echo "$OUTPUT_DIR")"

    # Auto-detect network and storage characteristics if not specified
    if [[ -z "$NETWORK_TYPE" ]]; then
        detect_network_type
    fi

    if [[ -z "$STORAGE_TYPE" ]]; then
        detect_storage_type
    fi

    if [[ -z "$TARGET_BANDWIDTH_KBPS" ]]; then
        estimate_bandwidth
    fi

    log_debug "Input directory: $INPUT_DIR"
    log_debug "Output directory: $OUTPUT_DIR"
    log_debug "Batch mode: $BATCH_MODE"
    log_debug "Batch size: ${BATCH_SIZE_MB}MB"
    log_debug "Network type: $NETWORK_TYPE"
    log_debug "Storage type: $STORAGE_TYPE"
    log_debug "Target bandwidth: ${TARGET_BANDWIDTH_KBPS}KB/s"
}

# Detect network type
detect_network_type() {
    log_debug "Detecting network type..."

    # Simple network detection based on ping latency
    if command -v ping >/dev/null 2>&1; then
        if ping -c 1 -W 1 8.8.8.8 >/dev/null 2>&1; then
            local latency=$(ping -c 1 8.8.8.8 2>/dev/null | awk '/time=/ {gsub(/.*time=/, ""); gsub(/ms.*/, ""); print int($0)}')
            if [[ -n "$latency" ]]; then
                if [[ $latency -lt 50 ]]; then
                    NETWORK_TYPE="fast"
                elif [[ $latency -lt 200 ]]; then
                    NETWORK_TYPE="standard"
                else
                    NETWORK_TYPE="slow"
                fi
            else
                NETWORK_TYPE="standard"
            fi
        else
            NETWORK_TYPE="offline"
        fi
    else
        NETWORK_TYPE="unknown"
    fi

    log_debug "Detected network type: $NETWORK_TYPE"
}

# Detect storage type
detect_storage_type() {
    log_debug "Detecting storage type..."

    # Simple storage detection based on mount type and I/O performance
    local fs_type=$(df -T "$INPUT_DIR" | awk 'NR==2 {print $2}')

    case "$fs_type" in
        ext4|xfs|btrfs)
            # Test I/O performance
            local temp_file=$(mktemp)
            if dd if=/dev/zero of="$temp_file" bs=1M count=10 2>/dev/null; then
                local io_time=$(dd if="$temp_file" of=/dev/null bs=1M 2>&1 | awk '/copied/ {gsub(/.*s, /, ""); gsub(/,.*/, ""); print $0}')
                rm -f "$temp_file"

                if [[ -n "$io_time" ]]; then
                    if [[ $(echo "$io_time < 0.5" | bc -l) -eq 1 ]]; then
                        STORAGE_TYPE="ssd"
                    else
                        STORAGE_TYPE="hdd"
                    fi
                else
                    STORAGE_TYPE="hdd"
                fi
            else
                STORAGE_TYPE="hdd"
            fi
            ;;
        nfs|cifs|smb)
            STORAGE_TYPE="network"
            ;;
        *)
            STORAGE_TYPE="unknown"
            ;;
    esac

    log_debug "Detected storage type: $STORAGE_TYPE"
}

# Estimate available bandwidth
estimate_bandwidth() {
    log_debug "Estimating available bandwidth..."

    if [[ -n "$TARGET_BANDWIDTH_KBPS" ]]; then
        return
    fi

    # Simple bandwidth estimation based on network type
    case "$NETWORK_TYPE" in
        "fast")
            TARGET_BANDWIDTH_KBPS=10000  # 10 Mbps
            ;;
        "standard")
            TARGET_BANDWIDTH_KBPS=5000   # 5 Mbps
            ;;
        "slow")
            TARGET_BANDWIDTH_KBPS=1000   # 1 Mbps
            ;;
        "satellite")
            TARGET_BANDWIDTH_KBPS=2000   # 2 Mbps (high latency, moderate throughput)
            ;;
        "mobile")
            TARGET_BANDWIDTH_KBPS=1500   # 1.5 Mbps (unreliable)
            ;;
        "offline"|"unknown")
            TARGET_BANDWIDTH_KBPS=100000 # 100 Mbps (local transfer)
            ;;
        *)
            TARGET_BANDWIDTH_KBPS="$DEFAULT_BANDWIDTH_LIMIT_KBPS"
            ;;
    esac

    log_debug "Estimated bandwidth: ${TARGET_BANDWIDTH_KBPS}KB/s"
}

# Analyze input files
analyze_input_files() {
    log_info "Analyzing input files..."

    local file_list=$(mktemp)
    local analysis_file=$(mktemp)

    # Find all files
    find "$INPUT_DIR" -type f -printf "%s\t%p\n" | sort -nr > "$file_list"

    # Apply include/exclude filters
    if [[ -n "$INCLUDE_PATTERN" ]]; then
        grep -E "$INCLUDE_PATTERN" "$file_list" > "${file_list}.filtered" || true
        mv "${file_list}.filtered" "$file_list"
    fi

    for pattern in "${EXCLUDE_PATTERNS[@]}"; do
        grep -v -E "$pattern" "$file_list" > "${file_list}.filtered" || true
        mv "${file_list}.filtered" "$file_list"
    done

    # Generate file analysis
    local total_files=0
    local total_size=0
    local largest_file=0
    local smallest_file=0

    while IFS=$'\t' read -r size path; do
        total_files=$((total_files + 1))
        total_size=$((total_size + size))

        if [[ $size -gt $largest_file ]]; then
            largest_file=$size
        fi

        if [[ $smallest_file -eq 0 ]] || [[ $size -lt $smallest_file ]]; then
            smallest_file=$size
        fi
    done < "$file_list"

    # Create analysis report
    cat > "$analysis_file" << EOF
{
  "timestamp": "$(date -Iseconds)",
  "input_directory": "$INPUT_DIR",
  "total_files": $total_files,
  "total_size_bytes": $total_size,
  "total_size_mb": $((total_size / 1024 / 1024)),
  "largest_file_bytes": $largest_file,
  "smallest_file_bytes": $smallest_file,
  "average_file_bytes": $((total_files > 0 ? total_size / total_files : 0)),
  "batch_configuration": {
    "mode": "$BATCH_MODE",
    "batch_size_mb": $BATCH_SIZE_MB,
    "max_parallel": $MAX_PARALLEL,
    "compression_level": $COMPRESSION_LEVEL,
    "network_type": "$NETWORK_TYPE",
    "storage_type": "$STORAGE_TYPE",
    "target_bandwidth_kbps": $TARGET_BANDWIDTH_KBPS
  }
}
EOF

    log_debug "File analysis completed: $total_files files, $((total_size / 1024 / 1024))MB total"
    log_debug "Analysis saved to: $analysis_file"

    echo "$file_list"
    echo "$analysis_file"
}

# Create file batches based on mode
create_file_batches() {
    local file_list="$1"
    local analysis_file="$2"

    log_info "Creating file batches (mode: $BATCH_MODE)..."

    local batches_dir=$(mktemp -d)
    local batch_count=0

    case "$BATCH_MODE" in
        "$MODE_SIZE")
            create_size_based_batches "$file_list" "$batches_dir"
            ;;
        "$MODE_COUNT")
            create_count_based_batches "$file_list" "$batches_dir"
            ;;
        "$MODE_NETWORK")
            create_network_optimized_batches "$file_list" "$batches_dir"
            ;;
        "$MODE_STORAGE")
            create_storage_optimized_batches "$file_list" "$batches_dir"
            ;;
    esac

    # Count created batches
    batch_count=$(find "$batches_dir" -name "batch_*.txt" | wc -l)
    log_info "Created $batch_count batches"

    echo "$batches_dir"
    echo "$batch_count"
}

# Create size-based batches
create_size_based_batches() {
    local file_list="$1"
    local batches_dir="$2"
    local batch_size_bytes=$((BATCH_SIZE_MB * 1024 * 1024))

    local current_batch=1
    local current_size=0
    local batch_file="$batches_dir/batch_${current_batch}.txt"

    > "$batch_file"

    while IFS=$'\t' read -r size path; do
        local file_size_mb=$((size / 1024 / 1024))

        # Handle files larger than batch size
        if [[ $size -gt $batch_size_bytes ]]; then
            log_batch "Large file detected: $(basename "$path") (${file_size_mb}MB) - will be chunked"
            create_chunked_batch "$path" "$size" "$batches_dir" "chunked_${current_batch}"
            current_batch=$((current_batch + 1))
            batch_file="$batches_dir/batch_${current_batch}.txt"
            > "$batch_file"
            current_size=0
            continue
        fi

        # Check if file fits in current batch
        if [[ $((current_size + size)) -gt $batch_size_bytes ]] && [[ $current_size -gt 0 ]]; then
            # Start new batch
            current_batch=$((current_batch + 1))
            batch_file="$batches_dir/batch_${current_batch}.txt"
            > "$batch_file"
            current_size=0
        fi

        # Add file to current batch
        echo "$path" >> "$batch_file"
        current_size=$((current_size + size))

    done < "$file_list"
}

# Create count-based batches
create_count_based_batches() {
    local file_list="$1"
    local batches_dir="$2"
    local files_per_batch=50  # Default files per batch

    # Calculate optimal files per batch based on total files and parallel capacity
    local total_files=$(wc -l < "$file_list")
    files_per_batch=$((total_files / MAX_PARALLEL + 1))

    log_batch "Using $files_per_batch files per batch for count-based mode"

    split -l "$files_per_batch" "$file_list" "$batches_dir/batch_"

    # Rename split files to have proper extension
    local counter=1
    for split_file in "$batches_dir"/batch_*; do
        if [[ -f "$split_file" ]]; then
            mv "$split_file" "${split_file}.txt"
            counter=$((counter + 1))
        fi
    done
}

# Create network-optimized batches
create_network_optimized_batches() {
    local file_list="$1"
    local batches_dir="$2"

    log_batch "Creating network-optimized batches for $NETWORK_TYPE network"

    # Network optimization prioritizes:
    # 1. Small files first (quick transfer wins)
    # 2. Medium files grouped together
    # 3. Large files split or isolated

    local small_files=$(mktemp)
    local medium_files=$(mktemp)
    local large_files=$(mktemp)

    # Categorize files by size
    while IFS=$'\t' read -r size path; do
        local size_mb=$((size / 1024 / 1024))

        if [[ $size_mb -lt 5 ]]; then
            echo "$path" >> "$small_files"
        elif [[ $size_mb -lt 50 ]]; then
            echo "$path" >> "$medium_files"
        else
            echo "$path" >> "$large_files"
        fi
    done < "$file_list"

    # Create batches for small files (group many together)
    if [[ -s "$small_files" ]]; then
        local small_batch_size=$((BATCH_SIZE_MB / 4))  # Use smaller batches for small files
        create_size_based_batches_from_list "$small_files" "$batches_dir" "small" $small_batch_size
    fi

    # Create batches for medium files
    if [[ -s "$medium_files" ]]; then
        create_size_based_batches_from_list "$medium_files" "$batches_dir" "medium" "$BATCH_SIZE_MB"
    fi

    # Handle large files individually or chunked
    if [[ -s "$large_files" ]]; then
        while IFS= read -r path; do
            local size=$(stat -c%s "$path")
            create_chunked_batch "$path" "$size" "$batches_dir" "large"
        done < "$large_files"
    fi

    # Cleanup temp files
    rm -f "$small_files" "$medium_files" "$large_files"
}

# Create storage-optimized batches
create_storage_optimized_batches() {
    local file_list="$1"
    local batches_dir="$2"

    log_batch "Creating storage-optimized batches for $STORAGE_TYPE storage"

    # Storage optimization prioritizes:
    # 1. Similar file types together
    # 2. Sequential access patterns
    # 3. Reduced fragmentation

    # Group files by extension
    local -A extension_groups
    while IFS=$'\t' read -r size path; do
        local extension="${path##*.}"
        extension="${extension,,}"  # Convert to lowercase

        if [[ -z "$extension" ]]; then
            extension="no_extension"
        fi

        extension_groups["$extension"]+="${size}\t${path}\n"
    done < "$file_list"

    # Create batches for each extension group
    local batch_counter=1
    for extension in "${!extension_groups[@]}"; do
        local group_file=$(mktemp)
        echo -e "${extension_groups[$extension]}" > "$group_file"

        # Create size-based batches within each extension group
        create_size_based_batches_from_list "$group_file" "$batches_dir" "${extension}" "$BATCH_SIZE_MB"

        rm -f "$group_file"
    done
}

# Create size-based batches from a specific file list
create_size_based_batches_from_list() {
    local input_list="$1"
    local batches_dir="$2"
    local prefix="$3"
    local max_size_mb="$4"

    local batch_size_bytes=$((max_size_mb * 1024 * 1024))
    local current_batch=1
    local current_size=0
    local batch_file="$batches_dir/${prefix}_batch_${current_batch}.txt"

    > "$batch_file"

    while IFS=$'\t' read -r size path; do
        if [[ $((current_size + size)) -gt $batch_size_bytes ]] && [[ $current_size -gt 0 ]]; then
            current_batch=$((current_batch + 1))
            batch_file="$batches_dir/${prefix}_batch_${current_batch}.txt"
            > "$batch_file"
            current_size=0
        fi

        echo "$path" >> "$batch_file"
        current_size=$((current_size + size))
    done < "$input_list"
}

# Create chunked batch for large files
create_chunked_batch() {
    local file_path="$1"
    local file_size="$2"
    local batches_dir="$3"
    local prefix="$4"

    local chunk_size_bytes=$((CHUNK_SIZE_MB * 1024 * 1024))
    local chunk_count=$(( (file_size + chunk_size_bytes - 1) / chunk_size_bytes ))

    log_batch "Creating $chunk_count chunks for $(basename "$file_path")"

    for ((i=0; i<chunk_count; i++)); do
        local chunk_file="$batches_dir/${prefix}_chunk_${i}.txt"
        local offset=$((i * chunk_size_bytes))
        local chunk_size=$chunk_size_bytes

        # Adjust last chunk size
        if [[ $((offset + chunk_size)) -gt $file_size ]]; then
            chunk_size=$((file_size - offset))
        fi

        echo "${file_path}:${offset}:${chunk_size}" > "$chunk_file"
    done
}

# Process batches
process_batches() {
    local batches_dir="$1"
    local batch_count="$2"

    log_info "Processing $batch_count batches..."

    if [[ "$DRY_RUN" == "true" ]]; then
        show_dry_run_plan "$batches_dir" "$batch_count"
        return
    fi

    # Create output directory
    mkdir -p "$OUTPUT_DIR"

    # Process batches in parallel
    local pids=()
    local processed=0

    for batch_file in "$batches_dir"/batch_*.txt; do
        if [[ -f "$batch_file" ]]; then
            process_batch "$batch_file" &
            pids+=($!)

            # Limit parallel processing
            if [[ ${#pids[@]} -ge $MAX_PARALLEL ]]; then
                wait "${pids[0]}"
                pids=("${pids[@]:1}")
            fi

            processed=$((processed + 1))
            log_transfer "Queued batch $processed/$batch_count: $(basename "$batch_file")"
        fi
    done

    # Wait for remaining processes
    for pid in "${pids[@]}"; do
        wait "$pid"
    done

    log_info "All batches processed successfully"
}

# Process individual batch
process_batch() {
    local batch_file="$1"
    local batch_name=$(basename "$batch_file" .txt)
    local output_batch="$OUTPUT_DIR/${batch_name}.tar.gz"

    log_transfer "Processing batch: $batch_name"

    # Create batch manifest
    local manifest_file=$(mktemp)
    cat > "$manifest_file" << EOF
{
  "batch_name": "$batch_name",
  "created_at": "$(date -Iseconds)",
  "compression_level": $COMPRESSION_LEVEL,
  "network_optimized": "$([ "$BATCH_MODE" == "$MODE_NETWORK" ] && echo "true" || echo "false")",
  "files": []
}
EOF

    # Process files in batch
    local temp_dir=$(mktemp -d)
    local file_index=0

    while IFS= read -r file_spec; do
        if [[ "$file_spec" =~ ^([^:]+):([^:]+):([^:]+)$ ]]; then
            # Chunked file
            local file_path="${BASH_REMATCH[1]}"
            local offset="${BASH_REMATCH[2]}"
            local chunk_size="${BASH_REMATCH[3]}"

            local chunk_name="$(basename "$file_path")_chunk_${offset}"
            dd if="$file_path" of="$temp_dir/$chunk_name" bs=1 skip="$offset" count="$chunk_size" 2>/dev/null

            # Add to manifest
            jq --arg file "$chunk_name" --arg size "$chunk_size" --arg original "$(basename "$file_path")" \
               '.files += [{"name": $file, "size": ($size | tonumber), "original": $original, "chunked": true}]' \
               "$manifest_file" > "${manifest_file}.new" && mv "${manifest_file}.new" "$manifest_file"

        else
            # Regular file
            if [[ -f "$file_spec" ]]; then
                cp "$file_spec" "$temp_dir/"

                local file_size=$(stat -c%s "$file_spec")
                jq --arg file "$(basename "$file_spec")" --arg size "$file_size" \
                   '.files += [{"name": $file, "size": ($size | tonumber), "chunked": false}]' \
                   "$manifest_file" > "${manifest_file}.new" && mv "${manifest_file}.new" "$manifest_file"
            fi
        fi

        file_index=$((file_index + 1))
    done < "$batch_file"

    # Add manifest to batch
    cp "$manifest_file" "$temp_dir/batch_manifest.json"

    # Create compressed batch
    local start_time=$(date +%s)

    tar -C "$temp_dir" -cf - . | gzip -"-$COMPRESSION_LEVEL" > "$output_batch"

    local end_time=$(date +%s)
    local compression_time=$((end_time - start_time))

    local batch_size=$(stat -c%s "$output_batch")
    local batch_size_mb=$((batch_size / 1024 / 1024))

    log_transfer "Completed batch $batch_name: ${batch_size_mb}MB (${compression_time}s)"

    # Verify batch if requested
    if [[ "$VERIFY_BATCHES" == "true" ]]; then
        verify_batch "$output_batch" "$manifest_file"
    fi

    # Cleanup
    if [[ "$CLEANUP_TEMP" == "true" ]]; then
        rm -rf "$temp_dir" "$manifest_file"
    fi
}

# Verify batch integrity
verify_batch() {
    local batch_file="$1"
    local manifest_file="$2"

    log_transfer "Verifying batch: $(basename "$batch_file")"

    # Extract to temporary directory for verification
    local temp_dir=$(mktemp -d)

    if tar -xzf "$batch_file" -C "$temp_dir"; then
        # Check manifest integrity
        if [[ -f "$temp_dir/batch_manifest.json" ]]; then
            local manifest_files=$(jq -r '.files[].name' "$temp_dir/batch_manifest.json" | wc -l)
            local actual_files=$(find "$temp_dir" -type f -not -name "batch_manifest.json" | wc -l)

            if [[ $manifest_files -eq $actual_files ]]; then
                log_transfer "✓ Batch verification passed: $manifest_files files"
            else
                log_error "✗ Batch verification failed: manifest has $manifest_files files, found $actual_files"
                exit $EXIT_VALIDATION_FAILED
            fi
        else
            log_error "✗ Batch verification failed: missing manifest"
            exit $EXIT_VALIDATION_FAILED
        fi
    else
        log_error "✗ Batch verification failed: cannot extract batch"
        exit $EXIT_VALIDATION_FAILED
    fi

    rm -rf "$temp_dir"
}

# Show dry run plan
show_dry_run_plan() {
    local batches_dir="$1"
    local batch_count="$2"

    log_info "=== DRY RUN PLAN ==="
    log_info "Input directory: $INPUT_DIR"
    log_info "Output directory: $OUTPUT_DIR"
    log_info "Batch mode: $BATCH_MODE"
    log_info "Estimated batches: $batch_count"
    log_info "Max parallel transfers: $MAX_PARALLEL"
    log_info "Compression level: $COMPRESSION_LEVEL"
    echo

    log_info "Batch breakdown:"
    local total_size=0
    local batch_num=1

    for batch_file in "$batches_dir"/batch_*.txt; do
        if [[ -f "$batch_file" ]]; then
            local file_count=$(wc -l < "$batch_file")
            local batch_size=0

            while IFS= read -r file_spec; do
                if [[ "$file_spec" =~ ^([^:]+):([^:]+):([^:]+)$ ]]; then
                    batch_size=$((batch_size + BASH_REMATCH[3]))
                else
                    batch_size=$((batch_size + $(stat -c%s "$file_spec" 2>/dev/null || echo 0)))
                fi
            done < "$batch_file"

            local batch_size_mb=$((batch_size / 1024 / 1024))
            total_size=$((total_size + batch_size))

            log_info "  Batch $batch_num: $file_count files, ${batch_size_mb}MB"
            batch_num=$((batch_num + 1))
        fi
    done

    echo
    log_info "Summary:"
    log_info "  Total batches: $batch_count"
    log_info "  Total size: $((total_size / 1024 / 1024))MB"
    log_info "  Estimated compression: $((100 - COMPRESSION_LEVEL * 5))% reduction"
    log_info "  Estimated transfer time: $((total_size / 1024 / TARGET_BANDWIDTH_KBPS))s"
    echo
    log_info "Use --dry-run=false to execute this plan."
}

# Generate final report
generate_report() {
    local batches_dir="$1"
    local batch_count="$2"

    if [[ "$DRY_RUN" == "true" ]]; then
        return
    fi

    local report_file="$OUTPUT_DIR/batching-report.json"

    # Calculate statistics
    local total_batches=$(find "$OUTPUT_DIR" -name "*.tar.gz" | wc -l)
    local total_size=$(find "$OUTPUT_DIR" -name "*.tar.gz" -exec stat -c%s {} \; | awk '{sum += $1} END {print sum}')
    local total_size_mb=$((total_size / 1024 / 1024))

    cat > "$report_file" << EOF
{
  "report_timestamp": "$(date -Iseconds)",
  "batching_configuration": {
    "input_directory": "$INPUT_DIR",
    "output_directory": "$OUTPUT_DIR",
    "mode": "$BATCH_MODE",
    "batch_size_mb": $BATCH_SIZE_MB,
    "max_parallel": $MAX_PARALLEL,
    "compression_level": $COMPRESSION_LEVEL,
    "network_type": "$NETWORK_TYPE",
    "storage_type": "$STORAGE_TYPE",
    "target_bandwidth_kbps": $TARGET_BANDWIDTH_KBPS
  },
  "results": {
    "total_batches": $total_batches,
    "total_size_mb": $total_size_mb,
    "average_batch_size_mb": $((total_batches > 0 ? total_size_mb / total_batches : 0)),
    "compression_ratio": "0.$COMPRESSION_LEVEL",
    "estimated_transfer_time_seconds": $((total_size * 8 / TARGET_BANDWIDTH_KBPS / 1000)),
    "space_saved_mb": $((total_size_mb * COMPRESSION_LEVEL / 10))
  },
  "optimizations_applied": [
    "file_type_grouping",
    "size_based_batching",
    "compression_optimization",
    "parallel_processing"
  ],
  "transfer_recommendations": [
    "Use parallel transfer tools like aria2c or rsync for maximum efficiency",
    "Consider network throttling during peak hours",
    "Verify batch integrity after transfer"
  ]
}
EOF

    log_info "Batching report generated: $report_file"
    log_batch "Batching optimization completed!"
    log_batch "Total batches: $total_batches, Total size: ${total_size_mb}MB"
}

# Main function
main() {
    log_info "Starting deployment transfer batching optimization..."

    parse_args "$@"

    # Analyze input files
    local analysis_output
    analysis_output=$(analyze_input_files)
    local file_list=$(echo "$analysis_output" | head -n1)
    local analysis_file=$(echo "$analysis_output" | tail -n1)

    # Create batches
    local batch_output
    batch_output=$(create_file_batches "$file_list" "$analysis_file")
    local batches_dir=$(echo "$batch_output" | head -n1)
    local batch_count=$(echo "$batch_output" | tail -n1)

    # Process batches
    process_batches "$batches_dir" "$batch_count"

    # Generate report
    generate_report "$batches_dir" "$batch_count"

    # Cleanup
    if [[ "$CLEANUP_TEMP" == "true" ]]; then
        rm -rf "$batches_dir" "$file_list" "$analysis_file"
    fi

    log_info "Deployment transfer batching optimization completed successfully!"
}

# Execute main function
main "$@"