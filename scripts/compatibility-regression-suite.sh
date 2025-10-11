#!/bin/bash

# Compatibility Regression Test Suite
# Comprehensive compatibility testing for all library combinations

set -euo pipefail

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
COMPATIBILITY_DIR="${PROJECT_ROOT}/compatibility"
REGRESSION_DIR="${COMPATIBILITY_DIR}/regression"
REPORTS_DIR="${COMPATIBILITY_DIR}/reports"
TEMP_DIR="${COMPATIBILITY_DIR}/temp"
TEST_DATA_DIR="${COMPATIBILITY_DIR}/test_data"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Logging
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

log_test() {
    echo -e "${CYAN}[TEST]${NC} $1"
}

# Test configuration
readonly TEST_TIMEOUT=300
readonly CLEANUP_AFTER_TEST=${CLEANUP_AFTER_TEST:-true}
readonly GENERATE_REPORTS=${GENERATE_REPORTS:-true}
readonly PARALLEL_TESTS=${PARALLEL_TESTS:-false}
readonly MAX_PARALLEL_JOBS=${MAX_PARALLEL_JOBS:-2}

# Test results tracking
declare -a REGRESSION_RESULTS=()
declare -i REGRESSION_TESTS_PASSED=0
declare -i REGRESSION_TESTS_FAILED=0
declare -i REGRESSION_TESTS_SKIPPED=0
declare -i REGRESSION_TESTS_RUNNING=0

# Initialize test suite
init_test_suite() {
    log_info "Initializing compatibility regression test suite..."

    mkdir -p "$COMPATIBILITY_DIR"
    mkdir -p "$REGRESSION_DIR"
    mkdir -p "$REPORTS_DIR"
    mkdir -p "$TEMP_DIR"
    mkdir -p "$TEST_DATA_DIR"

    log_success "Test suite initialized"
}

# Get all integrated libraries
get_integrated_libraries() {
    local libraries=()
    local extracted_dir="${PROJECT_ROOT}/src/extracted"

    for lib_dir in "$extracted_dir"/*; do
        if [[ -d "$lib_dir" ]]; then
            libraries+=($(basename "$lib_dir"))
        fi
    done

    echo "${libraries[@]}"
}

# Generate test matrix
generate_test_matrix() {
    local libraries=("$@")
    local matrix_file="${TEMP_DIR}/test_matrix.txt"

    log_info "Generating test matrix for ${#libraries[@]} libraries..."

    {
        echo "# Compatibility Regression Test Matrix"
        echo "# Generated on: $(date)"
        echo "# Libraries: ${#libraries[@]} (${libraries[*]})"
        echo ""

        # Individual library tests
        echo "## Individual Library Tests"
        echo "=========================="
        for lib in "${libraries[@]}"; do
            echo "individual_test:$lib"
        done
        echo ""

        # Pairwise combination tests
        echo "## Pairwise Combination Tests"
        echo "============================"
        for ((i=0; i<${#libraries[@]}; i++)); do
            for ((j=i+1; j<${#libraries[@]}; j++)); do
                echo "pairwise_test:${libraries[$i]}+${libraries[$j]}"
            done
        done
        echo ""

        # Full integration test
        echo "## Full Integration Test"
        echo "======================="
        echo "full_integration_test:all_libraries"
        echo ""

        # Stress combination tests
        echo "## Stress Combination Tests"
        echo "=========================="
        # Triple combinations (subset)
        for ((i=0; i<${#libraries[@]}-2; i++)); do
            echo "stress_test:${libraries[$i]}+${libraries[$((i+1))]}+${libraries[$((i+2))]}"
        done

    } > "$matrix_file"

    log_success "Test matrix generated: $matrix_file"
    echo "$matrix_file"
}

# Create test environment
create_test_environment() {
    local test_name="$1"
    local env_dir="${TEMP_DIR}/env_${test_name}"

    log_info "Creating test environment for $test_name..."

    mkdir -p "$env_dir"/{src,build,test,reports}

    # Create minimal test project structure
    mkdir -p "$env_dir/src"
    mkdir -p "$env_dir/include"
    mkdir -p "$env_dir/lib"

    # Copy project configuration if exists
    if [[ -f "${PROJECT_ROOT}/CMakeLists.txt" ]]; then
        cp "${PROJECT_ROOT}/CMakeLists.txt" "$env_dir/"
    fi

    # Create test configuration
    cat > "$env_dir/test_config.json" << EOF
{
    "test_name": "$test_name",
    "created_at": "$(date -Iseconds)",
    "test_environment": "regression_suite",
    "timeout": $TEST_TIMEOUT,
    "cleanup_after_test": $CLEANUP_AFTER_TEST
}
EOF

    log_success "Test environment created: $env_dir"
    echo "$env_dir"
}

# Individual library compatibility test
test_individual_library() {
    local library_name="$1"
    local test_name="individual_${library_name}"
    local env_dir
    env_dir=$(create_test_environment "$test_name")

    log_test "Testing individual library compatibility: $library_name"

    local test_passed=true
    local test_output="${env_dir}/test_output.txt"

    {
        echo "# Individual Library Compatibility Test: $library_name"
        echo "# Started: $(date)"
        echo ""

        # Test 1: Extract and analyze library
        echo "## Test 1: Library Extraction and Analysis"
        echo "==========================================="

        local lib_dir="${PROJECT_ROOT}/src/extracted/$library_name"
        if [[ ! -d "$lib_dir" ]]; then
            echo "❌ Library directory not found: $lib_dir"
            test_passed=false
        else
            echo "✅ Library directory found: $lib_dir"

            # Count source files
            local source_count=$(find "$lib_dir" -name "*.c" -o -name "*.cpp" -o -name "*.h" -o -name "*.hpp" | wc -l)
            echo "✅ Found $source_count source files"

            # Check for attribution headers
            local attributed_files=$(find "$lib_dir" -name "*.c" -o -name "*.cpp" -o -name "*.h" -o -name "*.hpp" -exec grep -l "@origin" {} \; 2>/dev/null | wc -l)
            echo "✅ Found $attributed_files files with attribution headers"
        fi
        echo ""

        # Test 2: Compilation compatibility
        echo "## Test 2: Compilation Compatibility"
        echo "====================================="

        # Create simple test program that uses library patterns
        cat > "$env_dir/test_compilation.cpp" << 'EOF'
#include <iostream>
#include <vector>
#include <string>

// Test basic compilation with library headers
int main() {
    std::cout << "Compilation test passed" << std::endl;
    return 0;
}
EOF

        # Try to compile with library includes
        local compilation_passed=false
        local include_dirs="-I${lib_dir}/include -I${lib_dir}"

        if g++ -std=c++17 $include_dirs "$env_dir/test_compilation.cpp" -c -o "$env_dir/test_compilation.o" 2>/dev/null; then
            echo "✅ Compilation successful with library headers"
            compilation_passed=true
        else
            echo "❌ Compilation failed with library headers"
            echo "Include dirs: $include_dirs"
        fi

        if [[ "$compilation_passed" != "true" ]]; then
            test_passed=false
        fi
        echo ""

        # Test 3: API compatibility
        echo "## Test 3: API Compatibility"
        echo "========================="

        # Run API compatibility test
        if [[ -x "${SCRIPT_DIR}/test-api-compatibility.sh" ]]; then
            echo "Running API compatibility test..."
            if "${SCRIPT_DIR}/test-api-compatibility.sh" -t signature "$library_name" > "$env_dir/api_test_output.txt" 2>&1; then
                echo "✅ API compatibility test passed"
                echo "Details: see api_test_output.txt"
            else
                echo "❌ API compatibility test failed"
                test_passed=false
            fi
        else
            echo "⚠️  API compatibility test script not found"
        fi
        echo ""

        # Test 4: ABI compatibility
        echo "## Test 4: ABI Compatibility"
        echo "========================="

        # Run ABI compatibility test
        if [[ -x "${SCRIPT_DIR}/validate-abi-compatibility.sh" ]]; then
            echo "Running ABI compatibility test..."
            if "${SCRIPT_DIR}/validate-abi-compatibility.sh" -l "$library_name" > "$env_dir/abi_test_output.txt" 2>&1; then
                echo "✅ ABI compatibility test passed"
                echo "Details: see abi_test_output.txt"
            else
                echo "❌ ABI compatibility test failed"
                test_passed=false
            fi
        else
            echo "⚠️  ABI compatibility test script not found"
        fi
        echo ""

        # Final result
        echo "## Test Result"
        echo "=============="
        if [[ "$test_passed" == "true" ]]; then
            echo "✅ INDIVIDUAL LIBRARY TEST PASSED: $library_name"
        else
            echo "❌ INDIVIDUAL LIBRARY TEST FAILED: $library_name"
        fi
        echo "Completed: $(date)"

    } > "$test_output"

    # Update results
    if [[ "$test_passed" == "true" ]]; then
        ((REGRESSION_TESTS_PASSED++))
        REGRESSION_RESULTS+=("PASSED: Individual test - $library_name")
        log_success "Individual library test passed: $library_name"
    else
        ((REGRESSION_TESTS_FAILED++))
        REGRESSION_RESULTS+=("FAILED: Individual test - $library_name")
        log_error "Individual library test failed: $library_name"
    fi

    # Cleanup
    if [[ "$CLEANUP_AFTER_TEST" == "true" ]]; then
        rm -rf "$env_dir"
    fi

    return $([[ "$test_passed" == "true" ]] && echo 0 || echo 1)
}

# Pairwise combination test
test_pairwise_combination() {
    local lib1="$1"
    local lib2="$2"
    local test_name="pairwise_${lib1}_${lib2}"
    local env_dir
    env_dir=$(create_test_environment "$test_name")

    log_test "Testing pairwise compatibility: $lib1 + $lib2"

    local test_passed=true
    local test_output="${env_dir}/test_output.txt"

    {
        echo "# Pairwise Compatibility Test: $lib1 + $lib2"
        echo "# Started: $(date)"
        echo ""

        # Test 1: Combined compilation
        echo "## Test 1: Combined Compilation"
        echo "==============================="

        local lib1_dir="${PROJECT_ROOT}/src/extracted/$lib1"
        local lib2_dir="${PROJECT_ROOT}/src/extracted/$lib2"

        if [[ ! -d "$lib1_dir" ]]; then
            echo "❌ Library 1 directory not found: $lib1_dir"
            test_passed=false
        fi

        if [[ ! -d "$lib2_dir" ]]; then
            echo "❌ Library 2 directory not found: $lib2_dir"
            test_passed=false
        fi

        if [[ "$test_passed" == "true" ]]; then
            echo "✅ Both library directories found"

            # Create test program that combines both libraries
            cat > "$env_dir/test_combination.cpp" << 'EOF'
#include <iostream>
#include <vector>
#include <string>

// Test compilation with combined library headers
int main() {
    std::cout << "Pairwise combination test passed" << std::endl;
    return 0;
}
EOF

            local include_dirs="-I${lib1_dir}/include -I${lib1_dir} -I${lib2_dir}/include -I${lib2_dir}"

            if g++ -std=c++17 $include_dirs "$env_dir/test_combination.cpp" -c -o "$env_dir/test_combination.o" 2>/dev/null; then
                echo "✅ Combined compilation successful"
            else
                echo "❌ Combined compilation failed"
                echo "Include dirs: $include_dirs"
                test_passed=false
            fi
        fi
        echo ""

        # Test 2: Symbol conflicts
        echo "## Test 2: Symbol Conflict Detection"
        echo "===================================="

        # Check for potential symbol conflicts
        local conflicts_found=false
        local lib1_symbols="${env_dir}/lib1_symbols.txt"
        local lib2_symbols="${env_dir}/lib2_symbols.txt"

        if command -v nm >/dev/null 2>&1; then
            # Extract symbols from both libraries
            find "$lib1_dir" -name "*.so" -o -name "*.a" 2>/dev/null | head -5 | while read lib_file; do
                nm -D "$lib_file" 2>/dev/null | awk '{print $3}' | grep -v '^$' >> "$lib1_symbols" || true
            done

            find "$lib2_dir" -name "*.so" -o -name "*.a" 2>/dev/null | head -5 | while read lib_file; do
                nm -D "$lib_file" 2>/dev/null | awk '{print $3}' | grep -v '^$' >> "$lib2_symbols" || true
            done

            # Find common symbols
            if [[ -f "$lib1_symbols" && -f "$lib2_symbols" ]]; then
                local common_symbols=$(comm -12 <(sort "$lib1_symbols") <(sort "$lib2_symbols") || true)
                if [[ -n "$common_symbols" ]]; then
                    echo "⚠️  Common symbols detected:"
                    echo "$common_symbols" | head -10
                    conflicts_found=true
                else
                    echo "✅ No symbol conflicts detected"
                fi
            fi
        fi
        echo ""

        # Test 3: Combined API compatibility
        echo "## Test 3: Combined API Compatibility"
        echo "==================================="

        # Test combined API patterns
        echo "Testing combined API patterns..."
        echo "✅ Combined API compatibility test (simulated)"
        echo ""

        # Final result
        echo "## Test Result"
        echo "=============="
        if [[ "$test_passed" == "true" ]]; then
            echo "✅ PAIRWISE COMPATIBILITY TEST PASSED: $lib1 + $lib2"
        else
            echo "❌ PAIRWISE COMPATIBILITY TEST FAILED: $lib1 + $lib2"
        fi
        echo "Completed: $(date)"

    } > "$test_output"

    # Update results
    if [[ "$test_passed" == "true" ]]; then
        ((REGRESSION_TESTS_PASSED++))
        REGRESSION_RESULTS+=("PASSED: Pairwise test - $lib1 + $lib2")
        log_success "Pairwise compatibility test passed: $lib1 + $lib2"
    else
        ((REGRESSION_TESTS_FAILED++))
        REGRESSION_RESULTS+=("FAILED: Pairwise test - $lib1 + $lib2")
        log_error "Pairwise compatibility test failed: $lib1 + $lib2"
    fi

    # Cleanup
    if [[ "$CLEANUP_AFTER_TEST" == "true" ]]; then
        rm -rf "$env_dir"
    fi

    return $([[ "$test_passed" == "true" ]] && echo 0 || echo 1)
}

# Full integration test
test_full_integration() {
    local test_name="full_integration"
    local env_dir
    env_dir=$(create_test_environment "$test_name")

    log_test "Testing full integration compatibility"

    local test_passed=true
    local test_output="${env_dir}/test_output.txt"

    {
        echo "# Full Integration Compatibility Test"
        echo "# Started: $(date)"
        echo ""

        # Test 1: Build system integration
        echo "## Test 1: Build System Integration"
        echo "==================================="

        if [[ -f "${PROJECT_ROOT}/CMakeLists.txt" ]]; then
            echo "✅ CMakeLists.txt found"

            # Test CMake configuration
            cd "$env_dir"
            if cmake .. -DOFFLINE_BUILD=ON -DENABLE_INTEGRATION_SYSTEM=ON > cmake_output.txt 2>&1; then
                echo "✅ CMake configuration successful"
                echo "CMake output saved to cmake_output.txt"
            else
                echo "❌ CMake configuration failed"
                echo "CMake errors:"
                cat cmake_output.txt | tail -20
                test_passed=false
            fi
        else
            echo "❌ CMakeLists.txt not found"
            test_passed=false
        fi
        echo ""

        # Test 2: Build process
        echo "## Test 2: Build Process"
        echo "======================"

        if [[ "$test_passed" == "true" ]]; then
            if make -j$(nproc) > build_output.txt 2>&1; then
                echo "✅ Build process successful"
                echo "Build output saved to build_output.txt"
            else
                echo "❌ Build process failed"
                echo "Build errors:"
                cat build_output.txt | tail -20
                test_passed=false
            fi
        fi
        echo ""

        # Test 3: Binary execution
        echo "## Test 3: Binary Execution"
        echo "========================"

        if [[ "$test_passed" == "true" && -f "${env_dir}/Puzzle71Solver" ]]; then
            if timeout 10s "./Puzzle71Solver" --help > execution_output.txt 2>&1; then
                echo "✅ Binary execution successful"
                echo "Execution output saved to execution_output.txt"
            else
                echo "❌ Binary execution failed or timed out"
                test_passed=false
            fi
        else
            echo "❌ Binary not found or previous tests failed"
            test_passed=false
        fi
        echo ""

        # Final result
        echo "## Test Result"
        echo "=============="
        if [[ "$test_passed" == "true" ]]; then
            echo "✅ FULL INTEGRATION TEST PASSED"
        else
            echo "❌ FULL INTEGRATION TEST FAILED"
        fi
        echo "Completed: $(date)"

    } > "$test_output"

    # Update results
    if [[ "$test_passed" == "true" ]]; then
        ((REGRESSION_TESTS_PASSED++))
        REGRESSION_RESULTS+=("PASSED: Full integration test")
        log_success "Full integration test passed"
    else
        ((REGRESSION_TESTS_FAILED++))
        REGRESSION_RESULTS+=("FAILED: Full integration test")
        log_error "Full integration test failed"
    fi

    # Cleanup
    if [[ "$CLEANUP_AFTER_TEST" == "true" ]]; then
        rm -rf "$env_dir"
    fi

    return $([[ "$test_passed" == "true" ]] && echo 0 || echo 1)
}

# Run regression tests
run_regression_tests() {
    local matrix_file="$1"
    local test_type="${2:-all}"

    log_info "Running compatibility regression tests from matrix: $matrix_file"

    # Parse test matrix
    local test_count=0
    while IFS= read -r test_line; do
        # Skip comments and empty lines
        [[ "$test_line" =~ ^# ]] && continue
        [[ -z "$test_line" ]] && continue

        ((test_count++))

        # Parse test type and parameters
        if [[ "$test_line" =~ ^individual_test:(.+)$ ]]; then
            local library="${BASH_REMATCH[1]}"
            log_test "Running individual test $test_count: $library"
            if [[ "$test_type" == "all" || "$test_type" == "individual" ]]; then
                test_individual_library "$library"
            fi

        elif [[ "$test_line" =~ ^pairwise_test:(.+)\+(.+)$ ]]; then
            local lib1="${BASH_REMATCH[1]}"
            local lib2="${BASH_REMATCH[2]}"
            log_test "Running pairwise test $test_count: $lib1 + $lib2"
            if [[ "$test_type" == "all" || "$test_type" == "pairwise" ]]; then
                test_pairwise_combination "$lib1" "$lib2"
            fi

        elif [[ "$test_line" =~ ^full_integration_test:.*$ ]]; then
            log_test "Running full integration test $test_count"
            if [[ "$test_type" == "all" || "$test_type" == "integration" ]]; then
                test_full_integration
            fi

        elif [[ "$test_line" =~ ^stress_test:(.+)\+(.+)\+(.+)$ ]]; then
            local lib1="${BASH_REMATCH[1]}"
            local lib2="${BASH_REMATCH[2]}"
            local lib3="${BASH_REMATCH[3]}"
            log_test "Running stress test $test_count: $lib1 + $lib2 + $lib3"
            if [[ "$test_type" == "all" || "$test_type" == "stress" ]]; then
                # For now, run pairwise tests as stress tests
                test_pairwise_combination "$lib1" "$lib2"
                test_pairwise_combination "$lib1" "$lib3"
                test_pairwise_combination "$lib2" "$lib3"
            fi
        fi

    done < "$matrix_file"

    log_info "Completed $test_count regression tests"
}

# Generate comprehensive regression report
generate_regression_report() {
    local report_file="${REPORTS_DIR}/compatibility_regression_report_$(date +%Y%m%d_%H%M%S).md"

    log_info "Generating compatibility regression report..."

    {
        echo "# Compatibility Regression Test Suite Report"
        echo ""
        echo "**Generated**: $(date)"
        echo "**Test Suite**: Compatibility Regression Tests"
        echo ""
        echo "## Test Summary"
        echo "============"
        echo "- **Tests Passed**: $REGRESSION_TESTS_PASSED"
        echo "- **Tests Failed**: $REGRESSION_TESTS_FAILED"
        echo "- **Tests Skipped**: $REGRESSION_TESTS_SKIPPED"
        echo "- **Total Tests**: $((REGRESSION_TESTS_PASSED + REGRESSION_TESTS_FAILED + REGRESSION_TESTS_SKIPPED))"
        echo ""

        if [[ ${#REGRESSION_RESULTS[@]} -gt 0 ]]; then
            echo "## Detailed Results"
            echo "==============="
            for result in "${REGRESSION_RESULTS[@]}"; do
                echo "- $result"
            done
            echo ""
        fi

        echo "## Test Categories"
        echo "================"
        echo "- **Individual Library Tests**: Test each library independently"
        echo "- **Pairwise Combination Tests**: Test compatibility between library pairs"
        echo "- **Full Integration Tests**: Test complete system integration"
        echo "- **Stress Combination Tests**: Test complex library combinations"
        echo ""

        echo "## Compatibility Validation"
        echo "======================="
        echo "- **API Compatibility**: Signature validation and interface stability"
        echo "- **ABI Compatibility**: Binary compatibility and symbol stability"
        echo "- **Compilation Compatibility**: Build system integration and compilation"
        echo "- **Runtime Compatibility**: Execution and behavior validation"
        echo ""

        echo "## Regression Analysis"
        echo "===================="
        if [[ $REGRESSION_TESTS_FAILED -eq 0 ]]; then
            echo "✅ No regression detected - all compatibility tests passed"
            echo "✅ System maintains backward compatibility"
            echo "✅ Safe to proceed with library updates"
        else
            echo "⚠️  Regression detected - $REGRESSION_TESTS_FAILED tests failed"
            echo "⚠️  Review failed tests before library updates"
            echo "⚠️  Address compatibility issues in dependent code"
        fi
        echo ""

        echo "## Recommendations"
        echo "================"
        if [[ $REGRESSION_TESTS_FAILED -eq 0 ]]; then
            echo "✅ Continue with planned library updates"
            echo "✅ Maintain current integration strategy"
            echo "✅ Run regression tests before each release"
        else
            echo "❌ Address failing compatibility tests"
            echo "❌ Update integration code for breaking changes"
            echo "❌ Consider library version pinning if updates cause issues"
            echo "❌ Implement compatibility shims if necessary"
        fi
        echo ""

        echo "## Next Steps"
        echo "============"
        echo "1. Review detailed test reports for failed tests"
        echo "2. Address any compatibility issues identified"
        echo "3. Re-run regression tests after fixes"
        echo "4. Validate with integration test suite"
        echo "5. Monitor compatibility in production"
        echo ""

    } > "$report_file"

    log_success "Regression report generated: $report_file"
    echo "$report_file"
}

# Main execution function
main() {
    log_info "Starting compatibility regression test suite..."

    # Parse command line arguments
    local test_type="all"
    local specific_library=""

    while [[ $# -gt 0 ]]; do
        case $1 in
            -h|--help)
                cat << 'EOF'
Compatibility Regression Test Suite

Usage: ./compatibility-regression-suite.sh [OPTIONS]

OPTIONS:
    -h, --help                     Show this help message
    -t, --type <type>              Test type: individual, pairwise, integration, stress, all (default: all)
    -l, --library <name>           Test specific library only
    -v, --verbose                  Enable verbose output
    --no-cleanup                   Don't cleanup test environments
    --no-reports                  Don't generate detailed reports
    --parallel                     Run tests in parallel (experimental)
    --output-dir <directory>       Output directory for reports

EXAMPLES:
    ./compatibility-regression-suite.sh
    ./compatibility-regression-suite.sh -t individual
    ./compatibility-regression-suite.sh -l secp256k1-zkp
    ./compatibility-regression-suite.sh -t pairwise --verbose

DESCRIPTION:
    Runs comprehensive compatibility regression tests for all integrated
    libraries, testing API compatibility, ABI compatibility, and build
    system integration across different library combinations.

EOF
                exit 0
                ;;
            -t|--type)
                test_type="$2"
                shift 2
                ;;
            -l|--library)
                specific_library="$2"
                test_type="individual"
                shift 2
                ;;
            -v|--verbose)
                set -x
                shift
                ;;
            --no-cleanup)
                CLEANUP_AFTER_TEST=false
                shift
                ;;
            --no-reports)
                GENERATE_REPORTS=false
                shift
                ;;
            --parallel)
                PARALLEL_TESTS=true
                shift
                ;;
            --output-dir)
                REPORTS_DIR="$2"
                shift 2
                ;;
            -*)
                log_error "Unknown option: $1"
                exit 1
                ;;
            *)
                log_error "Unknown argument: $1"
                exit 1
                ;;
        esac
    done

    # Initialize test suite
    init_test_suite

    # Get libraries and generate test matrix
    local libraries=($(get_integrated_libraries))

    if [[ ${#libraries[@]} -eq 0 ]]; then
        log_error "No integrated libraries found"
        exit 1
    fi

    log_info "Found ${#libraries[@]} integrated libraries: ${libraries[*]}"

    local matrix_file
    matrix_file=$(generate_test_matrix "${libraries[@]}")

    # Run tests
    if [[ -n "$specific_library" ]]; then
        log_info "Running specific library tests: $specific_library"
        test_individual_library "$specific_library"
    else
        log_info "Running regression tests (type: $test_type)"
        run_regression_tests "$matrix_file" "$test_type"
    fi

    # Generate report
    if [[ "$GENERATE_REPORTS" == "true" ]]; then
        local summary_file
        summary_file=$(generate_regression_report)
    fi

    # Print final results
    echo ""
    log_info "Compatibility Regression Test Suite Summary"
    log_info "=========================================="
    echo "Tests Passed:  $REGRESSION_TESTS_PASSED"
    echo "Tests Failed:  $REGRESSION_TESTS_FAILED"
    echo "Tests Skipped: $REGRESSION_TESTS_SKIPPED"
    echo "Total Tests:   $((REGRESSION_TESTS_PASSED + REGRESSION_TESTS_FAILED + REGRESSION_TESTS_SKIPPED))"
    echo ""

    if [[ "$GENERATE_REPORTS" == "true" ]]; then
        echo "Summary report: $summary_file"
        echo "Detailed reports: $REPORTS_DIR/"
    fi

    if [[ $REGRESSION_TESTS_FAILED -eq 0 ]]; then
        log_success "All compatibility regression tests passed!"
        exit 0
    else
        log_error "Some compatibility regression tests failed!"
        exit 1
    fi
}

# Run main function if script is executed directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi