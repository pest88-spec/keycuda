#!/bin/bash

# Offline build script for Puzzle71Solver
# This script configures and builds the project without external network dependencies

set -e

echo "=== Puzzle71Solver Offline Build Script ==="
echo "Building without external network dependencies..."
echo

# Configuration options
BUILD_TYPE="${BUILD_TYPE:-Release}"
BUILD_DIR="${BUILD_DIR:-build-offline}"

echo "Build configuration:"
echo "  Build type: $BUILD_TYPE"
echo "  Build directory: $BUILD_DIR"
echo

# Clean previous build if requested
if [[ "$1" == "clean" ]]; then
    echo "Cleaning previous build..."
    rm -rf "$BUILD_DIR"
fi

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "Configuring CMake for offline build..."

# Configure with offline mode enabled
cmake .. \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DOFFLINE_BUILD=ON \
    -DENABLE_INTEGRATION_SYSTEM=ON \
    -DSTRICT_ATTRIBUTION=ON \
    -DSECP256K1_AVAILABLE=OFF

echo
echo "Building Puzzle71Solver (this may take a while)..."

# Build the project
make -j$(nproc)

echo
echo "=== Build Complete ==="
echo "Executable location: $(pwd)/Puzzle71Solver"
echo
echo "To run the solver:"
echo "  ./Puzzle71Solver [options]"
echo
echo "Build features:"
if [[ -f "Puzzle71Solver" ]]; then
    echo "  ✓ secp256k1-zkp integration: Enabled (extracted)"
    echo "  ✓ Integration system: Enabled"
    echo "  ✓ Strict attribution: Enabled"
    echo "  ✓ Offline build: Yes"
else
    echo "  ✗ Build failed - executable not found"
    exit 1
fi