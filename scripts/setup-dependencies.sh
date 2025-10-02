#!/usr/bin/env bash
# Setup script for production environment dependencies

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

echo "=== Puzzle71Solver Dependency Setup ==="
echo "Repository: $REPO_ROOT"
echo ""

# 1. Check Git submodules
echo "[1/3] Checking Git submodules..."
cd "$REPO_ROOT"

if [[ -d ".git" ]]; then
    echo "Initializing and updating submodules..."
    git submodule update --init --recursive
    echo "✓ Submodules updated"
else
    echo "⚠ Not a git repository, skipping submodule init"
fi

# 2. Install OpenSSL if missing
echo ""
echo "[2/3] Checking OpenSSL..."

if ! pkg-config --exists openssl 2>/dev/null; then
    echo "OpenSSL not found. Installing..."

    # Detect package manager
    if command -v apt-get >/dev/null 2>&1; then
        sudo apt-get update
        sudo apt-get install -y libssl-dev
    elif command -v yum >/dev/null 2>&1; then
        sudo yum install -y openssl-devel
    elif command -v apk >/dev/null 2>&1; then
        sudo apk add openssl-dev
    elif command -v conda >/dev/null 2>&1; then
        conda install -y openssl -c conda-forge
    else
        echo "❌ Error: Cannot detect package manager"
        echo "Please manually install: libssl-dev or openssl-devel"
        exit 1
    fi

    echo "✓ OpenSSL installed"
else
    OPENSSL_VERSION=$(pkg-config --modversion openssl)
    echo "✓ OpenSSL already installed: $OPENSSL_VERSION"
fi

# 3. Verify bitcoin-core-secp256k1
echo ""
echo "[3/3] Verifying bitcoin-core-secp256k1..."

SECP256K1_DIR="$REPO_ROOT/third_party/bitcoin-core-secp256k1"

if [[ ! -f "$SECP256K1_DIR/CMakeLists.txt" ]]; then
    echo "⚠ bitcoin-core-secp256k1 not properly initialized"

    # Try git submodule first
    if [[ -d ".git" ]]; then
        echo "Attempting git submodule update..."
        git submodule update --init --recursive third_party/bitcoin-core-secp256k1
    fi

    # If still missing, clone directly
    if [[ ! -f "$SECP256K1_DIR/CMakeLists.txt" ]]; then
        echo "Cloning bitcoin-core/secp256k1 directly..."
        mkdir -p "$SECP256K1_DIR"
        git clone https://github.com/bitcoin-core/secp256k1.git "$SECP256K1_DIR" || {
            echo "❌ Error: Failed to clone secp256k1"
            exit 1
        }
    fi
fi

if [[ -f "$SECP256K1_DIR/CMakeLists.txt" ]]; then
    echo "✓ bitcoin-core-secp256k1 ready"
else
    echo "❌ Error: bitcoin-core-secp256k1 still missing CMakeLists.txt"
    exit 1
fi

# 4. Summary
echo ""
echo "=== Dependency Check Complete ==="
echo "✓ Git submodules: OK"
echo "✓ OpenSSL: OK"
echo "✓ bitcoin-core-secp256k1: OK"
echo ""
echo "You can now run:"
echo "  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release"
echo "  cmake --build build -j\$(nproc)"
