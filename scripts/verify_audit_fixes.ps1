# Verification Script for Audit Fixes - Session 2025-10-13
# PowerShell version

Write-Host "=========================================" -ForegroundColor Cyan
Write-Host "Audit Fixes Verification Script" -ForegroundColor Cyan
Write-Host "Session: 2025-10-13" -ForegroundColor Cyan
Write-Host "=========================================" -ForegroundColor Cyan
Write-Host ""

$PassCount = 0
$FailCount = 0

function Test-Result {
    param(
        [string]$TestName,
        [bool]$Condition,
        [string]$FailMessage = ""
    )
    
    if ($Condition) {
        Write-Host "✅ PASS: $TestName" -ForegroundColor Green
        $script:PassCount++
    } else {
        Write-Host "❌ FAIL: $TestName - $FailMessage" -ForegroundColor Red
        $script:FailCount++
    }
}

Write-Host "1. Verifying M-001: Duplicate digest_verifier deletion" -ForegroundColor Yellow
Write-Host "-----------------------------------------------------"

Test-Result "M-001: Duplicate file deleted" `
    (-not (Test-Path "src/integration/digest_verifier.h")) `
    "File still exists"

Test-Result "M-001: Main implementation preserved" `
    ((Test-Path "src/integration/verification/digest_verifier.h") -and (Test-Path "src/integration/verification/digest_verifier.cpp")) `
    "Files missing"

Test-Result "M-001: Specialized implementation preserved" `
    ((Test-Path "src/utils/digest_verifier.h") -and (Test-Path "src/utils/digest_verifier.cpp")) `
    "Files missing"

Write-Host ""
Write-Host "2. Verifying M-002: Dead code analysis" -ForegroundColor Yellow
Write-Host "-----------------------------------------------------"

Test-Result "M-002: gpu_context preserved (not dead)" `
    ((Test-Path "src/compute/adapters/reference/gpu_context.h") -and (Test-Path "src/compute/adapters/reference/gpu_context.cpp")) `
    "Files missing"

Test-Result "M-002: conversions preserved (not dead)" `
    ((Test-Path "src/compute/adapters/reference/conversions.h") -and (Test-Path "src/compute/adapters/reference/conversions.cpp")) `
    "Files missing"

Write-Host ""
Write-Host "3. Verifying L-004: Checkpoint manifest nonce implementation" -ForegroundColor Yellow
Write-Host "-----------------------------------------------------"

$solverContent = Get-Content "src/solver.cpp" -Raw

Test-Result "L-004: BytesToHex function added" `
    ($solverContent -match "BytesToHex") `
    "Function not found"

Test-Result "L-004: Nonce length constant added" `
    ($solverContent -match "kNonceLength = 12") `
    "Constant not found"

Test-Result "L-004: TODO comment removed" `
    (-not ($solverContent -match "TODO: populate once crypto is implemented")) `
    "TODO still exists"

Write-Host ""
Write-Host "4. Verifying L-001: Endomorphism split validation test" -ForegroundColor Yellow
Write-Host "-----------------------------------------------------"

if (Test-Path "tests/validation/test_endomorphism_split.cpp") {
    $lineCount = (Get-Content "tests/validation/test_endomorphism_split.cpp").Count
    Test-Result "L-001: Test file implemented ($lineCount lines)" `
        ($lineCount -gt 100) `
        "File too small ($lineCount lines)"
} else {
    Test-Result "L-001: Test file exists" $false "File not found"
}

$endoContent = Get-Content "tests/validation/test_endomorphism_split.cpp" -Raw

Test-Result "L-001: DISABLED test removed" `
    (-not ($endoContent -match "DISABLED_GpuMatchesCpuScalarSplit")) `
    "DISABLED test still exists"

Test-Result "L-001: TODO comment removed" `
    (-not ($endoContent -match "TODO: Implement CUDA vs CPU scalar split")) `
    "TODO still exists"

Write-Host ""
Write-Host "5. Verifying L-002: Batch step increment validation test" -ForegroundColor Yellow
Write-Host "-----------------------------------------------------"

if (Test-Path "tests/validation/test_batch_step_increment.cpp") {
    $lineCount = (Get-Content "tests/validation/test_batch_step_increment.cpp").Count
    Test-Result "L-002: Test file implemented ($lineCount lines)" `
        ($lineCount -gt 100) `
        "File too small ($lineCount lines)"
} else {
    Test-Result "L-002: Test file exists" $false "File not found"
}

$batchContent = Get-Content "tests/validation/test_batch_step_increment.cpp" -Raw

Test-Result "L-002: DISABLED test removed" `
    (-not ($batchContent -match "DISABLED_IncrementsMatchFullMultiplication")) `
    "DISABLED test still exists"

Test-Result "L-002: TODO comment removed" `
    (-not ($batchContent -match "TODO: Implement batch stepping")) `
    "TODO still exists"

Write-Host ""
Write-Host "6. Verifying L-003: GPU validation in test_cpu_gpu_parity" -ForegroundColor Yellow
Write-Host "-----------------------------------------------------"

$parityContent = Get-Content "tests/validation/test_cpu_gpu_parity.cpp" -Raw

Test-Result "L-003: GTEST_SKIP removed" `
    (-not ($parityContent -match "GTEST_SKIP.*Edge case GPU validation")) `
    "GTEST_SKIP still exists"

Test-Result "L-003: TODO comment removed" `
    (-not ($parityContent -match "TODO: GPU validation.*T024")) `
    "TODO still exists"

Test-Result "L-003: GPU validation code added" `
    ($parityContent -match "adapter_->computePublicKey") `
    "GPU validation not found"

Write-Host ""
Write-Host "7. Verifying Documentation" -ForegroundColor Yellow
Write-Host "-----------------------------------------------------"

Test-Result "Documentation: L-004 complete doc" `
    (Test-Path "docs/fixes/L-004-NONCE-IMPLEMENTATION-COMPLETE.md") `
    "File not found"

Test-Result "Documentation: L-001 plan doc" `
    (Test-Path "docs/fixes/L-001-ENDOMORPHISM-SPLIT-PLAN.md") `
    "File not found"

Test-Result "Documentation: Session summary" `
    (Test-Path "SESSION_SUMMARY_2025-10-13_AUDIT_FIXES.md") `
    "File not found"

Test-Result "Documentation: Audit fixes complete" `
    (Test-Path "docs/fixes/AUDIT_FIXES_COMPLETE_2025-10-13.md") `
    "File not found"

Test-Result "Documentation: Completion checklist" `
    (Test-Path "docs/fixes/COMPLETION_CHECKLIST_2025-10-13.md") `
    "File not found"

Write-Host ""
Write-Host "=========================================" -ForegroundColor Cyan
Write-Host "Verification Summary" -ForegroundColor Cyan
Write-Host "=========================================" -ForegroundColor Cyan
Write-Host "Passed:  $PassCount" -ForegroundColor Green
Write-Host "Failed:  $FailCount" -ForegroundColor Red
Write-Host ""

$Total = $PassCount + $FailCount
if ($Total -gt 0) {
    $SuccessRate = [math]::Round(($PassCount * 100 / $Total), 2)
    Write-Host "Success Rate: $SuccessRate%"
}

Write-Host ""
if ($FailCount -eq 0) {
    Write-Host "✅ All verifications passed!" -ForegroundColor Green
    exit 0
} else {
    Write-Host "❌ Some verifications failed!" -ForegroundColor Red
    exit 1
}

