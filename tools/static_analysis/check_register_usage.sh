#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
OUTPUT_DIR="${REPO_ROOT}/docs/validation/evidence/nsight"
OBJ_FILE="${REPO_ROOT}/build/CMakeFiles/Puzzle71Solver.dir/src/puzzle71_kernel.cu.o"
MAX_REGISTERS=128

mkdir -p "$OUTPUT_DIR"
REPORT_TXT="$OUTPUT_DIR/register_report.txt"
REPORT_JSON="$OUTPUT_DIR/register_usage.json"

if [[ ! -f "$OBJ_FILE" ]]; then
  echo "[error] Object file not found: $OBJ_FILE" >&2
  echo "        Run: cmake --build build --target Puzzle71Solver" >&2
  exit 2
fi

if ! command -v cuobjdump >/dev/null 2>&1; then
  echo "[error] cuobjdump not found; ensure CUDA toolkit is installed" >&2
  exit 3
fi

echo "=== CUDA Kernel Resource Usage (cuobjdump) ==="
cuobjdump --dump-resource-usage "$OBJ_FILE" | tee "$REPORT_TXT"

REG_LINE=$(awk '/Function .*Puzzle71FusedKernel/ {getline; if ($0 ~ /REG:/) {print $0; exit}}' "$REPORT_TXT")

if [[ -z "$REG_LINE" ]]; then
  echo "[error] Could not parse register usage for Puzzle71FusedKernel" >&2
  exit 4
fi

REG_COUNT=$(echo "$REG_LINE" | awk -F'[: ]+' '{print $3}')

echo ""
echo "  $REG_LINE"

echo ""
if [[ "$REG_COUNT" -gt "$MAX_REGISTERS" ]]; then
  echo "❌ Registers/thread exceeds budget: $REG_COUNT > $MAX_REGISTERS" >&2
  COMPLIANT_BOOL="False"
  EXIT_CODE=1
else
  echo "✓ Register budget satisfied: $REG_COUNT ≤ $MAX_REGISTERS"
  COMPLIANT_BOOL="True"
  EXIT_CODE=0
fi

echo "" && echo "Writing JSON report to $REPORT_JSON"
python3 - <<PYJSON
import json, time
report = {
  "kernel": "Puzzle71FusedKernel",
  "registers_per_thread": int("${REG_COUNT}"),
  "budget": int("${MAX_REGISTERS}"),
  "compliant": ${COMPLIANT_BOOL},
  "timestamp": time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
  "method": "cuobjdump_resource_usage",
  "environment": "WSL2"
}
with open("${REPORT_JSON}", "w", encoding="utf-8") as fh:
    json.dump(report, fh, indent=2)
PYJSON

exit $EXIT_CODE
