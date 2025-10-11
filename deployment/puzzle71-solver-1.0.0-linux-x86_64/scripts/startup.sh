#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PACKAGE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
export LD_LIBRARY_PATH="$PACKAGE_DIR/lib:$LD_LIBRARY_PATH"
echo "Starting Puzzle71Solver from $PACKAGE_DIR"
exec "$PACKAGE_DIR/bin/Puzzle71Solver" "$@"
