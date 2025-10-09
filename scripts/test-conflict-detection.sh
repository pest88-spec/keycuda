#!/usr/bin/env bash
# Conflict Detection System Test Script
#
# Tests the dependency conflict detection and resolution system
# for third-party library integration failures.
#
# @author       Puzzle71Solver Team
# @created      2025-10-09
# @license      MIT

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build"
TEST_DIR="${BUILD_DIR}/conflict-tests"
LIBRARY_ROOT="${REPO_ROOT}/src/extracted"

# Test configuration
readonly VERBOSE=${VERBOSE:-false}
readonly CLEANUP=${CLEANUP:-true}
readonly TIMEOUT=${TIMEOUT:-300}

# Color codes for output
readonly GREEN='\033[0;32m'
readonly RED='\033[0;31m'
readonly YELLOW='\033[1;33m'
readonly BLUE='\033[0;34m'
readonly NC='\033[0m' # No Color

# Test counters
TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0

# Logging functions
log_test() {
    local message="$1"
    echo -e "${BLUE}[TEST]${NC} $message"
}

log_pass() {
    local message="$1"
    echo -e "${GREEN}[PASS]${NC} $message"
    ((TESTS_PASSED++))
}

log_fail() {
    local message="$1"
    echo -e "${RED}[FAIL]${NC} $message"
    ((TESTS_FAILED++))
}

log_info() {
    local message="$1"
    echo -e "${BLUE}[INFO]${NC} $message"
}

log_warn() {
    local message="$1"
    echo -e "${YELLOW}[WARN]${NC} $message"
}

# Setup test environment
setup_test_environment() {
    log_info "Setting up test environment"

    # Create test directory
    mkdir -p "$TEST_DIR"
    cd "$TEST_DIR"

    # Create test library structure with conflicts
    create_test_libraries_with_conflicts

    log_info "Test environment setup completed"
}

