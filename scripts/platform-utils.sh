#!/bin/bash

# Platform Detection and Configuration Utilities
# Helper functions for cross-platform deployment verification
# Part of T040: Add deployment verification for cross-platform compatibility

set -euo pipefail

# Platform detection constants
readonly PLATFORM_LINUX="linux"
readonly PLATFORM_WINDOWS="windows"
readonly PLATFORM_MACOS="macos"
readonly ARCH_X86_64="x86_64"
readonly ARCH_AARCH64="aarch64"
readonly ARCH_ARMV7="armv7"
readonly ARCH_RISCV64="riscv64"

# =============================================================================
# PLATFORM DETECTION FUNCTIONS
# =============================================================================

detect_os() {
    case "$(uname -s)" in
        Linux*)     echo "$PLATFORM_LINUX" ;;
        Darwin*)    echo "$PLATFORM_MACOS" ;;
        CYGWIN*|MINGW*|MSYS*) echo "$PLATFORM_WINDOWS" ;;
        *)          echo "unknown" ;;
    esac
}

detect_architecture() {
    case "$(uname -m)" in
        x86_64|amd64)    echo "$ARCH_X86_64" ;;
        aarch64|arm64)   echo "$ARCH_AARCH64" ;;
        armv7*|armhf)    echo "$ARCH_ARMV7" ;;
        riscv64)         echo "$ARCH_RISCV64" ;;
        *)               echo "unknown" ;;
    esac
}

detect_platform() {
    echo "$(detect_os)-$(detect_architecture)"
}

detect_distribution() {
    if [[ "$(detect_os)" != "$PLATFORM_LINUX" ]]; then
        echo "not-applicable"
        return
    fi

    if [[ -f /etc/os-release ]]; then
        # Parse /etc/os-release for distribution information
        . /etc/os-release
        echo "$ID"
    elif [[ -f /etc/redhat-release ]]; then
        echo "rhel"
    elif [[ -f /etc/debian_version ]]; then
        echo "debian"
    else
        echo "unknown"
    fi
}

detect_kernel_version() {
    uname -r
}

detect_cpu_features() {
    local cpu_info=""
    local os=$(detect_os)

    case "$os" in
        "$PLATFORM_LINUX")
            if [[ -f /proc/cpuinfo ]]; then
                cpu_info=$(grep "^flags" /proc/cpuinfo | head -1 | cut -d: -f2)
            fi
            ;;
        "$PLATFORM_MACOS")
            if command -v sysctl >/dev/null 2>&1; then
                cpu_info=$(sysctl -n machdep.cpu.features 2>/dev/null || echo "")
            fi
            ;;
        "$PLATFORM_WINDOWS")
            # Windows CPU feature detection would be more complex
            cpu_info=""
            ;;
    esac

    echo "$cpu_info"
}

# =============================================================================
# PLATFORM COMPATIBILITY CHECKS
# =============================================================================

supports_docker() {
    command -v docker >/dev/null 2>&1 && docker info >/dev/null 2>&1
}

supports_podman() {
    command -v podman >/dev/null 2>&1 && podman info >/dev/null 2>&1
}

supports_qemu() {
    command -v qemu-system-x86_64 >/dev/null 2>&1 || \
    command -v qemu-system-aarch64 >/dev/null 2>&1
}

supports_wsl() {
    [[ -f /proc/version ]] && grep -qi microsoft /proc/version
}

supports_cross_compilation() {
    local target_arch="$1"
    local current_arch=$(detect_architecture)

    # Check if cross-compilation toolchain is available
    case "$target_arch" in
        "$ARCH_X86_64")
            # x86_64 is usually natively supported or easily cross-compiled
            return 0
            ;;
        "$ARCH_AARCH64")
            # Check for aarch64 cross-compilation support
            command -v aarch64-linux-gnu-gcc >/dev/null 2>&1 || \
            command -v gcc-aarch64-linux-gnu >/dev/null 2>&1
            ;;
        "$ARCH_ARMV7")
            # Check for ARMv7 cross-compilation support
            command -v arm-linux-gnueabihf-gcc >/dev/null 2>&1 || \
            command -v gcc-arm-linux-gnueabihf >/dev/null 2>&1
            ;;
        "$ARCH_RISCV64")
            # Check for RISC-V cross-compilation support
            command -v riscv64-linux-gnu-gcc >/dev/null 2>&1 || \
            command -v gcc-riscv64-linux-gnu >/dev/null 2>&1
            ;;
        *)
            return 1
            ;;
    esac
}

# =============================================================================
# EMULATION AND CONTAINER SUPPORT
# =============================================================================

can_emulate_platform() {
    local target_platform="$1"
    local target_os="${target_platform%%-*}"
    local target_arch="${target_platform##*-}"
    local current_os=$(detect_os)
    local current_arch=$(detect_architecture)

    # Native platform is always supported
    if [[ "$target_platform" == "$(detect_platform)" ]]; then
        return 0
    fi

    case "$current_os" in
        "$PLATFORM_LINUX")
            case "$target_os" in
                "$PLATFORM_LINUX")
                    # Linux on Linux: Check for binfmt_misc, QEMU, or containers
                    can_emulate_linux_arch "$target_arch"
                    ;;
                "$PLATFORM_WINDOWS")
                    # Linux to Windows: Check for Wine
                    command -v wine >/dev/null 2>&1
                    ;;
                "$PLATFORM_MACOS")
                    # Linux to macOS: Generally not supported
                    return 1
                    ;;
            esac
            ;;
        "$PLATFORM_MACOS")
            case "$target_os" in
                "$PLATFORM_LINUX")
                    # macOS to Linux: Check for Docker or Parallels
                    supports_docker || command -v parallels >/dev/null 2>&1
                    ;;
                "$PLATFORM_WINDOWS")
                    # macOS to Windows: Check for Parallels or Boot Camp
                    command -v parallels >/dev/null 2>&1
                    ;;
                "$PLATFORM_MACOS")
                    # macOS to macOS: Check for Rosetta or Universal Binary
                    can_emulate_macos_arch "$target_arch"
                    ;;
            esac
            ;;
        "$PLATFORM_WINDOWS")
            case "$target_os" in
                "$PLATFORM_LINUX")
                    # Windows to Linux: Check for WSL or Docker Desktop
                    supports_wsl || supports_docker
                    ;;
                "$PLATFORM_MACOS")
                    # Windows to macOS: Generally not supported
                    return 1
                    ;;
                "$PLATFORM_WINDOWS")
                    # Windows to Windows: Check for different architectures
                    can_emulate_windows_arch "$target_arch"
                    ;;
            esac
            ;;
    esac
}

can_emulate_linux_arch() {
    local target_arch="$1"
    local current_arch=$(detect_architecture)

    # Same architecture is natively supported
    if [[ "$target_arch" == "$current_arch" ]]; then
        return 0
    fi

    # Check for QEMU user mode emulation
    if command -v qemu-"$target_arch" >/dev/null 2>&1; then
        return 0
    fi

    # Check for container support with different architectures
    if supports_docker || supports_podman; then
        # Check if multi-arch support is available
        if docker run --rm --platform "linux/$target_arch" alpine:latest uname -m >/dev/null 2>&1; then
            return 0
        fi
    fi

    return 1
}

can_emulate_macos_arch() {
    local target_arch="$1"
    local current_arch=$(detect_architecture)

    # Same architecture is natively supported
    if [[ "$target_arch" == "$current_arch" ]]; then
        return 0
    fi

    # Check for Rosetta 2 (Apple Silicon to Intel)
    if [[ "$current_arch" == "$ARCH_AARCH64" ]] && [[ "$target_arch" == "$ARCH_X86_64" ]]; then
        # Apple Silicon can run Intel binaries via Rosetta 2
        command -v arch >/dev/null 2>&1
    elif [[ "$current_arch" == "$ARCH_X86_64" ]] && [[ "$target_arch" == "$ARCH_AARCH64" ]]; then
        # Intel Mac cannot run Apple Silicon binaries
        return 1
    fi
}

can_emulate_windows_arch() {
    local target_arch="$1"
    local current_arch=$(detect_architecture)

    # Same architecture is natively supported
    if [[ "$target_arch" == "$current_arch" ]]; then
        return 0
    fi

    # Windows emulation between architectures is limited
    return 1
}

# =============================================================================
# PLATFORM-SPECIFIC CONFIGURATION
# =============================================================================

get_platform_package_format() {
    local platform="$1"
    local os="${platform%%-*}"

    case "$os" in
        "$PLATFORM_LINUX")
            echo "tar.gz"
            ;;
        "$PLATFORM_WINDOWS")
            echo "zip"
            ;;
        "$PLATFORM_MACOS")
            echo "tar.gz"
            ;;
        *)
            echo "tar.gz"  # Default
            ;;
    esac
}

get_platform_binary_extension() {
    local platform="$1"
    local os="${platform%%-*}"

    case "$os" in
        "$PLATFORM_WINDOWS")
            echo ".exe"
            ;;
        *)
            echo ""  # No extension for Unix-like systems
            ;;
    esac
}

get_platform_library_extension() {
    local platform="$1"
    local os="${platform%%-*}"

    case "$os" in
        "$PLATFORM_LINUX")
            echo ".so"
            ;;
        "$PLATFORM_WINDOWS")
            echo ".dll"
            ;;
        "$PLATFORM_MACOS")
            echo ".dylib"
            ;;
        *)
            echo ".so"  # Default to Linux-style
            ;;
    esac
}

get_platform_dependency_command() {
    local platform="$1"
    local os="${platform%%-*}"

    case "$os" in
        "$PLATFORM_LINUX")
            echo "ldd"
            ;;
        "$PLATFORM_MACOS")
            echo "otool -L"
            ;;
        "$PLATFORM_WINDOWS")
            echo "objdump -p"
            ;;
        *)
            echo "ldd"  # Default to Linux-style
            ;;
    esac
}

# =============================================================================
# ENVIRONMENT DETECTION FOR TESTING
# =============================================================================

is_ci_environment() {
    [[ -n "${CI:-}" ]] || [[ -n "${CONTINUOUS_INTEGRATION:-}" ]] || \
    [[ -n "${GITHUB_ACTIONS:-}" ]] || [[ -n "${JENKINS_URL:-}" ]] || \
    [[ -n "${TRAVIS:-}" ]] || [[ -n "${CIRCLECI:-}" ]]
}

is_docker_environment() {
    [[ -f /.dockerenv ]] || [[ -n "${DOCKER_CONTAINER:-}" ]]
}

is_headless_environment() {
    [[ -z "${DISPLAY:-}" ]] && ! command -v xvfb-run >/dev/null 2>&1
}

get_environment_limits() {
    local limits=""

    # Memory limits
    if [[ -f /proc/meminfo ]]; then
        local total_kb=$(grep MemTotal /proc/meminfo | awk '{print $2}')
        local total_mb=$((total_kb / 1024))
        limits="${limits}memory_mb:${total_mb};"
    fi

    # CPU limits
    if command -v nproc >/dev/null 2>&1; then
        local cpu_count=$(nproc)
        limits="${limits}cpu_cores:${cpu_count};"
    fi

    # Disk limits
    if command -v df >/dev/null 2>&1; then
        local disk_kb=$(df . | tail -1 | awk '{print $4}')
        local disk_mb=$((disk_kb / 1024))
        limits="${limits}disk_mb:${disk_mb};"
    fi

    echo "$limits"
}

# =============================================================================
# PLATFORM TESTING UTILITIES
# =============================================================================

create_platform_test_environment() {
    local platform="$1"
    local test_dir="$2"

    mkdir -p "$test_dir"

    # Create platform-specific test configuration
    cat > "$test_dir/platform_config.json" << EOF
{
    "target_platform": "$platform",
    "host_platform": "$(detect_platform)",
    "timestamp": "$(date -u +"%Y-%m-%dT%H:%M:%SZ")",
    "can_emulate": $(can_emulate_platform "$platform" && echo true || echo false),
    "supports_containers": $(supports_docker && echo true || echo false),
    "supports_cross_compilation": $(supports_cross_compilation "${platform##*-}" && echo true || echo false),
    "environment_type": "$(determine_environment_type)",
    "resource_limits": "$(get_environment_limits)"
}
EOF
}

determine_environment_type() {
    if is_ci_environment; then
        echo "ci"
    elif is_docker_environment; then
        echo "docker"
    elif is_headless_environment; then
        echo "headless"
    else
        echo "interactive"
    fi
}

validate_platform_requirements() {
    local platform="$1"
    local requirements_file="$2"

    if [[ ! -f "$requirements_file" ]]; then
        echo "Requirements file not found: $requirements_file"
        return 1
    fi

    # Parse requirements JSON and validate each requirement
    if command -v jq >/dev/null 2>&1; then
        local min_memory=$(jq -r '.min_memory_mb // 0' "$requirements_file")
        local min_cpu=$(jq -r '.min_cpu_cores // 1' "$requirements_file")
        local required_features=$(jq -r '.required_cpu_features // []' "$requirements_file")

        # Check memory requirements
        if [[ $min_memory -gt 0 ]]; then
            local available_memory=$(get_available_memory_mb)
            if [[ $available_memory -lt $min_memory ]]; then
                echo "Memory requirement not met: need ${min_memory}MB, have ${available_memory}MB"
                return 1
            fi
        fi

        # Check CPU requirements
        if [[ $min_cpu -gt 1 ]]; then
            local available_cpu=$(get_available_cpu_cores)
            if [[ $available_cpu -lt $min_cpu ]]; then
                echo "CPU requirement not met: need ${min_cpu} cores, have ${available_cpu}"
                return 1
            fi
        fi

        # Check CPU feature requirements
        if [[ "$required_features" != "[]" ]]; then
            local available_features=$(detect_cpu_features)
            for feature in $(echo "$required_features" | jq -r '.[]'); do
                if [[ "$available_features" != *"$feature"* ]]; then
                    echo "CPU feature requirement not met: missing $feature"
                    return 1
                fi
            done
        fi
    fi

    return 0
}

get_available_memory_mb() {
    if [[ -f /proc/meminfo ]]; then
        grep MemAvailable /proc/meminfo | awk '{print int($2/1024)}'
    elif command -v free >/dev/null 2>&1; then
        free -m | awk 'NR==2{print $7}'
    else
        echo "0"
    fi
}

get_available_cpu_cores() {
    if command -v nproc >/dev/null 2>&1; then
        nproc
    elif [[ -f /proc/cpuinfo ]]; then
        grep -c ^processor /proc/cpuinfo
    else
        echo "1"
    fi
}

# =============================================================================
# MAIN FUNCTIONS FOR SCRIPT INTEGRATION
# =============================================================================

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    # Script is being executed directly
    case "${1:-}" in
        "detect")
            echo "Platform: $(detect_platform)"
            echo "OS: $(detect_os)"
            echo "Architecture: $(detect_architecture)"
            echo "Distribution: $(detect_distribution)"
            echo "Kernel: $(detect_kernel_version)"
            echo "Environment: $(determine_environment_type)"
            ;;
        "can-emulate")
            if [[ -n "${2:-}" ]]; then
                if can_emulate_platform "$2"; then
                    echo "Can emulate $2: YES"
                    exit 0
                else
                    echo "Can emulate $2: NO"
                    exit 1
                fi
            else
                echo "Usage: $0 can-emulate <platform>"
                exit 1
            fi
            ;;
        "requirements")
            if [[ -n "${2:-}" ]] && [[ -n "${3:-}" ]]; then
                if validate_platform_requirements "$2" "$3"; then
                    echo "Platform requirements met: YES"
                    exit 0
                else
                    echo "Platform requirements met: NO"
                    exit 1
                fi
            else
                echo "Usage: $0 requirements <platform> <requirements.json>"
                exit 1
            fi
            ;;
        "test-env")
            if [[ -n "${2:-}" ]] && [[ -n "${3:-}" ]]; then
                create_platform_test_environment "$2" "$3"
                echo "Test environment created for $2 at $3"
            else
                echo "Usage: $0 test-env <platform> <test_dir>"
                exit 1
            fi
            ;;
        *)
            echo "Platform Detection Utilities"
            echo "Usage: $0 <command> [options]"
            echo ""
            echo "Commands:"
            echo "  detect              Show current platform information"
            echo "  can-emulate <plat>  Check if platform can be emulated"
            echo "  requirements <plat> <file>  Validate platform requirements"
            echo "  test-env <plat> <dir>      Create platform test environment"
            echo ""
            echo "Examples:"
            echo "  $0 detect"
            echo "  $0 can-emulate linux-aarch64"
            echo "  $0 test-env linux-x86_64 /tmp/test-env"
            ;;
    esac
fi