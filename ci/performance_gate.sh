#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

DEVICES=${CI_BENCHMARK_DEVICES:-0}
WARMUP=${CI_BENCHMARK_WARMUP:-1}
SAMPLES=${CI_BENCHMARK_SAMPLES:-3}
KEYSPACE=${CI_BENCHMARK_KEYSPACE:-0x400000000000000000:0x40000000000000FFFF}
DRY_FLAG=""
if [[ ${CI_BENCHMARK_DRY_RUN:-false} == "true" ]]; then
  DRY_FLAG="--dry-run-only"
fi

args=("--devices" "$DEVICES" "--warmup" "$WARMUP" "--samples" "$SAMPLES" "--keyspace" "$KEYSPACE")
if [[ -n "$DRY_FLAG" ]]; then
  args+=("$DRY_FLAG")
fi

"$REPO_ROOT/scripts/run-benchmarks.sh" "${args[@]}"

python3 - <<'PY'
import json
from pathlib import Path
result = json.loads(Path('benchmarks/latest.json').read_text())
print(f"[performance] median={result['stats']['median']:.2f} keys/sec, min={result['stats']['min']:.2f}, max={result['stats']['max']:.2f}")
PY