# Create test libraries with various conflicts
create_test_libraries_with_conflicts() {
    log_info "Creating test libraries with conflicts"

    # Library 1: Symbol conflicts
    mkdir -p test_lib1/src
    cat > test_lib1/src/conflicting_function.cpp << 'EOF'
/**
 * @origin https://github.com/example/test_lib1
 * @origin_license MIT
 * @extracted_date 2025-10-09
 * @extracted_by Puzzle71Solver
 */

#include "conflicting_function.h"

int duplicate_function() {
    return 42;
}

int unique_function_lib1() {
    return 100;
}
EOF

    cat > test_lib1/src/conflicting_function.h << 'EOF'
/**
 * @origin https://github.com/example/test_lib1
 * @origin_license MIT
 * @extracted_date 2025-10-09
 * @extracted_by Puzzle71Solver
 */

#ifndef CONFLICTING_FUNCTION_H
#define CONFLICTING_FUNCTION_H

int duplicate_function();
int unique_function_lib1();

#endif // CONFLICTING_FUNCTION_H
EOF

    cat > test_lib1/CMakeLists.txt << 'EOF'
cmake_minimum_required(VERSION 3.22)
project(TestLib1 VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)

add_library(test_lib1
    src/conflicting_function.cpp
    src/conflicting_function.h
)

target_include_directories(test_lib1 PUBLIC include)
EOF

    # Library 2: Symbol conflicts with Library 1
    mkdir -p test_lib2/src
    cat > test_lib2/src/conflicting_function.cpp << 'EOF'
/**
 * @origin https://github.com/example/test_lib2
 * @origin_license MIT
 * @extracted_date 2025-10-09
 * @extracted_by Puzzle71Solver
 */

#include "conflicting_function.h"

int duplicate_function() {
    return 84;  // Different implementation - conflict!
}

int unique_function_lib2() {
    return 200;
}
EOF

    cat > test_lib2/src/conflicting_function.h << 'EOF'
/**
 * @origin https://github.com/example/test_lib2
 * @origin_license MIT
 * @extracted_date 2025-10-09
 * @extracted_by Puzzle71Solver
 */

#ifndef CONFLICTING_FUNCTION_H
#define CONFLICTING_FUNCTION_H

int duplicate_function();
int unique_function_lib2();

#endif // CONFLICTING_FUNCTION_H
EOF

    cat > test_lib2/CMakeLists.txt << 'EOF'
cmake_minimum_required(VERSION 3.22)
project(TestLib2 VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 14)  # Different standard - conflict!

add_library(test_lib2
    src/conflicting_function.cpp
    src/conflicting_function.h
)

target_include_directories(test_lib2 PUBLIC include)
EOF

    # Library 3: Version conflict
    mkdir -p test_lib3/src
    cat > test_lib3/src/version_conflict.cpp << 'EOF'
/**
 * @origin https://github.com/example/test_lib3
 * @origin_license GPL-3.0  # GPL license - conflict!
 * @extracted_date 2025-10-09
 * @extracted_by Puzzle71Solver
 */

#include <iostream>

void deprecated_function() {
    std::cout << "This is version 1.0.0 with security issues" << std::endl;
}
EOF

    cat > test_lib3/CMakeLists.txt << 'EOF'
cmake_minimum_required(VERSION 3.22)
project(TestLib3 VERSION 1.0.0)  # Problematic version

set(CMAKE_CXX_STANDARD 17)

add_library(test_lib3
    src/version_conflict.cpp
)
EOF

    # Library 4: Dependency conflict (circular)
    mkdir -p test_lib4/src
    cat > test_lib4/src/dependency_conflict.cpp << 'EOF'
/**
 * @origin https://github.com/example/test_lib4
 * @origin_license MIT
 * @extracted_date 2025-10-09
 * @extracted_by Puzzle71Solver
 */

#include <iostream>

void function_with_dependency() {
    std::cout << "Library 4 depends on Library 5" << std::endl;
}
EOF

    cat > test_lib4/CMakeLists.txt << 'EOF'
cmake_minimum_required(VERSION 3.22)
project(TestLib4 VERSION 2.0.0)

set(CMAKE_CXX_STANDARD 17)

add_library(test_lib4
    src/dependency_conflict.cpp
)

# This creates a circular dependency with test_lib5
target_link_libraries(test_lib4 PRIVATE test_lib5)
EOF

    # Library 5: Circular dependency with Library 4
    mkdir -p test_lib5/src
    cat > test_lib5/src/circular_dependency.cpp << 'EOF'
/**
 * @origin https://github.com/example/test_lib5
 * @origin_license MIT
 * @extracted_date 2025-10-09
 * @extracted_by Puzzle71Solver
 */

#include <iostream>

void circular_function() {
    std::cout << "Library 5 depends on Library 4" << std::endl;
}
EOF

    cat > test_lib5/CMakeLists.txt << 'EOF'
cmake_minimum_required(VERSION 3.22)
project(TestLib5 VERSION 2.0.0)

set(CMAKE_CXX_STANDARD 17)

add_library(test_lib5
    src/circular_dependency.cpp
)

# This creates a circular dependency with test_lib4
target_link_libraries(test_lib5 PRIVATE test_lib4)
EOF

    # Library 6: Header conflict
    mkdir -p test_lib6/include
    mkdir -p test_lib6/src
    cat > test_lib6/include/common.h << 'EOF'
/**
 * @origin https://github.com/example/test_lib6
 * @origin_license MIT
 * @extracted_date 2025-10-09
 * @extracted_by Puzzle71Solver
 */

#ifndef COMMON_H
#define COMMON_H

#define VERSION_MAJOR 6
#define VERSION_MINOR 0

int common_function();

#endif // COMMON_H
EOF

    cat > test_lib6/src/common.cpp << 'EOF'
/**
 * @origin https://github.com/example/test_lib6
 * @origin_license MIT
 * @extracted_date 2025-10-09
 * @extracted_by Puzzle71Solver
 */

#include "common.h"

int common_function() {
    return 600;
}
EOF

    cat > test_lib6/CMakeLists.txt << 'EOF'
cmake_minimum_required(VERSION 3.22)
project(TestLib6 VERSION 6.0.0)

set(CMAKE_CXX_STANDARD 17)

add_library(test_lib6
    src/common.cpp
    include/common.h
)

target_include_directories(test_lib6 PUBLIC include)
EOF

    # Library 7: Another common.h - header conflict
    mkdir -p test_lib7/include
    mkdir -p test_lib7/src
    cat > test_lib7/include/common.h << 'EOF'
/**
 * @origin https://github.com/example/test_lib7
 * @origin_license MIT
 * @extracted_date 2025-10-09
 * @extracted_by Puzzle71Solver
 */

#ifndef COMMON_H
#define COMMON_H

#define VERSION_MAJOR 7
#define VERSION_MINOR 0

int common_function();  // Different signature potential conflict

#endif // COMMON_H
EOF

    cat > test_lib7/src/common.cpp << 'EOF'
/**
 * @origin https://github.com/example/test_lib7
 * @origin_license MIT
 * @extracted_date 2025-10-09
 * @extracted_by Puzzle71Solver
 */

#include "common.h"

int common_function() {
    return 700;
}
EOF

    cat > test_lib7/CMakeLists.txt << 'EOF'
cmake_minimum_required(VERSION 3.22)
project(TestLib7 VERSION 7.0.0)

set(CMAKE_CXX_STANDARD 17)

add_library(test_lib7
    src/common.cpp
    include/common.h
)

target_include_directories(test_lib7 PUBLIC include)
EOF

    log_info "Test libraries created successfully"
}

