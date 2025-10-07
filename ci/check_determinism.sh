#!/usr/bin/env bash
# puzzle71_constraints.md §1.1 Determinism Gate

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GATE_SCRIPT="${SCRIPT_DIR}/determinism_gate.sh"

if [[ ! -x "${GATE_SCRIPT}" ]]; then
  echo "[error] determinism_gate.sh is missing or not executable" >&2
  exit 1
fi

"${GATE_SCRIPT}"
