#!/bin/bash
# Verification Script for Audit Fixes - Session 2025-10-13
# This script verifies that all audit fixes have been properly implemented

set -e  # Exit on error

echo "========================================="
echo "Audit Fixes Verification Script"
echo "Session: 2025-10-13"
echo "========================================="
echo ""

# Color codes
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Counters
PASS_COUNT=0
FAIL_COUNT=0
SKIP_COUNT=0

# Function to print test result
print_result() {
    local test_name="$1"
    local result="$2"
    local message="$3"
    
    if [ "$result" = "PASS" ]; then
        echo -e "${GREEN}✅ PASS${NC}: $test_name"
        ((PASS_COUNT++))
    elif [ "$result" = "FAIL" ]; then
        echo -e "${RED}❌ FAIL${NC}: $test_name - $message"
        ((FAIL_COUNT++))
    elif [ "$result" = "SKIP" ]; then
        echo -e "${YELLOW}⏭️  SKIP${NC}: $test_name - $message"
        ((SKIP_COUNT++))
    fi
}

echo "1. Verifying M-001: Duplicate digest_verifier deletion"
echo "-----------------------------------------------------"

# Check that duplicate file was deleted
if [ ! -f "src/integration/digest_verifier.h" ]; then
    print_result "M-001: Duplicate file deleted" "PASS"
else
    print_result "M-001: Duplicate file deleted" "FAIL" "File still exists"
fi

# Check that main implementation still exists
if [ -f "src/integration/verification/digest_verifier.h" ] && [ -f "src/integration/verification/digest_verifier.cpp" ]; then
    print_result "M-001: Main implementation preserved" "PASS"
else
    print_result "M-001: Main implementation preserved" "FAIL" "Files missing"
fi

# Check that specialized implementation still exists
if [ -f "src/utils/digest_verifier.h" ] && [ -f "src/utils/digest_verifier.cpp" ]; then
    print_result "M-001: Specialized implementation preserved" "PASS"
else
    print_result "M-001: Specialized implementation preserved" "FAIL" "Files missing"
fi

echo ""
echo "2. Verifying M-002: Dead code analysis"
echo "-----------------------------------------------------"

# Check that reference adapter files still exist (should NOT be deleted)
if [ -f "src/compute/adapters/reference/gpu_context.h" ] && [ -f "src/compute/adapters/reference/gpu_context.cpp" ]; then
    print_result "M-002: gpu_context preserved (not dead)" "PASS"
else
    print_result "M-002: gpu_context preserved (not dead)" "FAIL" "Files missing"
fi

if [ -f "src/compute/adapters/reference/conversions.h" ] && [ -f "src/compute/adapters/reference/conversions.cpp" ]; then
    print_result "M-002: conversions preserved (not dead)" "PASS"
else
    print_result "M-002: conversions preserved (not dead)" "FAIL" "Files missing"
fi

echo ""
echo "3. Verifying L-004: Checkpoint manifest nonce implementation"
echo "-----------------------------------------------------"

# Check that BytesToHex function exists in solver.cpp
if grep -q "BytesToHex" src/solver.cpp; then
    print_result "L-004: BytesToHex function added" "PASS"
else
    print_result "L-004: BytesToHex function added" "FAIL" "Function not found"
fi

# Check that nonce generation code exists
if grep -q "kNonceLength = 12" src/solver.cpp; then
    print_result "L-004: Nonce length constant added" "PASS"
else
    print_result "L-004: Nonce length constant added" "FAIL" "Constant not found"
fi

# Check that TODO comment was removed
if grep -q "TODO: populate once crypto is implemented" src/solver.cpp; then
    print_result "L-004: TODO comment removed" "FAIL" "TODO still exists"
else
    print_result "L-004: TODO comment removed" "PASS"
fi

echo ""
echo "4. Verifying L-001: Endomorphism split validation test"
echo "-----------------------------------------------------"

# Check that test file exists and has content
if [ -f "tests/validation/test_endomorphism_split.cpp" ]; then
    LINE_COUNT=$(wc -l < tests/validation/test_endomorphism_split.cpp)
    if [ "$LINE_COUNT" -gt 100 ]; then
        print_result "L-001: Test file implemented ($LINE_COUNT lines)" "PASS"
    else
        print_result "L-001: Test file implemented" "FAIL" "File too small ($LINE_COUNT lines)"
    fi
else
    print_result "L-001: Test file exists" "FAIL" "File not found"
fi

# Check that DISABLED prefix was removed
if grep -q "DISABLED_GpuMatchesCpuScalarSplit" tests/validation/test_endomorphism_split.cpp; then
    print_result "L-001: DISABLED test removed" "FAIL" "DISABLED test still exists"
else
    print_result "L-001: DISABLED test removed" "PASS"
fi

# Check that TODO comment was removed
if grep -q "TODO: Implement CUDA vs CPU scalar split" tests/validation/test_endomorphism_split.cpp; then
    print_result "L-001: TODO comment removed" "FAIL" "TODO still exists"
else
    print_result "L-001: TODO comment removed" "PASS"
fi

echo ""
echo "5. Verifying L-002: Batch step increment validation test"
echo "-----------------------------------------------------"

# Check that test file exists and has content
if [ -f "tests/validation/test_batch_step_increment.cpp" ]; then
    LINE_COUNT=$(wc -l < tests/validation/test_batch_step_increment.cpp)
    if [ "$LINE_COUNT" -gt 100 ]; then
        print_result "L-002: Test file implemented ($LINE_COUNT lines)" "PASS"
    else
        print_result "L-002: Test file implemented" "FAIL" "File too small ($LINE_COUNT lines)"
    fi
else
    print_result "L-002: Test file exists" "FAIL" "File not found"
fi

# Check that DISABLED prefix was removed
if grep -q "DISABLED_IncrementsMatchFullMultiplication" tests/validation/test_batch_step_increment.cpp; then
    print_result "L-002: DISABLED test removed" "FAIL" "DISABLED test still exists"
else
    print_result "L-002: DISABLED test removed" "PASS"
fi

# Check that TODO comment was removed
if grep -q "TODO: Implement batch stepping" tests/validation/test_batch_step_increment.cpp; then
    print_result "L-002: TODO comment removed" "FAIL" "TODO still exists"
else
    print_result "L-002: TODO comment removed" "PASS"
fi

echo ""
echo "6. Verifying L-003: GPU validation in test_cpu_gpu_parity"
echo "-----------------------------------------------------"

# Check that GTEST_SKIP was removed
if grep -q "GTEST_SKIP.*Edge case GPU validation" tests/validation/test_cpu_gpu_parity.cpp; then
    print_result "L-003: GTEST_SKIP removed" "FAIL" "GTEST_SKIP still exists"
else
    print_result "L-003: GTEST_SKIP removed" "PASS"
fi

# Check that TODO comment was removed
if grep -q "TODO: GPU validation.*T024" tests/validation/test_cpu_gpu_parity.cpp; then
    print_result "L-003: TODO comment removed" "FAIL" "TODO still exists"
else
    print_result "L-003: TODO comment removed" "PASS"
fi

# Check that GPU validation code was added
if grep -q "adapter_->computePublicKey" tests/validation/test_cpu_gpu_parity.cpp; then
    print_result "L-003: GPU validation code added" "PASS"
else
    print_result "L-003: GPU validation code added" "FAIL" "GPU validation not found"
fi

echo ""
echo "7. Verifying Documentation"
echo "-----------------------------------------------------"

# Check that documentation files were created
if [ -f "docs/fixes/L-004-NONCE-IMPLEMENTATION-COMPLETE.md" ]; then
    print_result "Documentation: L-004 complete doc" "PASS"
else
    print_result "Documentation: L-004 complete doc" "FAIL" "File not found"
fi

if [ -f "docs/fixes/L-001-ENDOMORPHISM-SPLIT-PLAN.md" ]; then
    print_result "Documentation: L-001 plan doc" "PASS"
else
    print_result "Documentation: L-001 plan doc" "FAIL" "File not found"
fi

if [ -f "SESSION_SUMMARY_2025-10-13_AUDIT_FIXES.md" ]; then
    print_result "Documentation: Session summary" "PASS"
else
    print_result "Documentation: Session summary" "FAIL" "File not found"
fi

if [ -f "docs/fixes/AUDIT_FIXES_COMPLETE_2025-10-13.md" ]; then
    print_result "Documentation: Audit fixes complete" "PASS"
else
    print_result "Documentation: Audit fixes complete" "FAIL" "File not found"
fi

if [ -f "docs/fixes/COMPLETION_CHECKLIST_2025-10-13.md" ]; then
    print_result "Documentation: Completion checklist" "PASS"
else
    print_result "Documentation: Completion checklist" "FAIL" "File not found"
fi

echo ""
echo "========================================="
echo "Verification Summary"
echo "========================================="
echo -e "${GREEN}Passed:${NC} $PASS_COUNT"
echo -e "${RED}Failed:${NC} $FAIL_COUNT"
echo -e "${YELLOW}Skipped:${NC} $SKIP_COUNT"
echo ""

TOTAL=$((PASS_COUNT + FAIL_COUNT + SKIP_COUNT))
if [ $TOTAL -gt 0 ]; then
    SUCCESS_RATE=$((PASS_COUNT * 100 / TOTAL))
    echo "Success Rate: $SUCCESS_RATE%"
fi

echo ""
if [ $FAIL_COUNT -eq 0 ]; then
    echo -e "${GREEN}✅ All verifications passed!${NC}"
    exit 0
else
    echo -e "${RED}❌ Some verifications failed!${NC}"
    exit 1
fi

