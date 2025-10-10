#!/usr/bin/env bash
# One-click build script for Puzzle71Solver
# Combines dependency setup, configuration, and build

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Default configuration
BUILD_TYPE="${BUILD_TYPE:-Release}"
BUILD_DIR="${BUILD_DIR:-build}"
CLEAN_BUILD="${CLEAN_BUILD:-false}"
OFFLINE_BUILD="${OFFLINE_BUILD:-false}"
ENABLE_TESTS="${ENABLE_TESTS:-false}"
JOBS="${JOBS:-$(nproc)}"

# Parse command line arguments
function show_help() {
    cat << EOF
Puzzle71Solver One-Click Build Script

Usage: $0 [OPTIONS]

OPTIONS:
    -t, --type TYPE        Build type (Debug|Release|RelWithDebInfo) [default: Release]
    -j, --jobs N           Number of parallel build jobs [default: $(nproc)]
    -c, --clean            Clean build directory before building
    -o, --offline          Force offline build mode
    -T, --enable-tests     Enable testing (requires network)
    -d, --dir DIR          Build directory name [default: build]
    -h, --help             Show this help message

ENVIRONMENT VARIABLES:
    BUILD_TYPE             Same as --type
    BUILD_DIR              Same as --dir
    CLEAN_BUILD            Same as --clean
    OFFLINE_BUILD          Same as --offline
    ENABLE_TESTS           Same as --enable-tests
    JOBS                   Same as --jobs

EXAMPLES:
    # Basic release build
    $0

    # Clean debug build with testing
    $0 --clean --type Debug --enable-tests

    # Offline build (no network access)
    $0 --offline --clean

    # Custom build directory and parallel jobs
    $0 --dir my-build --jobs 8

EOF
}

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -t|--type)
            BUILD_TYPE="$2"
            shift 2
            ;;
        -j|--jobs)
            JOBS="$2"
            shift 2
            ;;
        -c|--clean)
            CLEAN_BUILD="true"
            shift
            ;;
        -o|--offline)
            OFFLINE_BUILD="true"
            shift
            ;;
        -T|--enable-tests)
            ENABLE_TESTS="true"
            shift
            ;;
        -d|--dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        -h|--help)
            show_help
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            show_help
            exit 1
            ;;
    esac
done

echo "=== Puzzle71Solver One-Click Build ==="
echo "Repository: $REPO_ROOT"
echo "Build Configuration:"
echo "  Build Type: $BUILD_TYPE"
echo "  Build Directory: $BUILD_DIR"
echo "  Parallel Jobs: $JOBS"
echo "  Clean Build: $CLEAN_BUILD"
echo "  Offline Mode: $OFFLINE_BUILD"
echo "  Enable Tests: $ENABLE_TESTS"
echo ""

# Function to check if running as root
check_root() {
    if [[ $EUID -eq 0 ]]; then
        echo "⚠ Running as root is not recommended for builds"
        echo "  Consider running as a regular user"
        read -p "Continue? (y/N): " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            exit 1
        fi
    fi
}

# Function to clean build directory
clean_build_dir() {
    local build_path="$REPO_ROOT/$BUILD_DIR"
    if [[ -d "$build_path" ]]; then
        echo "Cleaning build directory..."
        rm -rf "$build_path"
        echo "✓ Build directory cleaned"
    fi
}

# Function to run setup script
run_setup() {
    echo "Running dependency setup..."
    cd "$REPO_ROOT"

    if [[ -x "./scripts/setup-simplified.sh" ]]; then
        ./scripts/setup-simplified.sh
    else
        echo "❌ Error: setup-simplified.sh not found or not executable"
        exit 1
    fi
}

# Function to configure cmake
configure_cmake() {
    local build_path="$REPO_ROOT/$BUILD_DIR"
    mkdir -p "$build_path"
    cd "$build_path"

    echo "Configuring CMake..."

    local cmake_args=(
        "-DCMAKE_BUILD_TYPE=$BUILD_TYPE"
        "-DENABLE_INTEGRATION_SYSTEM=ON"
        "-DSTRICT_ATTRIBUTION=ON"
    )

    # Add offline flag if requested
    if [[ "$OFFLINE_BUILD" == "true" ]]; then
        cmake_args+=("-DOFFLINE_BUILD=ON")
    else
        cmake_args+=("-DOFFLINE_BUILD=OFF")
        # Only enable SECP256K1_AVAILABLE if not offline
        if [[ -f "$REPO_ROOT/third_party/bitcoin-core-secp256k1/CMakeLists.txt" ]]; then
            cmake_args+=("-DSECP256K1_AVAILABLE=ON")
        else
            cmake_args+=("-DSECP256K1_AVAILABLE=OFF")
        fi
    fi

    # Configure
    cmake .. "${cmake_args[@]}"

    echo "✓ CMake configuration complete"
}

# Function to build project
build_project() {
    local build_path="$REPO_ROOT/$BUILD_DIR"
    cd "$build_path"

    echo "Building Puzzle71Solver (this may take a while)..."

    # Build with specified number of jobs
    make -j"$JOBS"

    echo "✓ Build complete"
}

# Function to run tests
run_tests() {
    if [[ "$ENABLE_TESTS" == "true" && "$OFFLINE_BUILD" != "true" ]]; then
        local build_path="$REPO_ROOT/$BUILD_DIR"
        cd "$build_path"

        echo "Running tests..."

        if [[ -x "./puzzle71_tests" ]]; then
            ./puzzle71_tests
            echo "✓ Tests passed"
        else
            echo "ℹ Test executable not found (testing may be disabled in this configuration)"
        fi
    elif [[ "$OFFLINE_BUILD" == "true" ]]; then
        echo "ℹ Tests skipped (offline mode)"
    else
        echo "ℹ Tests disabled"
    fi
}

# Function to verify build
verify_build() {
    local build_path="$REPO_ROOT/$BUILD_DIR"
    local executable="$build_path/Puzzle71Solver"

    if [[ -x "$executable" ]]; then
        echo "✓ Executable created: $executable"

        # Get version info
        echo "Getting build information..."
        "$executable" --version 2>/dev/null || echo "Version info not available"

        # Show build artifacts
        echo ""
        echo "Build artifacts:"
        ls -lh "$executable" 2>/dev/null || true

        # Show library dependencies
        echo ""
        echo "Library dependencies:"
        ldd "$executable" 2>/dev/null | head -10 || echo "Dependency information not available"

    else
        echo "❌ Error: Executable not found at $executable"
        exit 1
    fi
}

# Main execution
main() {
    # Check if running as root
    check_root

    # Clean build if requested
    if [[ "$CLEAN_BUILD" == "true" ]]; then
        clean_build_dir
    fi

    # Run setup
    run_setup

    # Configure CMake
    configure_cmake

    # Build project
    build_project

    # Run tests if enabled
    run_tests

    # Verify build
    verify_build

    echo ""
    echo "=== Build Complete ==="
    echo "✓ Dependencies setup: Complete"
    echo "✓ Build configuration: Complete"
    echo "✓ Compilation: Complete"
    if [[ "$ENABLE_TESTS" == "true" && "$OFFLINE_BUILD" != "true" ]]; then
        echo "✓ Tests: Complete"
    fi
    echo "✓ Verification: Complete"
    echo ""
    echo "Your Puzzle71Solver executable is ready:"
    echo "  $REPO_ROOT/$BUILD_DIR/Puzzle71Solver"
    echo ""
    echo "To run the solver:"
    echo "  cd $REPO_ROOT/$BUILD_DIR"
    echo "  ./Puzzle71Solver --help"
    echo ""
    echo "For usage examples and configuration options, see the documentation:"
    echo "  - README.md"
    echo "  - docs/OFFLINE_BUILD.md"
    echo "  - docs/SUBMODULE_FREE_BUILD.md"
}

# Run main function
main