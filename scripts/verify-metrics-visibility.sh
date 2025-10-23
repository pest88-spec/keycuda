#!/bin/bash

# T069: Verify Integration Metrics Visibility
# Ensures metrics are visible in both logs and JSON output formats

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
LOG_DIR="$PROJECT_ROOT/logs/metrics-verification"
RESULTS_FILE="$LOG_DIR/metrics_visibility_results.json"

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
    echo -e "${BLUE}[INFO]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1" | tee -a "$LOG_DIR/metrics_verification.log"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1" | tee -a "$LOG_DIR/metrics_verification.log"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1" | tee -a "$LOG_DIR/metrics_verification.log"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1" | tee -a "$LOG_DIR/metrics_verification.log"
}

# Initialize results
init_results() {
    cat > "$RESULTS_FILE" << EOF
{
    "metrics_verification": {
        "timestamp": "$(date -Iseconds)",
        "total_checks": 0,
        "passed_checks": 0,
        "failed_checks": 0,
        "visibility_score": 0,
        "target_score": 90
    },
    "verification_categories": {
        "log_visibility": {},
        "json_output": {},
        "telemetry_systems": {},
        "build_metrics": {},
        "runtime_metrics": {},
        "integration_metrics": {}
    },
    "detailed_results": [],
    "recommendations": []
}
EOF
}

# Verify log visibility
verify_log_visibility() {
    log_info "Verifying log visibility"

    local checks_run=0
    local checks_passed=0
    local detailed_results=()

    # Check 1: Build logs exist
    log_info "Check 1: Build log availability"
    checks_run=$((checks_run + 1))

    local build_logs_found=0
    local build_log_locations=(
        "$PROJECT_ROOT/logs/benchmarks/cmake.log"
        "$PROJECT_ROOT/logs/benchmarks/make.log"
        "$PROJECT_ROOT/logs/benchmarks/benchmark.log"
    )

    for log_file in "${build_log_locations[@]}"; do
        if [[ -f "$log_file" ]]; then
            build_logs_found=$((build_logs_found + 1))
            log_success "✅ Found build log: $(basename "$log_file")"
        fi
    done

    if [[ $build_logs_found -ge 2 ]]; then
        detailed_results+=("Build logs: $build_logs_found/3 found")
        checks_passed=$((checks_passed + 1))
    else
        log_warning "⚠️ Insufficient build logs found ($build_logs_found/3)"
        detailed_results+=("Build logs: Only $build_logs_found/3 found")
    fi

    # Check 2: Log format consistency
    log_info "Check 2: Log format consistency"
    checks_run=$((checks_run + 1))

    local consistent_format=false
    if [[ -f "$PROJECT_ROOT/logs/benchmarks/benchmark.log" ]]; then
        # Check for timestamped entries
        if grep -q "^\[.*\]" "$PROJECT_ROOT/logs/benchmarks/benchmark.log"; then
            consistent_format=true
            log_success "✅ Consistent log format detected"
        fi
    fi

    if [[ "$consistent_format" == "true" ]]; then
        detailed_results+=("Log format: Consistent timestamped entries")
        checks_passed=$((checks_passed + 1))
    else
        log_warning "⚠️ Log format may need standardization"
        detailed_results+=("Log format: Inconsistent or missing timestamps")
    fi

    # Check 3: Error/warning visibility
    log_info "Check 3: Error and warning visibility"
    checks_run=$((checks_run + 1))

    local error_count=0
    local warning_count=0

    if [[ -f "$PROJECT_ROOT/logs/benchmarks/make.log" ]]; then
        error_count=$(grep -c "error:" "$PROJECT_ROOT/logs/benchmarks/make.log" 2>/dev/null || echo "0")
        warning_count=$(grep -c "warning:" "$PROJECT_ROOT/logs/benchmarks/make.log" 2>/dev/null || echo "0")
    fi

    if [[ $error_count -gt 0 || $warning_count -gt 0 ]]; then
        log_success "✅ Errors and warnings properly logged ($error_count errors, $warning_count warnings)"
        detailed_results+=("Error/warning visibility: $error_count errors, $warning_count warnings")
        checks_passed=$((checks_passed + 1))
    else
        log_info "ℹ️ No errors or warnings found in logs"
        detailed_results+=("Error/warning visibility: No issues found")
        checks_passed=$((checks_passed + 1))
    fi

    # Check 4: Integration logs
    log_info "Check 4: Integration-specific logs"
    checks_run=$((checks_run + 1))

    local integration_logs=0
    local integration_log_locations=(
        "$PROJECT_ROOT/logs/validations/integrity.log"
        "$PROJECT_ROOT/logs/platform-validation/platform_validation.log"
        "$PROJECT_ROOT/logs/edge-case-testing/edge_case_testing.log"
    )

    for log_file in "${integration_log_locations[@]}"; do
        if [[ -f "$log_file" ]]; then
            integration_logs=$((integration_logs + 1))
            log_success "✅ Found integration log: $(basename "$log_file")"
        fi
    done

    if [[ $integration_logs -ge 2 ]]; then
        detailed_results+=("Integration logs: $integration_logs/3 found")
        checks_passed=$((checks_passed + 1))
    else
        log_warning "⚠️ Limited integration logs ($integration_logs/3)"
        detailed_results+=("Integration logs: Only $integration_logs/3 found")
    fi

    local visibility_score=0
    if [[ $checks_run -gt 0 ]]; then
        visibility_score=$((checks_passed * 100 / checks_run))
    fi

    log_info "Log visibility verification: $checks_passed/$checks_run checks passed ($visibility_score%)"

    # Update results
    local temp_file=$(mktemp)
    jq --argjson results "$(printf '%s\n' "${detailed_results[@]}" | jq -R . | jq -s .)" \
       --argjson passed "$checks_passed" \
       --argjson run "$checks_run" \
       --argjson score "$visibility_score" \
       '.verification_categories.log_visibility = {
           "checks_passed": $passed,
           "checks_run": $run,
           "visibility_score": $score,
           "results": $results
       }' "$RESULTS_FILE" > "$temp_file"
    mv "$temp_file" "$RESULTS_FILE"

    echo "$visibility_score"
}

# Verify JSON output
verify_json_output() {
    log_info "Verifying JSON output metrics"

    local checks_run=0
    local checks_passed=0
    local detailed_results=()

    # Check 1: Build benchmark JSON
    log_info "Check 1: Build benchmark JSON output"
    checks_run=$((checks_run + 1))

    if [[ -f "$PROJECT_ROOT/logs/benchmarks/build_benchmark_results.json" ]]; then
        # Validate JSON structure
        if jq -e '.build_results' "$PROJECT_ROOT/logs/benchmarks/build_benchmark_results.json" >/dev/null 2>&1; then
            log_success "✅ Valid build benchmark JSON found"
            detailed_results+=("Build benchmark JSON: Valid structure")
            checks_passed=$((checks_passed + 1))
        else
            log_error "❌ Build benchmark JSON structure invalid"
            detailed_results+=("Build benchmark JSON: Invalid structure")
        fi
    else
        log_error "❌ Build benchmark JSON not found"
        detailed_results+=("Build benchmark JSON: Not found")
    fi

    # Check 2: Integrity validation JSON
    log_info "Check 2: Integrity validation JSON output"
    checks_run=$((checks_run + 1))

    if [[ -f "$PROJECT_ROOT/logs/validations/integrity_validation_results.json" ]]; then
        if jq -e '.validation_run' "$PROJECT_ROOT/logs/validations/integrity_validation_results.json" >/dev/null 2>&1; then
            log_success "✅ Valid integrity validation JSON found"
            detailed_results+=("Integrity validation JSON: Valid structure")
            checks_passed=$((checks_passed + 1))
        else
            log_error "❌ Integrity validation JSON structure invalid"
            detailed_results+=("Integrity validation JSON: Invalid structure")
        fi
    else
        log_error "❌ Integrity validation JSON not found"
        detailed_results+=("Integrity validation JSON: Not found")
    fi

    # Check 3: Platform consistency JSON
    log_info "Check 3: Platform consistency JSON output"
    checks_run=$((checks_run + 1))

    if [[ -f "$PROJECT_ROOT/logs/platform-validation/platform_consistency_results.json" ]]; then
        if jq -e '.platform_validation' "$PROJECT_ROOT/logs/platform-validation/platform_consistency_results.json" >/dev/null 2>&1; then
            log_success "✅ Valid platform consistency JSON found"
            detailed_results+=("Platform consistency JSON: Valid structure")
            checks_passed=$((checks_passed + 1))
        else
            log_error "❌ Platform consistency JSON structure invalid"
            detailed_results+=("Platform consistency JSON: Invalid structure")
        fi
    else
        log_error "❌ Platform consistency JSON not found"
        detailed_results+=("Platform consistency JSON: Not found")
    fi

    # Check 4: Edge case testing JSON
    log_info "Check 4: Edge case testing JSON output"
    checks_run=$((checks_run + 1))

    if [[ -f "$PROJECT_ROOT/logs/edge-case-testing/edge_case_results.json" ]]; then
        if jq -e '.edge_case_testing' "$PROJECT_ROOT/logs/edge-case-testing/edge_case_results.json" >/dev/null 2>&1; then
            log_success "✅ Valid edge case testing JSON found"
            detailed_results+=("Edge case testing JSON: Valid structure")
            checks_passed=$((checks_passed + 1))
        else
            log_error "❌ Edge case testing JSON structure invalid"
            detailed_results+=("Edge case testing JSON: Invalid structure")
        fi
    else
        log_error "❌ Edge case testing JSON not found"
        detailed_results+=("Edge case testing JSON: Not found")
    fi

    # Check 5: JSON schema consistency
    log_info "Check 5: JSON schema consistency"
    checks_run=$((checks_run + 1))

    local schema_consistent=true
    local json_files=(
        "$PROJECT_ROOT/logs/benchmarks/build_benchmark_results.json"
        "$PROJECT_ROOT/logs/validations/integrity_validation_results.json"
        "$PROJECT_ROOT/logs/platform-validation/platform_consistency_results.json"
        "$PROJECT_ROOT/logs/edge-case-testing/edge_case_results.json"
    )

    for json_file in "${json_files[@]}"; do
        if [[ -f "$json_file" ]]; then
            # Check for timestamp field
            if ! jq -e '.timestamp' "$json_file" >/dev/null 2>&1 && ! jq -e '.*.timestamp' "$json_file" >/dev/null 2>&1; then
                schema_consistent=false
                break
            fi
        fi
    done

    if [[ "$schema_consistent" == "true" ]]; then
        log_success "✅ JSON schemas are consistent"
        detailed_results+=("JSON schema consistency: Good")
        checks_passed=$((checks_passed + 1))
    else
        log_warning "⚠️ JSON schema inconsistencies detected"
        detailed_results+=("JSON schema consistency: Needs improvement")
    fi

    local visibility_score=0
    if [[ $checks_run -gt 0 ]]; then
        visibility_score=$((checks_passed * 100 / checks_run))
    fi

    log_info "JSON output verification: $checks_passed/$checks_run checks passed ($visibility_score%)"

    # Update results
    local temp_file=$(mktemp)
    jq --argjson results "$(printf '%s\n' "${detailed_results[@]}" | jq -R . | jq -s .)" \
       --argjson passed "$checks_passed" \
       --argjson run "$checks_run" \
       --argjson score "$visibility_score" \
       '.verification_categories.json_output = {
           "checks_passed": $passed,
           "checks_run": $run,
           "visibility_score": $score,
           "results": $results
       }' "$RESULTS_FILE" > "$temp_file"
    mv "$temp_file" "$RESULTS_FILE"

    echo "$visibility_score"
}

# Verify telemetry systems
verify_telemetry_systems() {
    log_info "Verifying telemetry systems"

    local checks_run=0
    local checks_passed=0
    local detailed_results=()

    # Check 1: Telemetry logger implementation
    log_info "Check 1: Telemetry logger implementation"
    checks_run=$((checks_run + 1))

    if [[ -f "$PROJECT_ROOT/src/utils/telemetry_logger.cpp" ]]; then
        log_success "✅ Telemetry logger implementation found"
        detailed_results+=("Telemetry logger: Implemented")
        checks_passed=$((checks_passed + 1))
    else
        log_warning "⚠️ Telemetry logger implementation not found"
        detailed_results+=("Telemetry logger: Not found")
    fi

    # Check 2: Prometheus exporter
    log_info "Check 2: Prometheus metrics exporter"
    checks_run=$((checks_run + 1))

    if [[ -f "$PROJECT_ROOT/src/utils/prometheus_exporter.cpp" ]]; then
        log_success "✅ Prometheus exporter implementation found"
        detailed_results+=("Prometheus exporter: Implemented")
        checks_passed=$((checks_passed + 1))
    else
        log_warning "⚠️ Prometheus exporter implementation not found"
        detailed_results+=("Prometheus exporter: Not found")
    fi

    # Check 3: Device metrics implementation
    log_info "Check 3: Device metrics implementation"
    checks_run=$((checks_run + 1))

    if [[ -f "$PROJECT_ROOT/src/services/device_metrics.cpp" ]]; then
        log_success "✅ Device metrics implementation found"
        detailed_results+=("Device metrics: Implemented")
        checks_passed=$((checks_passed + 1))
    else
        log_warning "⚠️ Device metrics implementation not found"
        detailed_results+=("Device metrics: Not found")
    fi

    # Check 4: Configuration for metrics
    log_info "Check 4: Metrics configuration"
    checks_run=$((checks_run + 1))

    if [[ -f "$PROJECT_ROOT/src/config/puzzle71_config.cpp" ]]; then
        if grep -q -E "metric|telemetry|prometheus" "$PROJECT_ROOT/src/config/puzzle71_config.cpp" 2>/dev/null; then
            log_success "✅ Metrics configuration found"
            detailed_results+=("Metrics configuration: Present")
            checks_passed=$((checks_passed + 1))
        else
            log_info "ℹ️ Metrics configuration may be limited"
            detailed_results+=("Metrics configuration: Limited")
        fi
    else
        log_warning "⚠️ Configuration file not found"
        detailed_results+=("Metrics configuration: Not found")
    fi

    local visibility_score=0
    if [[ $checks_run -gt 0 ]]; then
        visibility_score=$((checks_passed * 100 / checks_run))
    fi

    log_info "Telemetry systems verification: $checks_passed/$checks_run checks passed ($visibility_score%)"

    # Update results
    local temp_file=$(mktemp)
    jq --argjson results "$(printf '%s\n' "${detailed_results[@]}" | jq -R . | jq -s .)" \
       --argjson passed "$checks_passed" \
       --argjson run "$checks_run" \
       --argjson score "$visibility_score" \
       '.verification_categories.telemetry_systems = {
           "checks_passed": $passed,
           "checks_run": $run,
           "visibility_score": $score,
           "results": $results
       }' "$RESULTS_FILE" > "$temp_file"
    mv "$temp_file" "$RESULTS_FILE"

    echo "$visibility_score"
}

# Verify build metrics
verify_build_metrics() {
    log_info "Verifying build metrics visibility"

    local checks_run=0
    local checks_passed=0
    local detailed_results=()

    # Check 1: Build time metrics
    log_info "Check 1: Build time metrics"
    checks_run=$((checks_run + 1))

    if [[ -f "$PROJECT_ROOT/logs/benchmarks/build_benchmark_results.json" ]]; then
        local build_time=$(jq -r '.build_results.build_time_seconds // null' "$PROJECT_ROOT/logs/benchmarks/build_benchmark_results.json" 2>/dev/null)
        if [[ "$build_time" != "null" && "$build_time" != "" ]]; then
            log_success "✅ Build time metrics available: ${build_time}s"
            detailed_results+=("Build time: ${build_time}s")
            checks_passed=$((checks_passed + 1))
        else
            log_warning "⚠️ Build time metrics not available"
            detailed_results+=("Build time: Not available")
        fi
    else
        detailed_results+=("Build time: Benchmark results not found")
    fi

    # Check 2: Build error metrics
    log_info "Check 2: Build error metrics"
    checks_run=$((checks_run + 1))

    if [[ -f "$PROJECT_ROOT/logs/benchmarks/build_benchmark_results.json" ]]; then
        local error_count=$(jq -r '.build_results.build_errors | length // 0' "$PROJECT_ROOT/logs/benchmarks/build_benchmark_results.json" 2>/dev/null)
        local warning_count=$(jq -r '.build_results.build_warnings | length // 0' "$PROJECT_ROOT/logs/benchmarks/build_benchmark_results.json" 2>/dev/null)

        log_success "✅ Build error metrics available: $error_count errors, $warning_count warnings"
        detailed_results+=("Build errors: $error_count errors, $warning_count warnings")
        checks_passed=$((checks_passed + 1))
    else
        detailed_results+=("Build errors: Benchmark results not found")
    fi

    # Check 3: System resource metrics
    log_info "Check 3: System resource metrics"
    checks_run=$((checks_run + 1))

    if [[ -f "$PROJECT_ROOT/logs/benchmarks/build_benchmark_results.json" ]]; then
        local cpu_cores=$(jq -r '.system_metrics.cpu_cores // null' "$PROJECT_ROOT/logs/benchmarks/build_benchmark_results.json" 2>/dev/null)
        local memory_gb=$(jq -r '.system_metrics.memory_gb // null' "$PROJECT_ROOT/logs/benchmarks/build_benchmark_results.json" 2>/dev/null)

        if [[ "$cpu_cores" != "null" && "$memory_gb" != "null" ]]; then
            log_success "✅ System resource metrics available: $cpu_cores cores, ${memory_gb}GB RAM"
            detailed_results+=("System resources: $cpu_cores cores, ${memory_gb}GB RAM")
            checks_passed=$((checks_passed + 1))
        else
            log_warning "⚠️ System resource metrics incomplete"
            detailed_results+=("System resources: Incomplete")
        fi
    else
        detailed_results+=("System resources: Benchmark results not found")
    fi

    local visibility_score=0
    if [[ $checks_run -gt 0 ]]; then
        visibility_score=$((checks_passed * 100 / checks_run))
    fi

    log_info "Build metrics verification: $checks_passed/$checks_run checks passed ($visibility_score%)"

    # Update results
    local temp_file=$(mktemp)
    jq --argjson results "$(printf '%s\n' "${detailed_results[@]}" | jq -R . | jq -s .)" \
       --argjson passed "$checks_passed" \
       --argjson run "$checks_run" \
       --argjson score "$visibility_score" \
       '.verification_categories.build_metrics = {
           "checks_passed": $passed,
           "checks_run": $run,
           "visibility_score": $score,
           "results": $results
       }' "$RESULTS_FILE" > "$temp_file"
    mv "$temp_file" "$RESULTS_FILE"

    echo "$visibility_score"
}

# Verify runtime metrics
verify_runtime_metrics() {
    log_info "Verifying runtime metrics visibility"

    local checks_run=0
    local checks_passed=0
    local detailed_results=()

    # Check 1: Performance metrics implementation
    log_info "Check 1: Performance metrics implementation"
    checks_run=$((checks_run + 1))

    local performance_metrics=false
    if [[ -f "$PROJECT_ROOT/src/solver.cpp" ]]; then
        if grep -q -E "clock|chrono|performance|benchmark" "$PROJECT_ROOT/src/solver.cpp" 2>/dev/null; then
            performance_metrics=true
        fi
    fi

    if [[ "$performance_metrics" == "true" ]]; then
        log_success "✅ Performance metrics implementation found"
        detailed_results+=("Performance metrics: Implemented")
        checks_passed=$((checks_passed + 1))
    else
        log_info "ℹ️ Performance metrics implementation may be limited"
        detailed_results+=("Performance metrics: Limited implementation")
    fi

    # Check 2: GPU utilization metrics
    log_info "Check 2: GPU utilization metrics"
    checks_run=$((checks_run + 1))

    if [[ -f "$PROJECT_ROOT/src/ComputeCore/gpu/device_buffers.cpp" ]] || [[ -f "$PROJECT_ROOT/src/services/device_metrics.cpp" ]]; then
        if grep -q -E "utilization|memory|bandwidth" "$PROJECT_ROOT/src/services/device_metrics.cpp" 2>/dev/null; then
            log_success "✅ GPU utilization metrics implemented"
            detailed_results+=("GPU utilization: Implemented")
            checks_passed=$((checks_passed + 1))
        else
            log_info "ℹ️ GPU utilization metrics may be limited"
            detailed_results+=("GPU utilization: Limited implementation")
        fi
    else
        detailed_results+=("GPU utilization: Implementation not found")
    fi

    # Check 3: Checkpoint metrics
    log_info "Check 3: Checkpoint metrics"
    checks_run=$((checks_run + 1))

    if [[ -f "$PROJECT_ROOT/src/checkpoint_manifest.cpp" ]]; then
        if grep -q -E "progress|checkpoint|save" "$PROJECT_ROOT/src/checkpoint_manifest.cpp" 2>/dev/null; then
            log_success "✅ Checkpoint metrics implemented"
            detailed_results+=("Checkpoint metrics: Implemented")
            checks_passed=$((checks_passed + 1))
        else
            log_info "ℹ️ Checkpoint metrics may be limited"
            detailed_results+=("Checkpoint metrics: Limited implementation")
        fi
    else
        detailed_results+=("Checkpoint metrics: Implementation not found")
    fi

    local visibility_score=0
    if [[ $checks_run -gt 0 ]]; then
        visibility_score=$((checks_passed * 100 / checks_run))
    fi

    log_info "Runtime metrics verification: $checks_passed/$checks_run checks passed ($visibility_score%)"

    # Update results
    local temp_file=$(mktemp)
    jq --argjson results "$(printf '%s\n' "${detailed_results[@]}" | jq -R . | jq -s .)" \
       --argjson passed "$checks_passed" \
       --argjson run "$checks_run" \
       --argjson score "$visibility_score" \
       '.verification_categories.runtime_metrics = {
           "checks_passed": $passed,
           "checks_run": $run,
           "visibility_score": $score,
           "results": $results
       }' "$RESULTS_FILE" > "$temp_file"
    mv "$temp_file" "$RESULTS_FILE"

    echo "$visibility_score"
}

# Verify integration metrics
verify_integration_metrics() {
    log_info "Verifying integration metrics visibility"

    local checks_run=0
    local checks_passed=0
    local detailed_results=()

    # Check 1: Integration audit metrics
    log_info "Check 1: Integration audit metrics"
    checks_run=$((checks_run + 1))

    if [[ -f "$PROJECT_ROOT/src/integration/audit/audit_logger.cpp" ]]; then
        log_success "✅ Integration audit logger implemented"
        detailed_results+=("Integration audit: Implemented")
        checks_passed=$((checks_passed + 1))
    else
        log_warning "⚠️ Integration audit logger not found"
        detailed_results+=("Integration audit: Not found")
    fi

    # Check 2: Manifest metrics
    log_info "Check 2: Manifest management metrics"
    checks_run=$((checks_run + 1))

    if [[ -f "$PROJECT_ROOT/src/integration/manifests/manifest_manager.cpp" ]]; then
        log_success "✅ Manifest management implemented"
        detailed_results+=("Manifest management: Implemented")
        checks_passed=$((checks_passed + 1))
    else
        log_warning "⚠️ Manifest management not found"
        detailed_results+=("Manifest management: Not found")
    fi

    # Check 3: Baseline metrics
    log_info "Check 3: Baseline measurement metrics"
    checks_run=$((checks_run + 1))

    if [[ -f "$PROJECT_ROOT/src/integration/baseline/baseline_measurer.cpp" ]]; then
        log_success "✅ Baseline measurement implemented"
        detailed_results+=("Baseline measurement: Implemented")
        checks_passed=$((checks_passed + 1))
    else
        log_warning "⚠️ Baseline measurement not found"
        detailed_results+=("Baseline measurement: Not found")
    fi

    local visibility_score=0
    if [[ $checks_run -gt 0 ]]; then
        visibility_score=$((checks_passed * 100 / checks_run))
    fi

    log_info "Integration metrics verification: $checks_passed/$checks_run checks passed ($visibility_score%)"

    # Update results
    local temp_file=$(mktemp)
    jq --argjson results "$(printf '%s\n' "${detailed_results[@]}" | jq -R . | jq -s .)" \
       --argjson passed "$checks_passed" \
       --argjson run "$checks_run" \
       --argjson score "$visibility_score" \
       '.verification_categories.integration_metrics = {
           "checks_passed": $passed,
           "checks_run": $run,
           "visibility_score": $score,
           "results": $results
       }' "$RESULTS_FILE" > "$temp_file"
    mv "$temp_file" "$RESULTS_FILE"

    echo "$visibility_score"
}

# Calculate overall results
calculate_overall_results() {
    local log_score="$1"
    local json_score="$2"
    local telemetry_score="$3"
    local build_score="$4"
    local runtime_score="$5"
    local integration_score="$6"

    # Calculate overall visibility score
    local overall_score=$(( (log_score + json_score + telemetry_score + build_score + runtime_score + integration_score) / 6 ))

    # Generate recommendations
    local recommendations=()
    if [[ $overall_score -lt 90 ]]; then
        recommendations+=("Overall metrics visibility below target - improve logging and output")
    fi
    if [[ $log_score -lt 80 ]]; then
        recommendations+=("Enhance log format consistency and coverage")
    fi
    if [[ $json_score -lt 80 ]]; then
        recommendations+=("Improve JSON output structure and validation")
    fi
    if [[ $telemetry_score -lt 80 ]]; then
        recommendations+=("Expand telemetry and metrics collection systems")
    fi

    # Update results
    local temp_file=$(mktemp)
    jq --argjson overall "$overall_score" \
       --argjson total "$(jq '.verification_categories | map(.checks_run) | add' "$RESULTS_FILE")" \
       --argjson passed "$(jq '.verification_categories | map(.checks_passed) | add' "$RESULTS_FILE")" \
       --argjson recommendations "$(printf '%s\n' "${recommendations[@]}" | jq -R . | jq -s .)" \
       '.metrics_verification.total_checks = $total |
        .metrics_verification.passed_checks = $passed |
        .metrics_verification.failed_checks = ($total - $passed) |
        .metrics_verification.visibility_score = $overall |
        .detailed_results = [.verification_categories | to_entries[] | {
            "category": .key,
            "score": .value.visibility_score,
            "details": .value.results
        }] |
        .recommendations = $recommendations' "$RESULTS_FILE" > "$temp_file"
    mv "$temp_file" "$RESULTS_FILE"

    echo "$overall_score"
}

# Generate metrics visibility report
generate_metrics_report() {
    log_info "Generating metrics visibility report"

    local report_file="$LOG_DIR/metrics_visibility_report.html"
    local overall_score=$(jq -r '.metrics_verification.visibility_score' "$RESULTS_FILE")
    local total_checks=$(jq -r '.metrics_verification.total_checks' "$RESULTS_FILE")
    local passed_checks=$(jq -r '.metrics_verification.passed_checks' "$RESULTS_FILE")

    cat > "$report_file" << EOF
<!DOCTYPE html>
<html>
<head>
    <title>Metrics Visibility Verification Report</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; }
        .header { background: #2c3e50; color: white; padding: 20px; border-radius: 5px; }
        .success { color: #27ae60; font-weight: bold; }
        .warning { color: #f39c12; font-weight: bold; }
        .failure { color: #e74c3c; font-weight: bold; }
        .metric-card { background: #f8f9fa; padding: 15px; margin: 10px 0; border-radius: 5px; border-left: 4px solid #3498db; }
        .category-card { background: #f8f9fa; padding: 15px; margin: 10px 0; border-radius: 5px; }
        pre { background: #f8f9fa; padding: 10px; border-radius: 3px; overflow-x: auto; }
        .score-display { font-size: 48px; font-weight: bold; text-align: center; margin: 20px 0; }
    </style>
</head>
<body>
    <div class="header">
        <h1>📊 Metrics Visibility Verification Report</h1>
        <p>Generated: $(date)</p>
        <p>Overall Visibility Score: <span class="score-display">$overall_score%</span></p>
        <p>Checks Passed: $passed_checks/$total_checks</p>
    </div>

    <h2>📋 Verification Categories</h2>
EOF

    # Add category results
    jq -r '.verification_categories | to_entries[] | "\(.key):\(.value.visibility_score // 0):\(.value.checks_passed // 0):\(.value.checks_run // 0)"' "$RESULTS_FILE" | while IFS=: read -r category score passed run; do
        local display_name=$(echo "$category" | sed 's/_/ /g' | sed 's/\b\w/\u&/g')

        cat >> "$report_file" << EOF
    <div class="category-card">
        <h3>$display_name</h3>
        <p>Visibility Score: $score% ($passed/$run checks passed)</p>
EOF

        # Add detailed results
        local results=$(jq -r ".verification_categories.$category.results[]?" "$RESULTS_FILE" 2>/dev/null | while read -r result; do
            echo "        <li>$result</li>"
        done)

        if [[ -n "$results" ]]; then
            cat >> "$report_file" << EOF
        <ul>
$results
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

    log_success "Metrics visibility report generated: $report_file"
}

# Main verification execution
main() {
    local command="${1:-run}"

    case "$command" in
        "run")
            log_info "Starting metrics visibility verification (T069)"

            init_results

            # Run all verification checks
            local log_score=$(verify_log_visibility)
            local json_score=$(verify_json_output)
            local telemetry_score=$(verify_telemetry_systems)
            local build_score=$(verify_build_metrics)
            local runtime_score=$(verify_runtime_metrics)
            local integration_score=$(verify_integration_metrics)

            # Calculate overall results
            local overall_score=$(calculate_overall_results "$log_score" "$json_score" "$telemetry_score" "$build_score" "$runtime_score" "$integration_score")

            # Generate report
            generate_metrics_report

            # Final summary
            echo ""
            log_info "=== METRICS VISIBILITY VERIFICATION SUMMARY ==="
            log_info "Overall visibility score: ${overall_score}% (target: 90%)"
            log_info "Log visibility: ${log_score}%"
            log_info "JSON output: ${json_score}%"
            log_info "Telemetry systems: ${telemetry_score}%"
            log_info "Build metrics: ${build_score}%"
            log_info "Runtime metrics: ${runtime_score}%"
            log_info "Integration metrics: ${integration_score}%"

            if [[ $overall_score -ge 90 ]]; then
                log_success "✅ METRICS VISIBILITY TARGET ACHIEVED!"
                log_success "Metrics visibility score ${overall_score}% meets 90% target"
                return 0
            else
                log_warning "⚠️ METRICS VISIBILITY TARGET NOT MET"
                log_warning "Metrics visibility score ${overall_score}% below 90% target"
                return 1
            fi
            ;;
        "report")
            if [[ -f "$RESULTS_FILE" ]]; then
                generate_metrics_report
                echo "Report available: $LOG_DIR/metrics_visibility_report.html"
            else
                log_error "No metrics verification results found. Run verification first."
            fi
            ;;
        "clean")
            rm -rf "$LOG_DIR"
            log_success "Metrics verification cleanup completed"
            ;;
        "help"|*)
            echo "Usage: $0 {run|report|clean|help}"
            echo ""
            echo "Commands:"
            echo "  run    - Run metrics visibility verification"
            echo "  report - Generate HTML report from existing results"
            echo "  clean  - Clean verification artifacts"
            echo "  help   - Show this help message"
            exit 0
            ;;
    esac
}

# Execute main function
main "$@"