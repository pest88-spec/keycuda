#!/bin/bash

# Puzzle71Solver - Deployment Documentation Generator
#
# Generates comprehensive deployment documentation including build information,
# dependency manifests, test results, and deployment guides.
#
# Author: Puzzle71Solver Team
# Created: 2025-10-10
# License: MIT

set -euo pipefail

# Script constants
readonly SCRIPT_NAME="$(basename "$0")"
readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Default configuration
DEFAULT_OUTPUT_DIR="${PROJECT_ROOT}/build/deployment/docs"
DEFAULT_INCLUDE_BUILD_INFO="true"
DEFAULT_INCLUDE_DEPENDENCIES="true"
DEFAULT_INCLUDE_TEST_RESULTS="true"

# Exit codes
readonly EXIT_SUCCESS=0
readonly EXIT_INVALID_ARGS=1
readonly EXIT_MISSING_TOOLS=2
readonly EXIT_FILE_GENERATION_FAILED=3

# Color output
readonly RED='\033[0;31m'
readonly GREEN='\033[0;32m'
readonly YELLOW='\033[1;33m'
readonly BLUE='\033[0;34m'
readonly NC='\033[0m'

# Logging functions
log_info() {
    echo -e "${GREEN}[INFO]${NC} $*" >&2
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $*" >&2
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $*" >&2
}

log_debug() {
    if [[ "${DEBUG:-false}" == "true" ]]; then
        echo -e "${BLUE}[DEBUG]${NC} $*" >&2
    fi
}

# Show usage information
show_usage() {
    cat << EOF
Usage: $SCRIPT_NAME [OPTIONS]

Generate comprehensive deployment documentation for Puzzle71Solver.

OPTIONS:
    --output-dir DIR           Output directory for documentation (default: $DEFAULT_OUTPUT_DIR)
    --include-build-info       Include build system information (default: enabled)
    --no-include-build-info    Exclude build system information
    --include-dependencies     Include dependency information (default: enabled)
    --no-include-dependencies  Exclude dependency information
    --include-test-results     Include test results (default: enabled)
    --no-include-test-results  Exclude test results
    --format FORMAT            Output format: html, markdown, pdf (default: markdown)
    --verbose                  Enable verbose logging
    --debug                    Enable debug output
    --help                     Show this help message

EXAMPLES:
    $SCRIPT_NAME
    $SCRIPT_NAME --output-dir ./deployment-docs --format html
    $SCRIPT_NAME --no-include-test-results --verbose

EOF
}

# Parse command line arguments
parse_args() {
    OUTPUT_DIR="$DEFAULT_OUTPUT_DIR"
    INCLUDE_BUILD_INFO="$DEFAULT_INCLUDE_BUILD_INFO"
    INCLUDE_DEPENDENCIES="$DEFAULT_INCLUDE_DEPENDENCIES"
    INCLUDE_TEST_RESULTS="$DEFAULT_INCLUDE_TEST_RESULTS"
    OUTPUT_FORMAT="markdown"
    VERBOSE="false"
    DEBUG="false"

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --output-dir)
                OUTPUT_DIR="$2"
                shift 2
                ;;
            --include-build-info)
                INCLUDE_BUILD_INFO="true"
                shift
                ;;
            --no-include-build-info)
                INCLUDE_BUILD_INFO="false"
                shift
                ;;
            --include-dependencies)
                INCLUDE_DEPENDENCIES="true"
                shift
                ;;
            --no-include-dependencies)
                INCLUDE_DEPENDENCIES="false"
                shift
                ;;
            --include-test-results)
                INCLUDE_TEST_RESULTS="true"
                shift
                ;;
            --no-include-test-results)
                INCLUDE_TEST_RESULTS="false"
                shift
                ;;
            --format)
                OUTPUT_FORMAT="$2"
                shift 2
                ;;
            --verbose)
                VERBOSE="true"
                shift
                ;;
            --debug)
                DEBUG="true"
                shift
                ;;
            --help)
                show_usage
                exit $EXIT_SUCCESS
                ;;
            *)
                log_error "Unknown option: $1"
                show_usage
                exit $EXIT_INVALID_ARGS
                ;;
        esac
    done

    # Validate arguments
    if [[ ! "$OUTPUT_FORMAT" =~ ^(markdown|html|pdf)$ ]]; then
        log_error "Invalid output format: $OUTPUT_FORMAT. Supported formats: markdown, html, pdf"
        exit $EXIT_INVALID_ARGS
    fi

    # Convert to absolute paths
    OUTPUT_DIR="$(realpath "$OUTPUT_DIR")"
}

# Check prerequisites
check_prerequisites() {
    log_debug "Checking prerequisites..."

    local missing_tools=()

    # Check required tools
    for tool in find date sed awk; do
        if ! command -v "$tool" >/dev/null 2>&1; then
            missing_tools+=("$tool")
        fi
    done

    # Check optional tools based on output format
    if [[ "$OUTPUT_FORMAT" == "html" ]] && ! command -v pandoc >/dev/null 2>&1; then
        log_warn "pandoc not found, HTML generation may be limited"
    fi

    if [[ "$OUTPUT_FORMAT" == "pdf" ]] && ! command -v pandoc >/dev/null 2>&1; then
        log_error "pandoc required for PDF generation but not found"
        missing_tools+=("pandoc")
    fi

    if [[ ${#missing_tools[@]} -gt 0 ]]; then
        log_error "Missing required tools: ${missing_tools[*]}"
        exit $EXIT_MISSING_TOOLS
    fi

    log_debug "Prerequisites check passed"
}

# Create output directory structure
create_output_structure() {
    log_info "Creating output directory structure..."

    mkdir -p "$OUTPUT_DIR"
    mkdir -p "$OUTPUT_DIR/assets"
    mkdir -p "$OUTPUT_DIR/configs"
    mkdir -p "$OUTPUT_DIR/reports"

    log_debug "Created output directory: $OUTPUT_DIR"
}

# Generate build information documentation
generate_build_info() {
    if [[ "$INCLUDE_BUILD_INFO" != "true" ]]; then
        log_debug "Skipping build information generation"
        return
    fi

    log_info "Generating build information documentation..."

    local build_info_file="$OUTPUT_DIR/build-info.md"

    cat > "$build_info_file" << 'EOF'
# Build Information

## Project Overview

This document provides comprehensive build information for the Puzzle71Solver deployment package.

EOF

    # Add basic project information
    cat >> "$build_info_file" << EOF
### Basic Information

- **Project Name**: Puzzle71Solver
- **Build Date**: $(date -u +"%Y-%m-%d %H:%M:%S UTC")
- **Build Type**: ${CMAKE_BUILD_TYPE:-"RelWithDebInfo"}
- **Target Architecture**: $(uname -m)

### Compiler Information

EOF

    # Add compiler information if available
    if command -v g++ >/dev/null 2>&1; then
        cat >> "$build_info_file" << EOF
- **C++ Compiler**: $(g++ --version | head -n1)
EOF
    fi

    if command -v nvcc >/dev/null 2>&1; then
        cat >> "$build_info_file" << EOF
- **CUDA Compiler**: $(nvcc --version | grep release | head -n1)
EOF
    fi

    # Add system information
    cat >> "$build_info_file" << EOF

### System Information

- **Operating System**: $(uname -s) $(uname -r)
- **Hostname**: $(hostname)
- **User**: $(whoami)

### Build Configuration

EOF

    # Add CMake configuration if available
    if [[ -f "$PROJECT_ROOT/CMakeLists.txt" ]]; then
        cat >> "$build_info_file" << EOF
- **CMake Version**: $(cmake --version | head -n1)
- **C++ Standard**: C++17
- **CUDA Standard**: C++17
- **Position Independent Code**: Enabled

EOF

        # Extract enabled features from CMakeLists.txt
        if grep -q "ENABLE_INTEGRATION_SYSTEM" "$PROJECT_ROOT/CMakeLists.txt"; then
            cat >> "$build_info_file" << EOF
- **Integration System**: Enabled
EOF
        fi

        if grep -q "ENABLE_DEPLOYMENT_SYSTEM" "$PROJECT_ROOT/CMakeLists.txt"; then
            cat >> "$build_info_file" << EOF
- **Deployment System**: Enabled
EOF
        fi

        if grep -q "OFFLINE_BUILD" "$PROJECT_ROOT/CMakeLists.txt"; then
            cat >> "$build_info_file" << EOF
- **Offline Build**: Enabled
EOF
        fi
    fi

    cat >> "$build_info_file" << EOF

### Build Commands

The following commands were used to build this deployment package:

\`\`\`bash
# Configure build
cmake -B build -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE:-RelWithDebInfo}

# Build project
cmake --build build --parallel

# Create deployment package
cmake --build build --target package-deployment
\`\`\`

EOF

    log_debug "Generated build information: $build_info_file"
}

# Generate dependency information
generate_dependency_info() {
    if [[ "$INCLUDE_DEPENDENCIES" != "true" ]]; then
        log_debug "Skipping dependency information generation"
        return
    fi

    log_info "Generating dependency information documentation..."

    local dependency_file="$OUTPUT_DIR/dependencies.md"

    cat > "$dependency_file" << 'EOF'
# Dependency Information

## Overview

This document details all dependencies included in this Puzzle71Solver deployment package.

EOF

    # Add system dependencies
    cat >> "$dependency_file" << EOF
### System Dependencies

The following system libraries are required for runtime:

EOF

    # Check for CUDA
    if command -v nvcc >/dev/null 2>&1; then
        cat >> "$dependency_file" << EOF
- **CUDA Toolkit**: Required for GPU acceleration
  - Version: $(nvcc --version | grep release | awk '{print $6}' | sed 's/,//')
  - Architecture support: sm_75, sm_86, sm_89, sm_90

EOF
    fi

    # Check for OpenSSL
    if pkg-config --exists openssl 2>/dev/null; then
        cat >> "$dependency_file" << EOF
- **OpenSSL**: Required for cryptographic operations
  - Version: $(pkg-config --modversion openssl)
  - Libraries: libssl, libcrypto

EOF
    fi

    # Add integrated third-party libraries
    cat >> "$dependency_file" << 'EOF'
### Integrated Third-Party Libraries

The following third-party libraries are integrated directly into this package:

EOF

    # BitCrack integration
    if [[ -d "$PROJECT_ROOT/src/extracted/bitcrack" ]]; then
        cat >> "$dependency_file" << EOF
#### BitCrack
- **Description**: Bitcoin address brute force tool
- **License**: MIT License
- **Integration Method**: Source code extraction
- **Files Integrated**: $(find "$PROJECT_ROOT/src/extracted/bitcrack" -name "*.cpp" -o -name "*.cu" -o -name "*.h" | wc -l) source files
- **Attribution**: Full attribution headers maintained

EOF
    fi

    # secp256k1-zkp integration
    if [[ -d "$PROJECT_ROOT/src/extracted/secp256k1-zkp" ]]; then
        cat >> "$dependency_file" << EOF
#### secp256k1-zkp
- **Description**: Optimized secp256k1 library with zero-knowledge proof support
- **License**: Apache License 2.0
- **Integration Method**: Source code extraction
- **Files Integrated**: $(find "$PROJECT_ROOT/src/extracted/secp256k1-zkp" -name "*.c" -o -name "*.h" | wc -l) source files
- **Attribution**: Full attribution headers maintained

EOF
    fi

    # Add optional dependencies
    cat >> "$dependency_file" << EOF
### Optional Dependencies

The following dependencies are optional and may not be included in all builds:

- **nlohmann/json**: JSON parsing library (development builds only)
- **Google Test**: Testing framework (development builds only)

EOF

    log_debug "Generated dependency information: $dependency_file"
}

# Generate test results documentation
generate_test_results() {
    if [[ "$INCLUDE_TEST_RESULTS" != "true" ]]; then
        log_debug "Skipping test results generation"
        return
    fi

    log_info "Generating test results documentation..."

    local test_results_file="$OUTPUT_DIR/test-results.md"

    cat > "$test_results_file" << 'EOF'
# Test Results

## Overview

This document summarizes test results for the Puzzle71Solver deployment package.

EOF

    # Check for test results
    local test_dir="$PROJECT_ROOT/build/test-results"
    if [[ -d "$test_dir" ]]; then
        cat >> "$test_results_file" << EOF
### Automated Test Results

**Test Date**: $(date -u +"%Y-%m-%d %H:%M:%S UTC")

EOF

        # Look for test result files
        for test_file in "$test_dir"/*.xml "$test_dir"/*.json; do
            if [[ -f "$test_file" ]]; then
                local test_name=$(basename "$test_file")
                cat >> "$test_results_file" << EOF
#### $test_name
- **File**: $test_name
- **Size**: $(stat -f%z "$test_file" 2>/dev/null || stat -c%s "$test_file") bytes

EOF
            fi
        done
    else
        cat >> "$test_results_file" << EOF
### Automated Test Results

No test results found. Tests may not have been run or may not be available in this build.

EOF
    fi

    # Add manual testing information
    cat >> "$test_results_file" << EOF
### Manual Testing Guidelines

#### Basic Functionality Test
1. Run the solver with a small test case
2. Verify GPU detection and initialization
3. Check basic cryptographic operations

#### Performance Test
1. Run with standard benchmark parameters
2. Monitor GPU memory usage
3. Verify expected performance metrics

#### Integration Test
1. Test deployment package extraction
2. Verify all dependencies are resolved
3. Confirm self-contained operation

EOF

    log_debug "Generated test results: $test_results_file"
}

# Generate deployment guide
generate_deployment_guide() {
    log_info "Generating deployment guide..."

    local deployment_guide_file="$OUTPUT_DIR/deployment-guide.md"

    cat > "$deployment_guide_file" << 'EOF'
# Deployment Guide

## Quick Start

### Prerequisites

- CUDA-compatible GPU (Compute Capability 7.5+)
- CUDA Driver 12.0 or later
- 4GB+ RAM recommended
- 10GB+ disk space

### Installation

1. Extract the deployment package:
   ```bash
   tar -xzf puzzle71solver-*.tar.gz
   cd puzzle71solver
   ```

2. Run the installation script:
   ```bash
   ./scripts/install.sh
   ```

3. Verify installation:
   ```bash
   ./bin/Puzzle71Solver --help
   ```

### Basic Usage

Run a simple search:
```bash
./bin/Puzzle71Solver --target <address> --max-range 1000000
```

Run with GPU specification:
```bash
./bin/Puzzle71Solver --target <address> --gpu 0 --blocks 256 --threads 256
```

## Advanced Configuration

### Environment Variables

- `CUDA_VISIBLE_DEVICES`: Specify which GPUs to use
- `PUZZLE71_CONFIG_FILE`: Path to configuration file
- `PUZZLE71_LOG_LEVEL`: Set logging verbosity (DEBUG, INFO, WARN, ERROR)

### Configuration Files

Configuration files can be placed in:
- `./config/puzzle71.json`
- `~/.config/puzzle71/config.json`
- `/etc/puzzle71/config.json`

## Troubleshooting

### Common Issues

1. **GPU not detected**
   - Verify CUDA drivers are installed
   - Check GPU compatibility
   - Run `nvidia-smi` to verify GPU status

2. **Memory allocation errors**
   - Reduce `--blocks` and `--threads` parameters
   - Ensure sufficient GPU memory is available
   - Try running with `--gpu-memory-limit` parameter

3. **Permission denied**
   - Ensure scripts are executable: `chmod +x scripts/*.sh`
   - Check file permissions in deployment directory

### Support

For additional support:
1. Check the logs in `./logs/` directory
2. Review system requirements
3. Consult the full documentation at `./docs/`

EOF

    log_debug "Generated deployment guide: $deployment_guide_file"
}

# Generate main index file
generate_index() {
    log_info "Generating main documentation index..."

    local index_file="$OUTPUT_DIR/README.md"

    cat > "$index_file" << EOF
# Puzzle71Solver Deployment Documentation

**Generated**: $(date -u +"%Y-%m-%d %H:%M:%S UTC")
**Version**: ${DEPLOYMENT_VERSION:-"Unknown"}

## Documentation Sections

EOF

    if [[ "$INCLUDE_BUILD_INFO" == "true" ]]; then
        echo "- [Build Information](build-info.md)" >> "$index_file"
    fi

    if [[ "$INCLUDE_DEPENDENCIES" == "true" ]]; then
        echo "- [Dependencies](dependencies.md)" >> "$index_file"
    fi

    if [[ "$INCLUDE_TEST_RESULTS" == "true" ]]; then
        echo "- [Test Results](test-results.md)" >> "$index_file"
    fi

    cat >> "$index_file" << 'EOF'
- [Deployment Guide](deployment-guide.md)

## Quick Links

- [Binary Executable](../bin/Puzzle71Solver)
- [Configuration Files](../config/)
- [Example Scripts](../scripts/examples/)
- [Logs Directory](../logs/)

EOF

    log_debug "Generated main index: $index_file"
}

# Convert to requested format
convert_format() {
    if [[ "$OUTPUT_FORMAT" == "markdown" ]]; then
        log_debug "Output format is markdown, no conversion needed"
        return
    fi

    log_info "Converting documentation to $OUTPUT_FORMAT format..."

    # Convert main README
    local main_file="$OUTPUT_DIR/README.md"
    if [[ -f "$main_file" ]] && command -v pandoc >/dev/null 2>&1; then
        case "$OUTPUT_FORMAT" in
            html)
                pandoc "$main_file" -o "$OUTPUT_DIR/README.html" --standalone --self-contained
                log_debug "Generated HTML documentation: $OUTPUT_DIR/README.html"
                ;;
            pdf)
                pandoc "$main_file" -o "$OUTPUT_DIR/README.pdf"
                log_debug "Generated PDF documentation: $OUTPUT_DIR/README.pdf"
                ;;
        esac
    fi
}

# Main function
main() {
    log_info "Starting deployment documentation generation..."

    parse_args "$@"
    check_prerequisites
    create_output_structure

    generate_build_info
    generate_dependency_info
    generate_test_results
    generate_deployment_guide
    generate_index
    convert_format

    log_info "Documentation generation completed successfully!"
    log_info "Output directory: $OUTPUT_DIR"

    if [[ "$VERBOSE" == "true" ]]; then
        echo
        echo "Generated files:"
        find "$OUTPUT_DIR" -type f -name "*.md" -o -name "*.html" -o -name "*.pdf" | sort
    fi
}

# Execute main function
main "$@"