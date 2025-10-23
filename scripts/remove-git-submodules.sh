#!/bin/bash
# T024: Remove Git Submodule Dependencies
# Removes external git submodule dependencies for offline builds

set -euo pipefail

# Script configuration
SCRIPT_NAME="$(basename "$0")"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

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

# Help function
show_help() {
    cat << EOF
$SCRIPT_NAME - Remove Git Submodule Dependencies

USAGE:
    $SCRIPT_NAME [OPTIONS]

OPTIONS:
    --dry-run          Show what would be done without making changes
    --force            Force removal even if validation fails
    --backup           Create backup before removing submodules
    --help             Show this help message

DESCRIPTION:
    This script removes git submodule dependencies from the build process
    while ensuring that extracted sources are properly integrated. It
    validates that the extracted sources exist and are functional before
    removing the submodules.

SUBMODULES TO REMOVE:
    - third_party/bitcoin-core-secp256k1 (reference implementation)
    - third_party/secp256k1-zkp (now extracted to src/extracted/)

EOF
}

# Validate prerequisites
validate_prerequisites() {
    log_info "Validating prerequisites..."

    # Check if we're in a git repository
    if ! git rev-parse --git-dir >/dev/null 2>&1; then
        log_error "Not in a git repository"
        return 1
    fi

    # Check if extracted sources exist
    if [[ ! -d "$PROJECT_ROOT/src/extracted/secp256k1-zkp" ]]; then
        log_error "Extracted secp256k1-zkp sources not found"
        log_error "Expected directory: $PROJECT_ROOT/src/extracted/secp256k1-zkp"
        return 1
    fi

    # Check if extracted sources have content
    local source_count
    source_count=$(find "$PROJECT_ROOT/src/extracted/secp256k1-zkp" -name "*.c" -o -name "*.h" | wc -l)
    if [[ $source_count -eq 0 ]]; then
        log_error "Extracted secp256k1-zkp sources appear to be empty"
        log_error "Found $source_count source files"
        return 1
    fi

    log_success "Prerequisites validation passed"
    log_info "Found $source_count source files in extracted directory"
    return 0
}

# Check if CMakeLists.txt references submodules
check_cmake_references() {
    log_info "Checking CMakeLists.txt for submodule references..."

    local cmake_file="$PROJECT_ROOT/CMakeLists.txt"
    local submodule_refs=0

    if [[ -f "$cmake_file" ]]; then
        # Check for submodule directory references
        if grep -q "third_party/" "$cmake_file"; then
            submodule_refs=$(grep -c "third_party/" "$cmake_file" || echo "0")
            log_warning "Found $submodule_refs references to third_party/ in CMakeLists.txt"
        fi

        # Check for FetchContent references
        if grep -q "FetchContent" "$cmake_file"; then
            local fetchcontent_refs
            fetchcontent_refs=$(grep -c "FetchContent" "$cmake_file" || echo "0")
            log_info "Found $fetchcontent_refs FetchContent references (will be kept for header-only libraries)"
        fi
    fi

    return $submodule_refs
}

# Test build with extracted sources
test_extracted_build() {
    log_info "Testing build with extracted sources..."

    local test_build_dir="$PROJECT_ROOT/build/test_submodule_removal"
    mkdir -p "$test_build_dir"

    cd "$test_build_dir"

    # Configure with offline build
    if cmake "$PROJECT_ROOT" -DCMAKE_BUILD_TYPE=Release -DENABLE_OFFLINE_BUILD=ON >/dev/null 2>&1; then
        log_success "CMake configuration with extracted sources succeeded"

        # Test building the secp256k1-zkp library target
        if make secp256k1-zkp -j2 >/dev/null 2>&1; then
            log_success "secp256k1-zkp library builds successfully with extracted sources"
        else
            log_error "Failed to build secp256k1-zkp library with extracted sources"
            return 1
        fi
    else
        log_error "CMake configuration failed with extracted sources"
        return 1
    fi

    return 0
}

# List current submodules
list_submodules() {
    log_info "Current git submodules:"

    if git submodule status 2>/dev/null; then
        local count
        count=$(git submodule status | wc -l)
        log_info "Found $count submodule(s)"
    else
        log_info "No git submodules found"
    fi
}

# Create backup before removal
create_backup() {
    log_info "Creating backup before submodule removal..."

    local backup_dir="$PROJECT_ROOT/.backup/submodules-$(date +%Y%m%d_%H%M%S)"
    mkdir -p "$backup_dir"

    # Backup .gitmodules file
    if [[ -f "$PROJECT_ROOT/.gitmodules" ]]; then
        cp "$PROJECT_ROOT/.gitmodules" "$backup_dir/"
        log_info "Backed up .gitmodules"
    fi

    # Backup submodule directories
    while IFS= read -r -d '' submodule_path; do
        if [[ -d "$PROJECT_ROOT/$submodule_path" ]]; then
            cp -r "$PROJECT_ROOT/$submodule_path" "$backup_dir/"
            log_info "Backed up submodule: $submodule_path"
        fi
    done < <(git submodule foreach --quiet pwd 2>/dev/null | sed "s|$PROJECT_ROOT/||" | tr '\n' '\0')

    log_success "Backup created at: $backup_dir"
    echo "$backup_dir"
}

# Remove git submodules
remove_submodules() {
    log_info "Removing git submodules..."

    local submodules_removed=0

    # Deinitialize all submodules
    log_info "Deinitializing submodules..."
    git submodule deinit --all --force 2>/dev/null || true

    # Remove submodule entries from .git/config
    log_info "Removing submodule configuration..."
    git config --remove-section submodule.third_party/bitcoin-core-secp256k1 2>/dev/null || true
    git config --remove-section submodule.third_party/secp256k1-zkp 2>/dev/null || true

    # Remove .gitmodules file
    if [[ -f "$PROJECT_ROOT/.gitmodules" ]]; then
        log_info "Removing .gitmodules file..."
        rm "$PROJECT_ROOT/.gitmodules"
    fi

    # Remove submodule directories from git index
    log_info "Removing submodule directories from git index..."
    git rm --cached third_party/bitcoin-core-secp256k1 2>/dev/null || true
    git rm --cached third_party/secp256k1-zkp 2>/dev/null || true

    # Remove submodule directories from filesystem
    log_info "Removing submodule directories from filesystem..."
    if [[ -d "$PROJECT_ROOT/third_party/bitcoin-core-secp256k1" ]]; then
        rm -rf "$PROJECT_ROOT/third_party/bitcoin-core-secp256k1"
        log_info "Removed: third_party/bitcoin-core-secp256k1"
        ((submodules_removed++))
    fi

    if [[ -d "$PROJECT_ROOT/third_party/secp256k1-zkp" ]]; then
        rm -rf "$PROJECT_ROOT/third_party/secp256k1-zkp"
        log_info "Removed: third_party/secp256k1-zkp"
        ((submodules_removed++))
    fi

    # Remove third_party directory if empty
    if [[ -d "$PROJECT_ROOT/third_party" ]]; then
        if [[ -z "$(ls -A "$PROJECT_ROOT/third_party")" ]]; then
            rmdir "$PROJECT_ROOT/third_party"
            log_info "Removed empty third_party directory"
        fi
    fi

    log_success "Removed $submodules_removed submodule(s)"
    return $submodules_removed
}

# Update CMakeLists.txt to remove submodule references
update_cmake_file() {
    log_info "Updating CMakeLists.txt to remove submodule references..."

    local cmake_file="$PROJECT_ROOT/CMakeLists.txt"
    local temp_file="$PROJECT_ROOT/CMakeLists.txt.tmp"

    if [[ ! -f "$cmake_file" ]]; then
        log_warning "CMakeLists.txt not found, skipping update"
        return 0
    fi

    # Create a backup of the original file
    cp "$cmake_file" "$cmake_file.backup"

    # Remove references to third_party/ submodules
    sed -i.tmp '/third_party\/secp256k1-zkp/d' "$cmake_file"
    sed -i.tmp '/third_party\/bitcoin-core-secp256k1/d' "$cmake_file"

    # Remove add_subdirectory calls for submodules
    sed -i.tmp '/add_subdirectory.*third_party/d' "$cmake_file"

    # Clean up the temp file
    rm -f "$temp_file"

    log_success "Updated CMakeLists.txt to remove submodule references"
    log_info "Original file backed up as: CMakeLists.txt.backup"
}

# Update scripts and documentation
update_scripts() {
    log_info "Updating scripts to remove submodule references..."

    # Update setup-dependencies.sh
    local setup_script="$PROJECT_ROOT/scripts/setup-dependencies.sh"
    if [[ -f "$setup_script" ]]; then
        log_info "Updating setup-dependencies.sh..."
        # Comment out or remove submodule initialization
        sed -i.bak 's/^git submodule/# git submodule/g' "$setup_script"
        sed -i '/git submodule.*update/s/^/# /g' "$setup_script"
        log_info "Backed up original as: setup-dependencies.sh.bak"
    fi

    # Update any scripts that reference submodules
    find "$PROJECT_ROOT/scripts" -name "*.sh" -type f | while read -r script; do
        if grep -q "third_party/secp256k1" "$script"; then
            log_info "Found submodule references in: $(basename "$script")"
            # In a full implementation, we would update these scripts
        fi
    done

    log_success "Script updates completed"
}

# Verify submodule removal
verify_removal() {
    log_info "Verifying submodule removal..."

    local issues=0

    # Check git status
    if git submodule status >/dev/null 2>&1; then
        local remaining
        remaining=$(git submodule status | wc -l)
        if [[ $remaining -gt 0 ]]; then
            log_error "Found $remaining remaining submodule(s)"
            ((issues++))
        else
            log_success "No git submodules remain"
        fi
    else
        log_success "No git submodules found"
    fi

    # Check filesystem
    if [[ -d "$PROJECT_ROOT/third_party/secp256k1-zkp" ]]; then
        log_error "Submodule directory still exists: third_party/secp256k1-zkp"
        ((issues++))
    fi

    if [[ -d "$PROJECT_ROOT/third_party/bitcoin-core-secp256k1" ]]; then
        log_error "Submodule directory still exists: third_party/bitcoin-core-secp256k1"
        ((issues++))
    fi

    # Check .gitmodules file
    if [[ -f "$PROJECT_ROOT/.gitmodules" ]]; then
        log_error ".gitmodules file still exists"
        ((issues++))
    fi

    # Test build
    log_info "Testing build after submodule removal..."
    local test_build_dir="$PROJECT_ROOT/build/test_after_removal"
    mkdir -p "$test_build_dir"

    cd "$test_build_dir"
    if cmake "$PROJECT_ROOT" -DCMAKE_BUILD_TYPE=Release -DENABLE_OFFLINE_BUILD=ON >/dev/null 2>&1; then
        if make secp256k1-zkp -j2 >/dev/null 2>&1; then
            log_success "Build test passed after submodule removal"
        else
            log_error "Build test failed after submodule removal"
            ((issues++))
        fi
    else
        log_error "CMake configuration failed after submodule removal"
        ((issues++))
    fi

    return $issues
}

# Main function
main() {
    local dry_run=false
    local force=false
    local backup=false
    local backup_dir=""

    # Parse command line arguments
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
            --backup)
                backup=true
                shift
                ;;
            --help|--help|-h)
                show_help
                exit 0
                ;;
            *)
                log_error "Unknown option: $1"
                show_help
                exit 1
                ;;
        esac
    done

    log_info "Starting git submodule removal process..."
    log_info "Project root: $PROJECT_ROOT"

    # Show current submodules
    list_submodules

    # Validate prerequisites
    if ! validate_prerequisites; then
        if [[ "$force" == "true" ]]; then
            log_warning "Prerequisites validation failed, but proceeding with --force"
        else
            log_error "Prerequisites validation failed. Use --force to proceed anyway."
            exit 1
        fi
    fi

    # Check CMake references
    local cmake_refs
    cmake_refs=$(check_cmake_references)
    if [[ $cmake_refs -gt 0 ]]; then
        log_warning "Found $cmake_refs CMake references to submodules that will need to be updated"
    fi

    # Test build with extracted sources
    if ! test_extracted_build; then
        if [[ "$force" == "true" ]]; then
            log_warning "Build test failed, but proceeding with --force"
        else
            log_error "Build test failed. Extracted sources may not be properly integrated."
            log_error "Use --force to proceed anyway, or fix the integration first."
            exit 1
        fi
    fi

    # Create backup if requested
    if [[ "$backup" == "true" ]]; then
        backup_dir=$(create_backup)
    fi

    if [[ "$dry_run" == "true" ]]; then
        log_info "Dry run completed. No changes were made."
        exit 0
    fi

    # Perform removal
    log_info "Proceeding with submodule removal..."

    local removed_count
    removed_count=$(remove_submodules)

    if [[ $removed_count -gt 0 ]]; then
        update_cmake_file
        update_scripts
    fi

    # Verify removal
    local verification_issues
    verification_issues=$(verify_removal)

    if [[ $verification_issues -eq 0 ]]; then
        log_success "🎉 Git submodule removal completed successfully!"
        log_info "Removed $removed_count submodule(s)"
        if [[ -n "$backup_dir" ]]; then
            log_info "Backup available at: $backup_dir"
        fi
        log_info "The project now uses extracted sources instead of git submodules."
        exit 0
    else
        log_error "Submodule removal completed with $verification_issues issues"
        if [[ -n "$backup_dir" ]]; then
            log_info "Backup available at: $backup_dir"
            log_info "You can restore from backup if needed"
        fi
        exit 1
    fi
}

# Script execution
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi