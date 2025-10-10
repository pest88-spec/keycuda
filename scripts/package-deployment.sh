#!/usr/bin/env bash
# One-Click Deployment Package Generation Script
#
# Creates self-contained deployment packages with all integrated dependencies,
# enabling deployment without additional dependency installation steps.
#
# @author       Puzzle71Solver Team
# @created      2025-10-10
# @license      MIT

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build"
DEPLOYMENT_DIR="${REPO_ROOT}/deployment"
PACKAGES_DIR="${DEPLOYMENT_DIR}/packages"
MANIFESTS_DIR="${BUILD_DIR}/manifests"

# Exit codes
readonly EXIT_SUCCESS=0
readonly EXIT_BUILD_FAILED=1
readonly EXIT_PACKAGING_FAILED=2
readonly EXIT_VALIDATION_FAILED=3
readonly EXIT_MISSING_DEPENDENCIES=4
readonly EXIT_CONFIG_ERROR=5

# Color codes for output
readonly GREEN='\033[0;32m'
readonly RED='\033[0;31m'
readonly YELLOW='\033[1;33m'
readonly BLUE='\033[0;34m'
readonly NC='\033[0m' # No Color

# Deployment configuration
readonly PACKAGE_VERSION=${PACKAGE_VERSION:-"1.0.0"}
readonly PACKAGE_NAME=${PACKAGE_NAME:-"puzzle71solver-deployment"}
readonly TARGET_PLATFORMS=${TARGET_PLATFORMS:-"linux-x86_64"}
readonly INCLUDE_SOURCE=${INCLUDE_SOURCE:-true}
readonly INCLUDE_DOCUMENTATION=${INCLUDE_DOCUMENTATION:-true}
readonly INCLUDE_DEBUG_INFO=${INCLUDE_DEBUG_INFO:-false}
readonly STRIP_BINARIES=${STRIP_BINARIES:-true}
readonly COMPRESS_PACKAGE=${COMPRESS_PACKAGE:-true}
readonly CREATE_INSTALLER=${CREATE_INSTALLER:-false}

# Package types
readonly PACKAGE_TYPES=("tar.gz" "zip")
if [[ "$CREATE_INSTALLER" == "true" ]]; then
    PACKAGE_TYPES+=("installer")
fi

# Global state
TOTAL_PACKAGES=0
SUCCESSFUL_PACKAGES=0
FAILED_PACKAGES=0

# Logging functions
log_deployment() {
    local level="$1"
    local message="$2"
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')

    case "$level" in
        "SUCCESS")
            echo -e "${GREEN}[DEPLOY-SUCCESS]${NC} ${timestamp} - ${message}"
            ;;
        "ERROR")
            echo -e "${RED}[DEPLOY-ERROR]${NC} ${timestamp} - ${message}"
            ;;
        "WARNING")
            echo -e "${YELLOW}[DEPLOY-WARN]${NC} ${timestamp} - ${message}"
            ;;
        "INFO")
            echo -e "${BLUE}[DEPLOY-INFO]${NC} ${timestamp} - ${message}"
            ;;
        *)
            echo -e "${BLUE}[DEPLOY-${level}]${NC} ${timestamp} - ${message}"
            ;;
    esac
}

# Check prerequisites
check_prerequisites() {
    log_deployment "INFO" "Checking deployment packaging prerequisites"

    local missing_deps=()

    # Check required commands
    for cmd in cmake make tar gzip find file ldobjdump; do
        if ! command -v "$cmd" >/dev/null 2>&1; then
            missing_deps+=("$cmd")
        fi
    done

    # Check for optional tools
    if ! command -v zip >/dev/null 2>&1; then
        log_deployment "WARNING" "zip command not found, skipping ZIP package creation"
    fi

    if [[ ${#missing_deps[@]} -gt 0 ]]; then
        log_deployment "ERROR" "Missing required dependencies: ${missing_deps[*]}"
        return $EXIT_MISSING_DEPENDENCIES
    fi

    # Check if build directory exists
    if [[ ! -d "$BUILD_DIR" ]]; then
        log_deployment "ERROR" "Build directory not found: $BUILD_DIR. Please build the project first."
        return $EXIT_BUILD_FAILED
    fi

    # Check if main executable exists
    local main_binary="$BUILD_DIR/Puzzle71Solver"
    if [[ ! -f "$main_binary" ]]; then
        log_deployment "ERROR" "Main executable not found: $main_binary. Please build the project first."
        return $EXIT_BUILD_FAILED
    fi

    # Create deployment directories
    mkdir -p "$PACKAGES_DIR"

    log_deployment "SUCCESS" "Prerequisites check completed"
    return $EXIT_SUCCESS
}

# Detect build configuration
detect_build_configuration() {
    log_deployment "INFO" "Detecting build configuration"

    local config_file="$BUILD_DIR/CMakeCache.txt"
    if [[ ! -f "$config_file" ]]; then
        log_deployment "WARNING" "CMake cache not found, using default configuration"
        return
    fi

    # Extract build information
    local build_type=$(grep "CMAKE_BUILD_TYPE:STRING=" "$config_file" | cut -d'=' -f2 || echo "Release")
    local compiler=$(grep "CMAKE_CXX_COMPILER:FILEPATH=" "$config_file" | cut -d'=' -f2 || echo "unknown")
    local cuda_available=$(grep "CUDA_FOUND:BOOL=" "$config_file" | cut -d'=' -f2 || echo "FALSE")

    log_deployment "INFO" "Build type: $build_type"
    log_deployment "INFO" "Compiler: $compiler"
    log_deployment "INFO" "CUDA available: $cuda_available"
}

# Create deployment package structure
create_package_structure() {
    local package_dir="$1"
    local package_name="$2"

    log_deployment "INFO" "Creating package structure for $package_name"

    mkdir -p "$package_dir/bin"
    mkdir -p "$package_dir/lib"
    mkdir -p "$package_dir/include"
    mkdir -p "$package_dir/share"
    mkdir -p "$package_dir/config"
    mkdir -p "$package_dir/scripts"

    # Create deployment manifest
    local manifest_file="$package_dir/deployment-manifest.json"
    cat > "$manifest_file" << EOF
{
    "package_name": "$package_name",
    "package_version": "$PACKAGE_VERSION",
    "created_at": "$(date -Iseconds)",
    "created_by": "package-deployment.sh",
    "target_platform": "$TARGET_PLATFORMS",
    "build_configuration": {
        "build_type": "Release",
        "compiler": "GCC",
        "cuda_support": true,
        "optimization_level": "O2"
    },
    "package_contents": {
        "binaries": [],
        "libraries": [],
        "headers": [],
        "resources": [],
        "documentation": []
    },
    "dependencies": {
        "system": ["libstdc++6", "libgcc1", "libc6"],
        "cuda": ["libcuda.so.1", "libcudart.so.11.0"],
        "openssl": ["libssl.so.1.1", "libcrypto.so.1.1"]
    },
    "installation_info": {
        "install_prefix": "/opt/puzzle71solver",
        "environment_variables": {
            "PUZZLE71_ROOT": "\${INSTALL_PREFIX}",
            "CUDA_PATH": "\${INSTALL_PREFIX}/cuda"
        },
        "post_install_actions": [
            "ldconfig",
            "chmod +x bin/Puzzle71Solver",
            "scripts/setup-environment.sh"
        ]
    },
    "verification": {
        "checksums": {},
        "file_integrity": {},
        "binary_tests": []
    }
}
EOF

    log_deployment "SUCCESS" "Package structure created: $package_dir"
}

# Package main binary
package_main_binary() {
    local package_dir="$1"
    local main_binary="$BUILD_DIR/Puzzle71Solver"

    log_deployment "INFO" "Packaging main binary"

    if [[ ! -f "$main_binary" ]]; then
        log_deployment "ERROR" "Main binary not found: $main_binary"
        return $EXIT_BUILD_FAILED
    fi

    # Copy main binary
    cp "$main_binary" "$package_dir/bin/"

    # Copy supporting binaries if they exist
    for binary in "generate-report.sh" "purge-checkpoints.sh"; do
        if [[ -f "$BUILD_DIR/$binary" ]]; then
            cp "$BUILD_DIR/$binary" "$package_dir/bin/"
        fi
    done

    # Strip binaries if requested
    if [[ "$STRIP_BINARIES" == "true" ]]; then
        log_deployment "INFO" "Stripping debug symbols from binaries"
        for binary in "$package_dir/bin"/*; do
            if file "$binary" | grep -q "ELF"; then
                strip --strip-unneeded "$binary" 2>/dev/null || true
            fi
        done
    fi

    # Set executable permissions
    chmod +x "$package_dir/bin"/*

    log_deployment "SUCCESS" "Main binary packaged successfully"
}

# Package integrated libraries
package_integrated_libraries() {
    local package_dir="$1"

    log_deployment "INFO" "Packaging integrated libraries"

    # Find and copy all built libraries
    while IFS= read -r -d '' lib_file; do
        local relative_path="${lib_file#$BUILD_DIR/}"
        local target_dir="$package_dir/lib/$(dirname "$relative_path")"

        mkdir -p "$target_dir"
        cp "$lib_file" "$target_dir/"

        log_deployment "INFO" "Packaged library: $relative_path"
    done < <(find "$BUILD_DIR" -name "*.so*" -o -name "*.a" -print0 2>/dev/null || true)

    # Copy CUDA libraries if available
    local cuda_libs_dir="$BUILD_DIR/cuda/lib"
    if [[ -d "$cuda_libs_dir" ]]; then
        log_deployment "INFO" "Packaging CUDA libraries"
        mkdir -p "$package_dir/lib/cuda"
        cp -r "$cuda_libs_dir"/* "$package_dir/lib/cuda/" 2>/dev/null || true
    fi

    log_deployment "SUCCESS" "Integrated libraries packaged successfully"
}

# Package headers and includes
package_headers() {
    local package_dir="$1"

    log_deployment "INFO" "Packaging headers and includes"

    # Copy integration headers
    if [[ -d "$REPO_ROOT/src/integration" ]]; then
        mkdir -p "$package_dir/include/integration"
        cp -r "$REPO_ROOT/src/integration"/*.h "$package_dir/include/integration/" 2>/dev/null || true
    fi

    # Copy extracted library headers
    if [[ -d "$REPO_ROOT/src/extracted" ]]; then
        while IFS= read -r -d '' header_file; do
            local relative_path="${header_file#$REPO_ROOT/src/extracted/}"
            local target_dir="$package_dir/include/$(dirname "$relative_path")"

            mkdir -p "$target_dir"
            cp "$header_file" "$target_dir/"
        done < <(find "$REPO_ROOT/src/extracted" -name "*.h" -o -name "*.hpp" -print0 2>/dev/null || true)
    fi

    log_deployment "SUCCESS" "Headers packaged successfully"
}

# Package configuration and resources
package_configuration() {
    local package_dir="$1"

    log_deployment "INFO" "Packaging configuration and resources"

    # Copy configuration files
    if [[ -f "$REPO_ROOT/config/puzzle71.yaml" ]]; then
        cp "$REPO_ROOT/config/puzzle71.yaml" "$package_dir/config/"
    fi

    # Create default configuration
    if [[ ! -f "$package_dir/config/puzzle71.yaml" ]]; then
        cat > "$package_dir/config/puzzle71.yaml" << 'EOF'
# Default Puzzle71Solver Configuration
project:
  name: "Puzzle71Solver"
  version: "1.0.0"
  description: "Bitcoin puzzle solver with CUDA acceleration"

puzzle:
  target_address: "1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU"
  private_key_file: ""
  reward_threshold: 1.0

cuda:
  device_id: 0
  block_size: 256
  grid_size: 0  # Auto-detect
  points_per_thread: 64

search:
  batch_size: 1000000
  checkpoint_interval: 10000000
  max_memory_mb: 8192
EOF
    fi

    # Copy scripts and utilities
    mkdir -p "$package_dir/scripts"
    cp "$SCRIPT_DIR"/*.sh "$package_dir/scripts/" 2>/dev/null || true

    # Create installation script
    cat > "$package_dir/scripts/install.sh" << 'EOF'
#!/bin/bash
# Puzzle71Solver Installation Script

set -euo pipefail

INSTALL_PREFIX="${INSTALL_PREFIX:-/opt/puzzle71solver}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PACKAGE_DIR="$(dirname "$SCRIPT_DIR")"

echo "Installing Puzzle71Solver to $INSTALL_PREFIX"

# Create installation directories
sudo mkdir -p "$INSTALL_PREFIX"/{bin,lib,include,share,config,scripts}

# Copy files
sudo cp -r "$PACKAGE_DIR/bin"/* "$INSTALL_PREFIX/bin/"
sudo cp -r "$PACKAGE_DIR/lib"/* "$INSTALL_PREFIX/lib/"
sudo cp -r "$PACKAGE_DIR/include"/* "$INSTALL_PREFIX/include/"
sudo cp -r "$PACKAGE_DIR/share"/* "$INSTALL_PREFIX/share/"
sudo cp -r "$PACKAGE_DIR/config"/* "$INSTALL_PREFIX/config/"
sudo cp -r "$PACKAGE_DIR/scripts"/* "$INSTALL_PREFIX/scripts/"

# Set permissions
sudo chmod +x "$INSTALL_PREFIX/bin"/*
sudo chmod +x "$INSTALL_PREFIX/scripts"/*

# Create environment setup script
sudo cat > "$INSTALL_PREFIX/scripts/setup-environment.sh" << 'ENVEOF'
#!/bin/bash
export PUZZLE71_ROOT="'$INSTALL_PREFIX'"
export PATH="$PUZZLE71_ROOT/bin:$PATH"
export LD_LIBRARY_PATH="$PUZZLE71_ROOT/lib:$LD_LIBRARY_PATH"
echo "Puzzle71Solver environment configured"
echo "PUZZLE71_ROOT: $PUZZLE71_ROOT"
ENVEOF

sudo chmod +x "$INSTALL_PREFIX/scripts/setup-environment.sh"

# Create uninstall script
sudo cat > "$INSTALL_PREFIX/scripts/uninstall.sh" << 'UNENVEOF'
#!/bin/bash
INSTALL_PREFIX="'$INSTALL_PREFIX'"
echo "Uninstalling Puzzle71Solver from $INSTALL_PREFIX"
sudo rm -rf "$INSTALL_PREFIX"
echo "Uninstallation completed"
UNENVEOF

sudo chmod +x "$INSTALL_PREFIX/scripts/uninstall.sh"

# Run ldconfig
sudo ldconfig

echo "Installation completed successfully!"
echo "Run '$INSTALL_PREFIX/scripts/setup-environment.sh' to configure your environment"
EOF

    chmod +x "$package_dir/scripts/install.sh"

    log_deployment "SUCCESS" "Configuration and resources packaged successfully"
}

# Package documentation
package_documentation() {
    local package_dir="$1"

    if [[ "$INCLUDE_DOCUMENTATION" != "true" ]]; then
        log_deployment "INFO" "Documentation packaging disabled"
        return
    fi

    log_deployment "INFO" "Packaging documentation"

    mkdir -p "$package_dir/share/doc"

    # Copy README files
    if [[ -f "$REPO_ROOT/README.md" ]]; then
        cp "$REPO_ROOT/README.md" "$package_dir/share/doc/"
    fi

    if [[ -f "$REPO_ROOT/QUICKSTART.md" ]]; then
        cp "$REPO_ROOT/QUICKSTART.md" "$package_dir/share/doc/"
    fi

    # Copy integration documentation
    if [[ -d "$REPO_ROOT/scripts/verification" ]]; then
        cp -r "$REPO_ROOT/scripts/verification" "$package_dir/share/doc/"
    fi

    if [[ -d "$REPO_ROOT/scripts/conflict-detection" ]]; then
        cp -r "$REPO_ROOT/scripts/conflict-detection" "$package_dir/share/doc/"
    fi

    # Create user guide
    cat > "$package_dir/share/doc/USER_GUIDE.md" << 'EOF'
# Puzzle71Solver Deployment Guide

## Installation

1. Extract the deployment package to your desired location
2. Run the installation script:
   ```bash
   sudo ./scripts/install.sh
   ```
3. Configure your environment:
   ```bash
   source /opt/puzzle71solver/scripts/setup-environment.sh
   ```

## Usage

Basic usage:
```bash
Puzzle71Solver --help
```

Search for puzzle:
```bash
Puzzle71Solver --keyspace 1.. --target-address YOUR_ADDRESS
```

## Configuration

Edit the configuration file:
```bash
nano /opt/puzzle71solver/config/puzzle71.yaml
```

## Troubleshooting

If you encounter CUDA-related errors, ensure your CUDA drivers are properly installed:
```bash
nvidia-smi
```

Check library dependencies:
```bash
ldd /opt/puzzle71solver/bin/Puzzle71Solver
```

## Uninstallation

To uninstall:
```bash
sudo /opt/puzzle71solver/scripts/uninstall.sh
```
EOF

    log_deployment "SUCCESS" "Documentation packaged successfully"
}

# Generate checksums
generate_checksums() {
    local package_dir="$1"
    local manifest_file="$package_dir/deployment-manifest.json"

    log_deployment "INFO" "Generating file checksums"

    local checksums_json="{"
    local first=true

    while IFS= read -r -d '' file; do
        local relative_path="${file#$package_dir/}"
        local checksum=$(sha256sum "$file" | cut -d' ' -f1)

        if [[ "$first" == "false" ]]; then
            checksums_json+=","
        fi
        first=false
        checksums_json+="
        \"$relative_path\": \"$checksum\""
    done < <(find "$package_dir" -type f -print0 | sort -z)

    checksums_json+="
    }"

    # Update manifest with checksums
    if command -v jq >/dev/null 2>&1; then
        jq ".verification.checksums = $checksums_json" "$manifest_file" > "$manifest_file.tmp"
        mv "$manifest_file.tmp" "$manifest_file"
    else
        log_deployment "WARNING" "jq not available, checksums not added to manifest"
    fi

    log_deployment "SUCCESS" "Checksums generated"
}

# Create package archives
create_package_archives() {
    local package_dir="$1"
    local package_name="$2"

    log_deployment "INFO" "Creating package archives for $package_name"

    for package_type in "${PACKAGE_TYPES[@]}"; do
        case "$package_type" in
            "tar.gz")
                create_tar_gz_package "$package_dir" "$package_name"
                ;;
            "zip")
                create_zip_package "$package_dir" "$package_name"
                ;;
            "installer")
                create_installer_package "$package_dir" "$package_name"
                ;;
        esac
    done

    log_deployment "SUCCESS" "Package archives created"
}

# Create tar.gz package
create_tar_gz_package() {
    local package_dir="$1"
    local package_name="$2"
    local archive_name="${package_name}-${PACKAGE_VERSION}-${TARGET_PLATFORMS}.tar.gz"
    local archive_path="$PACKAGES_DIR/$archive_name"

    log_deployment "INFO" "Creating tar.gz package: $archive_name"

    cd "$package_dir"
    tar -czf "$archive_path" .
    cd - >/dev/null

    log_deployment "SUCCESS" "tar.gz package created: $archive_path"
}

# Create ZIP package
create_zip_package() {
    local package_dir="$1"
    local package_name="$2"
    local archive_name="${package_name}-${PACKAGE_VERSION}-${TARGET_PLATFORMS}.zip"
    local archive_path="$PACKAGES_DIR/$archive_name"

    if ! command -v zip >/dev/null 2>&1; then
        log_deployment "WARNING" "zip command not available, skipping ZIP package"
        return
    fi

    log_deployment "INFO" "Creating ZIP package: $archive_name"

    cd "$package_dir"
    zip -r "$archive_path" . >/dev/null
    cd - >/dev/null

    log_deployment "SUCCESS" "ZIP package created: $archive_path"
}

# Create installer package
create_installer_package() {
    local package_dir="$1"
    local package_name="$2"
    local installer_name="${package_name}-${PACKAGE_VERSION}-${TARGET_PLATFORMS}.run"
    local installer_path="$PACKAGES_DIR/$installer_name"

    log_deployment "INFO" "Creating installer package: $installer_name"

    # Create self-extracting installer
    cat > "$installer_path" << 'INSTALLER_EOF'
#!/bin/bash
# Puzzle71Solver Self-Extracting Installer

set -euo pipefail

SKIP_LINES=28
INSTALL_PREFIX="${INSTALL_PREFIX:-/opt/puzzle71solver}"

# Check for root privileges
if [[ $EUID -ne 0 ]]; then
    echo "This installer requires root privileges"
    echo "Please run with sudo or as root"
    exit 1
fi

echo "Puzzle71Solver Installer"
echo "======================="
echo ""

# Extract package
echo "Extracting files..."
tail -n +$SKIP_LINES "$0" | tar -xzf - -C "$INSTALL_PREFIX"

echo "Installation completed successfully!"
echo "Configure environment with: $INSTALL_PREFIX/scripts/setup-environment.sh"
echo "Uninstall with: $INSTALL_PREFIX/scripts/uninstall.sh"

exit 0
INSTALLER_EOF

    # Append the tar.gz content
    cd "$package_dir"
    tar -czf - . >> "$installer_path"
    cd - >/dev/null

    chmod +x "$installer_path"

    log_deployment "SUCCESS" "Installer package created: $installer_path"
}

# Validate package
validate_package() {
    local package_dir="$1"
    local package_name="$2"

    log_deployment "INFO" "Validating package: $package_name"

    # Check required files exist
    local required_files=("bin/Puzzle71Solver" "deployment-manifest.json" "scripts/install.sh")
    for file in "${required_files[@]}"; do
        if [[ ! -f "$package_dir/$file" ]]; then
            log_deployment "ERROR" "Required file missing: $file"
            return $EXIT_VALIDATION_FAILED
        fi
    done

    # Validate binary
    if ! file "$package_dir/bin/Puzzle71Solver" | grep -q "ELF"; then
        log_deployment "ERROR" "Main binary is not a valid ELF executable"
        return $EXIT_VALIDATION_FAILED
    fi

    # Validate dependencies
    local missing_deps=$(ldd "$package_dir/bin/Puzzle71Solver" | grep "not found" || true)
    if [[ -n "$missing_deps" ]]; then
        log_deployment "WARNING" "Binary has missing dependencies:"
        echo "$missing_deps"
    fi

    log_deployment "SUCCESS" "Package validation completed"
}

# Main packaging function
create_deployment_package() {
    local package_name="${1:-$PACKAGE_NAME}"
    local package_dir="$DEPLOYMENT_DIR/temp-$$-$package_name"

    log_deployment "INFO" "Starting deployment package creation for $package_name"

    # Create temporary package directory
    mkdir -p "$package_dir"

    # Package components
    create_package_structure "$package_dir" "$package_name"
    package_main_binary "$package_dir"
    package_integrated_libraries "$package_dir"
    package_headers "$package_dir"
    package_configuration "$package_dir"
    package_documentation "$package_dir"
    generate_checksums "$package_dir"

    # Validate package
    if ! validate_package "$package_dir" "$package_name"; then
        log_deployment "ERROR" "Package validation failed"
        rm -rf "$package_dir"
        return $EXIT_VALIDATION_FAILED
    fi

    # Create archives
    create_package_archives "$package_dir" "$package_name"

    # Cleanup
    rm -rf "$package_dir"

    ((TOTAL_PACKAGES++))
    ((SUCCESSFUL_PACKAGES++))

    log_deployment "SUCCESS" "Deployment package created successfully: $package_name"
    return $EXIT_SUCCESS
}

# Main execution function
main() {
    # Parse command line arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            -h|--help)
                cat << 'EOF'
One-Click Deployment Package Generation

Usage: ./package-deployment.sh [OPTIONS] [PACKAGE_NAME]

OPTIONS:
    -h, --help                     Show this help message
    -v, --verbose                  Enable verbose logging
    --version <version>           Package version (default: 1.0.0)
    --name <name>                  Package name (default: puzzle71solver-deployment)
    --platform <platform>          Target platform (default: linux-x86_64)
    --no-source                    Exclude source files
    --no-documentation            Exclude documentation
    --include-debug               Include debug information
    --no-strip                     Don't strip binaries
    --no-compress                   Don't compress packages
    --create-installer             Create self-extracting installer
    --output-dir <directory>       Output directory for packages

EXAMPLES:
    ./package-deployment.sh
    ./package-deployment.sh --version 2.0.0 --name my-pkg
    ./package-deployment.sh --include-debug --no-strip
    ./package-deployment.sh --create-installer --platform linux-arm64

DESCRIPTION:
    This script creates self-contained deployment packages with all integrated
    dependencies, enabling deployment without additional dependency installation
    steps. It supports multiple package formats including tar.gz, ZIP, and
    self-extracting installers.

EOF
                exit $EXIT_SUCCESS
                ;;
            -v|--verbose)
                set -x
                shift
                ;;
            --version)
                export PACKAGE_VERSION="$2"
                shift 2
                ;;
            --name)
                export PACKAGE_NAME="$2"
                shift 2
                ;;
            --platform)
                export TARGET_PLATFORMS="$2"
                shift 2
                ;;
            --no-source)
                export INCLUDE_SOURCE=false
                shift
                ;;
            --no-documentation)
                export INCLUDE_DOCUMENTATION=false
                shift
                ;;
            --include-debug)
                export INCLUDE_DEBUG_INFO=true
                export STRIP_BINARIES=false
                shift
                ;;
            --no-strip)
                export STRIP_BINARIES=false
                shift
                ;;
            --no-compress)
                export COMPRESS_PACKAGE=false
                shift
                ;;
            --create-installer)
                export CREATE_INSTALLER=true
                shift
                ;;
            --output-dir)
                export PACKAGES_DIR="$2"
                shift 2
                ;;
            *)
                # Assume it's a package name
                break
                ;;
        esac
    done

    local package_name="${1:-$PACKAGE_NAME}"

    # Check prerequisites
    if ! check_prerequisites; then
        exit $EXIT_MISSING_DEPENDENCIES
    fi

    # Detect build configuration
    detect_build_configuration

    # Create deployment package
    if create_deployment_package "$package_name"; then
        echo ""
        log_deployment "INFO" "Packaging Summary"
        log_deployment "INFO" "==================="
        log_deployment "INFO" "Total packages: $TOTAL_PACKAGES"
        log_deployment "INFO" "Successful: $SUCCESSFUL_PACKAGES"
        log_deployment "INFO" "Failed: $FAILED_PACKAGES"
        log_deployment "INFO" "Package name: $package_name"
        log_deployment "INFO" "Package version: $PACKAGE_VERSION"
        log_deployment "INFO" "Target platform: $TARGET_PLATFORMS"
        log_deployment "INFO" "Packages directory: $PACKAGES_DIR"

        if [[ $SUCCESSFUL_PACKAGES -gt 0 ]]; then
            log_deployment "SUCCESS" "Deployment package creation completed successfully"
            exit $EXIT_SUCCESS
        else
            log_deployment "ERROR" "No packages were created successfully"
            exit $EXIT_PACKAGING_FAILED
        fi
    else
        exit $EXIT_PACKAGING_FAILED
    fi
}

# Run main function if script is executed directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi