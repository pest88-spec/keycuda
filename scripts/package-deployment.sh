#!/bin/bash

# Deployment Package Generation Script
# Creates self-contained deployment packages with all dependencies included

set -euo pipefail

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
DEPLOYMENT_DIR="${PROJECT_ROOT}/deployment"
PACKAGES_DIR="${PROJECT_ROOT}/packages"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Logging
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
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

# Parse command line arguments
PACKAGE_NAME="puzzle71-solver"
VERSION="1.0.0"
PLATFORM="linux-x86_64"
INCLUDE_DEPS=true
COMPRESS=true

while [[ $# -gt 0 ]]; do
    case $1 in
        --name)
            PACKAGE_NAME="$2"
            shift 2
            ;;
        --version)
            VERSION="$2"
            shift 2
            ;;
        --platform)
            PLATFORM="$2"
            shift 2
            ;;
        --no-deps)
            INCLUDE_DEPS=false
            shift
            ;;
        --no-compress)
            COMPRESS=false
            shift
            ;;
        --help)
            cat << EOF
Deployment Package Generation Script

Usage: $0 [OPTIONS]

OPTIONS:
    --name NAME         Package name (default: puzzle71-solver)
    --version VERSION   Package version (default: 1.0.0)
    --platform PLATFORM Target platform (default: linux-x86_64)
    --no-deps          Exclude dependencies from package
    --no-compress      Create uncompressed package
    --help             Show this help message

EXAMPLES:
    $0                          # Default package with dependencies
    $0 --name myapp --version 2.0  # Custom name and version
    $0 --no-compress              # Uncompressed package for testing

