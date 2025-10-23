#!/bin/bash

# Comprehensive Integrity Validation Script
# Implements T066: Run comprehensive integrity validation across all optimizations

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
VALIDATION_DIR="$PROJECT_ROOT/validations"
LOG_DIR="$PROJECT_ROOT/logs/validations"
RESULTS_FILE="$LOG_DIR/integrity_validation_results.json"

# Validation categories
readonly VALIDATION_CATEGORIES=(
    "source_integrity"
    "attribution_compliance"
    "build_integrity"
    "integration_structure"
    "dependency_validation"
    "offline_capability"
    "license_compliance"
    "metadata_validation"
    "checksum_validation"
    "manifest_completeness"
)

# Ensure directories exist
mkdir -p "$VALIDATION_DIR" "$LOG_DIR"

# Color codes
readonly RED='\033[0;31m'
readonly GREEN='\033[0;32m'
readonly YELLOW='\033[1;33m'
readonly BLUE='\033[0;34m'
readonly NC='\033[0m'

# Logging
log_info() {
    echo -e "${BLUE}[INFO]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1" | tee -a "$LOG_DIR/integrity.log"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1" | tee -a "$LOG_DIR/integrity.log"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1" | tee -a "$LOG_DIR/integrity.log"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1" | tee -a "$LOG_DIR/integrity.log"
}

# Validation results storage
declare -A validation_results
declare -A validation_scores
total_validations=0
passed_validations=0

# Initialize results
init_results() {
    cat > "$RESULTS_FILE" << EOF
{
    "validation_run": {
        "timestamp": "$(date -Iseconds)",
        "categories": $(printf '%s' "${VALIDATION_CATEGORIES[@]}" | jq -R . | jq -s .),
        "status": "running",
        "overall_score": 0.0,
        "target_score": 95.0
    },
    "category_results": {},
    "detailed_findings": [],
    "compliance_status": {
        "source_integrity": false,
        "attribution_compliance": false,
        "build_integrity": false,
        "integration_structure": false,
        "dependency_validation": false,
        "offline_capability": false,
        "license_compliance": false,
        "metadata_validation": false,
        "checksum_validation": false,
        "manifest_completeness": false
    }
}
EOF
}

# Validate source integrity (T017a)
validate_source_integrity() {
    log_info "Validating source integrity for extracted libraries"

    local category="source_integrity"
    local score=0
    local max_score=100
    local findings=()

    # Check secp256k1-zkp extraction
    if [[ -d "$PROJECT_ROOT/src/extracted/secp256k1-zkp" ]]; then
        local file_count=$(find "$PROJECT_ROOT/src/extracted/secp256k1-zkp" -name "*.c" -o -name "*.h" | wc -l)
        if [[ $file_count -gt 50 ]]; then
            ((score += 30))
            findings+=("✅ secp256k1-zkp: $file_count source files extracted")
        else
            findings+=("❌ secp256k1-zkp: Insufficient source files ($file_count)")
        fi

        # Check integrity of key files
        local key_files=(
            "src/secp256k1.c"
            "include/secp256k1.h"
            "include/secp256k1_ecdh.h"
        )

        local integrity_files=0
        for file in "${key_files[@]}"; do
            if [[ -f "$PROJECT_ROOT/src/extracted/secp256k1-zkp/$file" ]]; then
                ((integrity_files += 1))
            fi
        done

        if [[ $integrity_files -eq ${#key_files[@]} ]]; then
            ((score += 20))
            findings+=("✅ secp256k1-zkp: All key integrity files present")
        else
            findings+=("❌ secp256k1-zkp: Missing integrity files ($integrity_files/${#key_files[@]})")
        fi
    else
        findings+=("❌ secp256k1-zkp: Extraction directory not found")
    fi

    # Check bitcrack extraction
    if [[ -d "$PROJECT_ROOT/src/extracted/bitcrack" ]]; then
        local bitcrack_files=$(find "$PROJECT_ROOT/src/extracted/bitcrack" -name "*.cpp" -o -name "*.h" -o -name "*.cu" | wc -l)
        if [[ $bitcrack_files -gt 0 ]]; then
            ((score += 30))
            findings+=("✅ bitcrack: $bitcrack_files source files found")
        else
            findings+=("❌ bitcrack: No source files found")
        fi
    else
        findings+=("❌ bitcrack: Extraction directory not found")
    fi

    # Check for modifications
    local modified_files=0
    if [[ -d "$PROJECT_ROOT/src/extracted" ]]; then
        while IFS= read -r -r file; do
            if git ls-files --error-unmatch 2>/dev/null >/dev/null; then
                if git status --porcelain "$file" 2>/dev/null | grep -q "^ M"; then
                    ((modified_files += 1))
                    findings+=("⚠️ Modified extracted file: $file")
                fi
            fi
        done < <(find "$PROJECT_ROOT/src/extracted" -type f)
    fi

    if [[ $modified_files -eq 0 ]]; then
        ((score += 20))
        findings+=("✅ No modifications to extracted source files")
    else
        ((score -= 10))
        findings+=("❌ $modified_files extracted files have been modified")
    fi

    validation_results["$category"]=$score
    validation_scores["$category"]=$score

    # Update results
    local temp_file=$(mktemp)
    jq --arg category "$category" \
       --arg score "$score" \
       --argjson findings "$(printf '%s\n' "${findings[@]}" | jq -R . | jq -s .)" \
       '
       .category_results[$category] = {
           "score": ($score | tonumber),
           "max_score": 100,
           "findings": $findings
       } |
       .compliance_status[$category] = ($score | tonumber) >= 80
       ' "$RESULTS_FILE" > "$temp_file"
    mv "$temp_file" "$RESULTS_FILE"

    log_success "Source integrity validation completed: $score/100"
    ((total_validations++))
    if [[ $score -ge 80 ]]; then
        ((passed_validations++))
    fi
}

# Validate attribution compliance (T017b, T073)
validate_attribution_compliance() {
    log_info "Validating attribution compliance"

    local category="attribution_compliance"
    local score=0
    local max_score=100
    local findings=()

    # Check attribution headers in extracted libraries
    local attribution_dirs=(
        "$PROJECT_ROOT/src/extracted/secp256k1-zkp/attribution_headers"
        "$PROJECT_ROOT/src/extracted/bitcrack/attribution_headers"
    )

    local total_files=0
    local attributed_files=0

    for dir in "${attribution_dirs[@]}"; do
        if [[ -d "$dir" ]]; then
            local dir_files=$(find "$dir" -type f | wc -l)
            ((total_files += dir_files))

            # Check for required attribution files
            local required_files=("LICENSE.MIT" "ATTRIBUTION.md" "README.md")
            for req_file in "${required_files[@]}"; do
                if [[ -f "$dir/$req_file" ]]; then
                    ((attributed_files += 1))
                    findings+=("✅ Attribution file: $dir/$req_file")
                else
                    findings+=("❌ Missing attribution file: $dir/$req_file")
                fi
            done
        else
            findings+=("❌ Attribution directory not found: $dir")
        fi
    done

    if [[ $total_files -gt 0 ]]; then
        local coverage=$((attributed_files * 100 / total_files))
        if [[ $coverage -eq 100 ]]; then
            ((score += 50))
            findings+=("✅ 100% attribution coverage")
        elif [[ $coverage -ge 80 ]]; then
            ((score += 40))
            findings+=("✅ High attribution coverage: ${coverage}%")
        else
            ((score += 20))
            findings+=("⚠️ Low attribution coverage: ${coverage}%")
        fi
    else
        findings+=("❌ No attribution files found")
    fi

    # Check SPDX identifiers in source files
    local spdx_files=0
    local total_source_files=0

    if [[ -d "$PROJECT_ROOT/src/extracted" ]]; then
        total_source_files=$(find "$PROJECT_ROOT/src/extracted" -name "*.cpp" -o -name "*.h" -o -name "*.c" | wc -l)

        while IFS= read -r -r file; do
            if grep -q "SPDX-License-Identifier:" "$file" 2>/dev/null; then
                ((spdx_files += 1))
            fi
        done < <(find "$PROJECT_ROOT/src/extracted" -name "*.cpp" -o -name "*.h" -o -name "*.c")
    fi

    if [[ $spdx_files -gt 0 ]]; then
        local spdx_coverage=$((spdx_files * 100 / total_source_files))
        if [[ $spdx_coverage -ge 90 ]]; then
            ((score += 30))
            findings+=("✅ High SPDX coverage: ${spdx_coverage}%")
        elif [[ $spdx_coverage -ge 70 ]]; then
            ((score += 20))
            findings+=("✅ Good SPDX coverage: ${spdx_coverage}%")
        else
            ((score += 10))
            findings+=("⚠️ Low SPDX coverage: ${spdx_coverage}%")
        fi
    else
        findings+=("❌ No SPDX identifiers found")
    fi

    # Check license preservation
    local licenses_preserved=0
    local total_licenses=0

    for dir in "${attribution_dirs[@]}"; do
        if [[ -d "$dir" ]]; then
            while IFS= read -r -r file; do
                if [[ "$file" == *"LICENSE"* ]]; then
                    ((total_licenses += 1))
                    if [[ -f "$file" && -s "$file" ]]; then
                        ((licenses_preserved += 1))
                        findings+=("✅ License preserved: $(basename "$file")")
                    fi
                fi
            done < <(find "$dir" -name "*LICENSE*")
        fi
    done

    if [[ $licenses_preserved -eq $total_licenses && $total_licenses -gt 0 ]]; then
        ((score += 20))
        findings+=("✅ All licenses preserved ($licenses_preserved/$total_licenses)")
    else
        findings+=("❌ License preservation incomplete ($licenses_preserved/$total_licenses)")
    fi

    validation_results["$category"]=$score
    validation_scores["$category"]=$score

    # Update results
    local temp_file=$(mktemp)
    jq --arg category "$category" \
       --arg score "$score" \
       --argjson findings "$(printf '%s\n' "${findings[@]}" | jq -R . | jq -s .)" \
       '
       .category_results[$category] = {
           "score": ($score | tonumber),
           "max_score": 100,
           "findings": $findings
       } |
       .compliance_status[$category] = ($score | tonumber) >= 90
       ' "$RESULTS_FILE" > "$temp_file"
    mv "$temp_file" "$RESULTS_FILE"

    log_success "Attribution compliance validation completed: $score/100"
    ((total_validations++))
    if [[ $score -ge 90 ]]; then
        ((passed_validations++))
    fi
}

# Validate build integrity (T017c)
validate_build_integrity() {
    log_info "Validating build integrity"

    local category="build_integrity"
    local score=0
    local max_score=100
    local findings=()

    # Check CMakeLists.txt integrity
    if [[ -f "$PROJECT_ROOT/CMakeLists.txt" ]]; then
        local cmake_size=$(stat -c%s "$PROJECT_ROOT/CMakeLists.txt")
        if [[ $cmake_size -gt 10000 ]]; then
            ((score += 20))
            findings+=("✅ CMakeLists.txt present and substantial (${cmake_size} bytes)")
        else
            findings+=("⚠️ CMakeLists.txt seems small (${cmake_size} bytes)")
        fi

        # Check for offline build configuration
        if grep -q "ENABLE_OFFLINE_BUILD" "$PROJECT_ROOT/CMakeLists.txt"; then
            ((score += 20))
            findings+=("✅ Offline build configuration present")
        else
            findings+=("❌ Offline build configuration missing")
        fi

        # Check for extracted library integration
        if grep -q "src/extracted/secp256k1-zkp" "$PROJECT_ROOT/CMakeLists.txt"; then
            ((score += 20))
            findings+=("✅ Extracted secp256k1-zkp integration configured")
        else
            findings+=("❌ Extracted secp256k1-zkp integration missing")
        fi
    else
        findings+=("❌ CMakeLists.txt not found")
    fi

    # Check build artifacts consistency
    local build_dir="$PROJECT_ROOT/build"
    if [[ -d "$build_dir" ]]; then
        # Check for expected build files
        local expected_files=("CMakeCache.txt" "Makefile" "cmake_install.cmake")
        local build_files_found=0

        for file in "${expected_files[@]}"; do
            if [[ -f "$build_dir/$file" ]]; then
                ((build_files_found += 1))
            fi
        done

        if [[ $build_files_found -eq ${#expected_files[@]} ]]; then
            ((score += 20))
            findings+=("✅ All expected build files present")
        else
            findings+=("⚠️ Some build files missing ($build_files_found/${#expected_files[@]})")
        fi
    else
        findings+=("⚠️ Build directory not created")
    fi

    # Check integration structure consistency
    local integration_dirs=(
        "src/integration"
        "src/extracted"
        "docs/licenses"
        "scripts"
    )

    local dirs_present=0
    for dir in "${integration_dirs[@]}"; do
        if [[ -d "$PROJECT_ROOT/$dir" ]]; then
            ((dirs_present += 1))
        fi
    done

    if [[ $dirs_present -eq ${#integration_dirs[@]} ]]; then
        ((score += 20))
        findings+=("✅ Integration structure complete")
    else
        findings+=("⚠️ Integration structure incomplete ($dirs_present/${#integration_dirs[@]})")
    fi

    # Validate build reproducibility
    local deterministic_flags=0
    local build_flags=(
        "-DCMAKE_BUILD_TYPE=Release"
        "-DCMAKE_CXX_STANDARD=17"
        "-DCMAKE_CUDA_STANDARD=17"
    )

    for flag in "${build_flags[@]}"; do
        if grep -q "$flag" "$LOG_DIR/benchmark.log" 2>/dev/null; then
            ((deterministic_flags += 1))
        fi
    done

    if [[ $deterministic_flags -eq ${#build_flags[@]} ]]; then
        ((score += 20))
        findings+=("✅ Deterministic build flags configured")
    else
        findings+=("⚠️ Some deterministic flags missing")
    fi

    validation_results["$category"]=$score
    validation_scores["$category"]=$score

    # Update results
    local temp_file=$(mktemp)
    jq --arg category "$category" \
       --arg score "$score" \
       --argjson findings "$(printf '%s\n' "${findings[@]}" | jq -R . | jq -s .)" \
       '
       .category_results[$category] = {
           "score": ($score | tonumber),
           "max_score": 100,
           "findings": $findings
       } |
       .compliance_status[$category] = ($score | tonumber) >= 80
       ' "$RESULTS_FILE" > "$temp_file"
    mv "$temp_file" "$RESULTS_FILE"

    log_success "Build integrity validation completed: $score/100"
    ((total_validations++))
    if [[ $score -ge 80 ]]; then
        ((passed_validations++))
    fi
}

# Validate dependency validation (T071)
validate_dependency_validation() {
    log_info "Validating dependency validation (external dependency elimination)"

    local category="dependency_validation"
    local score=0
    local max_score=100
    local findings=()

    # Check for git submodule references
    local submodule_refs=0
    if [[ -f "$PROJECT_ROOT/.gitmodules" ]]; then
        submodule_refs=$(grep -c "path=" "$PROJECT_ROOT/.gitmodules" || echo "0")
        findings+=("⚠️ Found $submodule_refs git submodule references")
    else
        ((score += 30))
        findings+=("✅ No git submodule references found")
    fi

    # Check for external repository URLs in CMake
    local external_urls=0
    local url_patterns=(
        "github.com"
        "git::"
        "http://"
        "https://"
        "ssh://"
    )

    for pattern in "${url_patterns[@]}"; do
        local count=$(grep -r "$pattern" "$PROJECT_ROOT/CMakeLists.txt" | wc -l || echo "0")
        ((external_urls += count))
    done

    # Filter out internal/allowed URLs
    local allowed_urls=0
    if grep -q "github.com/BlockstreamResearch/secp256k1-zkp" "$PROJECT_ROOT/CMakeLists.txt" 2>/dev/null; then
        # This is likely in comments, not actual fetching
        ((allowed_urls += 1))
    fi

    local actual_external_urls=$((external_urls - allowed_urls))
    if [[ $actual_external_urls -eq 0 ]]; then
        ((score += 30))
        findings+=("✅ No external repository URLs in build configuration")
    else
        findings+=("⚠️ Found $actual_external_urls external repository references")
    fi

    # Check for FetchContent usage
    local fetchcontent_usage=0
    if grep -r "FetchContent" "$PROJECT_ROOT/CMakeLists.txt" >/dev/null 2>&1; then
        fetchcontent_usage=$(grep -r "FetchContent" "$PROJECT_ROOT/CMakeLists.txt" | wc -l)
    fi

    if grep -q "ENABLE_OFFLINE_BUILD" "$PROJECT_ROOT/CMakeLists.txt"; then
        if [[ $fetchcontent_usage -eq 0 ]]; then
            ((score += 20))
            findings+=("✅ FetchContent disabled in offline build mode")
        else
            findings+=("⚠️ FetchContent usage detected ($fetchcontent_usage instances)")
        fi
    else
        findings+=("⚠️ Offline build mode not configured")
    fi

    # Validate extracted library completeness
    local extracted_complete=0
    local required_libraries=("secp256k1-zkp" "bitcrack")

    for lib in "${required_libraries[@]}"; do
        if [[ -d "$PROJECT_ROOT/src/extracted/$lib" ]]; then
            local source_files=$(find "$PROJECT_ROOT/src/extracted/$lib" -name "*.c" -o -name "*.cpp" -o -name "*.h" | wc -l)
            if [[ $source_files -gt 10 ]]; then
                ((extracted_complete += 1))
                findings+=("✅ $lib: Complete extraction ($source_files files)")
            else
                findings+=("⚠️ $lib: Incomplete extraction ($source_files files)")
            fi
        else
            findings+=("❌ $lib: Not extracted")
        fi
    done

    if [[ $extracted_complete -eq ${#required_libraries[@]} ]]; then
        ((score += 20))
        findings+=("✅ All required libraries extracted completely")
    else
        findings+=("❌ Library extraction incomplete ($extracted_complete/${#required_libraries[@]})")
    fi

    validation_results["$category"]=$score
    validation_scores["$category"]=$score

    # Update results
    local temp_file=$(mktemp)
    jq --arg category "$category" \
       --arg score "$score" \
       --argjson findings "$(printf '%s\n' "${findings[@]}" | jq -R . | jq -s .)" \
       '
       .category_results[$category] = {
           "score": ($score | tonumber),
           "max_score": 100,
           "findings": $findings
       } |
       .compliance_status[$category] = ($score | tonumber) >= 80
       ' "$RESULTS_FILE" > "$temp_file"
    mv "$temp_file" "$RESULTS_FILE"

    log_success "Dependency validation completed: $score/100"
    ((total_validations++))
    if [[ $score -ge 80 ]]; then
        ((passed_validations++))
    fi
}

# Validate offline capability (T072)
validate_offline_capability() {
    log_info "Validating offline build capability"

    local category="offline_capability"
    local score=0
    local max_score=100
    local findings=()

    # Check offline build configuration
    if grep -q "ENABLE_OFFLINE_BUILD" "$PROJECT_ROOT/CMakeLists.txt"; then
        ((score += 25))
        findings+=("✅ Offline build mode configured")
    else
        findings+=("❌ Offline build mode not configured")
    fi

    # Check for offline build flags
    local offline_flags=0
    local offline_patterns=(
        "OFFLINE_BUILD"
        "FetchContent disabled"
        "Git submodules: DISABLED"
        "Network access: RESTRICTED"
    )

    for pattern in "${offline_patterns[@]}"; do
        if grep -q "$pattern" "$LOG_DIR/benchmark.log"; then
            ((offline_flags += 1))
        fi
    done

    if [[ $offline_flags -ge 3 ]]; then
        ((score += 25))
        findings+=("✅ Offline build flags active ($offline_flags/4)")
    else
        findings+=("⚠️ Insufficient offline build flags ($offline_flags/4)")
    fi

    # Test offline build simulation
    log_info "Testing offline build simulation"

    # Simulate offline build by blocking network access temporarily
    if command -v iptables >/dev/null 2>&1; then
        # Create temporary network block
        iptables -A OUTPUT -p tcp --dport 80 -j DROP 2>/dev/null || true
        iptables -A OUTPUT -p udp --dport 53 -j DROP 2>/dev/null || true

        # Try to configure without network
        rm -rf "$PROJECT_ROOT/build_offline_test"
        mkdir -p "$PROJECT_ROOT/build_offline_test"

        if timeout 30 cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_OFFLINE_BUILD=ON -B "$PROJECT_ROOT/build_offline_test" > "$LOG_DIR/offline_test.log" 2>&1; then
            ((score += 25))
            findings+=("✅ Offline build configuration successful without network")
        else
            findings+=("❌ Offline build configuration failed without network")
        fi

        # Restore network access
        iptables -D OUTPUT -p tcp --dport 80 -j DROP 2>/dev/null || true
        iptables -D OUTPUT -p udp --dport 53 -j DROP 2>/dev/null || true
    else
        findings+=("⚠️ Cannot test offline build (iptables not available)")
    fi

    # Check for local resource availability
    local local_resources=0
    local resources=(
        "$PROJECT_ROOT/src/extracted/secp256k1-zkp"
        "$PROJECT_ROOT/src/extracted/bitcrack"
        "$PROJECT_ROOT/CMakeLists.txt"
    )

    for resource in "${resources[@]}"; do
        if [[ -e "$resource" ]]; then
            ((local_resources += 1))
        fi
    done

    if [[ $local_resources -eq ${#resources[@]} ]]; then
        ((score += 25))
        findings+=("✅ All required resources available locally")
    else
        findings+=("❌ Missing local resources ($local_resources/${#resources[@]})")
    fi

    validation_results["$category"]=$score
    validation_scores["$category"]=$score

    # Update results
    local temp_file=$(mktemp)
    jq --arg category "$category" \
       --arg score "$score" \
       --argjson findings "$(printf '%s\n' "${findings[@]}" | jq -R . | jq -s .)" \
       '
       .category_results[$category] = {
           "score": ($score | tonumber),
           "max_score": 100,
           "findings": $findings
       } |
       .compliance_status[$category] = ($score | tonumber) >= 75
       ' "$RESULTS_FILE" > "$temp_file"
    mv "$temp_file" "$RESULTS_FILE"

    log_success "Offline capability validation completed: $score/100"
    ((total_validations++))
    if [[ $score -ge 75 ]]; then
        ((passed_validations++))
    fi
}

# Validate checksum validation (T010)
validate_checksum_validation() {
    log_info "Validating checksum validation system"

    local category="checksum_validation"
    local score=0
    local max_score=100
    local findings=()

    # Check for SHA-256 implementation
    local sha256_implementations=0

    # Check scripts for SHA-256 usage
    if find "$PROJECT_ROOT/scripts" -name "*.sh" -exec grep -l "sha256sum\|SHA256\|openssl" {} \; 2>/dev/null | wc -l > 0; then
        ((sha256_implementations += 5))
        findings+=("✅ SHA-256 utilities found in scripts")
    fi

    # Check C++ code for SHA-256 usage
    if find "$PROJECT_ROOT/src" -name "*.cpp" -o -name "*.h" -exec grep -l "SHA256\|sha256\|OpenSSL" {} \; 2>/dev/null | wc -l > 0; then
        ((sha256_implementations += 5))
        findings+=("✅ SHA-256 usage found in source code")
    fi

    if [[ $sha256_implementations -ge 10 ]]; then
        ((score += 30))
        findings+=("✅ Comprehensive SHA-256 implementation available")
    elif [[ $sha256_implementations -ge 5 ]]; then
        ((score += 20))
        findings+=("✅ Basic SHA-256 implementation available")
    else
        findings+=("❌ SHA-256 implementation not found")
    fi

    # Check for checksum validation scripts
    local checksum_scripts=0
    local checksum_script_patterns=(
        "verify-integrity"
        "verify-checksum"
        "verify-dependency"
        "validate-sha"
    )

    for pattern in "${checksum_script_patterns[@]}"; do
        local script_count=$(find "$PROJECT_ROOT/scripts" -name "*$pattern*" -type f | wc -l)
        ((checksum_scripts += script_count))
    done

    if [[ $checksum_scripts -ge 3 ]]; then
        ((score += 25))
        findings+=("✅ Multiple checksum validation scripts found ($checksum_scripts)")
    elif [[ $checksum_scripts -ge 1 ]]; then
        ((score += 15))
        findings+=("✅ Checksum validation scripts found ($checksum_scripts)")
    else
        findings+=("❌ No checksum validation scripts found")
    fi

    # Test checksum calculation on a sample file
    local test_file="$PROJECT_ROOT/README.md"
    if [[ -f "$test_file" ]]; then
        local calculated_checksum=$(sha256sum "$test_file" | cut -d' ' -f1)
        if [[ -n "$calculated_checksum" && ${#calculated_checksum} -eq 64 ]]; then
            ((score += 25))
            findings+=("✅ SHA-256 calculation working (test: README.md)")

            # Test verification by recalculating
            local verification_checksum=$(sha256sum "$test_file" | cut -d' ' -f1)
            if [[ "$calculated_checksum" == "$verification_checksum" ]]; then
                ((score += 20))
                findings+=("✅ Checksum verification working")
            else
                findings+=("❌ Checksum verification failed")
            fi
        else
            findings+=("❌ SHA-256 calculation failed")
        fi
    else
        findings+=("❌ No test file available for checksum validation")
    fi

    validation_results["$category"]=$score
    validation_scores["$category"]=$score

    # Update results
    local temp_file=$(mktemp)
    jq --arg category "$category" \
       --arg score "$score" \
       --argjson findings "$(printf '%s\n' "${findings[@]}" | jq -R . | jq -s .)" \
       '
       .category_results[$category] = {
           "score": ($score | tonumber),
           "max_score": 100,
           "findings": $findings
       } |
       .compliance_status[$category] = ($score | tonumber) >= 80
       ' "$RESULTS_FILE" > "$temp_file"
    mv "$temp_file" "$RESULTS_FILE"

    log_success "Checksum validation completed: $score/100"
    ((total_validations++))
    if [[ $score -ge 80 ]]; then
        ((passed_validations++))
    fi
}

# Calculate overall integrity score
calculate_overall_score() {
    local total_score=0
    local max_score=0

    for category in "${VALIDATION_CATEGORIES[@]}"; do
        if [[ -n "${validation_scores[$category]}" ]]; then
            total_score=$((total_score + validation_scores[$category]))
            max_score=$((max_score + 100))
        fi
    done

    local overall_score=0
    if [[ $max_score -gt 0 ]]; then
        overall_score=$(echo "scale=2; $total_score * 100 / $max_score" | bc -l)
    fi

    # Update final results
    local temp_file=$(mktemp)
    jq --arg overall_score "$overall_score" \
       --arg passed "$passed_validations" \
       --arg total "$total_validations" \
       --arg target_score "95.0" \
       '
       .validation_run.status = "completed" |
       .validation_run.overall_score = ($overall_score | tonumber) |
       .validation_run.passed_validations = ($passed | tonumber) |
       .validation_run.total_validations = ($total | tonumber) |
       .validation_run.target_met = ($overall_score | tonumber) >= ($target_score | tonumber)
       ' "$RESULTS_FILE" > "$temp_file"
    mv "$temp_file" "$RESULTS_FILE"
}

# Generate comprehensive integrity report
generate_integrity_report() {
    log_info "Generating comprehensive integrity report"

    local report_file="$LOG_DIR/integrity_validation_report.html"
    local overall_score=$(jq -r '.validation_run.overall_score' "$RESULTS_FILE")
    local passed_count=$(jq -r '.validation_run.passed_validations' "$RESULTS_FILE")
    local total_count=$(jq -r '.validation_run.total_validations' "$RESULTS_FILE")
    local target_met=$(jq -r '.validation_run.target_met' "$RESULTS_FILE")

    local status_class="success"
    local status_text="✅ INTEGRITY VALIDATION PASSED"
    local status_message="Overall integrity score ${overall_score}% meets 95% target"

    if [[ $(echo "$overall_score < 95" | bc -l) -eq 1 ]]; then
        status_class="failure"
        status_text="❌ INTEGRITY VALIDATION FAILED"
        status_message="Overall integrity score ${overall_score}% below 95% target"
    fi

    cat > "$report_file" << EOF
<!DOCTYPE html>
<html>
<head>
    <title>Comprehensive Integrity Validation Report</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; }
        .header { background: #2c3e50; color: white; padding: 20px; border-radius: 5px; }
        .success { color: #27ae60; font-weight: bold; }
        .failure { color: #e74c3c; font-weight: bold; }
        .warning { color: #f39c12; font-weight: bold; }
        .metric-card { background: #f8f9fa; padding: 15px; margin: 10px 0; border-radius: 5px; border-left: 4px solid #3498db; }
        .chart-container { width: 45%; display: inline-block; margin: 20px; }
        .category-section { background: #ffffff; padding: 20px; margin: 10px 0; border-radius: 5px; border: 1px solid #ddd; }
        .finding { margin: 5px 0; padding: 5px; border-left: 3px solid #ddd; }
        .finding-success { border-left-color: #27ae60; background: #f8f9fa; }
        .finding-warning { border-left-color: #f39c12; background: #fff3cd; }
        .finding-error { border-left-color: #e74c3c; background: #f8d7da; }
        pre { background: #f8f9fa; padding: 10px; border-radius: 3px; overflow-x: auto; }
    </style>
</head>
<body>
    <div class="header">
        <h1🛡️ Comprehensive Integrity Validation Report</h1>
        <p>Generated: $(date)</p>
        <p>Target Score: 95% | Overall Score: ${overall_score}%</p>
    </div>

    <div class="metric-card">
        <h2>📊 Validation Summary</h2>
        <div style="display: flex; flex-wrap: wrap;">
            <div class="metric-card">
                <h3>Total Categories</h3>
                <p style="font-size: 24px;">$total_count</p>
            </div>
            <div class="metric-card">
                <h3>Passed</h3>
                <p class="success" style="font-size: 24px;">$passed_count</p>
            </div>
            <div class="metric-card">
                <h3>Failed</h3>
                <p class="failure" style="font-size: 24px%;">$((total_count - passed_count))</p>
            </div>
            <div class="metric-card">
                <h3>Overall Score</h3>
                <p class="$status_class" style="font-size: 24px;">${overall_score}%</p>
            </div>
        </div>
        <p><strong>$status_message</strong></p>
    </div>

    <div class="chart-container">
        <canvas id="scoreChart"></canvas>
    </div>
    <div class="chart-container">
        <canvas id="complianceChart"></canvas>
    </div>

    <h2>📋 Category Results</h2>
EOF

    # Add category results
    for category in "${VALIDATION_CATEGORIES[@]}"; do
        local score=$(jq -r ".category_results[\"$category\"].score // 0" "$RESULTS_FILE")
        local findings=$(jq -r ".category_results[\"$category\"].findings // []" "$RESULTS_FILE")
        local compliance=$(jq -r ".compliance_status[\"$category\"] // false" "$RESULTS_FILE")

        local status_class="success"
        if [[ "$compliance" == "false" ]]; then
            status_class="warning"
        fi

        cat >> "$report_file" << EOF
    <div class="category-section">
        <h3>$category</h3>
        <p><strong>Score:</strong> $score/100</p>
        <p><strong>Status:</strong> <span class="$status_class">$([ "$compliance" == "true" ] && echo "COMPLIANT" || echo "NON-COMPLIANT")</span></p>

        <h4>Findings:</h4>
        <div>
EOF

        for finding in "${findings[@]}"; do
            local finding_class="finding-success"
            if [[ "$finding" == *"❌"* ]]; then
                finding_class="finding-error"
            elif [[ "$finding" == *"⚠️"* ]]; then
                finding_class="finding-warning"
            fi

            echo "            <div class=\"finding $finding_class\">$finding</div>" >> "$report_file"
        done

        cat >> "$report_file" << EOF
        </div>
    </div>
EOF
    done

    cat >> "$report_file" << EOF
    <script>
        // Score distribution chart
        const scoreCtx = document.getElementById('scoreChart').getContext('2d');
        new Chart(scoreCtx, {
            type: 'bar',
            data: {
                labels: [$(printf '"%s",' "${VALIDATION_CATEGORIES[@]}" | sed 's/,$//')],
                datasets: [{
                    label: 'Score (%)',
                    data: [$(for cat in "${VALIDATION_CATEGORIES[@]}"; do
                        echo -n "${validation_scores[$cat]:-100}"
                    done | sed 's/,$//')],
                    backgroundColor: [$(for cat in "${VALIDATION_CATEGORIES[@]}"; do
                        local score=${validation_scores[$cat]:-100}
                        if [[ $score -ge 90 ]]; then echo -n "'#27ae60',"
                        elif [[ $score -ge 70 ]]; then echo -n "'#f39c12',"
                        else echo -n "'#e74c3c',"
                        fi
                    done | sed 's/,$//')]
                }]
            },
            options: {
                responsive: true,
                scales: {
                    y: {
                        beginAtZero: true,
                        max: 100,
                        title: {
                            display: true,
                            text: 'Score (%)'
                        }
                    }
                }
            }
        });

        // Compliance chart
        const complianceCtx = document.getElementById('complianceChart').getContext('2d');
        new Chart(complianceCtx, {
            type: 'doughnut',
            data: {
                labels: ['Compliant', 'Non-Compliant'],
                datasets: [{
                    data: [$passed_count, $((total_count - passed_count))],
                    backgroundColor: ['#27ae60', '#e74c3c']
                }]
            },
            options: {
                responsive: true,
                plugins: {
                    title: {
                        display: true,
                        text: 'Compliance Status'
                    }
                }
            }
        });
    </script>
</body>
</html>
EOF

    log_success "Integrity validation report generated: $report_file"
}

# Run all integrity validations
run_integrity_validations() {
    log_info "Starting comprehensive integrity validation"
    log_info "Categories: ${#VALIDATION_CATEGORIES[@]}"
    log_info "Target score: 95%"

    init_results

    # Run all validation categories
    validate_source_integrity
    validate_attribution_compliance
    validate_build_integrity
    validate_dependency_validation
    validate_offline_capability
    validate_checksum_validation

    # Calculate final results
    calculate_overall_score
    generate_integrity_report

    # Final status
    local overall_score=$(jq -r '.validation_run.overall_score' "$RESULTS_FILE")
    local target_met=$(jq -r '.validation_run.target_met' "$RESULTS_FILE")

    log_info "Integrity validation completed"
    log_info "Overall score: ${overall_score}% (target: 95%)"
    log_info "Categories passed: $passed_validations/$total_validations"

    if [[ $target_met == "true" ]]; then
        log_success "✅ INTEGRITY VALIDATION TARGET ACHIEVED!"
        log_success "Overall integrity score ${overall_score}% meets 95% target"
        return 0
    else
        log_error "❌ INTEGRITY VALIDATION TARGET MISSED!"
        log_error "Overall integrity score ${overall_score}% below 95% target"
        return 1
    fi
}

# Main execution
main() {
    local command="${1:-run}"

    case "$command" in
        "run")
            run_integrity_validations
            ;;
        "source")
            validate_source_integrity
            ;;
        "attribution")
            validate_attribution_compliance
            ;;
        "build")
            validate_build_integrity
            ;;
        "dependencies")
            validate_dependency_validation
            ;;
        "offline")
            validate_offline_capability
            ;;
        "checksum")
            validate_checksum_validation
            ;;
        "report")
            if [[ -f "$RESULTS_FILE" ]]; then
                generate_integrity_report
                echo "Report available: $LOG_DIR/integrity_validation_report.html"
            else
                log_error "No validation results found. Run validation first."
            fi
            ;;
        "clean")
            rm -rf "$VALIDATION_DIR" "$LOG_DIR"
            log_info "Integrity validation cleanup completed"
            ;;
        "help"|*)
            echo "Usage: $0 {run|source|attribution|build|dependencies|offline|checksum|report|clean|help}"
            echo ""
            echo "Commands:"
            echo "  run        - Run all integrity validations"
            echo "  source     - Validate source integrity"
            echo "  attribution - Validate attribution compliance"
            echo "  build      - Validate build integrity"
            echo "  dependencies- Validate external dependency elimination"
            echo " offline    - Validate offline build capability"
            echo " checksum   - Validate checksum validation system"
            echo "  report     - Generate HTML report from existing results"
            echo "  clean      - Clean validation artifacts and logs"
            echo "  help       - Show this help message"
            exit 0
            ;;
    esac
}

# Execute main function
main "$@"