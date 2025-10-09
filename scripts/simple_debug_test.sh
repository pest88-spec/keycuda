#!/bin/bash

# Simple KeySearchException Debug Test
# 用极小范围直接测试问题

set -e

DEBUG_DIR="debug_keysearch"
mkdir -p "$DEBUG_DIR"

echo "=== Simple KeySearchException Debug Test ==="
echo "Date: $(date)"
echo "GPU: $(nvidia-smi --query-gpu=name --format=csv,noheader,nounits | head -1)"
echo ""

# 测试极小范围
echo "Testing micro range (10 keys)..."
timeout 15s ./build/Puzzle71Solver \
    --keyspace "0x1:0xA" \
    --target-address "1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU" \
    --operator-id "debug-test" \
    --operator-purpose "keysearch-debug" \
    --telemetry-jsonl "$DEBUG_DIR/telemetry" \
    2>&1 | tee "$DEBUG_DIR/test_10_keys.log"

echo ""
echo "Test completed. Check logs in $DEBUG_DIR/"