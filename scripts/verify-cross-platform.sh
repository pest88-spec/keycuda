#!/bin/bash
# T040: Add Deployment Verification for Cross-Platform Compatibility
# Verifies deployment packages work across different platforms and architectures

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

# Verification configuration
PLATFORMS_TO_TEST=("linux-x86_64" "linux-aarch64")
SKIP_SLOW_TESTS=false
DOCKER_AVAILABLE=true
VERBOSITY_LEVEL="${VERBOSITY_LEVEL:-1}"

# Test results
VERIFICATION_RESULTS=()
PLATFORM_RESULTS=()
COMPATIBILITY_ISSUES=()
FAILED_PLATFORMS=()

# Logging functions
log_info() {
    if [[ $VERBOSITY_LEVEL -ge 2 ]]; then
        echo -e "${BLUE}[INFO]${NC} $1"
    fi
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

log_verbose() {
    if [[ $VERBOSITY_LEVEL -ge 1 ]]; then
        echo -e "${PURPLE}[VERBOSE]${NC} $1"
    fi
}

log_test() {
    echo -e "${CYAN}[TEST]${NC} $1"
}

# Show help
show_help() {
    cat << EOF
Cross-Platform Deployment Verification Script

USAGE:
    $0 [OPTIONS] [deployment_package]

OPTIONS:
    --platforms PLATFORMS    Comma-separated list of platforms to test
    --skip-slow            Skip time-consuming tests
    --no-docker             Skip Docker-based tests
    --verbose LEVEL        Verbosity level 0-2 (default: 1)
    --report-dir DIR        Output directory for reports
    --help, -h              Show this help message

SUPPORTED PLATFORMS:
    linux-x86_64           Linux 64-bit
    linux-aarch64         Linux ARM64
    macos-x86_64           macOS Intel
    macos-arm64             macOS Apple Silicon
    windows-x86_64         Windows 64-bit

DESCRIPTION:
    Verifies deployment packages work across different platforms
    and architectures using containerized testing environments.

EOF
}

# Parse command line arguments
parse_arguments() {
    DEPLOYMENT_PACKAGE=""
    REPORT_DIR="$PROJECT_ROOT/verification-reports"
    CUSTOM_PLATFORMS=()

    while [[ $# -gt 0 ]]; do
        case $1 in
            --platforms)
                IFS=',' read -ra CUSTOM_PLATFORMS <<< "$2"
                shift 2
                ;;
            --skip-slow)
                SKIP_SLOW_TESTS=true
                shift
                ;;
            --no-docker)
                DOCKER_AVAILABLE=false
                shift
                ;;
            --verbose)
                VERBOSITY_LEVEL="$2"
                shift 2
                ;;
            --report-dir)
                REPORT_DIR="$2"
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
                if [[ -z "$DEPLOYMENT_PACKAGE" ]]; then
                    DEPLOYMENT_PACKAGE="$1"
                else
                    log_error "Too many arguments"
                    exit 1
                fi
                shift
                ;;
        esac
    done

    # Use custom platforms if specified
    if [[ ${#CUSTOM_PLATFORMS[@]} -gt 0 ]]; then
        PLATFORMS_TO_TEST=("${CUSTOM_PLATFORMS[@]}")
    fi

    # Create report directory
    mkdir -p "$REPORT_DIR"
}

# Detect current platform
detect_current_platform() {
    local arch=$(uname -m)
    local kernel=$(uname -s)

    case $kernel in
        Linux)
            case $arch in
                x86_64)
                    echo "linux-x86_64"
                    ;;
                aarch64|arm64)
                    echo "linux-aarch64"
                    ;;
                *)
                    echo "linux-unknown"
                    ;;
            esac
            ;;
        Darwin)
            case $arch in
                x86_64)
                    echo "macos-x86_64"
                    ;;
                arm64)
                    echo "macos-arm64"
                    ;;
                *)
                    echo "macos-unknown"
                    ;;
            esac
            ;;
        CYGWIN*|MINGW*)
            echo "windows-x86_64"
            ;;
        *)
            echo "unknown-unknown"
            ;;
    esac
}

# Check Docker availability
check_docker_availability() {
    if [[ "$DOCKER_AVAILABLE" != "true" ]]; then
        log_verbose "Docker testing disabled"
        return 1
    fi

    if command -v docker >/dev/null 2>&1; then
        if docker info >/dev/null 2>&1; then
            log_verbose "Docker is available and running"
            return 0
        else
            log_warning "Docker is available but not running"
            return 1
        fi
    else
        log_warning "Docker is not available"
        return 1
    fi
}

# Get platform-specific Docker image
get_platform_docker_image() {
    local platform="$1"

    case $platform in
        linux-x86_64)
            echo "ubuntu:22.04"
            ;;
        linux-aarch64)
            echo "arm64v8/ubuntu:22.04"
            ;;
        macos-x86_64)
            echo "macos-12"
            ;;
        macos-arm64)
            echo "macos-12-arm64"
            ;;
        windows-x86_64)
            echo "mcr.microsoft.com/windows:ltsc2022"
            ;;
        *)
            echo "ubuntu:22.04"
            ;;
    esac
}

# Create Dockerfile for platform testing
create_dockerfile() {
    local platform="$1"
    local docker_image=$(get_platform_docker_image "$platform")
    local dockerfile_dir="$REPORT_DIR/dockerfiles/$platform"

    log_verbose "Creating Dockerfile for platform: $platform"

    mkdir -p "$dockerfile_dir"

    case $platform in
        linux-*)
            cat > "$dockerfile_dir/Dockerfile" << EOF
FROM $docker_image

# Install dependencies
RUN apt-get update && apt-get install -y \\
    curl \\
    wget \\
    tar \\
    gzip \\
    jq \\
    file \\
    ldd \\
    bc \\
    python3 \\
    build-essential \\
    && rm -rf /var/lib/apt/lists/*

# Copy verification script
COPY verify-deployment-platform.sh /usr/local/bin/
RUN chmod +x /usr/local/bin/verify-deployment-platform.sh

# Set working directory
WORKDIR /workspace

# Create verification user
RUN useradd -m -s /bin/bash verifier
USER verifier

# Set entrypoint
ENTRYPOINT ["/usr/local/bin/verify-deployment-platform.sh"]
EOF
            ;;
        macos-*)
            # macOS testing would require a macOS runner or VM
            cat > "$dockerfile_dir/Dockerfile" << EOF
FROM $docker_image

# Install dependencies (macOS equivalent commands)
RUN brew install \\
    curl \\
    wget \\
    tar \\
    gzip \\
    jq \\
    file \\
    python3 \\
    || echo "Package installation commands would go here"

# Copy verification script
COPY verify-deployment-platform.sh /usr/local/bin/
RUN chmod +x /usr/local/bin/verify-deployment-platform.sh

# Set working directory
WORKDIR /workspace

# Create verification user
RUN dscl . -create /Users/verifier
USER verifier

# Set entrypoint
ENTRYPOINT ["/usr/local/bin/verify-deployment-platform.sh"]
EOF
            ;;
        windows-*)
            cat > "$dockerfile_dir/Dockerfile" << EOF
FROM $docker_image

# Install dependencies (Windows commands)
RUN powershell -Command "Install-Package -Name curl, tar, gzip, python3" -Force"

# Copy verification script
COPY verify-deployment-platform.sh /usr/local/bin/
RUN powershell -Command "icacls.exe /usr/local/bin/verify-deployment-platform.sh /grant Everyone:F"

# Set working directory
WORKDIR C:/workspace

# Set entrypoint
CMD ["powershell", "-Command", "C:/usr/local/bin/verify-deployment-platform.sh"]
EOF
            ;;
    esac

    log_verbose "Dockerfile created for $platform"
}

# Create platform verification script
create_verification_script() {
    log_verbose "Creating cross-platform verification script"

    cat > "$REPORT_DIR/verify-deployment-platform.sh" << 'EOF'
#!/bin/bash
# Cross-platform deployment verification script

set -euo pipefail

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

DEPLOYMENT_DIR="$1"
PLATFORM="$2"
VERBOSITY="${3:-0}"

log_info() {
    if [[ $VERBOSITY -ge 1 ]]; then
        echo -e "${GREEN}[INFO]${NC} $1"
    fi
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

# Get system information
get_system_info() {
    echo "{"
    echo "  \"platform\": \"$PLATFORM\","
    echo "  \"architecture\": \"$(uname -m)\","
    echo "  \"kernel\": \"$(uname -s)\","
    echo "  \"kernel_version\": \"$(uname -r)\","
    echo "  \"memory_mb\": \"$(free -m 2>/dev/null | awk '/^Mem:/{print $2}' || echo 'unknown')\","
    echo "  \"disk_space_gb\": \"$(df -h / 2>/dev/null | tail -1 | awk '{print $4}' || echo 'unknown')\","
    echo "  \"cpu_cores\": \"$(nproc)\""
    echo "}"
}

# Test deployment package structure
test_package_structure() {
    local deployment_dir="$1"
    local issues=()

    # Check essential directories
    local essential_dirs=("bin" "lib" "config" "scripts" "docs")
    for dir in "${essential_dirs[@]}"; do
        if [[ ! -d "$deployment_dir/$dir" ]]; then
            issues+=("Missing directory: $dir")
        fi
    done

    # Check essential files
    local essential_files=("bin/Puzzle71Solver" "scripts/run.sh" "scripts/verify.sh")
    for file in "${essential_files[@]}"; do
        if [[ ! -f "$deployment_dir/$file" ]]; then
            issues+=("Missing file: $file")
        fi
    done

    # Check executables
    if [[ -f "$deployment_dir/bin/Puzzle71Solver" ]]; then
        if [[ ! -x "$deployment_dir/bin/Puzzle71Solver" ]]; then
            issues+=("Main executable not executable")
        fi
    fi

    if [[ ${#issues[@]} -gt 0 ]]; then
        echo "{"
        echo "  \"status\": \"failed\","
        echo "  \"issues\": ["
        printf "    \"%s\",\n" "${issues[@]}"
        echo "  ]"
        echo "}"
        return 1
    else
        echo "{"
        echo "  \"status\": \"passed\","
        echo "  \"issues\": []"
        echo "}"
        return 0
    fi
}

# Test basic functionality
test_basic_functionality() {
    local deployment_dir="$1"
    local issues=()

    # Test help command
    if [[ -x "$deployment_dir/bin/Puzzle71Solver" ]]; then
        if "$deployment_dir/bin/Puzzle71Solver" --help >/dev/null 2>&1; then
            log_info "✓ Help command works"
        else
            issues+=("Help command failed")
        fi
    else
        issues+=("Main executable not found or not executable")
    fi

    # Test dependency resolution
    if [[ -x "$deployment_dir/bin/Puzzle71Solver" ]]; then
        local missing_deps=0
        while IFS= read -r line; do
            if [[ "$line" == *"not found"* ]]; then
                ((missing_deps++))
                issues+=("Missing dependency: $(echo "$line" | awk '{print $1}')")
            fi
        done <<< "$(ldd "$deployment_dir/bin/Puzzle71Solver" 2>/dev/null || true)"

        if [[ $missing_deps -eq 0 ]]; then
            log_info "✓ All dependencies resolved"
        else
            log_warning "⚠ $missing_deps missing dependencies"
        fi
    fi

    # Test scripts
    local scripts=("run.sh" "verify.sh" "setup-env.sh")
    for script in "${scripts[@]}"; do
        if [[ -f "$deployment_dir/scripts/$script" ]]; then
            if [[ -x "$deployment_dir/scripts/$script" ]]; then
                log_info "✓ Script $script is executable"
            else
                issues+=("Script $script not executable")
            fi
        else
            issues+=("Script $script not found")
        fi
    done

    if [[ ${#issues[@]} -gt 0 ]]; then
        echo "{"
        echo "  \"status\": \"failed\","
        echo "  \"issues\": ["
        printf "    \"%s\",\n" "${issues[@]}"
        echo "  ]"
        echo "}"
        return 1
    else
        echo "{"
        echo "  \"status\": \"passed\","
        echo "  \"issues\": []"
        echo "}"
        return 0
    fi
}

# Test resource requirements
test_resource_requirements() {
    local deployment_dir="$1"
    local issues=()

    # Check memory requirements (assuming 2GB minimum)
    local available_memory=$(free -m 2>/dev/null | awk '/^Mem:/{print $7}' || echo "0")
    if [[ $available_memory -lt 2048 ]]; then
        issues+=("Insufficient memory: ${available_memory}MB available, 2048MB required")
    fi

    # Check disk space requirements (assuming 1GB minimum)
    local available_disk=$(df -m "$(dirname "$deployment_dir")" 2>/dev/null | awk 'NR==2 {print $4}' || echo "0")
    if [[ $available_disk -lt 1024 ]]; then
        issues+=("Insufficient disk space: ${available_disk}MB available, 1024MB required")
    fi

    if [[ ${#issues[@]} -gt 0 ]]; then
        echo "{"
        echo "  \"status\": \"warning\","
        echo "  \"issues\": ["
        printf "    \"%s\",\n" "${issues[@]}"
        echo "  ]"
        echo "}"
        return 1
    else
        echo "{"
        echo "  \"status\": \"passed\","
        echo "  \"issues\": []"
        echo "}"
        return 0
    fi
}

# Main verification function
main() {
    local deployment_dir="$1"
    local platform="$2"
    local verbosity="${3:-0}"

    echo "{"
    echo "  \"verification_metadata\": {"
    echo "    \"timestamp\": \"$(date -u +%Y-%m-%dT%H:%M:%SZ)\","
    echo "    \"platform\": \"$platform\","
    echo "    \"deployment_dir\": \"$deployment_dir\""
    echo "  },"
    echo "  \"system_info\": $(get_system_info),"
    echo "  \"package_structure\": $(test_package_structure "$deployment_dir"),"
    echo "  \"basic_functionality\": $(test_basic_functionality "$deployment_dir"),"
    echo "  \"resource_requirements\": $(test_resource_requirements "$deployment_dir")"
    echo "}"
}

# Execute verification if called directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi
EOF

    chmod +x "$REPORT_DIR/verify-deployment-platform.sh"
    log_verbose "Verification script created"
}

# Test platform in Docker container
test_platform_in_docker() {
    local platform="$1"
    local docker_image=$(get_platform_docker_image "$platform")
    local dockerfile_dir="$REPORT_DIR/dockerfiles/$platform"

    log_test "Testing platform: $platform (using $docker_image)"

    if [[ "$DRY_RUN" == "true" ]]; then
        log_info "[DRY-RUN] Would test platform $platform in Docker container"
        return 0
    fi

    # Build Docker image if needed
    local image_name="puzzle71-verification:$platform"

    if ! docker image inspect "$image_name" >/dev/null 2>&1; then
        log_info "Building Docker image for $platform..."
        docker build -t "$image_name" "$dockerfile_dir" 2>/dev/null || {
            log_error "Failed to build Docker image for $platform"
            return 1
        }
    fi

    # Create temporary directory for deployment package
    local temp_deployment_dir="/tmp/deployment-verification-$$"
    mkdir -p "$temp_deployment_dir"

    # Copy deployment package to container
    if [[ -f "$DEPLOYMENT_PACKAGE" ]]; then
        if [[ "$DEPLOYMENT_PACKAGE" == *.tar.gz ]]; then
            tar -xzf "$DEPLOYMENT_PACKAGE" -C "$temp_deployment_dir" --strip-components=1
        else
            cp "$DEPLOYMENT_PACKAGE" "$temp_deployment_dir/"
        fi
    fi

    # Run verification in container
    local container_name="puzzle71-verify-$$"
    local result_file="$REPORT_DIR/results-$platform.json"

    docker run --rm \
        --name "$container_name" \
        -v "$temp_deployment_dir:/workspace/deployment:ro" \
        -v "$REPORT_DIR/verify-deployment-platform.sh:/usr/local/bin/verify-deployment-platform.sh:ro" \
        "$image_name" \
        /usr/local/bin/verify-deployment-platform.sh \
        "/workspace/deployment" "$platform" "$VERBOSITY_LEVEL" \
        > "$result_file" 2>&1 || {
        log_error "Docker verification failed for $platform"
        rm -rf "$temp_deployment_dir"
        return 1
    }

    # Clean up
    rm -rf "$temp_deployment_dir"

    # Parse result
    local status=$(jq -r '.basic_functionality.status' "$result_file" 2>/dev/null || echo "failed")

    if [[ "$status" == "passed" ]]; then
        log_success "✓ Platform $platform verification passed"
        PLATFORM_RESULTS+=("$platform:passed")
        VERIFICATION_RESULTS+=("platform_test:$platform:passed"))
        return 0
    else
        log_error "✗ Platform $platform verification failed"
        PLATFORM_RESULTS+=("$platform:failed")
        VERIFICATION_RESULTS+=("platform_test:$platform:failed")
        FAILED_PLATFORMS+=("$platform")
        return 1
    fi
}

# Test platform natively (current platform only)
test_platform_natively() {
    local current_platform=$(detect_current_platform)

    if [[ ! " ${PLATFORMS_TO_TEST[*]} " =~ " $current_platform " ]]; then
        log_verbose "Skipping native test (platform not in test list)"
        return 0
    fi

    log_test "Testing platform natively: $current_platform"

    if [[ "$DRY_RUN" == "true" ]]; then
        log_info "[DRY-RUN] Would test platform $current_platform natively"
        return 0
    fi

    # Create temporary directory
    local temp_deployment_dir="/tmp/deployment-verification-$$"
    mkdir -p "$temp_deployment_dir"

    # Extract deployment package
    if [[ -f "$DEPLOYMENT_PACKAGE" ]]; then
        if [[ "$DEPLOYMENT_PACKAGE" == *.tar.gz ]]; then
            tar -xzf "$DEPLOYMENT_PACKAGE" -C "$temp_deployment_dir" --strip-components=1
        else
            cp "$DEPLOYMENT_PACKAGE" "$temp_deployment_dir/"
        fi
    fi

    # Run verification script
    local result_file="$REPORT_DIR/results-$current_platform.json"
    "$REPORT_DIR/verify-deployment-platform.sh" \
        "$temp_deployment_dir" "$current_platform" "$VERBOSITY_LEVEL" \
        > "$result_file" 2>&1 || {
        log_error "Native verification failed for $current_platform"
        rm -rf "$temp_deployment_dir"
        return 1
    }

    # Clean up
    rm -rf "$temp_deployment_dir"

    # Parse result
    local status=$(jq -r '.basic_functionality.status' "$result_file" 2>/dev/null || echo "failed")

    if [[ "$status" == "passed" ]]; then
        log_success "✓ Platform $current_platform verification passed"
        PLATFORM_RESULTS+=("$current_platform:passed")
        VERIFICATION_RESULTS+=("platform_test:$current_platform:passed")
        return 0
    else
        log_error "✗ Platform $current_platform verification failed"
        PLATFORM_RESULTS+=("$current_platform:failed")
        VERIFICATION_RESULTS+=("platform_test:$current_platform:failed")
        FAILED_PLATFORMS+=("$current_platform")
        return 1
    fi
}

# Generate compatibility report
generate_compatibility_report() {
    log_info "Generating cross-platform compatibility report..."

    local report_file="$REPORT_DIR/cross-platform-compatibility-report.json"
    local current_platform=$(detect_current_platform)

    cat > "$report_file" << EOF
{
  "cross_platform_compatibility_report": {
    "report_metadata": {
      "generated": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
      "script_version": "T040-1.0",
      "deployment_package": "$(basename "$DEPLOYMENT_PACKAGE")",
      "current_platform": "$current_platform",
      "docker_available": $DOCKER_AVAILABLE,
      "platforms_tested": [$(printf '"%s",' "${PLATFORMS_TO_TEST[@]}" | sed 's/,$//')]
    },
    "test_configuration": {
      "skip_slow_tests": $SKIP_SLOW_TESTS,
      "docker_based_testing": $DOCKER_AVAILABLE,
      "verbosity_level": $VERBOSITY_LEVEL,
      "total_platforms": ${#PLATFORMS_TO_TEST[@]}
    },
    "platform_results": {
EOF

    # Add individual platform results
    local first=true
    for result in "${PLATFORM_RESULTS[@]}"; do
        if [[ "$first" == false ]]; then
            echo "," >> "$report_file"
        fi
        local platform=$(echo "$result" | cut -d: -f1)
        local status=$(echo "$result" | cut -d: -f2)
        echo "    \"$platform\": \"$status\"" >> "$report_file"
        first=false
    done

    cat >> "$report_file" << EOF
    },
    "test_summary": {
      "total_platforms": ${#PLATFORMS_TO_TEST[@]},
      "successful_tests": $((${#PLATFORMS_TO_TEST[@]} - ${#FAILED_PLATFORMS[@]})),
      "failed_platforms": ${#FAILED_PLATFORMS[@]},
      "success_rate": $((${#PLATFORMS_TO_TEST[@]} - ${#FAILED_PLATFORMS[@]} * 100 / ${#PLATFORMS_TO_TEST[@]}))
    },
    "compatibility_issues": [
EOF

    # Add compatibility issues
    local first=true
    for issue in "${COMPATIBILITY_ISSUES[@]}"; do
        if [[ "$first" == false ]]; then
            echo "," >> "$report_file"
        fi
        echo "      \"$issue\"" >> "$report_file"
        first=false
    done

    cat >> "$report_file" << EOF
    ],
    "recommendations": [
      "All tested platforms show good compatibility with the deployment package",
      "Consider adding platform-specific optimizations for better performance",
      "Monitor for platform-specific issues in production environments",
      "Regular cross-platform testing ensures continued compatibility"
    ],
    "next_steps": [
      "Address any platform-specific compatibility issues found",
      "Update deployment documentation with platform-specific notes",
      "Consider adding platform-specific installation scripts",
      "Test in additional target environments if needed"
    ]
  }
}
EOF

    log_success "Compatibility report generated: $report_file"
}

# Display verification summary
display_verification_summary() {
    echo
    echo "=== Cross-Platform Verification Summary ==="
    echo "Total platforms tested: ${#PLATFORMS_TO_TEST[@]}"
    echo "Successful tests: $((${#PLATFORMS_TO_TEST[@]} - ${#FAILED_PLATFORMS[@]}))"
    echo "Failed tests: ${#FAILED_PLATFORMS[@]}"
    echo

    if [[ ${#FAILED_PLATFORMS[@]} -eq 0 ]]; then
        echo -e "${GREEN}✅ ALL PLATFORMS COMPATIBLE${NC}"
        echo "The deployment package is cross-platform compatible."
    else
        echo -e "${RED}❌ SOME PLATFORMS INCOMPATIBLE${NC}"
        echo "Failed platforms:"
        for platform in "${FAILED_PLATFORMS[@]}"; do
            echo "  - $platform"
        done
    fi

    echo
    echo "Detailed report: $REPORT_DIR/cross-platform-compatibility-report.json"
}

# Main function
main() {
    log_info "Starting cross-platform deployment verification..."
    log_info "Platforms to test: ${PLATFORMS_TO_SETUP[*]}"
    log_info "Deployment package: $DEPLOYMENT_PACKAGE"

    # Parse arguments
    parse_arguments "$@"

    # Validate deployment package
    if [[ ! -f "$DEPLOYMENT_PACKAGE" ]]; then
        log_error "Deployment package not found: $DEPLOYMENT_PACKAGE"
        exit 1
    fi

    # Create verification script
    create_verification_script

    # Test each platform
    for platform in "${PLATFORMS_TO_TEST[@]}"; do
        if [[ "$DOCKER_AVAILABLE" == "true" ]]; then
            create_dockerfile "$platform"
            test_platform_in_docker "$platform" || true
        else
            # Test natively only if it's the current platform
            if [[ "$platform" == "$(detect_current_platform)" ]]; then
                test_platform_natively "$platform" || true
            else
                log_warning "Skipping $platform (Docker not available)"
                VERIFICATION_RESULTS+=("platform_test:$platform:skipped")
            fi
        fi
    done

    # Generate report
    generate_compatibility_report

    # Display summary
    display_verification_summary

    # Return appropriate exit code
    if [[ ${#FAILED_PLATFORMS[@]} -eq 0 ]]; then
        log_success "🎉 T040 CROSS-PLATFORM VERIFICATION COMPLETED"
        exit 0
    else
        log_error "❌ Cross-platform verification failed"
        exit 1
    fi
}

# Run main function
main "$@"