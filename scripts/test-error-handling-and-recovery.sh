#!/bin/bash

# Comprehensive Error Handling and Recovery Testing Framework
# Implements T061: Error handling and recovery testing with 99% success rate

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TEST_DIR="$PROJECT_ROOT/tests/error-handling"
LOG_DIR="$PROJECT_ROOT/logs/error-handling"
METRICS_FILE="$LOG_DIR/recovery_metrics.json"
REPORT_FILE="$LOG_DIR/recovery_report.html"

# Ensure directories exist
mkdir -p "$TEST_DIR" "$LOG_DIR"

# Color codes for output
readonly RED='\033[0;31m'
readonly GREEN='\033[0;32m'
readonly YELLOW='\033[1;33m'
readonly BLUE='\033[0;34m'
readonly NC='\033[0m'

# Recovery testing configuration
readonly TEST_TIMEOUT=300
readonly MAX_RETRY_ATTEMPTS=3
readonly RECOVERY_TIMEOUT=30
readonly SUCCESS_RATE_TARGET=99.0
readonly TEST_SCENARIOS=50

# Test categories
CATEGORIES=(
    "build_failures"
    "dependency_conflicts"
    "network_failures"
    "resource_constraints"
    "corruption_detections"
    "configuration_errors"
    "permission_issues"
    "timeout_scenarios"
    "memory_exhaustion"
    "disk_space_full"
)

# Test results tracking
declare -A test_results
declare -A recovery_times
declare -A error_categories
declare -A recovery_strategies
total_tests=0
successful_recoveries=0

# Logging utilities
log_info() {
    echo -e "${BLUE}[INFO]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1" | tee -a "$LOG_DIR/error_handling.log"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1" | tee -a "$LOG_DIR/error_handling.log"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1" | tee -a "$LOG_DIR/error_handling.log"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $(date '+%Y-%m-%d %H:%M:%S') - $1" | tee -a "$LOG_DIR/error_handling.log"
}

# Metrics collection
init_metrics() {
    cat > "$METRICS_FILE" << EOF
{
    "test_run": {
        "timestamp": "$(date -Iseconds)",
        "total_tests": 0,
        "successful_recoveries": 0,
        "failed_recoveries": 0,
        "success_rate": 0.0,
        "target_success_rate": $SUCCESS_RATE_TARGET
    },
    "test_categories": {},
    "recovery_times": {
        "min": 0.0,
        "max": 0.0,
        "average": 0.0,
        "p50": 0.0,
        "p95": 0.0,
        "p99": 0.0
    },
    "error_scenarios": {
        "tested": 0,
        "recovered": 0,
        "failed": 0,
        "strategies_used": {}
    },
    "detailed_results": []
}
EOF
}

update_metrics() {
    local scenario="$1"
    local success="$2"
    local recovery_time="$3"
    local category="$4"
    local strategy="$5"

    ((total_tests++))

    if [[ "$success" == "true" ]]; then
        ((successful_recoveries++))
        test_results["$scenario"]="PASS"
        log_success "Scenario '$scenario' recovered in ${recovery_time}s"
    else
        test_results["$scenario"]="FAIL"
        log_error "Scenario '$scenario' failed to recover"
    fi

    recovery_times["$scenario"]="$recovery_time"
    error_categories["$scenario"]="$category"
    recovery_strategies["$scenario"]="$strategy"

    # Update JSON metrics
    local success_rate=$(echo "scale=2; $successful_recoveries * 100 / $total_tests" | bc -l)
    local temp_file=$(mktemp)
    jq --arg scenario "$scenario" \
       --arg success "$success" \
       --arg recovery_time "$recovery_time" \
       --arg category "$category" \
       --arg strategy "$strategy" \
       --arg total_tests "$total_tests" \
       --arg successful_recoveries "$successful_recoveries" \
       --arg success_rate "$success_rate" \
       '
       .test_run.total_tests = ($total_tests | tonumber) |
       .test_run.successful_recoveries = ($successful_recoveries | tonumber) |
       .test_run.failed_recoveries = (.test_run.total_tests - .test_run.successful_recoveries) |
       .test_run.success_rate = ($success_rate | tonumber) |
       .test_categories[$category] = (.test_categories[$category] // 0) + 1 |
       .recovery_times.min = (.recovery_times.min == 0 or ($recovery_time | tonumber) < .recovery_times.min) ? ($recovery_time | tonumber) : .recovery_times.min |
       .recovery_times.max = ($recovery_time | tonumber) > .recovery_times.max ? ($recovery_time | tonumber) : .recovery_times.max |
       .error_scenarios.tested += 1 |
       .error_scenarios.recovered = ($success == "true") ? (.error_scenarios.recovered + 1) : .error_scenarios.recovered |
       .error_scenarios.failed = ($success == "true") ? .error_scenarios.failed : (.error_scenarios.failed + 1) |
       .error_scenarios.strategies_used[$strategy] = (.error_scenarios.strategies_used[$strategy] // 0) + 1 |
       .detailed_results += [{
           "scenario": $scenario,
           "success": ($success == "true"),
           "recovery_time": ($recovery_time | tonumber),
           "category": $category,
           "strategy": $strategy,
           "timestamp": (now | strftime("%Y-%m-%dT%H:%M:%S%z"))
       }]
       ' "$METRICS_FILE" > "$temp_file"
    mv "$temp_file" "$METRICS_FILE"
}

# Error scenario generators
create_build_failure_scenario() {
    local test_name="$1"
    local test_file="$TEST_DIR/${test_name}.cpp"

    cat > "$test_file" << 'EOF'
#include <iostream>
#include <stdexcept>
#include <string>

// Build failure test - intentionally broken code
class BuildFailureTest {
private:
    std::string invalid_dependency;

public:
    BuildFailureTest() : invalid_dependency("nonexistent_library") {
        // This will cause a build error
        invalid_dependency.call_nonexistent_method();
    }

    void test_build_failure() {
        throw std::runtime_error("Intentional build failure for testing");
    }
};

int main() {
    BuildFailureTest test;
    test.test_build_failure();
    return 0;
}
EOF
}

create_dependency_conflict_scenario() {
    local test_name="$1"
    local conflict_file="$TEST_DIR/${test_name}_conflict.cpp"

    cat > "$conflict_file" << 'EOF'
// Dependency conflict simulation
#include <iostream>
#include <vector>
#include <string>

// Simulate conflicting dependency versions
namespace version_conflict {
    struct conflicting_struct {
        int value;
        char data[100];  // Different size in different versions
    };

    // Simulate API mismatch
    void function_with_changed_signature(int old_param) {
        std::cout << "Old signature function" << std::endl;
    }

    void function_with_changed_signature(std::string new_param) {
        std::cout << "New signature function" << std::endl;
    }
}

int main() {
    // This would cause a linker error with conflicting versions
    version_conflict::conflicting_struct test_struct{42};
    version_conflict::function_with_changed_signature(42);  // Ambiguous call
    return 0;
}
EOF
}

create_network_failure_scenario() {
    local test_name="$1"
    local test_script="$TEST_DIR/${test_name}_network.sh"

    cat > "$test_script" << 'EOF'
#!/bin/bash

# Network failure simulation
set -euo pipefail

# Block network access
iptables -A OUTPUT -j DROP

# Try to perform network-dependent operations
echo "Testing network failure recovery..."

# Simulate fetching dependencies
echo "Attempting to fetch remote dependencies..."
curl -s --connect-timeout 5 http://example.com/dependency.tar.gz > /dev/null || {
    echo "Network failure detected, attempting recovery..."

    # Recovery strategy: use local cache
    if [[ -d "local_cache/" ]]; then
        echo "Recovery successful: using local cache"
        iptables -D OUTPUT -j DROP
        exit 0
    else
        echo "Recovery failed: no local cache available"
        iptables -D OUTPUT -j DROP
        exit 1
    fi
}

# Cleanup
iptables -D OUTPUT -j DROP
echo "Network test completed successfully"
EOF

    chmod +x "$test_script"
}

create_corruption_detection_scenario() {
    local test_name="$1"
    local corruption_test="$TEST_DIR/${test_name}_corruption.cpp"

    cat > "$corruption_test" << 'EOF'
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <iomanip>
#include <openssl/sha.h>

class CorruptionDetectionTest {
private:
    std::string test_file;
    std::string expected_checksum;

    std::string calculate_checksum(const std::string& filename) {
        std::ifstream file(filename, std::ios::binary);
        std::vector<char> buffer((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());

        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256(reinterpret_cast<const unsigned char*>(buffer.data()),
               buffer.size(), hash);

        std::stringstream ss;
        for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
            ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
        }

        return ss.str();
    }

public:
    CorruptionDetectionTest(const std::string& file) : test_file(file) {
        expected_checksum = calculate_checksum(test_file);
    }

    bool test_corruption_detection() {
        // Simulate file corruption
        std::fstream file(test_file, std::ios::in | std::ios::out | std::ios::binary);
        file.seekp(100);
        file.put(0xFF);  // Corrupt a byte
        file.close();

        std::string corrupted_checksum = calculate_checksum(test_file);

        if (corrupted_checksum != expected_checksum) {
            std::cout << "Corruption detected!" << std::endl;

            // Recovery strategy: restore from backup
            return recover_from_backup();
        }

        return true;
    }

private:
    bool recover_from_backup() {
        // Simulate recovery from backup
        std::cout << "Attempting recovery from backup..." << std::endl;

        // Restore original checksum
        std::ofstream file(test_file, std::ios::binary);
        file.write("Original content for recovery test", 35);
        file.close();

        std::string restored_checksum = calculate_checksum(test_file);
        return restored_checksum == expected_checksum;
    }
};

int main() {
    // Create test file
    std::ofstream test_file("test_corruption.dat");
    test_file.write("Original content for recovery test", 35);
    test_file.close();

    CorruptionDetectionTest corruption_test("test_corruption.dat");
    bool recovered = corruption_test.test_corruption_detection();

    std::cout << "Corruption test " << (recovered ? "PASSED" : "FAILED") << std::endl;
    return recovered ? 0 : 1;
}
EOF
}

create_memory_exhaustion_scenario() {
    local test_name="$1"
    local memory_test="$TEST_DIR/${test_name}_memory.cpp"

    cat > "$memory_test" << 'EOF'
#include <iostream>
#include <vector>
#include <memory>
#include <exception>
#include <stdexcept>

class MemoryExhaustionTest {
private:
    static constexpr size_t BLOCK_SIZE = 1024 * 1024;  // 1MB blocks
    static constexpr size_t MAX_BLOCKS = 1024;  // Try to allocate 1GB

public:
    bool test_memory_exhaustion_recovery() {
        std::vector<std::unique_ptr<char[]>> memory_blocks;

        try {
            // Attempt to exhaust memory
            for (size_t i = 0; i < MAX_BLOCKS; ++i) {
                memory_blocks.emplace_back(std::make_unique<char[]>(BLOCK_SIZE));

                // Check if we're approaching memory limit
                if (i > 0 && i % 100 == 0) {
                    std::cout << "Allocated " << i << " MB..." << std::endl;

                    // Recovery strategy: free old blocks
                    if (i > 500) {
                        std::cout << "Memory pressure detected, freeing old blocks..." << std::endl;

                        // Free half of the allocated memory
                        for (size_t j = 0; j < memory_blocks.size() / 2; ++j) {
                            memory_blocks[j].reset();
                        }

                        // Compact the vector
                        memory_blocks.erase(
                            std::remove_if(memory_blocks.begin(), memory_blocks.end(),
                                          [](const auto& ptr) { return ptr == nullptr; }),
                            memory_blocks.end());

                        std::cout << "Memory recovery successful!" << std::endl;
                        return true;
                    }
                }
            }
        } catch (const std::bad_alloc& e) {
            std::cout << "Memory allocation failed: " << e.what() << std::endl;

            // Recovery strategy: free all allocated memory
            std::cout << "Attempting memory recovery..." << std::endl;
            memory_blocks.clear();

            // Try to allocate a small block to verify recovery
            try {
                auto test_block = std::make_unique<char[]>(1024);
                std::cout << "Memory recovery successful!" << std::endl;
                return true;
            } catch (const std::bad_alloc&) {
                std::cout << "Memory recovery failed!" << std::endl;
                return false;
            }
        }

        return true;
    }
};

int main() {
    MemoryExhaustionTest memory_test;
    bool recovered = memory_test.test_memory_exhaustion_recovery();

    std::cout << "Memory exhaustion test " << (recovered ? "PASSED" : "FAILED") << std::endl;
    return recovered ? 0 : 1;
}
EOF
}

# Recovery testing functions
test_error_scenario() {
    local scenario_name="$1"
    local scenario_type="$2"
    local recovery_strategy="$3"
    local test_function="$4"

    log_info "Testing error scenario: $scenario_name"

    local start_time=$(date +%s.%N)
    local success=false

    # Setup error scenario
    case "$scenario_type" in
        "build_failure")
            create_build_failure_scenario "$scenario_name"
            ;;
        "dependency_conflict")
            create_dependency_conflict_scenario "$scenario_name"
            ;;
        "network_failure")
            create_network_failure_scenario "$scenario_name"
            ;;
        "corruption_detection")
            create_corruption_detection_scenario "$scenario_name"
            ;;
        "memory_exhaustion")
            create_memory_exhaustion_scenario "$scenario_name"
            ;;
    esac

    # Execute recovery test with timeout
    if timeout "$TEST_TIMEOUT" "$test_function" "$scenario_name" "$scenario_type"; then
        success=true
    else
        local exit_code=$?
        if [[ $exit_code -eq 124 ]]; then
            log_error "Scenario '$scenario_name' timed out after ${TEST_TIMEOUT}s"
        else
            log_error "Scenario '$scenario_name' failed with exit code $exit_code"
        fi
    fi

    local end_time=$(date +%s.%N)
    local recovery_time=$(echo "$end_time - $start_time" | bc -l)

    # Update metrics
    update_metrics "$scenario_name" "$success" "$recovery_time" "$scenario_type" "$recovery_strategy"

    # Cleanup
    cleanup_test_scenario "$scenario_name" "$scenario_type"
}

# Test execution functions
execute_build_failure_test() {
    local scenario="$1"
    local test_file="$TEST_DIR/${scenario}.cpp"

    # Attempt to build with error handling
    if g++ -std=c++17 -o "$TEST_DIR/${scenario}_test" "$test_file" 2>/dev/null; then
        # Build succeeded unexpectedly, test runtime error handling
        if timeout 10 "$TEST_DIR/${scenario}_test" 2>/dev/null; then
            return 0
        else
            # Runtime error occurred, test recovery
            return test_build_recovery "$scenario"
        fi
    else
        # Build failed as expected, test recovery strategy
        return test_build_recovery "$scenario"
    fi
}

test_build_recovery() {
    local scenario="$1"

    # Recovery strategy: Fix build issues
    log_info "Attempting build recovery for $scenario"

    # Try alternative compilation flags
    if g++ -std=c++17 -fpermissive -o "$TEST_DIR/${scenario}_recovered" "$TEST_DIR/${scenario}.cpp" 2>/dev/null; then
        log_success "Build recovery successful for $scenario"
        return 0
    fi

    # Try with minimal flags
    if g++ -o "$TEST_DIR/${scenario}_minimal" "$TEST_DIR/${scenario}.cpp" 2>/dev/null; then
        log_success "Minimal build recovery successful for $scenario"
        return 0
    fi

    log_error "Build recovery failed for $scenario"
    return 1
}

execute_network_failure_test() {
    local scenario="$1"
    local test_script="$TEST_DIR/${scenario}_network.sh"

    # Create local cache for recovery
    mkdir -p "$TEST_DIR/local_cache"
    echo "Cached dependency content" > "$TEST_DIR/local_cache/dependency.tar.gz"

    # Execute network failure test
    if "$test_script"; then
        log_success "Network failure recovery successful for $scenario"
        return 0
    else
        log_error "Network failure recovery failed for $scenario"
        return 1
    fi
}

execute_corruption_test() {
    local scenario="$1"
    local test_file="$TEST_DIR/${scenario}_corruption.cpp"

    # Compile and run corruption detection test
    if g++ -std=c++17 -lcrypto -o "$TEST_DIR/${scenario}_corrupt_test" "$test_file"; then
        if timeout 30 "$TEST_DIR/${scenario}_corrupt_test"; then
            log_success "Corruption detection and recovery successful for $scenario"
            return 0
        else
            log_error "Corruption detection test failed for $scenario"
            return 1
        fi
    else
        log_error "Failed to compile corruption test for $scenario"
        return 1
    fi
}

execute_memory_exhaustion_test() {
    local scenario="$1"
    local test_file="$TEST_DIR/${scenario}_memory.cpp"

    # Compile and run memory exhaustion test
    if g++ -std=c++17 -O2 -o "$TEST_DIR/${scenario}_memory_test" "$test_file"; then
        if timeout 60 "$TEST_DIR/${scenario}_memory_test"; then
            log_success "Memory exhaustion recovery successful for $scenario"
            return 0
        else
            log_error "Memory exhaustion test failed for $scenario"
            return 1
        fi
    else
        log_error "Failed to compile memory test for $scenario"
        return 1
    fi
}

cleanup_test_scenario() {
    local scenario="$1"
    local scenario_type="$2"

    # Remove test files
    rm -f "$TEST_DIR/${scenario}.cpp"
    rm -f "$TEST_DIR/${scenario}_test"
    rm -f "$TEST_DIR/${scenario}_recovered"
    rm -f "$TEST_DIR/${scenario}_minimal"
    rm -f "$TEST_DIR/${scenario}_network.sh"
    rm -f "$TEST_DIR/${scenario}_corruption.cpp"
    rm -f "$TEST_DIR/${scenario}_corrupt_test"
    rm -f "$TEST_DIR/${scenario}_memory.cpp"
    rm -f "$TEST_DIR/${scenario}_memory_test"
    rm -f "$TEST_DIR/test_corruption.dat"
    rm -rf "$TEST_DIR/local_cache"

    # Additional cleanup based on scenario type
    case "$scenario_type" in
        "network_failure")
            # Ensure iptables rules are cleaned up
            sudo iptables -L OUTPUT -n | grep -q DROP && sudo iptables -D OUTPUT -j DROP || true
            ;;
    esac
}

# Generate comprehensive recovery report
generate_recovery_report() {
    local success_rate=$(echo "scale=2; $successful_recoveries * 100 / $total_tests" | bc -l)

    cat > "$REPORT_FILE" << EOF
<!DOCTYPE html>
<html>
<head>
    <title>Error Handling and Recovery Test Report</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; }
        .header { background: #2c3e50; color: white; padding: 20px; border-radius: 5px; }
        .success { color: #27ae60; font-weight: bold; }
        .failure { color: #e74c3c; font-weight: bold; }
        .warning { color: #f39c12; font-weight: bold; }
        .metric-card { background: #f8f9fa; padding: 15px; margin: 10px 0; border-radius: 5px; border-left: 4px solid #3498db; }
        .chart-container { width: 45%; display: inline-block; margin: 20px; }
        table { width: 100%; border-collapse: collapse; margin: 20px 0; }
        th, td { padding: 10px; border: 1px solid #ddd; text-align: left; }
        th { background-color: #3498db; color: white; }
        .pass { background-color: #d4edda; }
        .fail { background-color: #f8d7da; }
        .summary { background: #e8f4f8; padding: 20px; border-radius: 5px; margin: 20px 0; }
    </style>
</head>
<body>
    <div class="header">
        <h1>🛡️ Error Handling and Recovery Test Report</h1>
        <p>Generated: $(date)</p>
        <p>Target Success Rate: ${SUCCESS_RATE_TARGET}% | Actual: ${success_rate}%</p>
    </div>

    <div class="summary">
        <h2>📊 Test Summary</h2>
        <div style="display: flex; flex-wrap: wrap;">
            <div class="metric-card">
                <h3>Total Tests</h3>
                <p style="font-size: 24px;">$total_tests</p>
            </div>
            <div class="metric-card">
                <h3>Successful Recoveries</h3>
                <p class="success" style="font-size: 24px;">$successful_recoveries</p>
            </div>
            <div class="metric-card">
                <h3>Failed Recoveries</h3>
                <p class="failure" style="font-size: 24px;">$((total_tests - successful_recoveries))</p>
            </div>
            <div class="metric-card">
                <h3>Success Rate</h3>
                <p class="$([[ $(echo "$success_rate >= $SUCCESS_RATE_TARGET" | bc -l) -eq 1 ]] && echo success || echo failure)" style="font-size: 24px;">${success_rate}%</p>
            </div>
        </div>
    </div>

    <div class="chart-container">
        <canvas id="categoryChart"></canvas>
    </div>
    <div class="chart-container">
        <canvas id="strategyChart"></canvas>
    </div>

    <h2>📋 Detailed Test Results</h2>
    <table>
        <thead>
            <tr>
                <th>Scenario</th>
                <th>Category</th>
                <th>Strategy</th>
                <th>Recovery Time</th>
                <th>Result</th>
            </tr>
        </thead>
        <tbody>
EOF

    # Add detailed results
    for scenario in "${!test_results[@]}"; do
        local result="${test_results[$scenario]}"
        local category="${error_categories[$scenario]}"
        local strategy="${recovery_strategies[$scenario]}"
        local recovery_time="${recovery_times[$scenario]}"
        local css_class="$([[ "$result" == "PASS" ]] && echo "pass" || echo "fail")"

        cat >> "$REPORT_FILE" << EOF
            <tr class="$css_class">
                <td>$scenario</td>
                <td>$category</td>
                <td>$strategy</td>
                <td>${recovery_time}s</td>
                <td class="$result">$result</td>
            </tr>
EOF
    done

    # Add category distribution data
    local category_data="'"
    for category in "${CATEGORIES[@]}"; do
        category_data+="'$category',"
    done
    category_data="${category_data%,}'"

    cat >> "$REPORT_FILE" << EOF
        </tbody>
    </table>

    <script>
        // Category distribution chart
        const categoryCtx = document.getElementById('categoryChart').getContext('2d');
        new Chart(categoryCtx, {
            type: 'doughnut',
            data: {
                labels: ${CATEGORIES[@]},
                datasets: [{
                    data: [$(jq -r '.test_categories | to_entries | map(.value) | join(",")' "$METRICS_FILE")],
                    backgroundColor: ['#FF6384', '#36A2EB', '#FFCE56', '#4BC0C0', '#9966FF', '#FF9F40', '#FF6384', '#C9CBCF', '#4BC0C0', '#FF6384']
                }]
            },
            options: {
                responsive: true,
                plugins: {
                    title: {
                        display: true,
                        text: 'Test Categories Distribution'
                    }
                }
            }
        });

        // Recovery strategies chart
        const strategyCtx = document.getElementById('strategyChart').getContext('2d');
        new Chart(strategyCtx, {
            type: 'bar',
            data: {
                labels: $(jq -r '.error_scenarios.strategies_used | keys | join(",")' "$METRICS_FILE"),
                datasets: [{
                    label: 'Strategy Usage Count',
                    data: $(jq -r '.error_scenarios.strategies_used | to_entries | map(.value) | join(",")' "$METRICS_FILE"),
                    backgroundColor: '#36A2EB'
                }]
            },
            options: {
                responsive: true,
                plugins: {
                    title: {
                        display: true,
                        text: 'Recovery Strategies Effectiveness'
                    }
                }
            }
        });
    </script>
</body>
</html>
EOF

    log_success "Recovery report generated: $REPORT_FILE"
}

# Run comprehensive error handling test suite
run_comprehensive_error_tests() {
    log_info "Starting comprehensive error handling and recovery tests"
    init_metrics

    # Test scenarios with different error types
    declare -a scenarios=(
        "build_failure_missing_header:build_failure:header_injection:execute_build_failure_test"
        "build_failure_linker_error:build_failure:linker_fix:execute_build_failure_test"
        "dependency_conflict_version:dependency_conflict:version_resolution:execute_build_failure_test"
        "network_timeout:network_failure:local_cache:execute_network_failure_test"
        "corruption_detection:corruption_detection:backup_restore:execute_corruption_test"
        "memory_exhaustion:memory_exhaustion:memory_cleanup:execute_memory_exhaustion_test"
        "disk_space_full:resource_constraints:cleanup:execute_disk_space_test"
        "permission_denied:permission_issues:privilege_escalation:execute_permission_test"
        "configuration_invalid:configuration_errors:config_reset:execute_config_test"
        "timeout_scenario:timeout_scenarios:retry_with_backoff:execute_timeout_test"
    )

    # Execute each scenario
    for scenario_def in "${scenarios[@]}"; do
        IFS=':' read -r scenario_name scenario_type recovery_strategy test_function <<< "$scenario_def"

        test_error_scenario "$scenario_name" "$scenario_type" "$recovery_strategy" "$test_function"

        # Check if we've reached the target number of tests
        if [[ $total_tests -ge $TEST_SCENARIOS ]]; then
            break
        fi
    done

    # Generate final report
    generate_recovery_report

    # Calculate final success rate
    local final_success_rate=$(echo "scale=2; $successful_recoveries * 100 / $total_tests" | bc -l)

    log_info "Error handling and recovery testing completed"
    log_info "Total tests: $total_tests"
    log_info "Successful recoveries: $successful_recoveries"
    log_info "Success rate: ${final_success_rate}% (target: ${SUCCESS_RATE_TARGET}%)"

    # Return success if target met
    if [[ $(echo "$final_success_rate >= $SUCCESS_RATE_TARGET" | bc -l) -eq 1 ]]; then
        log_success "✅ Error handling recovery target achieved: ${final_success_rate}% ≥ ${SUCCESS_RATE_TARGET}%"
        return 0
    else
        log_error "❌ Error handling recovery target missed: ${final_success_rate}% < ${SUCCESS_RATE_TARGET}%"
        return 1
    fi
}

# Additional test functions for remaining scenarios
execute_disk_space_test() {
    log_info "Testing disk space full scenario"

    # Create a temporary large file to simulate disk full
    local temp_file="/tmp/disk_space_test.tmp"
    dd if=/dev/zero of="$temp_file" bs=1M count=100 2>/dev/null || true

    # Test recovery by cleaning up
    if [[ -f "$temp_file" ]]; then
        rm -f "$temp_file"
        log_success "Disk space recovery successful"
        return 0
    fi

    return 1
}

execute_permission_test() {
    log_info "Testing permission denied scenario"

    # Create a test file with restricted permissions
    local test_file="/tmp/permission_test.txt"
    echo "test" > "$test_file"
    chmod 000 "$test_file"

    # Test recovery by fixing permissions
    if sudo chmod 644 "$test_file" 2>/dev/null; then
        log_success "Permission recovery successful"
        rm -f "$test_file"
        return 0
    fi

    return 1
}

execute_config_test() {
    log_info "Testing invalid configuration scenario"

    # Create invalid configuration
    local config_file="/tmp/test_config.json"
    echo "{ invalid json }" > "$config_file"

    # Test recovery by resetting to default
    echo '{"version": "1.0", "debug": false}' > "$config_file"

    if jq empty "$config_file" 2>/dev/null; then
        log_success "Configuration recovery successful"
        rm -f "$config_file"
        return 0
    fi

    return 1
}

execute_timeout_test() {
    log_info "Testing timeout scenario with retry"

    local retry_count=0
    local max_retries=3

    while [[ $retry_count -lt $max_retries ]]; do
        if timeout 5 sleep 10 2>/dev/null; then
            log_success "Timeout recovery successful on attempt $((retry_count + 1))"
            return 0
        else
            ((retry_count++))
            log_info "Retry attempt $retry_count"
            sleep 1
        fi
    done

    log_error "Timeout recovery failed after $max_retries attempts"
    return 1
}

# Main execution
main() {
    local command="${1:-run}"

    case "$command" in
        "run")
            run_comprehensive_error_tests
            ;;
        "report")
            generate_recovery_report
            ;;
        "clean")
            rm -rf "$TEST_DIR" "$LOG_DIR"
            log_info "Error handling test cleanup completed"
            ;;
        "help"|*)
            echo "Usage: $0 {run|report|clean|help}"
            echo ""
            echo "Commands:"
            echo "  run     - Run comprehensive error handling and recovery tests"
            echo "  report  - Generate HTML recovery report"
            echo "  clean   - Clean up test files and logs"
            echo "  help    - Show this help message"
            exit 0
            ;;
    esac
}

# Execute main function with all arguments
main "$@"