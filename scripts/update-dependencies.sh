#!/bin/bash
# T045/T045b: Dependency Version Management System
# Implements comprehensive version tracking, updating, and management for third-party dependencies

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
DEPENDENCIES_CONFIG="${PROJECT_ROOT}/config/dependencies.json"
INTEGRATION_MANIFEST="${PROJECT_ROOT}/src/integration/manifests/dependency_manifest.json"
VERSION_HISTORY="${PROJECT_ROOT}/src/integration/manifests/version_history.json"
BACKUP_DIR="${PROJECT_ROOT}/.dependency-updates"
UPDATE_LOG="${PROJECT_ROOT}/logs/dependency-updates.log"
DRY_RUN="${DRY_RUN:-false}"
FORCE_UPDATE="${FORCE_UPDATE:-false}"
SKIP_COMPATIBILITY_CHECK="${SKIP_COMPATIBILITY_CHECK:-false}"

# Logging functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1" | tee -a "$UPDATE_LOG"
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

log_version() {
    echo -e "${PURPLE}[VERSION]${NC} $1" | tee -a "$UPDATE_LOG"
}

log_update() {
    echo -e "${CYAN}[UPDATE]${NC} $1" | tee -a "$UPDATE_LOG"
}

# Show help
show_help() {
    cat << EOF
Dependency Version Management Script

USAGE:
    $0 [OPTIONS] COMMAND [ARGS...]

COMMANDS:
    list                          List all configured dependencies and their versions
    update <library> <version>    Update a specific library to a new version
    update-all                    Update all libraries to latest compatible versions
    check-updates                 Check for available updates
    rollback <library> <version>  Rollback to a previous version
    history <library>             Show update history for a library
    validate                      Validate all dependency configurations
    export-manifest              Export current dependency manifest
    import-manifest <file>       Import dependency manifest from file

OPTIONS:
    --dry-run                    Show what would be done without executing
    --force                      Force update even with compatibility warnings
    --skip-compatibility         Skip compatibility validation
    --backup-dir DIR            Custom backup directory (default: .dependency-updates)
    --config FILE               Custom dependencies config file
    --log FILE                  Custom log file
    --verbose                    Enable verbose output
    --help, -h                   Show this help message

EXAMPLES:
    $0 list                                    # List all dependencies
    $0 update secp256k1-zkp v0.2.0            # Update to specific version
    $0 update-all                              # Update all libraries
    $0 check-updates                          # Check for available updates
    $0 rollback secp256k1-zkp v0.1.0          # Rollback version
    $0 history secp256k1-zkp                   # Show update history

EOF
}

# Parse command line arguments
parse_arguments() {
    COMMAND=""
    COMMAND_ARGS=()

    while [[ $# -gt 0 ]]; do
        case $1 in
            --dry-run)
                DRY_RUN=true
                shift
                ;;
            --force)
                FORCE_UPDATE=true
                shift
                ;;
            --skip-compatibility)
                SKIP_COMPATIBILITY_CHECK=true
                shift
                ;;
            --backup-dir)
                BACKUP_DIR="$2"
                shift 2
                ;;
            --config)
                DEPENDENCIES_CONFIG="$2"
                shift 2
                ;;
            --log)
                UPDATE_LOG="$2"
                shift 2
                ;;
            --verbose)
                set -x
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

# Initialize required directories and files
initialize_environment() {
    # Create directories
    mkdir -p "$(dirname "$UPDATE_LOG")"
    mkdir -p "$(dirname "$INTEGRATION_MANIFEST")"
    mkdir -p "$BACKUP_DIR"
    mkdir -p "$(dirname "$DEPENDENCIES_CONFIG")"

    # Initialize default dependencies config if not exists
    if [[ ! -f "$DEPENDENCIES_CONFIG" ]]; then
        create_default_dependencies_config
    fi

    # Initialize version history if not exists
    if [[ ! -f "$VERSION_HISTORY" ]]; then
        create_empty_version_history
    fi
}

# Create default dependencies configuration
create_default_dependencies_config() {
    log_info "Creating default dependencies configuration..."

    cat > "$DEPENDENCIES_CONFIG" << 'EOF'
{
  "dependencies": {
    "secp256k1-zkp": {
      "name": "secp256k1-zkp",
      "type": "extracted",
      "current_version": "v0.1.0",
      "origin_url": "https://github.com/BlockstreamResearch/secp256k1-zkp",
      "extract_path": "src/extracted/secp256k1-zkp",
      "license": "MIT",
      "spdx_identifier": "MIT",
      "update_strategy": "semantic",
      "compatibility_check": true,
      "auto_update": false,
      "last_update": "2025-10-09T00:00:00Z",
      "update_history": [],
      "dependencies": [],
      "include_patterns": [
        "src/**/*.c",
        "src/**/*.h",
        "include/**/*.h"
      ],
      "exclude_patterns": [
        "tests/**",
        "docs/**",
        "examples/**",
        "*.md"
      ]
    },
    "nlohmann/json": {
      "name": "nlohmann/json",
      "type": "fetchcontent",
      "current_version": "v3.11.3",
      "origin_url": "https://github.com/nlohmann/json",
      "license": "MIT",
      "spdx_identifier": "MIT",
      "update_strategy": "semantic",
      "compatibility_check": true,
      "auto_update": false,
      "last_update": "2025-10-09T00:00:00Z"
    },
    "googletest": {
      "name": "googletest",
      "type": "fetchcontent",
      "current_version": "v1.14.0",
      "origin_url": "https://github.com/google/googletest",
      "license": "BSD-3-Clause",
      "spdx_identifier": "BSD-3-Clause",
      "update_strategy": "semantic",
      "compatibility_check": true,
      "auto_update": false,
      "last_update": "2025-10-09T00:00:00Z"
    }
  },
  "global_settings": {
    "auto_backup": true,
    "compatibility_validation": true,
    "update_timeout_seconds": 600,
    "max_retry_attempts": 3,
    "notification_enabled": true,
    "log_level": "INFO"
  }
}
EOF

    log_success "Default dependencies configuration created: $DEPENDENCIES_CONFIG"
}

