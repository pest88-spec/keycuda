#!/usr/bin/env bash
# Simplified setup script for Puzzle71Solver (submodule-free build)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

echo "=== Puzzle71Solver Simplified Setup ==="
echo "Repository: $REPO_ROOT"
echo "Build Mode: Submodule-Free (Self-Contained)"
echo ""

# Function to detect OS and package manager
detect_package_manager() {
    if command -v apt-get >/dev/null 2>&1; then
        echo "apt"
    elif command -v yum >/dev/null 2>&1; then
        echo "yum"
    elif command -v apk >/dev/null 2>&1; then
        echo "apk"
    elif command -v brew >/dev/null 2>&1; then
        echo "brew"
    else
        echo "unknown"
    fi
}

# Function to install system dependencies
install_system_deps() {
    local pkg_manager=$(detect_package_manager)
    echo "Detected package manager: $pkg_manager"

    case $pkg_manager in
        apt)
            echo "Installing dependencies with apt..."
            sudo apt-get update
            sudo apt-get install -y build-essential cmake libssl-dev pkg-config
            ;;
        yum)
            echo "Installing dependencies with yum..."
            sudo yum groupinstall -y "Development Tools"
            sudo yum install -y cmake openssl-devel pkgconfig
            ;;
        apk)
            echo "Installing dependencies with apk..."
            sudo apk update
            sudo apk add build-base cmake openssl-dev pkgconfig
            ;;
        brew)
            echo "Installing dependencies with brew..."
            brew update
            brew install cmake openssl pkg-config
            ;;
        *)
            echo "❌ Error: Cannot detect supported package manager"
            echo "Please manually install: build-essential, cmake, libssl-dev, pkg-config"
            exit 1
            ;;
    esac
}

# 1. Check system dependencies
echo "[1/4] Checking system dependencies..."

# Check for required tools
MISSING_TOOLS=()
for tool in cmake make g++ gcc nvcc pkg-config; do
    if ! command -v $tool >/dev/null 2>&1; then
        if [[ "$tool" == "nvcc" ]]; then
            echo "⚠ CUDA Toolkit (nvcc) not found - required for GPU builds"
            echo "  Please install CUDA Toolkit from https://developer.nvidia.com/cuda-downloads"
        else
            MISSING_TOOLS+=($tool)
        fi
    fi
done

if [[ ${#MISSING_TOOLS[@]} -gt 0 ]]; then
    echo "Missing tools: ${MISSING_TOOLS[*]}"
    echo "Installing system dependencies..."
    install_system_deps
else
    echo "✓ All required tools found"
fi

# Check for OpenSSL development libraries
if ! pkg-config --exists openssl 2>/dev/null; then
    echo "⚠ OpenSSL development libraries not found"
    echo "Installing OpenSSL development libraries..."
    install_system_deps
else
    OPENSSL_VERSION=$(pkg-config --modversion openssl)
    echo "✓ OpenSSL found: $OPENSSL_VERSION"
fi

# 2. Verify extracted libraries
echo ""
echo "[2/4] Verifying extracted libraries..."

# Check secp256k1-zkp extraction
SECP256K1_ZKP_SOURCES=(
    "src/extracted/secp256k1-zkp/src/secp256k1.c"
    "src/extracted/secp256k1-zkp/include/secp256k1.h"
)

MISSING_SOURCES=()
for source in "${SECP256K1_ZKP_SOURCES[@]}"; do
    if [[ ! -f "$REPO_ROOT/$source" ]]; then
        MISSING_SOURCES+=($source)
    fi
done

if [[ ${#MISSING_SOURCES[@]} -gt 0 ]]; then
    echo "❌ Error: Missing extracted secp256k1-zkp sources:"
    printf '  %s\n' "${MISSING_SOURCES[@]}"
    echo ""
    echo "Please ensure the repository was cloned properly with all extracted sources."
    echo "If you're upgrading from an older version, you may need to:"
    echo "  git fetch origin"
    echo "  git pull origin main"
    exit 1
else
    echo "✓ Extracted secp256k1-zkp sources found"
fi

# Check BitCrack extraction
BITCRACK_SOURCES=(
    "src/extracted/bitcrack/CudaKeySearchDevice/CudaKeySearchDevice.cu"
    "src/extracted/bitcrack/cudaMath/secp256k1.cuh"
)

MISSING_BITCRACK=()
for source in "${BITCRACK_SOURCES[@]}"; do
    if [[ ! -f "$REPO_ROOT/$source" ]]; then
        MISSING_BITCRACK+=($source)
    fi
done

if [[ ${#MISSING_BITCRACK[@]} -gt 0 ]]; then
    echo "❌ Error: Missing extracted BitCrack sources:"
    printf '  %s\n' "${MISSING_BITCRACK[@]}"
    exit 1
else
    echo "✓ Extracted BitCrack sources found"
fi

# 3. Optional: Check git submodules (for reference/validation)
echo ""
echo "[3/4] Checking optional git submodules..."

cd "$REPO_ROOT"

if [[ -d ".git" && -f ".gitmodules" ]]; then
    echo "Git submodules available (optional for reference/validation)"

    # Check if bitcoin-core submodule is initialized
    if [[ -d "third_party/bitcoin-core-secp256k1" && -f "third_party/bitcoin-core-secp256k1/CMakeLists.txt" ]]; then
        echo "✓ bitcoin-core secp256k1 submodule available (for validation)"
        SECP256K1_MODE="validation_available"
    else
        echo "ℹ bitcoin-core secp256k1 submodule not initialized (not required)"
        SECP256K1_MODE="extracted_only"
    fi
else
    echo "ℹ No git submodules detected (using extracted sources only)"
    SECP256K1_MODE="extracted_only"
fi

# 4. Prepare build directory
echo ""
echo "[4/4] Preparing build configuration..."

BUILD_DIR="$REPO_ROOT/build"
if [[ -d "$BUILD_DIR" ]]; then
    echo "ℹ Build directory exists - will be reconfigured"
    echo "  To start fresh: rm -rf $BUILD_DIR"
else
    echo "✓ Clean build directory ready"
fi

# Summary
echo ""
echo "=== Setup Complete ==="
echo "✓ System dependencies: OK"
echo "✓ Extracted libraries: OK"
echo "✓ Git submodules: $SECP256K1_MODE"
echo "✓ Build directory: Ready"
echo ""
echo "Repository is ready for submodule-free building!"
echo ""
echo "Next steps:"
echo "  1. Configure build:"
echo "     cmake -S . -B build -DCMAKE_BUILD_TYPE=Release"
echo ""
echo "  2. Build:"
echo "     cmake --build build -j\$(nproc)"
echo ""
echo "  3. Or use the offline build script:"
echo "     ./scripts/build-offline.sh"
echo ""
echo "Build features available:"
if [[ "$SECP256K1_MODE" == "validation_available" ]]; then
    echo "  ✓ secp256k1 validation (bitcoin-core + extracted)"
else
    echo "  ✓ secp256k1-zkp (extracted only)"
fi
echo "  ✓ Integration system"
echo "  ✓ Strict attribution enforcement"
echo "  ✓ Offline build capability"
echo ""
echo "For detailed build options, see:"
echo "  - docs/OFFLINE_BUILD.md"
echo "  - docs/SUBMODULE_FREE_BUILD.md"