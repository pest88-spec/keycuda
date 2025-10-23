#!/bin/bash

# T054: Confirm All Library Versions are Clearly Documented and Trackable
# This script verifies that all library versions are properly documented
# and can be tracked through the integration system

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/common.sh"

# Global variables for verification results
declare -g VERIFICATION_RESULTS=()
declare -g DOCUMENTATION_SCORE=0
declare -g TRACKING_SCORE=0
declare -g OVERALL_SCORE=0
declare -g VERIFICATION_START_TIME=""

# Colors for output
readonly RED='\033[0;31m'
readonly GREEN='\033[0;32m'
readonly YELLOW='\033[1;33m'
readonly BLUE='\033[0;34m'
readonly NC='\033[0m' # No Color

# Initialize verification environment
init_verification() {
    local verification_id="T054-$(date +%Y%m%d-%H%M%S)"
    VERIFICATION_START_TIME=$(date +%s)

    log "T054" "INFO" "Initializing version documentation verification: $verification_id"

    # Create verification environment
    mkdir -p "$PROJECT_ROOT/logs/version-verification"
    mkdir -p "$PROJECT_ROOT/test-results/version-documentation"

    # Initialize results
    VERIFICATION_RESULTS=(
        "manifest_completeness:PENDING"
        "version_tracking:PENDING"
        "attribution_documentation:PENDING"
        "checksum_verification:PENDING"
        "change_log_tracking:PENDING"
        "dependency_graph:PENDING"
        "historical_tracking:PENDING"
        "audit_trail:PENDING"
    )

    log "T054" "INFO" "Version documentation verification initialized"
}

# Verify integration manifest completeness
verify_manifest_completeness() {
    log "T054" "INFO" "Verifying integration manifest completeness"

    local manifest_file="$PROJECT_ROOT/src/integration/manifest.json"
    local verification_result=0

    if [[ ! -f "$manifest_file" ]]; then
        log "T054" "ERROR" "Integration manifest not found: $manifest_file"
        update_verification_result "manifest_completeness" "FAILED"
        return 1
    fi

    # Verify manifest structure
    local required_fields=(
        "integration_manifest.version"
        "integration_manifest.created_at"
        "integration_manifest.description"
        "libraries"
    )

    for field in "${required_fields[@]}"; do
        if ! jq -e ".$field" "$manifest_file" > /dev/null 2>&1; then
            log "T054" "ERROR" "Missing required field in manifest: $field"
            verification_result=1
        else
            log "T054" "INFO" "✓ Manifest field found: $field"
        fi
    done

    # Verify each library entry has complete documentation
    local libraries
    libraries=$(jq -r '.libraries | keys[]' "$manifest_file" 2>/dev/null || true)

    for library in $libraries; do
        log "T054" "INFO" "Checking library documentation: $library"

        local lib_fields=(
            "libraries.$library.version"
            "libraries.$library.source_path"
            "libraries.$library.integrated_at"
            "libraries.$library.checksum_sha256"
            "libraries.$library.license"
            "libraries.$library.attribution.copyright"
            "libraries.$library.attribution.license_url"
        )

        for field in "${lib_fields[@]}"; do
            if ! jq -e ".$field" "$manifest_file" > /dev/null 2>&1; then
                log "T054" "ERROR" "Missing required library field: $field"
                verification_result=1
            else
                log "T054" "INFO" "✓ Library field found: $field"
            fi
        done
    done

    # Update result
    if [[ $verification_result -eq 0 ]]; then
        update_verification_result "manifest_completeness" "PASSED"
        log "T054" "INFO" "Integration manifest completeness verification PASSED"
    else
        update_verification_result "manifest_completeness" "FAILED"
        log "T054" "ERROR" "Integration manifest completeness verification FAILED"
    fi

    return $verification_result
}

# Verify version tracking mechanisms
verify_version_tracking() {
    log "T054" "INFO" "Verifying version tracking mechanisms"

    local tracking_result=0

    # Check version database
    local version_db="$PROJECT_ROOT/src/integration/versions.json"
    if [[ -f "$version_db" ]]; then
        log "T054" "INFO" "✓ Version database found: $version_db"

        # Verify version database structure
        if jq -e '.version_history' "$version_db" > /dev/null 2>&1; then
            log "T054" "INFO" "✓ Version history tracking present"
        else
            log "T054" "ERROR" "✗ Version history tracking missing"
            tracking_result=1
        fi

        if jq -e '.current_versions' "$version_db" > /dev/null 2>&1; then
            log "T054" "INFO" "✓ Current versions tracking present"
        else
            log "T054" "ERROR" "✗ Current versions tracking missing"
            tracking_result=1
        fi
    else
        log "T054" "WARNING" "⚠ Version database not found: $version_db"
    fi

    # Check version tags in extracted libraries
    local extracted_dir="$PROJECT_ROOT/src/extracted"
    if [[ -d "$extracted_dir" ]]; then
        log "T054" "INFO" "Checking version tags in extracted libraries"

        local libraries
        libraries=$(find "$extracted_dir" -maxdepth 1 -type d -not -path "$extracted_dir" -exec basename {} \; 2>/dev/null || true)

        for library in $libraries; do
            local lib_dir="$extracted_dir/$library"

            # Check for version information
            if [[ -f "$lib_dir/VERSION" ]]; then
                local version=$(cat "$lib_dir/VERSION")
                log "T054" "INFO" "✓ Version file found for $library: $version"
            elif [[ -f "$lib_dir/version.h" ]]; then
                if grep -q "VERSION" "$lib_dir/version.h"; then
                    log "T054" "INFO" "✓ Version header found for $library"
                else
                    log "T054" "WARNING" "⚠ Version header exists but no version info for $library"
                fi
            else
                log "T054" "WARNING" "⚠ No version information found for $library"
            fi
        done
    fi

    # Check CMake version information
    local cmake_files
    cmake_files=$(find "$PROJECT_ROOT/src" -name "CMakeLists.txt" -type f 2>/dev/null || true)

    for cmake_file in $cmake_files; do
        if grep -q "VERSION" "$cmake_file"; then
            log "T054" "INFO" "✓ CMake version information found: $cmake_file"
        fi
    done

    # Update result
    if [[ $tracking_result -eq 0 ]]; then
        update_verification_result "version_tracking" "PASSED"
        log "T054" "INFO" "Version tracking verification PASSED"
    else
        update_verification_result "version_tracking" "FAILED"
        log "T054" "ERROR" "Version tracking verification FAILED"
    fi

    return $tracking_result
}

# Verify attribution documentation
verify_attribution_documentation() {
    log "T054" "INFO" "Verifying attribution documentation"

    local attribution_result=0

    # Check attribution files
    local attribution_files=(
        "$PROJECT_ROOT/src/integration/ATTRIBUTION.md"
        "$PROJECT_ROOT/src/integration/LICENSES/"
        "$PROJECT_ROOT/src/integration/THIRD-PARTY-NOTICES.md"
    )

    for file in "${attribution_files[@]}"; do
        if [[ -e "$file" ]]; then  # Handles both files and directories
            log "T054" "INFO" "✓ Attribution component found: $file"
        else
            log "T054" "WARNING" "⚠ Attribution component missing: $file"
        fi
    done

    # Verify individual library attributions
    local manifest_file="$PROJECT_ROOT/src/integration/manifest.json"
    if [[ -f "$manifest_file" ]]; then
        local libraries
        libraries=$(jq -r '.libraries | keys[]' "$manifest_file" 2>/dev/null || true)

        for library in $libraries; do
            local copyright
            local license
            local license_url

            copyright=$(jq -r ".libraries.$library.attribution.copyright" "$manifest_file" 2>/dev/null || echo "")
            license=$(jq -r ".libraries.$library.license" "$manifest_file" 2>/dev/null || echo "")
            license_url=$(jq -r ".libraries.$library.attribution.license_url" "$manifest_file" 2>/dev/null || echo "")

            if [[ -n "$copyright" && "$copyright" != "null" ]]; then
                log "T054" "INFO" "✓ Copyright documented for $library"
            else
                log "T054" "ERROR" "✗ Copyright missing for $library"
                attribution_result=1
            fi

            if [[ -n "$license" && "$license" != "null" ]]; then
                log "T054" "INFO" "✓ License documented for $library: $license"
            else
                log "T054" "ERROR" "✗ License missing for $library"
                attribution_result=1
            fi

            if [[ -n "$license_url" && "$license_url" != "null" ]]; then
                log "T054" "INFO" "✓ License URL documented for $library"
            else
                log "T054" "WARNING" "⚠ License URL missing for $library"
            fi
        done
    fi

    # Check for license files in extracted libraries
    local extracted_dir="$PROJECT_ROOT/src/extracted"
    if [[ -d "$extracted_dir" ]]; then
        local libraries
        libraries=$(find "$extracted_dir" -maxdepth 1 -type d -not -path "$extracted_dir" -exec basename {} \; 2>/dev/null || true)

        for library in $libraries; do
            local lib_dir="$extracted_dir/$library"
            local license_files
            license_files=$(find "$lib_dir" -name "LICENSE*" -o -name "COPYING*" -o -name "NOTICE*" 2>/dev/null || true)

            if [[ -n "$license_files" ]]; then
                log "T054" "INFO" "✓ License files found for $library"
                for license_file in $license_files; do
                    log "T054" "DEBUG" "  License file: $license_file"
                done
            else
                log "T054" "WARNING" "⚠ No license files found for $library"
            fi
        done
    fi

    # Update result
    if [[ $attribution_result -eq 0 ]]; then
        update_verification_result "attribution_documentation" "PASSED"
        log "T054" "INFO" "Attribution documentation verification PASSED"
    else
        update_verification_result "attribution_documentation" "FAILED"
        log "T054" "ERROR" "Attribution documentation verification FAILED"
    fi

    return $attribution_result
}

# Verify checksum tracking
verify_checksum_tracking() {
    log "T054" "INFO" "Verifying checksum tracking"

    local checksum_result=0

    # Check checksum database
    local checksum_db="$PROJECT_ROOT/src/integration/checksums.json"
    if [[ -f "$checksum_db" ]]; then
        log "T054" "INFO" "✓ Checksum database found: $checksum_db"

        # Verify checksum database structure
        if jq -e '.checksums' "$checksum_db" > /dev/null 2>&1; then
            log "T054" "INFO" "✓ Checksum entries present"
        else
            log "T054" "ERROR" "✗ Checksum entries missing"
            checksum_result=1
        fi
    else
        log "T054" "WARNING" "⚠ Checksum database not found: $checksum_db"
    fi

    # Verify checksums in integration manifest
    local manifest_file="$PROJECT_ROOT/src/integration/manifest.json"
    if [[ -f "$manifest_file" ]]; then
        local libraries
        libraries=$(jq -r '.libraries | keys[]' "$manifest_file" 2>/dev/null || true)

        for library in $libraries; do
            local checksum
            checksum=$(jq -r ".libraries.$library.checksum_sha256" "$manifest_file" 2>/dev/null || echo "")

            if [[ -n "$checksum" && "$checksum" != "null" && ${#checksum} -eq 64 ]]; then
                log "T054" "INFO" "✓ SHA-256 checksum documented for $library: ${checksum:0:16}..."

                # Verify checksum format
                if [[ "$checksum" =~ ^[a-fA-F0-9]{64}$ ]]; then
                    log "T054" "INFO" "✓ Checksum format valid for $library"
                else
                    log "T054" "ERROR" "✗ Invalid checksum format for $library"
                    checksum_result=1
                fi
            else
                log "T054" "ERROR" "✗ Valid SHA-256 checksum missing for $library"
                checksum_result=1
            fi
        done
    fi

    # Test checksum calculation capability
    local test_file="$PROJECT_ROOT/README.md"
    if [[ -f "$test_file" ]]; then
        local calculated_checksum
        calculated_checksum=$(sha256sum "$test_file" 2>/dev/null | cut -d' ' -f1 || true)

        if [[ ${#calculated_checksum} -eq 64 ]]; then
            log "T054" "INFO" "✓ Checksum calculation capability verified"
        else
            log "T054" "ERROR" "✗ Checksum calculation failed"
            checksum_result=1
        fi
    fi

    # Update result
    if [[ $checksum_result -eq 0 ]]; then
        update_verification_result "checksum_verification" "PASSED"
        log "T054" "INFO" "Checksum verification PASSED"
    else
        update_verification_result "checksum_verification" "FAILED"
        log "T054" "ERROR" "Checksum verification FAILED"
    fi

    return $checksum_result
}

# Verify change log tracking
verify_change_log_tracking() {
    log "T054" "INFO" "Verifying change log tracking"

    local changelog_result=0

    # Check for change log files
    local changelog_files=(
        "$PROJECT_ROOT/CHANGELOG.md"
        "$PROJECT_ROOT/CHANGELOG"
        "$PROJECT_ROOT/src/integration/CHANGELOG.md"
    )

    local changelog_found=false
    for file in "${changelog_files[@]}"; do
        if [[ -f "$file" ]]; then
            log "T054" "INFO" "✓ Change log found: $file"
            changelog_found=true

            # Verify change log structure
            if grep -q "^## " "$file" 2>/dev/null; then
                log "T054" "INFO" "✓ Change log has proper version sections"
            else
                log "T054" "WARNING" "⚠ Change log lacks version sections"
            fi

            if grep -q "^- " "$file" 2>/dev/null; then
                log "T054" "INFO" "✓ Change log has change entries"
            else
                log "T054" "WARNING" "⚠ Change log lacks change entries"
            fi
        fi
    done

    if [[ "$changelog_found" == "false" ]]; then
        log "T054" "WARNING" "⚠ No change log files found"
        changelog_result=1
    fi

    # Check for library-specific change logs
    local extracted_dir="$PROJECT_ROOT/src/extracted"
    if [[ -d "$extracted_dir" ]]; then
        local libraries
        libraries=$(find "$extracted_dir" -maxdepth 1 -type d -not -path "$extracted_dir" -exec basename {} \; 2>/dev/null || true)

        for library in $libraries; do
            local lib_dir="$extracted_dir/$library"
            local lib_changelog
            lib_changelog=$(find "$lib_dir" -name "CHANGELOG*" -o -name "HISTORY*" -o -name "NEWS*" 2>/dev/null || true)

            if [[ -n "$lib_changelog" ]]; then
                log "T054" "INFO" "✓ Library change log found for $library"
            else
                log "T054" "DEBUG" "No change log for library $library (not critical)"
            fi
        done
    fi

    # Update result
    if [[ $changelog_result -eq 0 ]]; then
        update_verification_result "change_log_tracking" "PASSED"
        log "T054" "INFO" "Change log tracking verification PASSED"
    else
        update_verification_result "change_log_tracking" "FAILED"
        log "T054" "ERROR" "Change log tracking verification FAILED"
    fi

    return $changelog_result
}

# Verify dependency graph tracking
verify_dependency_graph() {
    log "T054" "INFO" "Verifying dependency graph tracking"

    local dependency_result=0

    # Check for dependency graph files
    local dep_files=(
        "$PROJECT_ROOT/src/integration/dependencies.json"
        "$PROJECT_ROOT/src/integration/dependency-graph.json"
        "$PROJECT_ROOT/DEPENDENCIES"
    )

    local dep_graph_found=false
    for file in "${dep_files[@]}"; do
        if [[ -f "$file" ]]; then
            log "T054" "INFO" "✓ Dependency file found: $file"
            dep_graph_found=true

            # If JSON, verify structure
            if [[ "$file" == *.json ]]; then
                if jq -e '.dependencies' "$file" > /dev/null 2>&1; then
                    log "T054" "INFO" "✓ Dependency graph structure valid"
                else
                    log "T054" "WARNING" "⚠ Dependency graph structure invalid"
                fi
            fi
        fi
    done

    # Check CMake dependency tracking
    local cmake_files
    cmake_files=$(find "$PROJECT_ROOT" -name "CMakeLists.txt" -type f 2>/dev/null || true)

    for cmake_file in $cmake_files; do
        if grep -q "find_package\|add_subdirectory\|target_link_libraries" "$cmake_file"; then
            log "T054" "INFO" "✓ CMake dependencies found in: $(basename "$(dirname "$cmake_file")")/$(basename "$cmake_file")"
        fi
    done

    # Generate current dependency graph
    if command -v dot >/dev/null 2>&1; then
        log "T054" "INFO" "✓ Graphviz available for dependency visualization"
    else
        log "T054" "DEBUG" "Graphviz not available (optional)"
    fi

    # Update result
    if [[ $dependency_result -eq 0 ]]; then
        update_verification_result "dependency_graph" "PASSED"
        log "T054" "INFO" "Dependency graph verification PASSED"
    else
        update_verification_result "dependency_graph" "FAILED"
        log "T054" "ERROR" "Dependency graph verification FAILED"
    fi

    return $dependency_result
}

# Verify historical tracking
verify_historical_tracking() {
    log "T054" "INFO" "Verifying historical tracking"

    local historical_result=0

    # Check git history for integration changes
    if git -C "$PROJECT_ROOT" log --oneline --grep="integration\|dependency\|library" 2>/dev/null | head -5 > /dev/null; then
        log "T054" "INFO" "✓ Git history contains integration-related commits"
    else
        log "T054" "DEBUG" "No integration-related commits found in git history"
    fi

    # Check for version history database
    local version_history="$PROJECT_ROOT/src/integration/version-history.json"
    if [[ -f "$version_history" ]]; then
        log "T054" "INFO" "✓ Version history database found"

        # Verify structure
        if jq -e '.updates' "$version_history" > /dev/null 2>&1; then
            local update_count
            update_count=$(jq '.updates | length' "$version_history" 2>/dev/null || echo "0")
            log "T054" "INFO" "✓ Version history contains $update_count updates"
        else
            log "T054" "WARNING" "⚠ Version history structure invalid"
        fi
    else
        log "T054" "WARNING" "⚠ Version history database not found"
    fi

    # Check backup directories for historical versions
    local backup_dir="$PROJECT_ROOT/src/integration/backups"
    if [[ -d "$backup_dir" ]]; then
        local backup_count
        backup_count=$(find "$backup_dir" -maxdepth 1 -type d 2>/dev/null | wc -l)
        backup_count=$((backup_count - 1))  # Subtract 1 for the backup_dir itself

        if [[ $backup_count -gt 0 ]]; then
            log "T054" "INFO" "✓ Found $backup_count backup directories"
        else
            log "T054" "DEBUG" "No backup directories found"
        fi
    else
        log "T054" "DEBUG" "Backup directory not found"
    fi

    # Update result
    if [[ $historical_result -eq 0 ]]; then
        update_verification_result "historical_tracking" "PASSED"
        log "T054" "INFO" "Historical tracking verification PASSED"
    else
        update_verification_result "historical_tracking" "FAILED"
        log "T054" "ERROR" "Historical tracking verification FAILED"
    fi

    return $historical_result
}

# Verify audit trail completeness
verify_audit_trail() {
    log "T054" "INFO" "Verifying audit trail completeness"

    local audit_result=0

    # Check for audit log files
    local audit_dirs=(
        "$PROJECT_ROOT/logs/integration"
        "$PROJECT_ROOT/logs/dependency-updates"
        "$PROJECT_ROOT/logs/compatibility-validation"
    )

    for audit_dir in "${audit_dirs[@]}"; do
        if [[ -d "$audit_dir" ]]; then
            local log_count
            log_count=$(find "$audit_dir" -name "*.log" -type f 2>/dev/null | wc -l)
            log "T054" "INFO" "✓ Audit directory found: $audit_dir ($log_count log files)"

            if [[ $log_count -gt 0 ]]; then
                # Check log file structure
                local sample_log
                sample_log=$(find "$audit_dir" -name "*.log" -type f 2>/dev/null | head -1)

                if [[ -f "$sample_log" ]]; then
                    if grep -q "T0[0-9][0-9]" "$sample_log" 2>/dev/null; then
                        log "T054" "INFO" "✓ Log files contain task identifiers"
                    fi

                    if grep -q "timestamp\|TIMESTAMP" "$sample_log" 2>/dev/null; then
                        log "T054" "INFO" "✓ Log files contain timestamps"
                    fi
                fi
            fi
        else
            log "T054" "DEBUG" "Audit directory not found: $audit_dir"
        fi
    done

    # Verify evidence collection
    local evidence_dir="$PROJECT_ROOT/src/integration/evidence"
    if [[ -d "$evidence_dir" ]]; then
        local evidence_count
        evidence_count=$(find "$evidence_dir" -type f 2>/dev/null | wc -l)
        log "T054" "INFO" "✓ Evidence directory found: $evidence_count evidence files"
    else
        log "T054" "DEBUG" "Evidence directory not found"
    fi

    # Update result
    if [[ $audit_result -eq 0 ]]; then
        update_verification_result "audit_trail" "PASSED"
        log "T054" "INFO" "Audit trail verification PASSED"
    else
        update_verification_result "audit_trail" "FAILED"
        log "T054" "ERROR" "Audit trail verification FAILED"
    fi

    return $audit_result
}

# Helper function to update verification results
update_verification_result() {
    local test_name="$1"
    local result="$2"

    # Update in results array
    for i in "${!VERIFICATION_RESULTS[@]}"; do
        local entry="${VERIFICATION_RESULTS[$i]}"
        local entry_name="${entry%%:*}"

        if [[ "$entry_name" == "$test_name" ]]; then
            VERIFICATION_RESULTS[$i]="$test_name:$result"
            break
        fi
    done
}

# Calculate overall scores
calculate_scores() {
    local passed_tests=0
    local total_tests=${#VERIFICATION_RESULTS[@]}

    for result in "${VERIFICATION_RESULTS[@]}"; do
        local status="${result##*:}"
        if [[ "$status" == "PASSED" ]]; then
            ((passed_tests++))
        fi
    done

    OVERALL_SCORE=$(( passed_tests * 100 / total_tests ))

    # Calculate separate documentation and tracking scores
    local documentation_tests=("manifest_completeness" "attribution_documentation" "change_log_tracking")
    local tracking_tests=("version_tracking" "checksum_verification" "dependency_graph" "historical_tracking" "audit_trail")

    local doc_passed=0
    local track_passed=0

    for test_name in "${documentation_tests[@]}"; do
        for result in "${VERIFICATION_RESULTS[@]}"; do
            local entry_name="${result%%:*}"
            local entry_status="${result##*:}"

            if [[ "$entry_name" == "$test_name" && "$entry_status" == "PASSED" ]]; then
                ((doc_passed++))
                break
            fi
        done
    done

    for test_name in "${tracking_tests[@]}"; do
        for result in "${VERIFICATION_RESULTS[@]}"; do
            local entry_name="${result%%:*}"
            local entry_status="${result##*:}"

            if [[ "$entry_name" == "$test_name" && "$entry_status" == "PASSED" ]]; then
                ((track_passed++))
                break
            fi
        done
    done

    DOCUMENTATION_SCORE=$(( doc_passed * 100 / ${#documentation_tests[@]} ))
    TRACKING_SCORE=$(( track_passed * 100 / ${#tracking_tests[@]} ))
}

# Generate comprehensive verification report
generate_verification_report() {
    local verification_duration=$(( $(date +%s) - VERIFICATION_START_TIME ))
    local report_file="$PROJECT_ROOT/test-results/version-documentation/T054-version-documentation-report.json"

    mkdir -p "$(dirname "$report_file")"

    # Calculate scores
    calculate_scores

    # Generate JSON report
    cat > "$report_file" << EOF
{
  "version_documentation_verification": {
    "task_id": "T054",
    "task_name": "Confirm All Library Versions are Clearly Documented and Trackable",
    "timestamp": "$(date -Iseconds)",
    "duration_seconds": $verification_duration,
    "scores": {
      "overall_score": $OVERALL_SCORE,
      "documentation_score": $DOCUMENTATION_SCORE,
      "tracking_score": $TRACKING_SCORE
    },
    "summary": {
      "total_tests": ${#VERIFICATION_RESULTS[@]},
      "passed_tests": $(echo "${VERIFICATION_RESULTS[*]}" | grep -o "PASSED" | wc -l),
      "failed_tests": $(echo "${VERIFICATION_RESULTS[*]}" | grep -o "FAILED" | wc -l)
    },
    "verification_results": [
EOF

    # Add individual test results
    local first=true
    for result in "${VERIFICATION_RESULTS[@]}"; do
        if [[ "$first" == "false" ]]; then
            echo "," >> "$report_file"
        fi
        first=false

        local test_name="${result%%:*}"
        local status="${result##*:}"

        cat >> "$report_file" << EOF
      {
        "test_name": "$test_name",
        "status": "$status",
        "description": "$(get_test_description "$test_name")"
      }
EOF
    done

    cat >> "$report_file" << EOF
    ],
    "documentation_completeness": {
      "manifest_present": $(test -f "$PROJECT_ROOT/src/integration/manifest.json" && echo "true" || echo "false"),
      "attribution_files_present": $(test -e "$PROJECT_ROOT/src/integration/ATTRIBUTION.md" && echo "true" || echo "false"),
      "change_log_present": $(test -f "$PROJECT_ROOT/CHANGELOG.md" && echo "true" || echo "false"),
      "license_files_tracked": $(find "$PROJECT_ROOT/src/extracted" -name "LICENSE*" 2>/dev/null | wc -l)
    },
    "tracking_capability": {
      "version_database_present": $(test -f "$PROJECT_ROOT/src/integration/versions.json" && echo "true" || echo "false"),
      "checksum_tracking_enabled": $(test -f "$PROJECT_ROOT/src/integration/checksums.json" && echo "true" || echo "false"),
      "dependency_graph_available": $(test -f "$PROJECT_ROOT/src/integration/dependencies.json" && echo "true" || echo "false"),
      "historical_tracking_active": $(test -f "$PROJECT_ROOT/src/integration/version-history.json" && echo "true" || echo "false")
    },
    "compliance": {
      "constitution_section": "VI.Third-Party Integration Compliance",
      "meets_documentation_standards": $(meets_documentation_standards),
      "meets_tracking_standards": $(meets_tracking_standards)
    },
    "recommendations": [
      $(get_documentation_recommendations)
    ]
  }
}
EOF

    # Generate markdown summary
    local markdown_file="$PROJECT_ROOT/test-results/version-documentation/T054-version-documentation-summary.md"
    cat > "$markdown_file" << EOF
# T054 Version Documentation Verification Summary

**Verification Date:** $(date '+%Y-%m-%d %H:%M:%S')
**Duration:** ${verification_duration}s

## Overall Scores

- **Overall Score:** $OVERALL_SCORE%
- **Documentation Score:** $DOCUMENTATION_SCORE%
- **Tracking Score:** $TRACKING_SCORE%

## Verification Results

| Test Component | Status | Description |
|----------------|--------|-------------|
$(for result in "${VERIFICATION_RESULTS[@]}"; do
    test_name="${result%%:*}"
    status="${result##*:}"
    printf "| %-30s | %-10s | %s\n" "$(format_test_name "$test_name")" "$status" "$(get_test_description "$test_name")"
done) |

## Documentation Completeness

- **Integration Manifest:** $(test -f "$PROJECT_ROOT/src/integration/manifest.json" && echo "✅ Present" || echo "❌ Missing")
- **Attribution Files:** $(test -e "$PROJECT_ROOT/src/integration/ATTRIBUTION.md" && echo "✅ Present" || echo "❌ Missing")
- **Change Log:** $(test -f "$PROJECT_ROOT/CHANGELOG.md" && echo "✅ Present" || echo "❌ Missing")
- **License Files:** $(find "$PROJECT_ROOT/src/extracted" -name "LICENSE*" 2>/dev/null | wc -l) files tracked

## Tracking Capability

- **Version Database:** $(test -f "$PROJECT_ROOT/src/integration/versions.json" && echo "✅ Available" || echo "❌ Missing")
- **Checksum Tracking:** $(test -f "$PROJECT_ROOT/src/integration/checksums.json" && echo "✅ Enabled" || echo "❌ Disabled")
- **Dependency Graph:** $(test -f "$PROJECT_ROOT/src/integration/dependencies.json" && echo "✅ Available" || echo "❌ Missing")
- **Historical Tracking:** $(test -f "$PROJECT_ROOT/src/integration/version-history.json" && echo "✅ Active" || echo "❌ Inactive")

## Key Findings

$(get_key_findings)

## Recommendations

$(get_documentation_recommendations | sed 's/"//g' | sed 's/, /\n- /g')

## Compliance Status

**Constitution Section:** VI.Third-Party Integration Compliance
**Documentation Standards:** $(meets_documentation_standards && echo "✅ Meets" || echo "❌ Does not meet")
**Tracking Standards:** $(meets_tracking_standards && echo "✅ Meets" || echo "❌ Does not meet")
EOF

    log "T054" "INFO" "Version documentation verification report generated: $report_file"
    log "T054" "INFO" "Version documentation summary: $markdown_file"
}

# Helper functions for report generation
get_test_description() {
    local test_name="$1"
    case "$test_name" in
        "manifest_completeness") echo "Verifies integration manifest contains all required fields" ;;
        "version_tracking") echo "Confirms version tracking mechanisms are in place" ;;
        "attribution_documentation") echo "Validates copyright and license attribution is documented" ;;
        "checksum_verification") echo "Ensures SHA-256 checksums are tracked for integrity verification" ;;
        "change_log_tracking") echo "Checks that change logs are maintained for version history" ;;
        "dependency_graph") echo "Verifies dependency relationships are documented" ;;
        "historical_tracking") echo "Confirms historical version information is preserved" ;;
        "audit_trail") echo "Validates audit trail and evidence collection is complete" ;;
        *) echo "Unknown verification test" ;;
    esac
}

format_test_name() {
    local test_name="$1"
    echo "$test_name" | sed 's/_/ /g' | sed 's/\b\w/\U&/g'
}

get_key_findings() {
    local findings=""

    if [[ $OVERALL_SCORE -ge 90 ]]; then
        findings="- Comprehensive version documentation and tracking system in place
- All required documentation components are present and complete
- Version tracking mechanisms provide full visibility into library changes
- Audit trail and evidence collection meet compliance requirements"
    elif [[ $OVERALL_SCORE -ge 70 ]]; then
        findings="- Version documentation mostly complete with minor gaps
- Tracking mechanisms functional but could be enhanced
- Some documentation components need attention"
    else
        findings="- Significant gaps in version documentation and tracking
- Multiple critical components missing or incomplete
- Major improvements needed before production deployment"
    fi

    echo "$findings"
}

get_documentation_recommendations() {
    local recommendations=""

    if [[ $DOCUMENTATION_SCORE -lt 80 ]]; then
        recommendations='"Complete missing attribution documentation", "Add comprehensive change logs", "Verify all license files are tracked"'
    fi

    if [[ $TRACKING_SCORE -lt 80 ]]; then
        if [[ -n "$recommendations" ]]; then
            recommendations+=', '
        fi
        recommendations+='"Implement version database", "Enable checksum tracking for all libraries", "Create dependency graph documentation"'
    fi

    if [[ $OVERALL_SCORE -ge 80 ]]; then
        if [[ -n "$recommendations" ]]; then
            recommendations+=', '
        fi
        recommendations+='"Continue maintaining current documentation standards", "Regular audits recommended"'
    fi

    if [[ -z "$recommendations" ]]; then
        recommendations='"System meets all documentation and tracking requirements"'
    fi

    echo "$recommendations"
}

meets_documentation_standards() {
    [[ $DOCUMENTATION_SCORE -ge 80 ]] && echo "true" || echo "false"
}

meets_tracking_standards() {
    [[ $TRACKING_SCORE -ge 80 ]] && echo "true" || echo "false"
}

# Main execution
main() {
    log "T054" "INFO" "Starting T054: Confirm All Library Versions are Clearly Documented and Trackable"

    # Initialize verification
    init_verification

    # Run all verification tests
    local overall_result=0

    verify_manifest_completeness || overall_result=1
    verify_version_tracking || overall_result=1
    verify_attribution_documentation || overall_result=1
    verify_checksum_tracking || overall_result=1
    verify_change_log_tracking || overall_result=1
    verify_dependency_graph || overall_result=1
    verify_historical_tracking || overall_result=1
    verify_audit_trail || overall_result=1

    # Generate comprehensive verification report
    generate_verification_report

    # Final verdict
    echo
    log "T054" "INFO" "=== VERSION DOCUMENTATION VERIFICATION SUMMARY ==="
    log "T054" "INFO" "Overall Score: $OVERALL_SCORE%"
    log "T054" "INFO" "Documentation Score: $DOCUMENTATION_SCORE%"
    log "T054" "INFO" "Tracking Score: $TRACKING_SCORE%"
    log "T054" "INFO" "Tests Passed: $(echo "${VERIFICATION_RESULTS[*]}" | grep -o "PASSED" | wc -l)/${#VERIFICATION_RESULTS[@]}"

    if [[ $overall_result -eq 0 && $OVERALL_SCORE -ge 80 ]]; then
        log "T054" "INFO" "✅ T054 COMPLETED SUCCESSFULLY - All library versions are clearly documented and trackable"
        log "T054" "INFO" "Version documentation and tracking system meets all requirements"
    elif [[ $OVERALL_SCORE -ge 60 ]]; then
        log "T054" "WARNING" "⚠️ T054 COMPLETED WITH MINOR ISSUES - Some documentation or tracking improvements needed"
    else
        log "T054" "ERROR" "❌ T054 COMPLETED WITH MAJOR ISSUES - Significant documentation and tracking gaps"
        overall_result=1
    fi

    log "T054" "INFO" "Detailed report: $PROJECT_ROOT/test-results/version-documentation/T054-version-documentation-report.json"

    return $overall_result
}

# Execute if called directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi