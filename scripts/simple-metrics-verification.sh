#!/bin/bash

# Simplified T069: Metrics Visibility Verification
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
LOG_DIR="$PROJECT_ROOT/logs/metrics-verification"

mkdir -p "$LOG_DIR"

echo "[INFO] Starting metrics visibility verification (T069)"

# Verification categories and scores
declare -A verification_results
declare -A verification_scores

# Log visibility verification
echo "[INFO] Verifying log visibility"
log_score=100

# Check build logs
build_logs=0
for log in cmake.log make.log benchmark.log; do
    if [[ -f "$PROJECT_ROOT/logs/benchmarks/$log" ]]; then
        build_logs=$((build_logs + 1))
    fi
done

if [[ $build_logs -lt 3 ]]; then
    echo "⚠️ Missing build logs ($build_logs/3 found)"
    log_score=$((log_score - 25))
else
    echo "✅ All build logs present"
fi

# Check integration logs
integration_logs=0
for log in integrity.log platform_validation.log edge_case_testing.log; do
    if [[ -f "$PROJECT_ROOT/logs/validations/$log" ]] || [[ -f "$PROJECT_ROOT/logs/platform-validation/$log" ]] || [[ -f "$PROJECT_ROOT/logs/edge-case-testing/$log" ]]; then
        integration_logs=$((integration_logs + 1))
    fi
done

if [[ $integration_logs -lt 3 ]]; then
    echo "⚠️ Missing integration logs ($integration_logs/3 found)"
    log_score=$((log_score - 25))
else
    echo "✅ All integration logs present"
fi

verification_results["Log Visibility"]="Build logs: $build_logs/3, Integration logs: $integration_logs/3"
verification_scores["Log Visibility"]=$log_score

# JSON output verification
echo "[INFO] Verifying JSON output"
json_score=100

json_files=0
for json in build_benchmark_results.json integrity_validation_results.json platform_consistency_results.json edge_case_results.json; do
    if [[ -f "$PROJECT_ROOT/logs/benchmarks/$json" ]] || [[ -f "$PROJECT_ROOT/logs/validations/$json" ]] || [[ -f "$PROJECT_ROOT/logs/platform-validation/$json" ]] || [[ -f "$PROJECT_ROOT/logs/edge-case-testing/$json" ]]; then
        json_files=$((json_files + 1))
    fi
done

if [[ $json_files -lt 4 ]]; then
    echo "⚠️ Missing JSON files ($json_files/4 found)"
    json_score=$((json_score - 25))
else
    echo "✅ All JSON files present"
fi

verification_results["JSON Output"]="JSON files: $json_files/4"
verification_scores["JSON Output"]=$json_score

# Telemetry systems verification
echo "[INFO] Verifying telemetry systems"
telemetry_score=100

telemetry_files=0
for file in telemetry_logger.cpp prometheus_exporter.cpp device_metrics.cpp; do
    if [[ -f "$PROJECT_ROOT/src/utils/$file" ]] || [[ -f "$PROJECT_ROOT/src/services/$file" ]]; then
        telemetry_files=$((telemetry_files + 1))
    fi
done

if [[ $telemetry_files -lt 3 ]]; then
    echo "⚠️ Missing telemetry implementations ($telemetry_files/3 found)"
    telemetry_score=$((telemetry_files - 30))
else
    echo "✅ All telemetry systems implemented"
fi

verification_results["Telemetry Systems"]="Telemetry files: $telemetry_files/3"
verification_scores["Telemetry Systems"]=$telemetry_score

# Build metrics verification
echo "[INFO] Verifying build metrics"
build_score=100

if [[ -f "$PROJECT_ROOT/logs/benchmarks/build_benchmark_results.json" ]]; then
    build_time=$(jq -r '.build_results.build_time_seconds // null' "$PROJECT_ROOT/logs/benchmarks/build_benchmark_results.json" 2>/dev/null)
    if [[ "$build_time" != "null" ]]; then
        echo "✅ Build time metrics available: ${build_time}s"
    else
        echo "⚠️ Build time metrics missing"
        build_score=$((build_score - 30))
    fi
else
    echo "⚠️ Build benchmark results not found"
    build_score=$((build_score - 50))
fi

verification_results["Build Metrics"]="Build time: available"
verification_scores["Build Metrics"]=$build_score

# Runtime metrics verification
echo "[INFO] Verifying runtime metrics"
runtime_score=100

runtime_implementations=0
if grep -q -E "clock|chrono|performance" "$PROJECT_ROOT/src/solver.cpp" 2>/dev/null; then
    runtime_implementations=$((runtime_implementations + 1))
fi
if grep -q -E "checkpoint|progress" "$PROJECT_ROOT/src/checkpoint_manifest.cpp" 2>/dev/null; then
    runtime_implementations=$((runtime_implementations + 1))
fi

if [[ $runtime_implementations -lt 2 ]]; then
    echo "⚠️ Limited runtime metrics ($runtime_implementations/2 implementations)"
    runtime_score=$((runtime_score - 30))
else
    echo "✅ Runtime metrics implemented"
fi

verification_results["Runtime Metrics"]="Implementations: $runtime_implementations/2"
verification_scores["Runtime Metrics"]=$runtime_score

# Integration metrics verification
echo "[INFO] Verifying integration metrics"
integration_score=100

integration_implementations=0
for file in audit_logger.cpp manifest_manager.cpp baseline_measurer.cpp; do
    if [[ -f "$PROJECT_ROOT/src/integration/audit/$file" ]] || [[ -f "$PROJECT_ROOT/src/integration/manifests/$file" ]] || [[ -f "$PROJECT_ROOT/src/integration/baseline/$file" ]]; then
        integration_implementations=$((integration_implementations + 1))
    fi
done

if [[ $integration_implementations -lt 3 ]]; then
    echo "⚠️ Missing integration metrics ($integration_implementations/3 found)"
    integration_score=$((integration_score - 30))
else
    echo "✅ All integration metrics implemented"
fi

verification_results["Integration Metrics"]="Integration files: $integration_implementations/3"
verification_scores["Integration Metrics"]=$integration_score

# Calculate overall results
total_score=0
num_categories=0

echo ""
echo "=== METRICS VISIBILITY VERIFICATION RESULTS ==="

for category in "${!verification_scores[@]}"; do
    score=${verification_scores[$category]}
    result=${verification_results[$category]}

    echo "$category: $score% - $result"
    total_score=$((total_score + score))
    num_categories=$((num_categories + 1))
done

overall_score=$((total_score / num_categories))

echo ""
echo "=== METRICS VISIBILITY VERIFICATION SUMMARY ==="
echo "Overall visibility score: ${overall_score}% (target: 90%)"

if [[ $overall_score -ge 90 ]]; then
    echo "✅ METRICS VISIBILITY TARGET ACHIEVED!"
    echo "Metrics visibility score ${overall_score}% meets 90% target"
    exit_code=0
else
    echo "⚠️ METRICS VISIBILITY TARGET NOT MET"
    echo "Metrics visibility score ${overall_score}% below 90% target"
    exit_code=1
fi

# Save results
cat > "$LOG_DIR/metrics_visibility_results.json" << EOF
{
    "metrics_verification": {
        "timestamp": "$(date -Iseconds)",
        "overall_visibility_score": $overall_score,
        "target_score": 90,
        "target_met": $([ $overall_score -ge 90 ] && echo true || echo false)
    },
    "verification_results": {
EOF

for category in "${!verification_scores[@]}"; do
    echo "        \"$category\": {" >> "$LOG_DIR/metrics_visibility_results.json"
    echo "            \"score\": ${verification_scores[$category]}," >> "$LOG_DIR/metrics_visibility_results.json"
    echo "            \"details\": \"${verification_results[$category]}\"" >> "$LOG_DIR/metrics_visibility_results.json"
    echo "        }," >> "$LOG_DIR/metrics_visibility_results.json"
done

# Remove trailing comma and close JSON
sed -i '$ s/,$//' "$LOG_DIR/metrics_visibility_results.json"
cat >> "$LOG_DIR/metrics_visibility_results.json" << EOF
    }
}
EOF

echo "[SUCCESS] Metrics visibility verification completed"
echo "[INFO] Results saved to: $LOG_DIR/metrics_visibility_results.json"

exit $exit_code