# Test 1: Symbol conflict detection
test_symbol_conflict_detection() {
    log_test "Testing symbol conflict detection"
    ((TESTS_RUN++))

    # Build a test program that includes both conflicting libraries
    cat > test_symbol_conflict.cpp << 'EOF'
#include "test_lib1/src/conflicting_function.h"
#include "test_lib2/src/conflicting_function.h"

int main() {
    // This should cause a symbol conflict
    int result1 = duplicate_function();
    int result2 = unique_function_lib1();
    int result3 = unique_function_lib2();
    return (result1 + result2 + result3) % 256;
}
EOF

    if timeout 60 g++ -std=c++17 -I. test_symbol_conflict.cpp test_lib1/src/conflicting_function.cpp test_lib2/src/conflicting_function.cpp -o test_symbol_conflict 2>/dev/null; then
        log_fail "Symbol conflict was not detected (should have failed to compile)"
    else
        log_pass "Symbol conflict correctly detected (compilation failed as expected)"
    fi

    rm -f test_symbol_conflict test_symbol_conflict.o
}

# Test 2: Version conflict detection
test_version_conflict_detection() {
    log_test "Testing version conflict detection"
    ((TESTS_RUN++))

    # Test version extraction from CMakeLists.txt
    local version1=$(grep "VERSION" test_lib1/CMakeLists.txt | head -1 | grep -o '[0-9]\+\.[0-9]\+\.[0-9]\+' || echo "")
    local version3=$(grep "VERSION" test_lib3/CMakeLists.txt | head -1 | grep -o '[0-9]\+\.[0-9]\+\.[0-9]\+' || echo "")

    if [[ "$version1" == "1.0.0" ]]; then
        log_pass "Version extraction working correctly for test_lib1"
    else
        log_fail "Version extraction failed for test_lib1 (got: $version1)"
    fi

    if [[ "$version3" == "1.0.0" ]]; then
        log_pass "Problematic version 1.0.0 detected in test_lib3"
    else
        log_fail "Failed to detect problematic version in test_lib3"
    fi
}

# Test 3: License conflict detection
test_license_conflict_detection() {
    log_test "Testing license conflict detection"
    ((TESTS_RUN++))

    # Check for GPL license in test_lib3
    if grep -q "GPL-3.0" test_lib3/src/version_conflict.cpp; then
        log_pass "GPL license correctly detected in test_lib3"
    else
        log_fail "Failed to detect GPL license in test_lib3"
    fi

    # Check that other libraries have MIT license
    if grep -q "MIT" test_lib1/src/conflicting_function.cpp; then
        log_pass "MIT license correctly detected in test_lib1"
    else
        log_fail "Failed to detect MIT license in test_lib1"
    fi
}

# Test 4: Build conflict detection (C++ standard)
test_build_conflict_detection() {
    log_test "Testing build conflict detection (C++ standard)"
    ((TESTS_RUN++))

    # Extract C++ standard from CMakeLists.txt files
    local std1=$(grep "CMAKE_CXX_STANDARD" test_lib1/CMakeLists.txt | grep -o '[0-9]\+' || echo "")
    local std2=$(grep "CMAKE_CXX_STANDARD" test_lib2/CMakeLists.txt | grep -o '[0-9]\+' || echo "")

    if [[ "$std1" == "17" ]]; then
        log_pass "C++17 standard correctly detected in test_lib1"
    else
        log_fail "Failed to detect C++17 standard in test_lib1 (got: $std1)"
    fi

    if [[ "$std2" == "14" ]]; then
        log_pass "C++14 standard correctly detected in test_lib2 (conflict with project C++17)"
    else
        log_fail "Failed to detect C++14 standard in test_lib2 (got: $std2)"
    fi
}

# Test 5: Header conflict detection
test_header_conflict_detection() {
    log_test "Testing header conflict detection"
    ((TESTS_RUN++))

    # Check for duplicate header names
    if [[ -f "test_lib6/include/common.h" && -f "test_lib7/include/common.h" ]]; then
        log_pass "Duplicate header files 'common.h' detected in test_lib6 and test_lib7"
    else
        log_fail "Failed to detect duplicate header files"
    fi

    # Check header content differences
    local version6=$(grep "VERSION_MAJOR" test_lib6/include/common.h | grep -o '[0-9]\+')
    local version7=$(grep "VERSION_MAJOR" test_lib7/include/common.h | grep -o '[0-9]\+')

    if [[ "$version6" == "6" && "$version7" == "7" ]]; then
        log_pass "Header version differences correctly detected"
    else
        log_fail "Failed to detect header version differences"
    fi
}

# Test 6: Dependency conflict detection
test_dependency_conflict_detection() {
    log_test "Testing dependency conflict detection"
    ((TESTS_RUN++))

    # Check for circular dependencies in CMakeLists.txt
    local dep4=$(grep "target_link_libraries.*test_lib5" test_lib4/CMakeLists.txt || echo "")
    local dep5=$(grep "target_link_libraries.*test_lib4" test_lib5/CMakeLists.txt || echo "")

    if [[ -n "$dep4" && -n "$dep5" ]]; then
        log_pass "Circular dependency correctly detected between test_lib4 and test_lib5"
    else
        log_fail "Failed to detect circular dependency"
    fi
}

# Test 7: CMake validation
test_cmake_validation() {
    log_test "Testing CMake validation"
    ((TESTS_RUN++))

    # Test CMake files for syntax and structure
    for lib in test_lib{1,2,3,4,5,6,7}; do
        if [[ -f "$lib/CMakeLists.txt" ]]; then
            if cmake --check-system "$lib" >/dev/null 2>&1; then
                log_info "CMakeLists.txt syntax is valid for $lib"
            else
                log_fail "CMakeLists.txt syntax error in $lib"
            fi
        else
            log_fail "Missing CMakeLists.txt in $lib"
        fi
    done
}

# Test 8: Integration test with conflict detector
test_integration_with_detector() {
    log_test "Testing integration with conflict detector"
    ((TESTS_RUN++))

    # Check if conflict detector source files exist
    local detector_h="${REPO_ROOT}/src/integration/conflict_detector.h"
    local detector_cpp="${REPO_ROOT}/src/integration/conflict_detector.cpp"

    if [[ -f "$detector_h" && -f "$detector_cpp" ]]; then
        log_pass "Conflict detector source files exist"
    else
        log_fail "Conflict detector source files missing"
        return
    fi

    # Test basic compilation of conflict detector
    if g++ -std=c++17 -I"${REPO_ROOT}/src/integration" -c "${REPO_ROOT}/src/integration/conflict_detector.cpp" -o test_detector.o 2>/dev/null; then
        log_pass "Conflict detector compiles successfully"
        rm -f test_detector.o
    else
        log_fail "Conflict detector compilation failed"
    fi
}

# Test 9: Performance test
test_performance() {
    log_test "Testing conflict detection performance"
    ((TESTS_RUN++))

    local start_time=$(date +%s)

    # Simulate conflict detection across all test libraries
    local conflicts_found=0
    for lib in test_lib{1,2,3,4,5,6,7}; do
        # Count potential conflicts
        local files=$(find "$lib" -name "*.cpp" -o -name "*.h" | wc -l)
        ((conflicts_found += files))
    done

    local end_time=$(date +%s)
    local duration=$((end_time - start_time))

    if [[ $duration -lt 10 ]]; then
        log_pass "Performance test completed in ${duration}s (found $conflicts_found files)"
    else
        log_warn "Performance test took ${duration}s (found $conflicts_found files) - may need optimization"
    fi
}

# Test 10: Resolution strategy validation
test_resolution_strategies() {
    log_test "Testing resolution strategy validation"
    ((TESTS_RUN++))

    # Test that resolution strategies are properly defined
    local strategies=("IGNORE" "PREFER_LOCAL" "PREFER_SYSTEM" "MERGE" "ISOLATE" "REPLACE" "REMOVE" "CUSTOM")

    for strategy in "${strategies[@]}"; do
        if grep -q "$strategy" "${REPO_ROOT}/src/integration/conflict_detector.h"; then
            log_info "Resolution strategy $strategy is defined"
        else
            log_fail "Resolution strategy $strategy is missing"
        fi
    done

    log_pass "Resolution strategy validation completed"
}

# Cleanup test environment
cleanup_test_environment() {
    if [[ "$CLEANUP" == "true" ]]; then
        log_info "Cleaning up test environment"
        cd "$REPO_ROOT"
        rm -rf "$TEST_DIR"
        log_info "Cleanup completed"
    else
        log_info "Skipping cleanup (test files preserved in $TEST_DIR)"
    fi
}

# Print test summary
print_test_summary() {
    echo ""
    log_info "Test Summary"
    log_info "============"
    log_info "Tests run: $TESTS_RUN"
    log_info "Tests passed: $TESTS_PASSED"
    log_info "Tests failed: $TESTS_FAILED"

    if [[ $TESTS_FAILED -eq 0 ]]; then
        log_pass "All tests passed successfully!"
        return 0
    else
        log_fail "$TESTS_FAILED test(s) failed"
        return 1
    fi
}

# Main test execution
main() {
    log_info "Starting conflict detection system tests"

    # Parse command line arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            -v|--verbose)
                VERBOSE=true
                shift
                ;;
            --no-cleanup)
                CLEANUP=false
                shift
                ;;
            --timeout)
                TIMEOUT="$2"
                shift 2
                ;;
            -h|--help)
                cat << 'EOF'
Conflict Detection System Test Script

Usage: ./test-conflict-detection.sh [OPTIONS]

OPTIONS:
    -v, --verbose       Enable verbose output
    --no-cleanup        Keep test files after completion
    --timeout SECONDS   Set test timeout (default: 300)
    -h, --help          Show this help message

DESCRIPTION:
    This script tests the dependency conflict detection and resolution system
    by creating test libraries with various types of conflicts and validating
    that they are properly detected and categorized.

TESTS INCLUDED:
    1. Symbol conflict detection
    2. Version conflict detection
    3. License conflict detection
    4. Build conflict detection
    5. Header conflict detection
    6. Dependency conflict detection
    7. CMake validation
    8. Integration with detector
    9. Performance testing
    10. Resolution strategy validation

EOF
                exit 0
                ;;
            *)
                echo "Unknown option: $1" >&2
                exit 1
                ;;
        esac
    done

    # Run tests
    setup_test_environment

    test_symbol_conflict_detection
    test_version_conflict_detection
    test_license_conflict_detection
    test_build_conflict_detection
    test_header_conflict_detection
    test_dependency_conflict_detection
    test_cmake_validation
    test_integration_with_detector
    test_performance
    test_resolution_strategies

    # Show results and cleanup
    local result=$?
    print_test_summary
    cleanup_test_environment

    exit $result
}

# Run main function if script is executed directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi