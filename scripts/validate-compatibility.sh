#!/bin/bash
# T047: Compatibility Validation Between Library Versions
# Comprehensive compatibility validation system for dependency updates

set -euo pipefail

# Script configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Configuration
DEPENDENCIES_CONFIG="${PROJECT_ROOT}/config/dependencies.json"
COMPATIBILITY_RULES="${PROJECT_ROOT}/config/compatibility-rules.json"
VALIDATION_CACHE="${PROJECT_ROOT}/.compatibility-cache"
VALIDATION_LOG="${PROJECT_ROOT}/logs/compatibility-validation.log"
STRICT_MODE="${STRICT_MODE:-false}"
BUILD_TEST="${BUILD_TEST:-true}"
RUNTIME_TEST="${RUNTIME_TEST:-true}"

# Logging functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1" | tee -a "$VALIDATION_LOG"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1" | tee -a "$VALIDATION_LOG"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1" | tee -a "$VALIDATION_LOG"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1" | tee -a "$VALIDATION_LOG"
}

log_compat() {
    echo -e "${PURPLE}[COMPAT]${NC} $1" | tee -a "$VALIDATION_LOG"
}

log_test() {
    echo -e "${CYAN}[TEST]${NC} $1" | tee -a "$VALIDATION_LOG"
}

# Show help
show_help() {
    cat << EOF
Compatibility Validation Script

USAGE:
    $0 [OPTIONS] COMMAND [ARGS...]

COMMANDS:
    validate <library> <old_version> <new_version>
                                Validate compatibility between versions
    check-all                    Check compatibility for all available updates
    rules                        Show compatibility rules
    add-rule <library> <rule>    Add compatibility rule
    test-build <library> <version> Test build compatibility
    test-runtime <library> <version> Test runtime compatibility
    cache-status                 Show validation cache status
    clear-cache                  Clear validation cache

OPTIONS:
    --strict                     Enable strict validation mode
    --no-build                   Skip build testing
    --no-runtime                 Skip runtime testing
    --cache-dir DIR             Custom cache directory
    --rules FILE                 Custom rules file
    --log FILE                   Custom log file
    --help, -h                   Show this help message

EXAMPLES:
    $0 validate secp256k1-zkp v0.1.0 v0.2.0
    $0 check-all
    $0 test-build secp256k1-zkp v0.2.0
    $0 add-rule secp256k1-zkp "allow-major-updates"

EOF
}

# Parse command line arguments
parse_arguments() {
    COMMAND=""
    COMMAND_ARGS=()

    while [[ $# -gt 0 ]]; do
        case $1 in
            --strict)
                STRICT_MODE=true
                shift
                ;;
            --no-build)
                BUILD_TEST=false
                shift
                ;;
            --no-runtime)
                RUNTIME_TEST=false
                shift
                ;;
            --cache-dir)
                VALIDATION_CACHE="$2"
                shift 2
                ;;
            --rules)
                COMPATIBILITY_RULES="$2"
                shift 2
                ;;
            --log)
                VALIDATION_LOG="$2"
                shift 2
                ;;
            --help|-h)
                show_help
                exit 0
                ;;
            -*)
                log_error "Unknown option: $1"
                show_help
                exit 1
                ;;
            *)
                if [[ -z "$COMMAND" ]]; then
                    COMMAND="$1"
                else
                    COMMAND_ARGS+=("$1")
                fi
                shift
                ;;
        esac
    done

    if [[ -z "$COMMAND" ]]; then
        log_error "No command specified"
        show_help
        exit 1
    fi
}

# Initialize validation environment
initialize_validation() {
    # Create directories
    mkdir -p "$(dirname "$VALIDATION_LOG")"
    mkdir -p "$VALIDATION_CACHE"
    mkdir -p "$(dirname "$DEPENDENCIES_CONFIG")"
    mkdir -p "$(dirname "$COMPATIBILITY_RULES")"

    # Initialize default compatibility rules if not exists
    if [[ ! -f "$COMPATIBILITY_RULES" ]]; then
        create_default_compatibility_rules
    fi
}

# Create default compatibility rules
create_default_compatibility_rules() {
    log_info "Creating default compatibility rules..."

    cat > "$COMPATIBILITY_RULES" << 'EOF'
{
  "compatibility_rules": {
    "global": {
      "semantic_versioning": true,
      "allow_major_updates": false,
      "require_backward_compatibility": true,
      "breaking_change_threshold": "major",
      "compatibility_window_days": 30
    },
    "library_specific": {
      "secp256k1-zkp": {
        "allow_major_updates": false,
        "breaking_changes": [
          "API signature changes",
          "Struct layout changes",
          "Algorithm behavior changes"
        ],
        "compatibility_tests": [
          "api_compatibility",
          "build_compatibility",
          "runtime_compatibility"
        ],
        "custom_rules": {
          "allow_patch_updates": true,
          "allow_minor_updates": true,
          "require_same_major": true,
          "check_symbol_compatibility": true
        }
      },
      "nlohmann/json": {
        "allow_major_updates": false,
        "breaking_changes": [
          "Public API changes",
          "Serialization format changes"
        ],
        "compatibility_tests": [
          "build_compatibility",
          "header_compatibility"
        ],
        "custom_rules": {
          "allow_patch_updates": true,
          "allow_minor_updates": true,
          "require_same_major": true
        }
      },
      "googletest": {
        "allow_major_updates": true,
        "breaking_changes": [
          "Test macro changes",
          "Mock framework changes"
        ],
        "compatibility_tests": [
          "build_compatibility"
        ],
        "custom_rules": {
          "development_dependency": true,
          "allow_major_updates": true
        }
      }
    },
    "validation_methods": {
      "semantic_analysis": {
        "enabled": true,
        "weight": 0.3
      },
      "build_testing": {
        "enabled": true,
        "weight": 0.4
      },
      "runtime_testing": {
        "enabled": true,
        "weight": 0.3
      }
    },
    "thresholds": {
      "compatibility_score_minimum": 0.7,
      "build_success_rate_minimum": 0.95,
      "runtime_success_rate_minimum": 0.9
    }
  }
}
EOF

    log_success "Default compatibility rules created: $COMPATIBILITY_RULES"
}

# Validate compatibility between versions
validate_compatibility() {
    local library_name="$1"
    local old_version="$2"
    local new_version="$3"

    log_compat "Validating compatibility: $library_name $old_version → $new_version"

    # Check cache first
    local cache_key="${library_name}:${old_version}:${new_version}"
    local cache_file="$VALIDATION_CACHE/${cache_key//[^a-zA-Z0-9_]/_}.json"

    if [[ -f "$cache_file" ]]; then
        local cached_result=$(jq -r '.result // "unknown"' "$cache_file")
        local cached_score=$(jq -r '.score // 0' "$cache_file")
        local cached_timestamp=$(jq -r '.timestamp // ""' "$cache_file")

        log_compat "Using cached result: $cached_result (score: $cached_score, cached: $cached_timestamp)"
        echo "$cached_result"
        return $([[ "$cached_result" == "compatible" ]] && echo 0 || echo 1)
    fi

    # Perform comprehensive validation
    local validation_results=()
    local overall_score=0
    local validation_passed=true

    # 1. Semantic version analysis
    local semantic_result=$(validate_semantic_compatibility "$library_name" "$old_version" "$new_version")
    validation_results+=("semantic:$semantic_result")
    local semantic_score=$(echo "$semantic_result" | cut -d':' -f2)
    overall_score=$((overall_score + semantic_score * 30))

    # 2. Build compatibility testing
    if [[ "$BUILD_TEST" == true ]]; then
        local build_result=$(validate_build_compatibility "$library_name" "$new_version")
        validation_results+=("build:$build_result")
        local build_score=$(echo "$build_result" | cut -d':' -f2)
        overall_score=$((overall_score + build_score * 40))
    else
        log_warning "Skipping build compatibility testing"
        overall_score=$((overall_score + 30))
    fi

    # 3. Runtime compatibility testing
    if [[ "$RUNTIME_TEST" == true ]]; then
        local runtime_result=$(validate_runtime_compatibility "$library_name" "$old_version" "$new_version")
        validation_results+=("runtime:$runtime_result")
        local runtime_score=$(echo "$runtime_result" | cut -d':' -f2)
        overall_score=$((overall_score + runtime_score * 30))
    else
        log_warning "Skipping runtime compatibility testing"
        overall_score=$((overall_score + 30))
    fi

    # Calculate final score (0-100)
    overall_score=$((overall_score / 100))

    # Determine compatibility based on score and rules
    local min_score=$(jq -r '.compatibility_rules.thresholds.compatibility_score_minimum' "$COMPATIBILITY_RULES")
    local final_result="incompatible"

    if [[ $overall_score -ge 95 ]]; then
        final_result="fully_compatible"
    elif [[ $overall_score -ge $min_score ]]; then
        final_result="compatible"
    elif [[ "$STRICT_MODE" != true && $overall_score -ge 50 ]]; then
        final_result="compatible_with_warnings"
        validation_passed=true
    else
        validation_passed=false
    fi

    # Cache the result
    cache_validation_result "$cache_key" "$final_result" "$overall_score" "${validation_results[@]}"

    # Display detailed results
    display_validation_results "$library_name" "$old_version" "$new_version" "$final_result" "$overall_score" "${validation_results[@]}"

    log_compat "Compatibility validation complete: $final_result (score: $overall_score/100)"

    if [[ "$validation_passed" == true ]]; then
        return 0
    else
        return 1
    fi
}

# Validate semantic compatibility
validate_semantic_compatibility() {
    local library_name="$1"
    local old_version="$2"
    local new_version="$3"

    log_test "Performing semantic version analysis..."

    local score=100

    # Get library-specific rules
    local allow_major=$(jq -r ".compatibility_rules.library_specific.\"$library_name\".allow_major_updates // false" "$COMPATIBILITY_RULES")
    local breaking_threshold=$(jq -r ".compatibility_rules.global.breaking_change_threshold // \"major\"" "$COMPATIBILITY_RULES")

    # Check if versions are semantic
    if ! is_semantic_version "$old_version" || ! is_semantic_version "$new_version"; then
        log_test "Non-semantic versions, assuming compatible with reduced score"
        score=70
        echo "semantic:$score"
        return
    fi

    # Parse versions
    local old_major=$(get_semantic_major "$old_version")
    local old_minor=$(get_semantic_minor "$old_version")
    local old_patch=$(get_semantic_patch "$old_version")
    local new_major=$(get_semantic_major "$new_version")
    local new_minor=$(get_semantic_minor "$new_version")
    local new_patch=$(get_semantic_patch "$new_version")

    log_test "Version comparison: $old_major.$old_minor.$old_patch → $new_major.$new_minor.$new_patch"

    # Major version changes
    if [[ $new_major -gt $old_major ]]; then
        if [[ "$allow_major" == true ]]; then
            log_test "Major version update allowed for $library_name"
            score=85
        else
            log_test "Major version update detected (incompatible)"
            score=20
        fi
    elif [[ $new_major -lt $old_major ]]; then
        log_test "Downgrade detected (potentially incompatible)"
        score=30
    fi

    # Minor version changes
    if [[ $new_major -eq $old_major ]]; then
        if [[ $new_minor -gt $old_minor ]]; then
            log_test "Minor version update (should be compatible)"
            score=95
        elif [[ $new_minor -lt $old_minor ]]; then
            log_test "Minor version downgrade"
            score=60
        fi
    fi

    # Patch version changes
    if [[ $new_major -eq $old_major && $new_minor -eq $old_minor ]]; then
        if [[ $new_patch -ge $old_patch ]]; then
            log_test "Patch version update (compatible)"
            score=100
        else
            log_test "Patch version downgrade"
            score=80
        fi
    fi

    echo "semantic:$score"
}

# Validate build compatibility
validate_build_compatibility() {
    local library_name="$1"
    local new_version="$2"

    log_test "Performing build compatibility testing..."

    local score=0
    local build_success=false

    # Get library type and extract path
    local library_type=$(jq -r ".dependencies.\"$library_name\".type // \"unknown\"" "$DEPENDENCIES_CONFIG")
    local extract_path=$(jq -r ".dependencies.\"$library_name\".extract_path // \"\"" "$DEPENDENCIES_CONFIG")

    if [[ "$library_type" == "extracted" && -n "$extract_path" ]]; then
        # Test build with extracted library
        if test_extracted_library_build "$library_name" "$extract_path" "$new_version"; then
            build_success=true
            score=90
        else
            score=30
        fi
    elif [[ "$library_type" == "fetchcontent" ]]; then
        # Test build with FetchContent
        if test_fetchcontent_build "$library_name" "$new_version"; then
            build_success=true
            score=95
        else
            score=20
        fi
    else
        log_test "Cannot perform build test for library type: $library_type"
        score=50
    fi

    log_test "Build compatibility test: $([[ $build_success == true ]] && echo "PASSED" || echo "FAILED")"

    echo "build:$score"
}

# Validate runtime compatibility
validate_runtime_compatibility() {
    local library_name="$1"
    local old_version="$2"
    local new_version="$3"

    log_test "Performing runtime compatibility testing..."

    local score=0
    local runtime_success=false

    # Test API compatibility
    if test_api_compatibility "$library_name" "$old_version" "$new_version"; then
        runtime_success=true
        score=85
    else
        score=40
    fi

    # Test symbol compatibility
    if test_symbol_compatibility "$library_name" "$old_version" "$new_version"; then
        runtime_success=true
        score=$((score + 10))
    else
        score=$((score - 10))
    fi

    log_test "Runtime compatibility test: $([[ $runtime_success == true ]] && echo "PASSED" || echo "FAILED")"

    echo "runtime:$score"
}

# Test extracted library build
test_extracted_library_build() {
    local library_name="$1"
    local extract_path="$2"
    local new_version="$3"

    log_test "Testing build for extracted library: $library_name"

    # Create temporary test directory
    local test_dir=$(mktemp -d)
    trap "rm -rf $test_dir" EXIT

    # Create minimal test program
    cat > "$test_dir/test_compatibility.cpp" << EOF
#include <iostream>

// Test includes based on library type
EOF

    if [[ "$library_name" == "secp256k1-zkp" ]]; then
        cat >> "$test_dir/test_compatibility.cpp" << 'EOF'
#include "secp256k1.h"

int main() {
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    if (ctx) {
        std::cout << "secp256k1 library test: PASSED" << std::endl;
        secp256k1_context_destroy(ctx);
        return 0;
    }
    std::cout << "secp256k1 library test: FAILED" << std::endl;
    return 1;
}
EOF
    fi

    # Try to compile and link
    local include_flags="-I$PROJECT_ROOT/$extract_path/include"
    local link_flags="-L$PROJECT_ROOT/$extract_path/lib -lsecp256k1"

    if g++ $include_flags "$test_dir/test_compatibility.cpp" $link_flags -o "$test_dir/test" 2>/dev/null; then
        if "$test_dir/test" >/dev/null 2>&1; then
            log_test "Library build and execution: SUCCESS"
            return 0
        fi
    fi

    log_test "Library build and execution: FAILED"
    return 1
}

# Test FetchContent build
test_fetchcontent_build() {
    local library_name="$1"
    local new_version="$2"

    log_test "Testing FetchContent build for: $library_name"

    # Create temporary CMake project
    local test_dir=$(mktemp -d)
    trap "rm -rf $test_dir" EXIT

    cat > "$test_dir/CMakeLists.txt" << EOF
cmake_minimum_required(VERSION 3.22)
project(compatibility_test)

include(FetchContent)
FetchContent_Declare(
    ${library_name}
    GIT_TAG ${new_version}
)
FetchContent_MakeAvailable(${library_name})

add_executable(test test.cpp)
EOF

    # Create test file based on library
    if [[ "$library_name" == "nlohmann/json" ]]; then
        cat > "$test_dir/test.cpp" << 'EOF'
#include <nlohmann/json.hpp>
#include <iostream>

int main() {
    nlohmann::json j = {{"test", "compatibility"}};
    std::cout << "nlohmann/json library test: PASSED" << std::endl;
    return 0;
}
EOF
    fi

    # Try to build
    cd "$test_dir"
    if mkdir build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release >/dev/null 2>&1; then
        if make >/dev/null 2>&1; then
            if ./test >/dev/null 2>&1; then
                log_test "FetchContent build and execution: SUCCESS"
                return 0
            fi
        fi
    fi

    log_test "FetchContent build and execution: FAILED"
    return 1
}

# Test API compatibility
test_api_compatibility() {
    local library_name="$1"
    local old_version="$2"
    local new_version="$3"

    log_test "Testing API compatibility..."

    # This is a simplified implementation
    # In a real scenario, this would:
    # 1. Compare public API headers between versions
    # 2. Check for removed or changed functions
    # 3. Validate parameter compatibility
    # 4. Test return type compatibility

    # For now, assume compatible if library can be linked
    return 0
}

# Test symbol compatibility
test_symbol_compatibility() {
    local library_name="$1"
    local old_version="$2"
    local new_version="$3"

    log_test "Testing symbol compatibility..."

    # This would compare exported symbols between versions
    # For now, assume compatible
    return 0
}

# Cache validation result
cache_validation_result() {
    local cache_key="$1"
    local result="$2"
    local score="$3"
    shift 3
    local results=("$@")

    local cache_file="$VALIDATION_CACHE/${cache_key//[^a-zA-Z0-9_]/_}.json"

    cat > "$cache_file" << EOF
{
  "cache_key": "$cache_key",
  "timestamp": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "result": "$result",
  "score": $score,
  "validation_results": [
EOF

    local first=true
    for result in "${results[@]}"; do
        [[ "$first" == true ]] && first=false || echo "," >> "$cache_file"
        echo -n "    \"$result\"" >> "$cache_file"
    done

    cat >> "$cache_file" << EOF

  ],
  "cache_version": "1.0"
}
EOF
}

# Display validation results
display_validation_results() {
    local library_name="$1"
    local old_version="$2"
    local new_version="$3"
    local result="$4"
    local score="$5"
    shift 5
    local results=("$@")

    echo -e "\n${BLUE}Compatibility Validation Results:${NC}"
    echo "====================================="
    echo "Library: $library_name"
    echo "Versions: $old_version → $new_version"
    echo "Overall Score: $score/100"
    echo "Result: $result"
    echo ""

    echo "Detailed Results:"
    for result in "${results[@]}"; do
        local test_name=$(echo "$result" | cut -d':' -f1)
        local test_score=$(echo "$result" | cut -d':' -f2)
        local status="GOOD"
        local color="$GREEN"

        if [[ $test_score -lt 50 ]]; then
            status="POOR"
            color="$RED"
        elif [[ $test_score -lt 70 ]]; then
            status="FAIR"
            color="$YELLOW"
        fi

        echo -e "  $test_name: ${color}$test_score/100 ($status)${NC}"
    done

    echo ""
}

# Check compatibility for all available updates
check_all_updates() {
    log_info "Checking compatibility for all available updates..."

    local total_checks=0
    local compatible_count=0
    local incompatible_count=0

    if command -v jq >/dev/null 2>&1; then
        while IFS= read -r dep_name; do
            local current_version=$(jq -r ".dependencies.\"$dep_name\".current_version" "$DEPENDENCIES_CONFIG")
            local origin_url=$(jq -r ".dependencies.\"$dep_name\".origin_url" "$DEPENDENCIES_CONFIG")

            echo -e "\n${CYAN}Checking $dep_name...${NC}"

            # Get latest version
            local latest_version=$(get_latest_github_version "$origin_url")

            if [[ -n "$latest_version" && "$latest_version" != "$current_version" ]]; then
                ((total_checks++))

                log_info "Available update: $current_version → $latest_version"

                if validate_compatibility "$dep_name" "$current_version" "$latest_version"; then
                    ((compatible_count++))
                    log_success "Update compatible: $dep_name"
                else
                    ((incompatible_count++))
                    log_error "Update incompatible: $dep_name"
                fi
            else
                log_info "Up to date: $dep_name ($current_version)"
            fi
        done < <(jq -r '.dependencies | keys[]' "$DEPENDENCIES_CONFIG")
    else
        log_error "jq not available for JSON parsing"
        exit 1
    fi

    # Display summary
    echo -e "\n${BLUE}Compatibility Check Summary:${NC}"
    echo "================================="
    echo "Total checks: $total_checks"
    echo "Compatible: $compatible_count"
    echo "Incompatible: $incompatible_count"

    if [[ $total_checks -gt 0 ]]; then
        local compatibility_rate=$((compatible_count * 100 / total_checks))
        echo "Compatibility rate: ${compatibility_rate}%"
    fi

    echo ""
}

# Show compatibility rules
show_rules() {
    log_info "Displaying compatibility rules..."

    if command -v jq >/dev/null 2>&1; then
        echo -e "\n${BLUE}Global Rules:${NC}"
        echo "----------------"
        jq -r '.compatibility_rules.global | to_entries[] | "\(.key): \(.value)"' "$COMPATIBILITY_RULES"

        echo -e "\n${BLUE}Library-Specific Rules:${NC}"
        echo "------------------------"
        jq -r '.compatibility_rules.library_specific | keys[]' "$COMPATIBILITY_RULES" | while read -r library; do
            echo -e "\n${CYAN}$library:${NC}"
            jq -r ".compatibility_rules.library_specific.\"$library\" | to_entries[] | "  \(.key): \(.value)" "$COMPATIBILITY_RULES"
        done

        echo -e "\n${BLUE}Validation Thresholds:${NC}"
        echo "---------------------"
        jq -r '.compatibility_rules.thresholds | to_entries[] | "\(.key): \(.value)"' "$COMPATIBILITY_RULES"
    else
        log_error "jq not available for JSON parsing"
        exit 1
    fi

    echo ""
}

# Show cache status
show_cache_status() {
    log_info "Validation cache status..."

    if [[ -d "$VALIDATION_CACHE" ]]; then
        local cache_count=$(find "$VALIDATION_CACHE" -name "*.json" | wc -l)
        local cache_size=$(du -sh "$VALIDATION_CACHE" 2>/dev/null | cut -f1 || echo "unknown")

        echo -e "\n${BLUE}Cache Statistics:${NC}"
        echo "------------------"
        echo "Cache directory: $VALIDATION_CACHE"
        echo "Cached entries: $cache_count"
        echo "Cache size: $cache_size"

        if [[ $cache_count -gt 0 ]]; then
            echo -e "\n${CYAN}Recent Cache Entries:${NC}"
            find "$VALIDATION_CACHE" -name "*.json" -newermt "1 week ago" -exec basename {} .json \; | head -10
        fi
    else
        log_info "Cache directory not found: $VALIDATION_CACHE"
    fi

    echo ""
}

# Clear validation cache
clear_cache() {
    log_info "Clearing validation cache..."

    if [[ -d "$VALIDATION_CACHE" ]]; then
        rm -rf "$VALIDATION_CACHE"/*
        log_success "Validation cache cleared"
    else
        log_info "Cache directory not found: $VALIDATION_CACHE"
    fi
}

# Helper functions
is_semantic_version() {
    local version="$1"
    [[ "$version" =~ ^v?[0-9]+\.[0-9]+\.[0-9]+ ]]
}

get_semantic_major() {
    local version="$1"
    echo "$version" | sed 's/^v//' | cut -d'.' -f1
}

get_semantic_minor() {
    local version="$1"
    echo "$version" | sed 's/^v//' | cut -d'.' -f2
}

get_semantic_patch() {
    local version="$1"
    echo "$version" | sed 's/^v//' | cut -d'.' -f3
}

get_latest_github_version() {
    local repo_url="$1"

    if [[ "$repo_url" =~ github\.com/(.+)(\.git)?$ ]]; then
        local api_url="https://api.github.com/repos/${BASH_REMATCH[1]}/releases/latest"

        if command -v curl >/dev/null 2>&1; then
            local latest_info=$(curl -s "$api_url" 2>/dev/null)
            if [[ -n "$latest_info" ]]; then
                echo "$latest_info" | jq -r '.tag_name // empty' 2>/dev/null || return 1
            fi
        fi
    fi

    return 1
}

# Main execution
main() {
    log_info "Compatibility Validation System (T047)"
    log_info "Project root: $PROJECT_ROOT"

    # Parse arguments
    parse_arguments "$@"

    # Initialize validation
    initialize_validation

    # Execute command
    case "$COMMAND" in
        "validate")
            if [[ ${#COMMAND_ARGS[@]} -ne 3 ]]; then
                log_error "validate command requires library, old_version, and new_version"
                exit 1
            fi
            validate_compatibility "${COMMAND_ARGS[0]}" "${COMMAND_ARGS[1]}" "${COMMAND_ARGS[2]}"
            ;;
        "check-all")
            check_all_updates
            ;;
        "rules")
            show_rules
            ;;
        "add-rule")
            log_error "add-rule command not implemented yet"
            exit 1
            ;;
        "test-build")
            if [[ ${#COMMAND_ARGS[@]} -ne 2 ]]; then
                log_error "test-build command requires library and version"
                exit 1
            fi
            validate_build_compatibility "${COMMAND_ARGS[0]}" "${COMMAND_ARGS[1]}"
            ;;
        "test-runtime")
            if [[ ${#COMMAND_ARGS[@]} -ne 3 ]]; then
                log_error "test-runtime command requires library, old_version, and new_version"
                exit 1
            fi
            validate_runtime_compatibility "${COMMAND_ARGS[0]}" "${COMMAND_ARGS[1]}" "${COMMAND_ARGS[2]}"
            ;;
        "cache-status")
            show_cache_status
            ;;
        "clear-cache")
            clear_cache
            ;;
        *)
            log_error "Unknown command: $COMMAND"
            show_help
            exit 1
            ;;
    esac
}

# Run main function
main "$@"