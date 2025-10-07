#!/usr/bin/env bash
# puzzle71_constraints.md §1.3 No-Crypto-Reinvention

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

VIOLATIONS=0
FORBIDDEN_IMPL=(
  "my_ec_mul" "custom_scalar_mul" "simple_point_add"
  "basic_modular_inverse" "quick_bigint" "fast_hash160"
  "optimized_ecdsa" "improved_secp256k1"
)

for pattern in "${FORBIDDEN_IMPL[@]}"; do
  if grep -R "${pattern}" "$REPO_ROOT/src" --include='*.cpp' --include='*.cu' --include='*.h' >/dev/null 2>&1; then
    echo "[error] Detected forbidden crypto identifier: ${pattern}" >&2
    VIOLATIONS=$((VIOLATIONS + 1))
  fi
  if grep -R "${pattern}" "$REPO_ROOT/tests" --include='*.cpp' --include='*.cu' >/dev/null 2>&1; then
    echo "[error] Detected forbidden crypto identifier in tests: ${pattern}" >&2
    VIOLATIONS=$((VIOLATIONS + 1))
  fi
  if grep -R "${pattern}" "$REPO_ROOT/scripts" --include='*.sh' >/dev/null 2>&1; then
    echo "[error] Detected forbidden crypto identifier in scripts: ${pattern}" >&2
    VIOLATIONS=$((VIOLATIONS + 1))
  fi
done

if grep -R "#include" "$REPO_ROOT/src" --include='*.cpp' --include='*.h' --include='*.cu' |
   grep -E "secp256k1.*\\.h" |
   grep -vE 'adapter|bridge|extracted' >/dev/null 2>&1; then
  echo "[warn] Direct secp256k1 header include detected outside adapters/extracted." >&2
fi

if [[ $VIOLATIONS -gt 0 ]]; then
  exit 1
fi

echo "[crypto] No forbidden custom implementations detected" >&2
