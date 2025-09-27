#!/bin/bash

# Test Block Size Fix
# 验证block size修复是否生效

set -e

echo "=== Testing Block Size Fix ==="
echo "Date: $(date)"
echo "GPU: $(nvidia-smi --query-gpu=name --format=csv,noheader,nounits | head -1)"
echo ""

# 创建测试目录
TEST_DIR="test_block_size"
mkdir -p "$TEST_DIR"

# 运行测试并捕获输出
echo "Running test with micro range..."
timeout 15s ./build/Puzzle71Solver \
    --keyspace "0x1:0x20" \
    --target-address "1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU" \
    --operator-id "block-size-test" \
    --operator-purpose "verify-fix" \
    2>&1 | tee "$TEST_DIR/block_size_test.log"

echo ""
echo "=== Analyzing Results ==="
echo ""

# 检查block size输出
if grep -q "Block Size: 992" "$TEST_DIR/block_size_test.log"; then
    echo "✅ Block size fix verified: 992 (multiple of 32)"
elif grep -q "Block Size:" "$TEST_DIR/block_size_test.log"; then
    BLOCK_SIZE=$(grep "Block Size:" "$TEST_DIR/block_size_test.log" | awk '{print $3}')
    echo "⚠️  Block size is $BLOCK_SIZE (should be multiple of 32)"
    if (( BLOCK_SIZE % 32 == 0 )); then
        echo "✅ Block size is multiple of 32"
    else
        echo "❌ Block size is NOT multiple of 32"
    fi
else
    echo "❌ Block size information not found in output"
fi

# 检查是否还有KeySearchException
if grep -q "KeySearchException" "$TEST_DIR/block_size_test.log"; then
    echo "⚠️  KeySearchException still present - need further investigation"
else
    echo "✅ No KeySearchException detected"
fi

echo ""
echo "Test completed. Full log: $TEST_DIR/block_size_test.log"