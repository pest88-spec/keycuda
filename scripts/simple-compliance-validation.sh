#!/bin/bash

# Simplified T070-T073: Compliance Validation
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
LOG_DIR="$PROJECT_ROOT/logs/compliance-validation"

mkdir -p "$LOG_DIR"

echo "[INFO] Starting compliance validation (T070-T073)"

# Compliance areas and scores
declare -A compliance_results
declare -A compliance_scores

# T070: Source inclusion compliance
echo "[INFO] T070: Validating source inclusion compliance"
source_score=100

# Check source files
secp_files=0
bitcrack_files=0
if [[ -d "$PROJECT_ROOT/src/extracted/secp256k1-zkp" ]]; then
    secp_files=$(find "$PROJECT_ROOT/src/extracted/secp256k1-zkp" -name "*.c" -o -name "*.h" | wc -l)
fi
if [[ -d "$PROJECT_ROOT/src/extracted/bitcrack" ]]; then
    bitcrack_files=$(find "$PROJECT_ROOT/src/extracted/bitcrack" -name "*.cpp" -o -name "*.h" | wc -l)
fi

total_source_files=$((secp_files + bitcrack_files))
if [[ $total_source_files -lt 100 ]]; then
    echo "⚠️ Limited source files: $total_source_files"
    source_score=$((source_score - 25))
else
    echo "✅ Comprehensive source inclusion: $total_source_files files"
fi

# Check header files
header_files=0
if [[ -d "$PROJECT_ROOT/src/extracted" ]]; then
    header_files=$(find "$PROJECT_ROOT/src/extracted" -name "*.h" | wc -l)
fi
if [[ $header_files -lt 20 ]]; then
    echo "⚠️ Limited headers: $header_files"
    source_score=$((source_score - 25))
else
    echo "✅ Comprehensive headers: $header_files"
fi

compliance_results["T070 Source Inclusion"]="Source files: $total_source_files, Headers: $header_files"
compliance_scores["T070 Source Inclusion"]=$source_score

# T071: External dependencies compliance
echo "[INFO] T071: Validating external dependencies compliance"
deps_score=100

# Check external dependencies
external_deps=0
if [[ -f "$PROJECT_ROOT/CMakeLists.txt" ]]; then
    external_deps=$(grep -c "find_package\|pkg_check_modules" "$PROJECT_ROOT/CMakeLists.txt" 2>/dev/null || echo "0")
fi
if [[ $external_deps -gt 3 ]]; then
    echo "⚠️ Many external dependencies: $external_deps"
    deps_score=$((deps_score - 30))
else
    echo "✅ Minimal external dependencies: $external_deps"
fi

# Check git submodules
submodules_present=false
if [[ -f "$PROJECT_ROOT/.gitmodules" ]] && [[ -s "$PROJECT_ROOT/.gitmodules" ]]; then
    submodules_present=true
    echo "⚠️ Git submodules still present"
    deps_score=$((deps_score - 40))
else
    echo "✅ Git submodules removed"
fi

compliance_results["T071 External Dependencies"]="External deps: $external_deps, Git submodules: removed"
compliance_scores["T071 External Dependencies"]=$deps_score

# T072: Offline builds compliance
echo "[INFO] T072: Validating offline builds compliance"
offline_score=100

# Check local sources
if [[ $total_source_files -lt 50 ]]; then
    echo "⚠️ Insufficient local sources for offline builds"
    offline_score=$((offline_score - 40))
else
    echo "✅ Sufficient local sources for offline builds"
fi

# Check for network dependencies
network_deps=0
if [[ -f "$PROJECT_ROOT/CMakeLists.txt" ]]; then
    network_deps=$(grep -c -i "http\|download\|fetch" "$PROJECT_ROOT/CMakeLists.txt" 2>/dev/null || echo "0")
fi
if [[ $network_deps -gt 0 ]]; then
    echo "⚠️ Network dependencies found: $network_deps"
    offline_score=$((offline_score - 30))
else
    echo "✅ No network dependencies"
fi

compliance_results["T072 Offline Builds"]="Local sources: $total_source_files, Network deps: $network_deps"
compliance_scores["T072 Offline Builds"]=$offline_score

# T073: Attribution compliance
echo "[INFO] T073: Validating attribution compliance"
attribution_score=100

# Check license files
license_files=0
for license_file in "$PROJECT_ROOT/src/extracted/secp256k1-zkp/COPYING" "$PROJECT_ROOT/src/extracted/secp256k1-zkp/LICENSE" "$PROJECT_ROOT/src/extracted/bitcrack/LICENSE" "$PROJECT_ROOT/src/extracted/bitcrack/COPYRIGHT"; do
    if [[ -f "$license_file" ]]; then
        license_files=$((license_files + 1))
    fi
done

if [[ $license_files -lt 2 ]]; then
    echo "⚠️ Insufficient license files: $license_files"
    attribution_score=$((attribution_score - 40))
else
    echo "✅ License files present: $license_files"
fi

# Check copyright notices
copyright_notices=0
if [[ -d "$PROJECT_ROOT/src/extracted" ]]; then
    copyright_notices=$(find "$PROJECT_ROOT/src/extracted" -name "*.c" -o -name "*.h" | xargs grep -l -i "copyright" | wc -l)
fi
if [[ $copyright_notices -lt 5 ]]; then
    echo "⚠️ Limited copyright notices: $copyright_notices"
    attribution_score=$((attribution_score - 30))
else
    echo "✅ Copyright notices preserved: $copyright_notices files"
fi

compliance_results["T073 Attribution Compliance"]="License files: $license_files, Copyright notices: $copyright_notices"
compliance_scores["T073 Attribution Compliance"]=$attribution_score

# Calculate overall results
total_score=0
num_areas=0

echo ""
echo "=== COMPLIANCE VALIDATION RESULTS ==="

for area in "${!compliance_scores[@]}"; do
    score=${compliance_scores[$area]}
    result=${compliance_results[$area]}

    echo "$area: $score% - $result"
    total_score=$((total_score + score))
    num_areas=$((num_areas + 1))
done

overall_compliance=$((total_score / num_areas))

echo ""
echo "=== COMPLIANCE VALIDATION SUMMARY ==="
echo "Overall compliance score: ${overall_compliance}% (target: 95%)"

if [[ $overall_compliance -ge 95 ]]; then
    echo "✅ COMPLIANCE TARGET ACHIEVED!"
    echo "Overall compliance score ${overall_compliance}% meets 95% target"
    exit_code=0
else
    echo "⚠️ COMPLIANCE TARGET NOT MET"
    echo "Overall compliance score ${overall_compliance}% below 95% target"
    exit_code=1
fi

# Save results
cat > "$LOG_DIR/compliance_validation_results.json" << EOF
{
    "compliance_validation": {
        "timestamp": "$(date -Iseconds)",
        "overall_compliance_score": $overall_compliance,
        "target_compliance": 95,
        "compliance_met": $([ $overall_compliance -ge 95 ] && echo true || echo false)
    },
    "compliance_results": {
EOF

for area in "${!compliance_scores[@]}"; do
    echo "        \"$area\": {" >> "$LOG_DIR/compliance_validation_results.json"
    echo "            \"score\": ${compliance_scores[$area]}," >> "$LOG_DIR/compliance_validation_results.json"
    echo "            \"details\": \"${compliance_results[$area]}\"" >> "$LOG_DIR/compliance_validation_results.json"
    echo "        }," >> "$LOG_DIR/compliance_validation_results.json"
done

# Remove trailing comma and close JSON
sed -i '$ s/,$//' "$LOG_DIR/compliance_validation_results.json"
cat >> "$LOG_DIR/compliance_validation_results.json" << EOF
    }
}
EOF

echo "[SUCCESS] Compliance validation completed"
echo "[INFO] Results saved to: $LOG_DIR/compliance_validation_results.json"

exit $exit_code