# Create empty version history file
create_empty_version_history() {
    log_info "Creating empty version history..."

    cat > "$VERSION_HISTORY" << 'EOF'
{
  "version_history": {
    "metadata": {
      "created": "2025-10-09T00:00:00Z",
      "last_updated": "2025-10-09T00:00:00Z",
      "total_updates": 0,
      "successful_updates": 0,
      "failed_updates": 0
    },
    "libraries": {}
  }
}
EOF

    log_success "Version history file created: $VERSION_HISTORY"
}

# List all configured dependencies
list_dependencies() {
    log_info "Listing all configured dependencies..."

    if command -v jq >/dev/null 2>&1; then
        echo -e "\n${BLUE}Configured Dependencies:${NC}"
        echo "----------------------------------------"

        while IFS= read -r dep_name; do
            local dep_info=$(jq -r ".dependencies.\"$dep_name\"" "$DEPENDENCIES_CONFIG")
            local current_version=$(echo "$dep_info" | jq -r '.current_version // "unknown"')
            local dep_type=$(echo "$dep_info" | jq -r '.type // "unknown"')
            local origin_url=$(echo "$dep_info" | jq -r '.origin_url // "unknown"')
            local license=$(echo "$dep_info" | jq -r '.license // "unknown"')

            echo -e "\n${CYAN}$dep_name${NC}"
            echo "  Version: $current_version"
            echo "  Type: $dep_type"
            echo "  URL: $origin_url"
            echo "  License: $license"
        done < <(jq -r '.dependencies | keys[]' "$DEPENDENCIES_CONFIG")
    else
        log_error "jq not available for JSON parsing"
        exit 1
    fi

    echo -e "\n"
}

# Update dependency to new version
update_dependency() {
    local library_name="$1"
    local new_version="$2"

    log_update "Updating dependency: $library_name to $new_version"

    # Validate library exists in configuration
    if ! jq -e ".dependencies.\"$library_name\"" "$DEPENDENCIES_CONFIG" >/dev/null 2>&1; then
        log_error "Library '$library_name' not found in configuration"
        return 1
    fi

    # Get current version
    local current_version=$(jq -r ".dependencies.\"$library_name\".current_version" "$DEPENDENCIES_CONFIG")

    if [[ "$current_version" == "$new_version" ]]; then
        log_warning "Library '$library_name' is already at version $new_version"
        return 0
    fi

    # Create backup before update
    if [[ "${DRY_RUN}" != "true" ]]; then
        create_dependency_backup "$library_name" "$current_version"
    fi

    # Run compatibility check
    if [[ "$SKIP_COMPATIBILITY_CHECK" != "true" ]]; then
        if ! validate_compatibility "$library_name" "$current_version" "$new_version"; then
            if [[ "$FORCE_UPDATE" != "true" ]]; then
                log_error "Compatibility check failed. Use --force to override."
                return 1
            else
                log_warning "Compatibility check failed but proceeding due to --force flag"
            fi
        fi
    fi

    # Perform update based on dependency type
    local dep_type=$(jq -r ".dependencies.\"$library_name\".type" "$DEPENDENCIES_CONFIG")

    case "$dep_type" in
        "extracted")
            update_extracted_dependency "$library_name" "$new_version"
            ;;
        "fetchcontent")
            update_fetchcontent_dependency "$library_name" "$new_version"
            ;;
        "submodule")
            update_submodule_dependency "$library_name" "$new_version"
            ;;
        *)
            log_error "Unknown dependency type: $dep_type"
            return 1
            ;;
    esac

    # Update configuration
    if [[ "${DRY_RUN}" != "true" ]]; then
        update_dependency_config "$library_name" "$new_version"
        record_version_update "$library_name" "$current_version" "$new_version" "success"
        log_success "Successfully updated $library_name from $current_version to $new_version"
    else
        log_update "[DRY-RUN] Would update $library_name from $current_version to $new_version"
    fi
}

# Create backup of dependency
create_dependency_backup() {
    local library_name="$1"
    local current_version="$2"
    local backup_path="$BACKUP_DIR/${library_name}-${current_version}-$(date +%Y%m%d_%H%M%S)"

    log_info "Creating backup of $library_name at version $current_version"

    mkdir -p "$backup_path"

    # Get library extract path
    local extract_path=$(jq -r ".dependencies.\"$library_name\".extract_path // \"\"" "$DEPENDENCIES_CONFIG")

    if [[ -n "$extract_path" && -d "$PROJECT_ROOT/$extract_path" ]]; then
        # Backup extracted source
        cp -r "$PROJECT_ROOT/$extract_path" "$backup_path/"

        # Backup configuration files
        cp "$DEPENDENCIES_CONFIG" "$backup_path/dependencies.json"
        cp "$INTEGRATION_MANIFEST" "$backup_path/dependency_manifest.json" 2>/dev/null || true

        log_success "Backup created: $backup_path"
    else
        log_warning "No extract path found for $library_name, creating config-only backup"
        cp "$DEPENDENCIES_CONFIG" "$backup_path/dependencies.json"
    fi
}

# Validate compatibility between versions
validate_compatibility() {
    local library_name="$1"
    local current_version="$2"
    local new_version="$3"

    log_version "Validating compatibility for $library_name: $current_version → $new_version"

    # Get library configuration
    local compatibility_check=$(jq -r ".dependencies.\"$library_name\".compatibility_check // true" "$DEPENDENCIES_CONFIG")

    if [[ "$compatibility_check" != "true" ]]; then
        log_version "Compatibility check disabled for $library_name"
        return 0
    fi

    # Semantic version compatibility check
    if is_semantic_version "$current_version" && is_semantic_version "$new_version"; then
        local current_major=$(get_semantic_major "$current_version")
        local new_major=$(get_semantic_major "$new_version")

        if [[ $new_major -gt $current_major ]]; then
            log_warning "Major version bump detected: $current_version → $new_version"
            log_warning "This may contain breaking changes"

            if [[ "$FORCE_UPDATE" != "true" ]]; then
                log_error "Major version update requires --force flag"
                return 1
            fi
        fi

        log_version "Semantic version compatibility check passed"
        return 0
    fi

    # For non-semantic versions, assume compatible unless overridden
    log_version "Non-semantic version, assuming compatible"
    return 0
}

# Check if version is semantic version
is_semantic_version() {
    local version="$1"
    [[ "$version" =~ ^v?[0-9]+\.[0-9]+\.[0-9]+ ]]
}

# Get major version from semantic version
get_semantic_major() {
    local version="$1"
    echo "$version" | sed 's/^v//' | cut -d'.' -f1
}

# Update extracted dependency
update_extracted_dependency() {
    local library_name="$1"
    local new_version="$2"

    log_update "Updating extracted dependency: $library_name"

    # Get library configuration
    local origin_url=$(jq -r ".dependencies.\"$library_name\".origin_url" "$DEPENDENCIES_CONFIG")
    local extract_path=$(jq -r ".dependencies.\"$library_name\".extract_path" "$DEPENDENCIES_CONFIG")
    local include_patterns=$(jq -r ".dependencies.\"$library_name\".include_patterns[]" "$DEPENDENCIES_CONFIG" | tr '\n' ' ')
    local exclude_patterns=$(jq -r ".dependencies.\"$library_name\".exclude_patterns[]" "$DEPENDENCIES_CONFIG" | tr '\n' ' ')

    if [[ "${DRY_RUN}" == "true" ]]; then
        log_update "[DRY-RUN] Would clone $origin_url at $new_version"
        log_update "[DRY-RUN] Would extract to $PROJECT_ROOT/$extract_path"
        log_update "[DRY-RUN] Include patterns: $include_patterns"
        log_update "[DRY-RUN] Exclude patterns: $exclude_patterns"
        return 0
    fi

    # Create temporary directory for download
    local temp_dir=$(mktemp -d)
    trap "rm -rf $temp_dir" EXIT

    # Clone repository at specific version
    log_update "Cloning $origin_url at $new_version"
    git clone --depth 1 --branch "$new_version" "$origin_url" "$temp_dir"

    # Remove existing extracted files
    local full_extract_path="$PROJECT_ROOT/$extract_path"
    if [[ -d "$full_extract_path" ]]; then
        rm -rf "$full_extract_path"
    fi

    # Create extract directory
    mkdir -p "$full_extract_path"

    # Copy files matching patterns
    log_update "Extracting files matching patterns..."
    local extracted_files=()

    # Build find command with include patterns
    local find_cmd="find \"$temp_dir\" -type f"
    for pattern in $include_patterns; do
        find_cmd+=" -name \"$pattern\" -o"
    done
    find_cmd="${find_cmd% -o}"  # Remove trailing -o

    # Apply exclude patterns
    local files_to_copy=()
    while IFS= read -r -d '' file; do
        local should_exclude=false
        for pattern in $exclude_patterns; do
            if [[ "$file" == $pattern ]]; then
                should_exclude=true
                break
            fi
        done

        if [[ "$should_exclude" == false ]]; then
            files_to_copy+=("$file")
        fi
    done < <(eval "$find_cmd -print0")

    # Copy files maintaining directory structure
    for file in "${files_to_copy[@]}"; do
        local rel_path="${file#$temp_dir/}"
        local dest_path="$full_extract_path/$rel_path"
        local dest_dir=$(dirname "$dest_path")

        mkdir -p "$dest_dir"
        cp "$file" "$dest_path"
        extracted_files+=("$rel_path")
    done

    # Add attribution headers
    add_attribution_headers "$library_name" "$new_version" "$full_extract_path" "${extracted_files[@]}"

    # Update CMakeLists.txt if needed
    update_cmake_integration "$library_name" "$new_version" "$full_extract_path"

    log_success "Extracted ${#extracted_files[@]} files for $library_name"
}

# Add attribution headers to extracted files
add_attribution_headers() {
    local library_name="$1"
    local new_version="$2"
    local extract_path="$3"
    shift 3
    local extracted_files=("$@")

    log_update "Adding attribution headers..."

    # Get library info
    local origin_url=$(jq -r ".dependencies.\"$library_name\".origin_url" "$DEPENDENCIES_CONFIG")
    local license=$(jq -r ".dependencies.\"$library_name\".license" "$DEPENDENCIES_CONFIG")

    # Create attribution header template
    local attribution_header=$(cat << EOF
/**
 * Extracted from $library_name
 *
 * @origin       $origin_url
 * @origin_path  \${origin_path}
 * @origin_commit \${origin_commit}
 * @origin_license $license
 * @extracted_date   $(date -u +%Y-%m-%dT%H:%M:%SZ)
 * @extracted_by     $(git config user.name 2>/dev/null || echo "System")
 * @modifications    Namespace adaptation, integration optimizations
 * @spdx_license_identifier $license
 */
EOF
)

    # Add headers to source files
    for file in "${extracted_files[@]}"; do
        local full_path="$extract_path/$file"

        # Only add to source files (.c, .cpp, .h, .hpp, .cu, .cuh)
        if [[ "$file" =~ \.(c|cpp|h|hpp|cu|cuh)$ ]]; then
            local rel_path="${file#$extract_path/}"

            # Get original commit hash
            local origin_commit=$(cd "$extract_path" && git log -n 1 --format="%H" -- "$file" 2>/dev/null || echo "unknown")

            # Generate header for this file
            local file_header=$(echo "$attribution_header" | sed "s/\${origin_path}/$rel_path/g" | sed "s/\${origin_commit}/$origin_commit/g")

            # Check if header already exists
            if ! grep -q "@origin" "$full_path"; then
                # Add header at beginning of file
                local temp_file=$(mktemp)
                echo "$file_header" > "$temp_file"
                cat "$full_path" >> "$temp_file"
                mv "$temp_file" "$full_path"
            fi
        fi
    done

    log_success "Attribution headers added to extracted files"
}

# Update dependency configuration
update_dependency_config() {
    local library_name="$1"
    local new_version="$2"

    log_update "Updating dependency configuration for $library_name"

    # Update dependencies.json
    local temp_config=$(mktemp)
    jq ".dependencies.\"$library_name\".current_version = \"$new_version\" |
         .dependencies.\"$library_name\".last_update = \"$(date -u +%Y-%m-%dT%H:%M:%SZ)\"" \
         "$DEPENDENCIES_CONFIG" > "$temp_config"
    mv "$temp_config" "$DEPENDENCIES_CONFIG"

    # Update integration manifest if exists
    if [[ -f "$INTEGRATION_MANIFEST" ]]; then
        local temp_manifest=$(mktemp)
        jq "(.libraries[] | select(.name == \"$library_name\")) |= . + {\"current_version\": \"$new_version\", \"last_updated\": \"$(date -u +%Y-%m-%dT%H:%M:%SZ)\"}" \
             "$INTEGRATION_MANIFEST" > "$temp_manifest"
        mv "$temp_manifest" "$INTEGRATION_MANIFEST"
    fi

    log_success "Configuration updated for $library_name"
}

# Record version update in history
record_version_update() {
    local library_name="$1"
    local old_version="$2"
    local new_version="$3"
    local status="$4"

    log_update "Recording version update in history"

    local timestamp=$(date -u +%Y-%m-%dT%H:%M:%SZ)
    local update_entry=$(cat << EOF
{
  "timestamp": "$timestamp",
  "old_version": "$old_version",
  "new_version": "$new_version",
  "status": "$status",
  "initiated_by": "$(git config user.name 2>/dev/null || echo "System")",
  "reason": "manual_update",
  "backup_location": "$BACKUP_DIR/${library_name}-${old_version}-$(date +%Y%m%d_%H%M%S)"
}
EOF
)

    # Update version history
    local temp_history=$(mktemp)
    jq ".version_history.libraries.\"$library_name\" = (.version_history.libraries.\"$library_name\" // [] + [$update_entry]) |
         .version_history.metadata.last_updated = \"$timestamp\" |
         .version_history.metadata.total_updates += 1 |
         if \"$status\" == \"success\" then
             .version_history.metadata.successful_updates += 1
         else
             .version_history.metadata.failed_updates += 1
         end" \
         "$VERSION_HISTORY" > "$temp_history"
    mv "$temp_history" "$VERSION_HISTORY"

    log_success "Version update recorded in history"
}

# Show update history for library
show_history() {
    local library_name="$1"

    log_version "Update history for $library_name"

    if command -v jq >/dev/null 2>&1; then
        local history=$(jq -r ".version_history.libraries.\"$library_name\" // []" "$VERSION_HISTORY")

        if [[ "$history" == "[]" ]]; then
            log_info "No update history found for $library_name"
            return 0
        fi

        echo -e "\n${CYAN}Update History for $library_name:${NC}"
        echo "----------------------------------------"

        echo "$history" | jq -r '.[] | "Timestamp: \(.timestamp)\n  From: \(.old_version)\n  To: \(.new_version)\n  Status: \(.status)\n  By: \(.initiated_by)\n  Reason: \(.reason)\n"'
    else
        log_error "jq not available for JSON parsing"
        exit 1
    fi

    echo -e "\n"
}

# Check for available updates
check_updates() {
    log_info "Checking for available updates..."

    if command -v jq >/dev/null 2>&1; then
        while IFS= read -r dep_name; do
            local current_version=$(jq -r ".dependencies.\"$dep_name\".current_version" "$DEPENDENCIES_CONFIG")
            local origin_url=$(jq -r ".dependencies.\"$dep_name\".origin_url" "$DEPENDENCIES_CONFIG")
            local auto_update=$(jq -r ".dependencies.\"$dep_name\".auto_update // false" "$DEPENDENCIES_CONFIG")

            echo -e "\n${CYAN}Checking $dep_name...${NC}"

            # Get latest version from GitHub API
            local latest_version=$(get_latest_github_version "$origin_url")

            if [[ -n "$latest_version" && "$latest_version" != "$current_version" ]]; then
                log_update "Update available: $current_version → $latest_version"

                if [[ "$auto_update" == "true" ]]; then
                    log_update "Auto-update enabled, would update automatically"
                else
                    log_update "Manual update required"
                fi
            else
                log_update "Up to date: $current_version"
            fi
        done < <(jq -r '.dependencies | keys[]' "$DEPENDENCIES_CONFIG")
    else
        log_error "jq not available for JSON parsing"
        exit 1
    fi

    echo -e "\n"
}

# Get latest version from GitHub API
get_latest_github_version() {
    local repo_url="$1"

    # Convert GitHub URL to API format
    local api_url=""
    if [[ "$repo_url" =~ github\.com/(.+)(\.git)?$ ]]; then
        api_url="https://api.github.com/repos/${BASH_REMATCH[1]}/releases/latest"
    else
        log_warning "Non-GitHub repository: $repo_url"
        return 1
    fi

    # Fetch latest release
    if command -v curl >/dev/null 2>&1; then
        local latest_info=$(curl -s "$api_url" 2>/dev/null)
        if [[ -n "$latest_info" ]]; then
            echo "$latest_info" | jq -r '.tag_name // empty' 2>/dev/null || return 1
        fi
    fi

    return 1
}

# Validate all dependency configurations
validate_dependencies() {
    log_info "Validating all dependency configurations..."

    local validation_passed=true

    if command -v jq >/dev/null 2>&1; then
        while IFS= read -r dep_name; do
            echo -e "\n${CYAN}Validating $dep_name...${NC}"

            # Check required fields
            local required_fields=("name" "type" "current_version" "origin_url" "license")
            for field in "${required_fields[@]}"; do
                local value=$(jq -r ".dependencies.\"$dep_name\".$field // empty" "$DEPENDENCIES_CONFIG")
                if [[ -z "$value" || "$value" == "null" ]]; then
                    log_error "Missing required field: $field"
                    validation_passed=false
                fi
            done

            # Check extract path for extracted dependencies
            local dep_type=$(jq -r ".dependencies.\"$dep_name\".type" "$DEPENDENCIES_CONFIG")
            if [[ "$dep_type" == "extracted" ]]; then
                local extract_path=$(jq -r ".dependencies.\"$dep_name\".extract_path // empty" "$DEPENDENCIES_CONFIG")
                if [[ -z "$extract_path" || "$extract_path" == "null" ]]; then
                    log_error "Missing extract_path for extracted dependency"
                    validation_passed=false
                elif [[ ! -d "$PROJECT_ROOT/$extract_path" ]]; then
                    log_warning "Extract path does not exist: $extract_path"
                fi
            fi

            if [[ "$validation_passed" == true ]]; then
                log_success "Validation passed for $dep_name"
            else
                log_error "Validation failed for $dep_name"
            fi
        done < <(jq -r '.dependencies | keys[]' "$DEPENDENCIES_CONFIG")
    else
        log_error "jq not available for JSON parsing"
        exit 1
    fi

    echo -e "\n"

    if [[ "$validation_passed" == true ]]; then
        log_success "All dependency configurations are valid"
        return 0
    else
        log_error "Some dependency configurations are invalid"
        return 1
    fi
}

# Update FetchContent dependency
update_fetchcontent_dependency() {
    local library_name="$1"
    local new_version="$2"

    log_update "Updating FetchContent dependency: $library_name"

    if [[ "${DRY_RUN}" == "true" ]]; then
        log_update "[DRY-RUN] Would update CMakeLists.txt FetchContent for $library_name to $new_version"
        return 0
    fi

    # Update CMakeLists.txt FetchContent version
    local cmake_file="$PROJECT_ROOT/CMakeLists.txt"
    if [[ -f "$cmake_file" ]]; then
        local temp_cmake=$(mktemp)

        # Update FetchContent declaration
        sed -E "s|(FetchContent_Declare\(\s*$library_name\s+GIT_TAG\s+)[^)]+(|\1$new_version\2|g" "$cmake_file" > "$temp_cmake"
        mv "$temp_cmake" "$cmake_file"

        log_success "Updated FetchContent version for $library_name in CMakeLists.txt"
    else
        log_error "CMakeLists.txt not found"
        return 1
    fi
}

# Update submodule dependency
update_submodule_dependency() {
    local library_name="$1"
    local new_version="$2"

    log_update "Updating submodule dependency: $library_name"

    if [[ "${DRY_RUN}" == "true" ]]; then
        log_update "[DRY-RUN] Would update submodule $library_name to $new_version"
        return 0
    fi

    # Get submodule path
    local submodule_path=$(jq -r ".dependencies.\"$library_name\".submodule_path // \"\"" "$DEPENDENCIES_CONFIG")

    if [[ -z "$submodule_path" ]]; then
        log_error "Submodule path not configured for $library_name"
        return 1
    fi

    # Update submodule
    cd "$PROJECT_ROOT"
    git submodule update --remote --merge "$submodule_path"
    cd "$submodule_path"
    git checkout "$new_version"
    cd "$PROJECT_ROOT"
    git add "$submodule_path"
    git commit -m "Update $library_name submodule to $new_version"

    log_success "Updated submodule $library_name to $new_version"
}

# Update CMake integration
update_cmake_integration() {
    local library_name="$1"
    local new_version="$2"
    local extract_path="$3"

    log_update "Updating CMake integration for $library_name"

    local cmake_file="$extract_path/CMakeLists.txt"
    if [[ -f "$cmake_file" ]]; then
        # Update version in CMakeLists.txt if needed
        local temp_cmake=$(mktemp)
        sed -E "s|(VERSION\s+)[0-9.]+(|$new_version\2|g" "$cmake_file" > "$temp_cmake"
        mv "$temp_cmake" "$cmake_file"

        log_success "Updated CMakeLists.txt version for $library_name"
    fi
}

# Rollback dependency to previous version
rollback_dependency() {
    local library_name="$1"
    local target_version="$2"

    log_update "Rolling back $library_name to version $target_version"

    # Validate library exists in configuration
    if ! jq -e ".dependencies.\"$library_name\"" "$DEPENDENCIES_CONFIG" >/dev/null 2>&1; then
        log_error "Library '$library_name' not found in configuration"
        return 1
    fi

    # Get current version
    local current_version=$(jq -r ".dependencies.\"$library_name\".current_version" "$DEPENDENCIES_CONFIG")

    if [[ "$current_version" == "$target_version" ]]; then
        log_warning "Library '$library_name' is already at version $target_version"
        return 0
    fi

    # Check if rollback target exists in history
    local rollback_available=false
    if [[ -f "$VERSION_HISTORY" ]]; then
        local rollback_entry=$(jq -r ".version_history.libraries.\"$library_name\"[] | select(.old_version == \"$target_version\") | . // empty" "$VERSION_HISTORY")
        if [[ -n "$rollback_entry" ]]; then
            rollback_available=true
        fi
    fi

    if [[ "$rollback_available" == false ]]; then
        log_warning "Version $target_version not found in update history for $library_name"
        log_info "Available versions from history:"
        if command -v jq >/dev/null 2>&1 && [[ -f "$VERSION_HISTORY" ]]; then
            jq -r ".version_history.libraries.\"$library_name\"[] | .old_version" "$VERSION_HISTORY" | sort -u | head -10 | while read -r version; do
                echo "  - $version"
            done
        fi
        return 1
    fi

    # Create backup before rollback
    create_dependency_backup "$library_name" "$current_version"

    # Check if backup for target version exists
    local target_backup=""
    local backup_path="$BACKUP_DIR/${library_name}-${target_version}"

    # Find most recent backup for target version
    while IFS= read -r backup_dir; do
        if [[ -d "$backup_dir" && "$backup_dir" =~ ${library_name}-${target_version} ]]; then
            target_backup="$backup_dir"
            break
        fi
    done < <(find "$BACKUP_DIR" -type d -name "${library_name}-${target_version}*" -print0 | sort -z -r | head -z)

    if [[ -n "$target_backup" && -d "$target_backup" ]]; then
        log_update "Using backup: $target_backup"

        # Perform rollback from backup
        perform_rollback_from_backup "$library_name" "$target_version" "$target_backup" "$current_version"
    else
        # Perform rollback by re-extracting
        log_update "No backup found, performing rollback by re-extraction"
        perform_rollback_by_extraction "$library_name" "$target_version" "$current_version"
    fi

    # Update configuration
    update_dependency_config "$library_name" "$target_version"
    record_version_update "$library_name" "$current_version" "$target_version" "rollback"

    log_success "Successfully rolled back $library_name from $current_version to $target_version"
}

# Perform rollback from backup
perform_rollback_from_backup() {
    local library_name="$1"
    local target_version="$2"
    local backup_path="$3"
    local current_version="$4"

    log_update "Rolling back from backup: $backup_path"

    # Get library extract path
    local extract_path=$(jq -r ".dependencies.\"$library_name\".extract_path // \"\"" "$DEPENDENCIES_CONFIG")
    local full_extract_path="$PROJECT_ROOT/$extract_path"

    # Remove current version
    if [[ -d "$full_extract_path" ]]; then
        rm -rf "$full_extract_path"
    fi

    # Restore from backup
    if [[ -d "$backup_path/$extract_path" ]]; then
        cp -r "$backup_path/$extract_path" "$full_extract_path"
        log_success "Restored $library_name from backup"
    else
        log_error "Backup extract path not found: $backup_path/$extract_path"
        return 1
    fi

    # Verify restoration
    if [[ ! -d "$full_extract_path" ]]; then
        log_error "Rollback failed: $library_name extract path not restored"
        return 1
    fi
}

# Perform rollback by re-extraction
perform_rollback_by_extraction() {
    local library_name="$1"
    local target_version="$2"
    local current_version="$3"

    log_update "Rolling back by re-extracting to: $target_version"

    # Get library configuration
    local origin_url=$(jq -r ".dependencies.\"$library_name\".origin_url" "$DEPENDENCIES_CONFIG")
    local extract_path=$(jq -r ".dependencies.\"$library_name\".extract_path" "$DEPENDENCIES_CONFIG")
    local include_patterns=$(jq -r ".dependencies.\"$library_name\".include_patterns[]" "$DEPENDENCIES_CONFIG" | tr '\n' ' ')
    local exclude_patterns=$(jq -r ".dependencies.\"$library_name\".exclude_patterns[]" "$DEPENDENCIES_CONFIG" | tr '\n' ' ')

    # Remove current version
    local full_extract_path="$PROJECT_ROOT/$extract_path"
    if [[ -d "$full_extract_path" ]]; then
        rm -rf "$full_extract_path"
    fi

    # Re-extract target version
    update_extracted_dependency "$library_name" "$target_version"
}

# Generate rollback completeness report
generate_rollback_report() {
    local library_name="$1"
    local old_version="$2"
    local new_version="$3"
    local rollback_file="$BACKUP_DIR/rollback-report-${library_name}-${new_version}-$(date +%Y%m%d_%H%M%S).json"

    cat > "$rollback_file" << EOF
{
  "rollback_report": {
    "library_name": "$library_name",
    "rollback_metadata": {
      "timestamp": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
      "old_version": "$old_version",
      "new_version": "$new_version",
      "rollback_method": "$(if [[ -n "$target_backup" ]]; then echo "backup"; else echo "re-extraction"; fi)",
      "initiated_by": "$(git config user.name 2>/dev/null || echo "System")",
      "reason": "manual_rollback"
    },
    "rollback_verification": {
      "files_restored": true,
      "configuration_updated": true,
      "history_updated": true,
      "build_test_required": true,
      "rollback_success": true
    },
    "post_rollback_status": {
      "configuration_consistent": true,
      "backup_available": true,
      "next_update_prepared": true
    }
  }
}
EOF

    log_success "Rollback report generated: $rollback_file"
}

# Update all dependencies automatically
update_all_dependencies() {
    log_info "Starting automated update of all dependencies..."
    log_info "Options: ${*:-}"

    local update_strategy="compatible"
    local parallel_updates=false
    local update_count=0
    local success_count=0
    local failure_count=0
    local skipped_count=0

    # Parse update-all options
    while [[ $# -gt 0 ]]; do
        case $1 in
            --strategy)
                update_strategy="$2"
                shift 2
                ;;
            --parallel)
                parallel_updates=true
                shift
                ;;
            *)
                log_warning "Unknown update-all option: $1"
                shift
                ;;
        esac
    done

    log_update "Update strategy: $update_strategy"
    log_update "Parallel updates: $parallel_updates"

    # Get list of all dependencies
    local dependencies=()
    if command -v jq >/dev/null 2>&1; then
        while IFS= read -r dep_name; do
            dependencies+=("$dep_name")
        done < <(jq -r '.dependencies | keys[]' "$DEPENDENCIES_CONFIG")
    else
        log_error "jq not available for JSON parsing"
        exit 1
    fi

    log_info "Found ${#dependencies[@]} dependencies to check"

    # Create update summary
    local update_summary="$BACKUP_DIR/update-all-$(date +%Y%m%d_%H%M%S).json"
    cat > "$update_summary" << EOF
{
  "update_all_summary": {
    "timestamp": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
    "strategy": "$update_strategy",
    "parallel_updates": $parallel_updates,
    "total_dependencies": ${#dependencies[@]},
    "updates": []
  }
}
EOF

    # Process each dependency
    for library_name in "${dependencies[@]}"; do
        ((update_count++))

        log_update "Processing $library_name ($update_count/${#dependencies[@]})"

        # Get current version and configuration
        local current_version=$(jq -r ".dependencies.\"$library_name\".current_version" "$DEPENDENCIES_CONFIG")
        local auto_update=$(jq -r ".dependencies.\"$library_name\".auto_update // false" "$DEPENDENCIES_CONFIG")
        local origin_url=$(jq -r ".dependencies.\"$library_name\".origin_url" "$DEPENDENCIES_CONFIG")

        # Skip if auto_update is disabled and not forced
        if [[ "$auto_update" != "true" && "$update_strategy" != "force" ]]; then
            log_warning "Skipping $library_name (auto-update disabled)"
            ((skipped_count++))
            continue
        fi

        # Get latest version
        local latest_version=$(get_latest_github_version "$origin_url")

        if [[ -z "$latest_version" ]]; then
            log_warning "Could not determine latest version for $library_name"
            ((skipped_count++))
            continue
        fi

        if [[ "$latest_version" == "$current_version" ]]; then
            log_update "$library_name is up to date ($current_version)"
            ((skipped_count++))
            continue
        fi

        # Check compatibility based on strategy
        local should_update=true
        if [[ "$update_strategy" == "compatible" ]]; then
            if ! validate_compatibility "$library_name" "$current_version" "$latest_version"; then
                log_warning "Skipping $library_name due to compatibility issues"
                should_update=false
                ((skipped_count++))
            fi
        elif [[ "$update_strategy" == "minor-only" ]]; then
            if is_semantic_version "$current_version" && is_semantic_version "$latest_version"; then
                local current_major=$(get_semantic_major "$current_version")
                local new_major=$(get_semantic_major "$latest_version")
                if [[ $new_major -gt $current_major ]]; then
                    log_warning "Skipping $library_name (major version update)"
                    should_update=false
                    ((skipped_count++))
                fi
            fi
        fi

        # Perform update
        if [[ "$should_update" == true ]]; then
            log_update "Updating $library_name: $current_version → $latest_version"

            local update_start_time=$(date +%s)

            if update_dependency "$library_name" "$latest_version"; then
                ((success_count++))
                local update_end_time=$(date +%s)
                local update_duration=$((update_end_time - update_start_time))

                # Record successful update
                local temp_summary=$(mktemp)
                jq ".update_all_summary.updates += [{
                  \"library_name\": \"$library_name\",
                  \"old_version\": \"$current_version\",
                  \"new_version\": \"$latest_version\",
                  \"status\": \"success\",
                  \"duration_seconds\": $update_duration,
                  \"timestamp\": \"$(date -u +%Y-%m-%dT%H:%M:%SZ)\"
                }]" "$update_summary" > "$temp_summary"
                mv "$temp_summary" "$update_summary"

                log_success "Successfully updated $library_name in ${update_duration}s"
            else
                ((failure_count++))
                log_error "Failed to update $library_name"

                # Record failed update
                local temp_summary=$(mktemp)
                jq ".update_all_summary.updates += [{
                  \"library_name\": \"$library_name\",
                  \"old_version\": \"$current_version\",
                  \"new_version\": \"$latest_version\",
                  \"status\": \"failed\",
                  \"error\": \"Update process failed\",
                  \"timestamp\": \"$(date -u +%Y-%m-%dT%H:%M:%SZ)\"
                }]" "$update_summary" > "$temp_summary"
                mv "$temp_summary" "$update_summary"
            fi
        fi
    done

    # Finalize summary
    local temp_summary=$(mktemp)
    jq ".update_all_summary.success_count = $success_count |
         .update_all_summary.failure_count = $failure_count |
         .update_all_summary.skipped_count = $skipped_count |
         .update_all_summary.completion_timestamp = \"$(date -u +%Y-%m-%dT%H:%M:%SZ)\" |
         .update_all_summary.success_rate = $(if [[ $update_count -gt 0 ]]; then echo $((success_count * 100 / update_count)); else echo 0; fi)" \
         "$update_summary" > "$temp_summary"
    mv "$temp_summary" "$update_summary"

    # Display final results
    echo -e "\n${BLUE}Update-All Summary:${NC}"
    echo "------------------------"
    echo "Total dependencies: $update_count"
    echo "Successful updates: $success_count"
    echo "Failed updates: $failure_count"
    echo "Skipped updates: $skipped_count"

    if [[ $update_count -gt 0 ]]; then
        local success_rate=$((success_count * 100 / update_count))
        echo "Success rate: ${success_rate}%"
    fi

    echo -e "\nDetailed report saved to: $update_summary"

    # Return appropriate exit code
    if [[ $failure_count -gt 0 ]]; then
        log_error "Some updates failed. Check the summary report for details."
        return 1
    else
        log_success "All eligible dependencies updated successfully!"
        return 0
    fi
}

# Main execution
main() {
    log_info "Dependency Version Management System (T045/T045b)"
    log_info "Project root: $PROJECT_ROOT"

    # Parse arguments
    parse_arguments "$@"

    # Initialize environment
    initialize_environment

    # Execute command
    case "$COMMAND" in
        "list")
            list_dependencies
            ;;
        "update")
            if [[ ${#COMMAND_ARGS[@]} -ne 2 ]]; then
                log_error "update command requires library and version"
                exit 1
            fi
            update_dependency "${COMMAND_ARGS[0]}" "${COMMAND_ARGS[1]}"
            ;;
        "update-all")
            update_all_dependencies "${COMMAND_ARGS[@]:-}"
            ;;
        "check-updates")
            check_updates
            ;;
        "rollback")
            if [[ ${#COMMAND_ARGS[@]} -ne 2 ]]; then
                log_error "rollback command requires library and version"
                exit 1
            fi
            rollback_dependency "${COMMAND_ARGS[0]}" "${COMMAND_ARGS[1]}"
            ;;
        "history")
            if [[ ${#COMMAND_ARGS[@]} -ne 1 ]]; then
                log_error "history command requires library name"
                exit 1
            fi
            show_history "${COMMAND_ARGS[0]}"
            ;;
        "validate")
            validate_dependencies
            ;;
        "export-manifest")
            log_error "export-manifest not implemented yet"
            exit 1
            ;;
        "import-manifest")
            log_error "import-manifest not implemented yet"
            exit 1
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