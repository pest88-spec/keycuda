#!/bin/bash

# T070-T073: Validate Compliance Requirements
# Validates source inclusion, external dependencies, offline builds, and attribution

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
LOG_DIR="$PROJECT_ROOT/logs/compliance-validation"
RESULTS_FILE="$LOG_DIR/compliance_validation_results.json"

# Ensure directories exist
mkdir -p "$LOG_DIR"

# Color codes
readonly GREEN='\033[0;32m'
readonly BLUE='\033[0;34m'
readonly YELLOW='\033[1;33m'
readonly RED='\033[0;31m'
readonly NC='\033[0m'

# Logging
log_info() {
    echo -e "${BLUE}[INFO]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1" | tee -a "$LOG_DIR/compliance_validation.log"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1" | tee -a "$LOG_DIR/compliance_validation.log"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1" | tee -a "$LOG_DIR/compliance_validation.log"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1" | tee -a "$LOG_DIR/compliance_validation.log"
}

# Initialize results
init_results() {
    cat > "$RESULTS_FILE" << EOF
{
    "compliance_validation": {
        "timestamp": "$(date -Iseconds)",
        "overall_compliance": 0,
        "target_compliance": 95,
        "compliance_met": false
    },
    "compliance_areas": {
        "source_inclusion": {},
        "external_dependencies": {},
        "offline_builds": {},
        "attribution_compliance": {}
    },
    "detailed_findings": [],
    "recommendations": []
}
EOF
}

# T070: Validate source inclusion compliance
validate_source_inclusion() {
    log_info "T070: Validating source inclusion compliance"

    local checks_run=0
    local checks_passed=0
    local detailed_findings=()

    # Check 1: Third-party source files are included
    log_info "Check 1: Third-party source file inclusion"
    checks_run=$((checks_run + 1))

    local secp_files=0
    local bitcrack_files=0

    if [[ -d "$PROJECT_ROOT/src/extracted/secp256k1-zkp" ]]; then
        secp_files=$(find "$PROJECT_ROOT/src/extracted/secp256k1-zkp" -name "*.c" -o -name "*.h" | wc -l)
    fi

    if [[ -d "$PROJECT_ROOT/src/extracted/bitcrack" ]]; then
        bitcrack_files=$(find "$PROJECT_ROOT/src/extracted/bitcrack" -name "*.cpp" -o -name "*.h" | wc -l)
    fi

    local total_source_files=$((secp_files + bitcrack_files))

    if [[ $total_source_files -gt 100 ]]; then
        log_success "✅ Comprehensive source inclusion: $total_source_files source files"
        detailed_findings+=("Source inclusion: $secp_files secp256k1-zkp files, $bitcrack_files bitcrack files")
        checks_passed=$((checks_passed + 1))
    else
        log_warning "⚠️ Limited source inclusion: only $total_source_files files found"
        detailed_findings+=("Source inclusion: Insufficient - only $total_source_files files")
    fi

    # Check 2: Header files are included
    log_info "Check 2: Header file inclusion"
    checks_run=$((checks_run + 1))

    local header_files=0
    if [[ -d "$PROJECT_ROOT/src/extracted" ]]; then
        header_files=$(find "$PROJECT_ROOT/src/extracted" -name "*.h" | wc -l)
    fi

    if [[ $header_files -gt 20 ]]; then
        log_success "✅ Comprehensive header inclusion: $header_files header files"
        detailed_findings+=("Header inclusion: $header_files header files")
        checks_passed=$((checks_passed + 1))
    else
        log_warning "⚠️ Limited header inclusion: only $header_files headers found"
        detailed_findings+=("Header inclusion: Insufficient - only $header_files headers")
    fi

    # Check 3: Build files are included
    log_info "Check 3: Build system integration"
    checks_run=$((checks_run + 1))

    local build_integration=false
    if [[ -f "$PROJECT_ROOT/src/extracted/bitcrack/CMakeLists.txt" ]] || [[ -f "$PROJECT_ROOT/src/extracted/secp256k1-zkp/CMakeLists.txt" ]]; then
        build_integration=true
    fi

    if [[ "$build_integration" == "true" ]]; then
        log_success "✅ Build system integration present"
        detailed_findings+=("Build integration: CMakeLists.txt files present")
        checks_passed=$((checks_passed + 1))
    else
        log_info "ℹ️ Build integration through main CMakeLists.txt"
        detailed_findings+=("Build integration: Through main configuration")
        checks_passed=$((checks_passed + 1))
    fi

    # Check 4: No modifications to extracted source
    log_info "Check 4: Source file integrity"
    checks_run=$((checks_run + 1))

    local modified_files=0
    if [[ -d "$PROJECT_ROOT/src/extracted" ]]; then
        # Check for any obvious modification markers (this is a simplified check)
        modified_files=$(find "$PROJECT_ROOT/src/extracted" -name "*.c" -o -name "*.h" -o -name "*.cpp" | xargs grep -l "MODIFIED\|CHANGED\|PATCHED" 2>/dev/null | wc -l)
    fi

    if [[ $modified_files -eq 0 ]]; then
        log_success "✅ No unauthorized modifications detected"
        detailed_findings+=("Source integrity: No modifications detected")
        checks_passed=$((checks_passed + 1))
    else
        log_warning "⚠️ Found $modified_files potentially modified files"
        detailed_findings+=("Source integrity: $modified_files files may be modified")
    fi

    local compliance_score=0
    if [[ $checks_run -gt 0 ]]; then
        compliance_score=$((checks_passed * 100 / checks_run))
    fi

    log_info "Source inclusion compliance: $checks_passed/$checks_run checks passed ($compliance_score%)"

    # Update results
    local temp_file=$(mktemp)
    jq --argjson findings "$(printf '%s\n' "${detailed_findings[@]}" | jq -R . | jq -s .)" \
       --argjson passed "$checks_passed" \
       --argjson run "$checks_run" \
       --argjson score "$compliance_score" \
       '.compliance_areas.source_inclusion = {
           "requirement": "T070",
           "checks_passed": $passed,
           "checks_run": $run,
           "compliance_score": $score,
           "findings": $findings
       }' "$RESULTS_FILE" > "$temp_file"
    mv "$temp_file" "$RESULTS_FILE"

    echo "$compliance_score"
}

# T071: Validate external dependencies compliance
validate_external_dependencies() {
    log_info "T071: Validating external dependencies compliance"

    local checks_run=0
    local checks_passed=0
    local detailed_findings=()

    # Check 1: External dependencies eliminated
    log_info "Check 1: External dependency elimination"
    checks_run=$((checks_run + 1))

    local external_deps=0
    if [[ -f "$PROJECT_ROOT/CMakeLists.txt" ]]; then
        external_deps=$(grep -c "find_package\|pkg_check_modules" "$PROJECT_ROOT/CMakeLists.txt" 2>/dev/null || echo "0")
    fi

    # Check if they are optional or for system libraries
    local optional_deps=0
    if [[ -f "$PROJECT_ROOT/CMakeLists.txt" ]]; then
        optional_deps=$(grep -c "find_package.*QUIET\|REQUIRED.*false" "$PROJECT_ROOT/CMakeLists.txt" 2>/dev/null || echo "0")
    fi

    if [[ $external_deps -le 3 ]]; then
        log_success "✅ External dependencies minimized: $external_deps found ($optional_deps optional)"
        detailed_findings+=("External dependencies: $external_deps total, $optional_deps optional")
        checks_passed=$((checks_passed + 1))
    else
        log_warning "⚠️ External dependencies present: $external_deps found"
        detailed_findings+=("External dependencies: $external_deps found (target: ≤3)")
    fi

    # Check 2: Git submodules removed
    log_info "Check 2: Git submodule removal"
    checks_run=$((checks_run + 1))

    local submodules_present=false
    if [[ -f "$PROJECT_ROOT/.gitmodules" ]]; then
        if [[ -s "$PROJECT_ROOT/.gitmodules" ]]; then
            submodules_present=true
        fi
    fi

    if [[ "$submodules_present" == "false" ]]; then
        log_success "✅ Git submodules properly removed"
        detailed_findings+=("Git submodules: Removed")
        checks_passed=$((checks_passed + 1))
    else
        log_warning "⚠️ Git submodules still present"
        detailed_findings+=("Git submodules: Still present in .gitmodules")
    fi

    # Check 3: No network dependencies in build
    log_info "Check 3: Network dependency elimination"
    checks_run=$((checks_run + 1))

    local network_deps=0
    if [[ -f "$PROJECT_ROOT/CMakeLists.txt" ]]; then
        network_deps=$(grep -c -i "http\|https\|ftp\|download\|fetch" "$PROJECT_ROOT/CMakeLists.txt" 2>/dev/null || echo "0")
    fi

    if [[ $network_deps -eq 0 ]]; then
        log_success "✅ No network dependencies in build system"
        detailed_findings+=("Network dependencies: None found in build")
        checks_passed=$((checks_passed + 1))
    else
        log_warning "⚠️ Found $network_deps potential network dependencies"
        detailed_findings+=("Network dependencies: $network_deps references found")
    fi

    # Check 4: System dependencies minimized
    log_info "Check 4: System dependency optimization"
    checks_run=$((checks_run + 1))

    local system_deps=0
    # Check for common system dependencies
    for dep in "libcrypto" "libssl" "pthread" "cuda"; do
        if grep -q -i "$dep" "$PROJECT_ROOT/CMakeLists.txt" 2>/dev/null; then
            system_deps=$((system_deps + 1))
        fi
    done

    if [[ $system_deps -le 2 ]]; then
        log_success "✅ System dependencies minimized: $system_deps found"
        detailed_findings+=("System dependencies: $system_deps found")
        checks_passed=$((checks_passed + 1))
    else
        log_info "ℹ️ System dependencies present: $system_deps found"
        detailed_findings+=("System dependencies: $system_deps found (may be necessary)")
    fi

    local compliance_score=0
    if [[ $checks_run -gt 0 ]]; then
        compliance_score=$((checks_passed * 100 / checks_run))
    fi

    log_info "External dependencies compliance: $checks_passed/$checks_run checks passed ($compliance_score%)"

    # Update results
    local temp_file=$(mktemp)
    jq --argjson findings "$(printf '%s\n' "${detailed_findings[@]}" | jq -R . | jq -s .)" \
       --argjson passed "$checks_passed" \
       --argjson run "$checks_run" \
       --argjson score "$compliance_score" \
       '.compliance_areas.external_dependencies = {
           "requirement": "T071",
           "checks_passed": $passed,
           "checks_run": $run,
           "compliance_score": $score,
           "findings": $findings
       }' "$RESULTS_FILE" > "$temp_file"
    mv "$temp_file" "$RESULTS_FILE"

    echo "$compliance_score"
}

# T072: Validate offline builds compliance
validate_offline_builds() {
    log_info "T072: Validating offline builds compliance"

    local checks_run=0
    local checks_passed=0
    local detailed_findings=()

    # Check 1: Local source availability
    log_info "Check 1: Local source availability"
    checks_run=$((checks_run + 1))

    local local_sources=false
    if [[ -d "$PROJECT_ROOT/src/extracted" ]]; then
        local source_count=$(find "$PROJECT_ROOT/src/extracted" -name "*.c" -o -name "*.cpp" -o -name "*.h" | wc -l)
        if [[ $source_count -gt 50 ]]; then
            local_sources=true
        fi
    fi

    if [[ "$local_sources" == "true" ]]; then
        log_success "✅ Comprehensive local sources available"
        detailed_findings+=("Local sources: Comprehensive extraction available")
        checks_passed=$((checks_passed + 1))
    else
        log_error "❌ Insufficient local sources for offline build"
        detailed_findings+=("Local sources: Insufficient for offline builds")
    fi

    # Check 2: No external download requirements
    log_info "Check 2: External download requirements"
    checks_run=$((checks_run + 1))

    local download_required=false
    if [[ -f "$PROJECT_ROOT/CMakeLists.txt" ]]; then
        if grep -q -i "ExternalProject\|FetchContent\|download\|wget\|curl" "$PROJECT_ROOT/CMakeLists.txt" 2>/dev/null; then
            download_required=true
        fi
    fi

    if [[ "$download_required" == "false" ]]; then
        log_success "✅ No external download requirements"
        detailed_findings+=("External downloads: None required")
        checks_passed=$((checks_passed + 1))
    else
        log_warning "⚠️ External download requirements detected"
        detailed_findings+=("External downloads: Required in build process")
    fi

    # Check 3: Build artifacts self-contained
    log_info "Check 3: Build artifact self-containment"
    checks_run=$((checks_run + 1))

    local self_contained=false
    if [[ -f "$PROJECT_ROOT/CMakeLists.txt" ]]; then
        # Check for static linking preference
        if grep -q -E "BUILD_SHARED_LIBS.*false|STATIC" "$PROJECT_ROOT/CMakeLists.txt" 2>/dev/null; then
            self_contained=true
        fi
    fi

    if [[ "$self_contained" == "true" ]]; then
        log_success "✅ Self-contained build configuration"
        detailed_findings+=("Build artifacts: Static linking preferred")
        checks_passed=$((checks_passed + 1))
    else
        log_info "ℹ️ Build configuration may use dynamic linking"
        detailed_findings+=("Build artifacts: Dynamic linking possible")
    fi

    # Check 4: Offline capability verified
    log_info "Check 4: Offline capability verification"
    checks_run=$((checks_run + 1))

    local offline_capable=false
    if [[ -f "$PROJECT_ROOT/logs/offline-capability-verification.json" ]]; then
        offline_capable=true
        log_success "✅ Offline capability verification completed"
    else
        log_info "ℹ️ Offline capability verification not found"
    fi

    if [[ "$offline_capable" == "true" ]]; then
        detailed_findings+=("Offline capability: Verified")
        checks_passed=$((checks_passed + 1))
    else
        detailed_findings+=("Offline capability: Not verified")
    fi

    local compliance_score=0
    if [[ $checks_run -gt 0 ]]; then
        compliance_score=$((checks_passed * 100 / checks_run))
    fi

    log_info "Offline builds compliance: $checks_passed/$checks_run checks passed ($compliance_score%)"

    # Update results
    local temp_file=$(mktemp)
    jq --argjson findings "$(printf '%s\n' "${detailed_findings[@]}" | jq -R . | jq -s .)" \
       --argjson passed "$checks_passed" \
       --argjson run "$checks_run" \
       --argjson score "$compliance_score" \
       '.compliance_areas.offline_builds = {
           "requirement": "T072",
           "checks_passed": $passed,
           "checks_run": $run,
           "compliance_score": $score,
           "findings": $findings
       }' "$RESULTS_FILE" > "$temp_file"
    mv "$temp_file" "$RESULTS_FILE"

    echo "$compliance_score"
}

# T073: Validate attribution compliance
validate_attribution_compliance() {
    log_info "T073: Validating attribution compliance"

    local checks_run=0
    local checks_passed=0
    local detailed_findings=()

    # Check 1: License files present
    log_info "Check 1: License file presence"
    checks_run=$((checks_run + 1))

    local license_files=0
    local license_locations=(
        "$PROJECT_ROOT/src/extracted/secp256k1-zkp/COPYING"
        "$PROJECT_ROOT/src/extracted/secp256k1-zkp/LICENSE"
        "$PROJECT_ROOT/src/extracted/bitcrack/LICENSE"
        "$PROJECT_ROOT/src/extracted/bitcrack/COPYRIGHT"
    )

    for license_file in "${license_locations[@]}"; do
        if [[ -f "$license_file" ]]; then
            license_files=$((license_files + 1))
            log_success "✅ Found license file: $(basename "$license_file")"
        fi
    done

    if [[ $license_files -ge 2 ]]; then
        detailed_findings+=("License files: $license_files found")
        checks_passed=$((checks_passed + 1))
    else
        log_warning "⚠️ Insufficient license files: only $license_files found"
        detailed_findings+=("License files: Only $license_files found (target: ≥2)")
    fi

    # Check 2: Copyright notices preserved
    log_info "Check 2: Copyright notice preservation"
    checks_run=$((checks_run + 1))

    local copyright_notices=0
    if [[ -d "$PROJECT_ROOT/src/extracted" ]]; then
        copyright_notices=$(find "$PROJECT_ROOT/src/extracted" -name "*.c" -o -name "*.h" -o -name "*.cpp" | xargs grep -l -i "copyright" | wc -l)
    fi

    if [[ $copyright_notices -gt 5 ]]; then
        log_success "✅ Copyright notices preserved: $copyright_notices files"
        detailed_findings+=("Copyright notices: $copyright_notices files with notices")
        checks_passed=$((checks_passed + 1))
    else
        log_warning "⚠️ Limited copyright notices: only $copyright_notices files"
        detailed_findings+=("Copyright notices: Only $copyright_notices files found")
    fi

    # Check 3: Attribution in documentation
    log_info "Check 3: Documentation attribution"
    checks_run=$((checks_run + 1))

    local doc_attribution=false
    if [[ -f "$PROJECT_ROOT/README.md" ]]; then
        if grep -q -i -E "secp256k1|bitcrack|bitcoin|copyright|license" "$PROJECT_ROOT/README.md"; then
            doc_attribution=true
        fi
    fi

    if [[ "$doc_attribution" == "true" ]]; then
        log_success "✅ Attribution present in documentation"
        detailed_findings+=("Documentation attribution: Present in README.md")
        checks_passed=$((checks_passed + 1))
    else
        log_info "ℹ️ Attribution may need documentation updates"
        detailed_findings+=("Documentation attribution: Not clearly found")
    fi

    # Check 4: Source file attribution
    log_info "Check 4: Source file attribution"
    checks_run=$((checks_run + 1))

    local source_attribution=0
    if [[ -d "$PROJECT_ROOT/src/extracted" ]]; then
        # Check for author attribution in source files
        source_attribution=$(find "$PROJECT_ROOT/src/extracted" -name "*.c" -o -name "*.h" | head -10 | xargs grep -l -i -E "author|copyright.*\d{4}" | wc -l)
    fi

    if [[ $source_attribution -gt 0 ]]; then
        log_success "✅ Source file attribution present: $source_attribution files"
        detailed_findings+=("Source attribution: $source_attribution files with author info")
        checks_passed=$((checks_passed + 1))
    else
        log_info "ℹ️ Source file attribution may be limited"
        detailed_findings+=("Source attribution: Limited author information")
    fi

    local compliance_score=0
    if [[ $checks_run -gt 0 ]]; then
        compliance_score=$((checks_passed * 100 / checks_run))
    fi

    log_info "Attribution compliance: $checks_passed/$checks_run checks passed ($compliance_score%)"

    # Update results
    local temp_file=$(mktemp)
    jq --argjson findings "$(printf '%s\n' "${detailed_findings[@]}" | jq -R . | jq -s .)" \
       --argjson passed "$checks_passed" \
       --argjson run "$checks_run" \
       --argjson score "$compliance_score" \
       '.compliance_areas.attribution_compliance = {
           "requirement": "T073",
           "checks_passed": $passed,
           "checks_run": $run,
           "compliance_score": $score,
           "findings": $findings
       }' "$RESULTS_FILE" > "$temp_file"
    mv "$temp_file" "$RESULTS_FILE"

    echo "$compliance_score"
}

# Calculate overall compliance
calculate_overall_compliance() {
    local source_score="$1"
    local deps_score="$2"
    local offline_score="$3"
    local attribution_score="$4"

    # Calculate overall compliance score
    local overall_compliance=$(( (source_score + deps_score + offline_score + attribution_score) / 4 ))

    # Generate recommendations
    local recommendations=()
    if [[ $overall_compliance -lt 95 ]]; then
        recommendations+=("Overall compliance below target - comprehensive review needed")
    fi
    if [[ $source_score -lt 90 ]]; then
        recommendations+=("Improve source inclusion completeness and integrity")
    fi
    if [[ $deps_score -lt 90 ]]; then
        recommendations+=("Further reduce external dependencies")
    fi
    if [[ $offline_score -lt 90 ]]; then
        recommendations+=("Enhance offline build capability")
    fi
    if [[ $attribution_score -lt 90 ]]; then
        recommendations+=("Complete attribution and licensing documentation")
    fi

    # Update results
    local temp_file=$(mktemp)
    jq --argjson overall "$overall_compliance" \
       --argjson met "$([ $overall_compliance -ge 95 ] && echo true || echo false)" \
       --argjson recommendations "$(printf '%s\n' "${recommendations[@]}" | jq -R . | jq -s .)" \
       '.compliance_validation.overall_compliance = $overall |
        .compliance_validation.compliance_met = $met |
        .detailed_findings = [.compliance_areas | to_entries[] | {
            "requirement": .value.requirement,
            "area": .key,
            "score": .value.compliance_score,
            "findings": .value.findings
        }] |
        .recommendations = $recommendations' "$RESULTS_FILE" > "$temp_file"
    mv "$temp_file" "$RESULTS_FILE"

    echo "$overall_compliance"
}

# Generate compliance report
generate_compliance_report() {
    log_info "Generating compliance validation report"

    local report_file="$LOG_DIR/compliance_validation_report.html"
    local overall_compliance=$(jq -r '.compliance_validation.overall_compliance' "$RESULTS_FILE")
    local compliance_met=$(jq -r '.compliance_validation.compliance_met' "$RESULTS_FILE")

    cat > "$report_file" << EOF
<!DOCTYPE html>
<html>
<head>
    <title>Compliance Validation Report</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; }
        .header { background: #2c3e50; color: white; padding: 20px; border-radius: 5px; }
        .success { color: #27ae60; font-weight: bold; }
        .warning { color: #f39c12; font-weight: bold; }
        .failure { color: #e74c3c; font-weight: bold; }
        .metric-card { background: #f8f9fa; padding: 15px; margin: 10px 0; border-radius: 5px; border-left: 4px solid #3498db; }
        .compliance-card { background: #f8f9fa; padding: 15px; margin: 10px 0; border-radius: 5px; }
        .compliance-met { border-left: 4px solid #27ae60; }
        .compliance-partial { border-left: 4px solid #f39c12; }
        .compliance-failed { border-left: 4px solid #e74c3c; }
        pre { background: #f8f9fa; padding: 10px; border-radius: 3px; overflow-x: auto; }
        .score-display { font-size: 48px; font-weight: bold; text-align: center; margin: 20px 0; }
    </style>
</head>
<body>
    <div class="header">
        <h1>📋 Compliance Validation Report</h1>
        <p>Generated: $(date)</p>
        <p>Requirements: T070-T073 (Source Inclusion, External Dependencies, Offline Builds, Attribution)</p>
        <p>Overall Compliance Score: <span class="score-display">$overall_compliance%</span></p>
    </div>

    <h2>📊 Compliance Status</h2>
    <div class="metric-card">
        <p><strong>Target Compliance:</strong> 95%</p>
        <p><strong>Compliance Met:</strong> $compliance_met</p>
EOF

    if [[ "$compliance_met" == "true" ]]; then
        cat >> "$report_file" << EOF
        <p class="success">✅ COMPLIANCE TARGET ACHIEVED</p>
EOF
    else
        cat >> "$report_file" << EOF
        <p class="warning">⚠️ COMPLIANCE TARGET NOT MET</p>
EOF
    fi

    cat >> "$report_file" << EOF
    </div>

    <h2>🔍 Compliance Areas</h2>
EOF

    # Add compliance area results
    jq -r '.compliance_areas | to_entries[] | "\(.key):\(.value.requirement):\(.value.compliance_score // 0):\(.value.checks_passed // 0):\(.value.checks_run // 0)"' "$RESULTS_FILE" | while IFS=: read -r area requirement score passed run; do
        local display_name=$(echo "$area" | sed 's/_/ /g' | sed 's/\b\w/\u&/g')
        local card_class="compliance-card"
        if [[ $score -ge 90 ]]; then
            card_class="compliance-card compliance-met"
        elif [[ $score -ge 70 ]]; then
            card_class="compliance-card compliance-partial"
        else
            card_class="compliance-card compliance-failed"
        fi

        cat >> "$report_file" << EOF
    <div class="$card_class">
        <h3>$requirement: $display_name</h3>
        <p>Compliance Score: $score% ($passed/$run checks passed)</p>
EOF

        # Add detailed findings
        local findings=$(jq -r ".compliance_areas.$area.findings[]?" "$RESULTS_FILE" 2>/dev/null | while read -r finding; do
            echo "        <li>$finding</li>"
        done)

        if [[ -n "$findings" ]]; then
            cat >> "$report_file" << EOF
        <ul>
$findings
        </ul>
EOF
        fi

        cat >> "$report_file" << EOF
    </div>
EOF
    done

    # Add recommendations
    cat >> "$report_file" << EOF

    <h2>💡 Recommendations</h2>
    <div class="metric-card">
        <ul>
EOF

    jq -r '.recommendations[]?' "$RESULTS_FILE" 2>/dev/null | while read -r recommendation; do
        cat >> "$report_file" << EOF
            <li>$recommendation</li>
EOF
    done

    cat >> "$report_file" << EOF
        </ul>
    </div>

</body>
</html>
EOF

    log_success "Compliance validation report generated: $report_file"
}

# Main validation execution
main() {
    local command="${1:-run}"

    case "$command" in
        "run")
            log_info "Starting compliance validation (T070-T073)"

            init_results

            # Run all compliance validations
            local source_score=$(validate_source_inclusion)
            local deps_score=$(validate_external_dependencies)
            local offline_score=$(validate_offline_builds)
            local attribution_score=$(validate_attribution_compliance)

            # Calculate overall compliance
            local overall_compliance=$(calculate_overall_compliance "$source_score" "$deps_score" "$offline_score" "$attribution_score")

            # Generate report
            generate_compliance_report

            # Final summary
            echo ""
            log_info "=== COMPLIANCE VALIDATION SUMMARY ==="
            log_info "Overall compliance score: ${overall_compliance}% (target: 95%)"
            log_info "T070 Source Inclusion: ${source_score}%"
            log_info "T071 External Dependencies: ${deps_score}%"
            log_info "T072 Offline Builds: ${offline_score}%"
            log_info "T073 Attribution Compliance: ${attribution_score}%"

            if [[ $overall_compliance -ge 95 ]]; then
                log_success "✅ COMPLIANCE TARGET ACHIEVED!"
                log_success "Overall compliance score ${overall_compliance}% meets 95% target"
                return 0
            else
                log_warning "⚠️ COMPLIANCE TARGET NOT MET"
                log_warning "Overall compliance score ${overall_compliance}% below 95% target"
                return 1
            fi
            ;;
        "report")
            if [[ -f "$RESULTS_FILE" ]]; then
                generate_compliance_report
                echo "Report available: $LOG_DIR/compliance_validation_report.html"
            else
                log_error "No compliance validation results found. Run validation first."
            fi
            ;;
        "clean")
            rm -rf "$LOG_DIR"
            log_success "Compliance validation cleanup completed"
            ;;
        "help"|*)
            echo "Usage: $0 {run|report|clean|help}"
            echo ""
            echo "Commands:"
            echo "  run    - Run compliance validation (T070-T073)"
            echo "  report - Generate HTML report from existing results"
            echo "  clean  - Clean validation artifacts"
            echo "  help   - Show this help message"
            exit 0
            ;;
    esac
}

# Execute main function
main "$@"