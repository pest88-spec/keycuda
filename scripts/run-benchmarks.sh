#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

DEVICES=${1:-"0"}
SAMPLES=${2:-5}

mkdir -p "${REPO_ROOT}/benchmarks"

echo "Benchmark runner stub (devices=${DEVICES}, samples=${SAMPLES})" >&2
# TODO(T046): Launch benchmark harness, capture metrics to benchmarks/latest.json.
exit 0
