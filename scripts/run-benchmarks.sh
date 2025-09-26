#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

DEVICES=${1:-"0"}
SAMPLES=${2:-3}

mkdir -p "${REPO_ROOT}/benchmarks"

if [[ ! -d "${REPO_ROOT}/build" ]]; then
  cmake -S "${REPO_ROOT}" -B "${REPO_ROOT}/build" -DCMAKE_BUILD_TYPE=Release
fi

cmake --build "${REPO_ROOT}/build" --target Puzzle71Solver --config Release >/dev/null

SOLVER="${REPO_ROOT}/build/Puzzle71Solver"
if [[ ! -x "$SOLVER" ]]; then
  echo "Solver binary not found at $SOLVER" >&2
  exit 1
fi

JSON="${REPO_ROOT}/benchmarks/latest.json"
echo "[" > "$JSON"
for ((i=1;i<=SAMPLES;i++)); do
  ts=$(date --iso-8601=seconds)
  "$SOLVER" \
    --keyspace 0x400000000000000000:0x40000000000FFFFF \
    --target-address 1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU \
    --operator-id benchmark \
    --operator-purpose smoke \
    --device "$DEVICES" \
    --dry-run >/dev/null 2>&1 || true
  printf '  {"timestamp": "%s", "devices": "%s"}' "$ts" "$DEVICES" >> "$JSON"
  if [[ $i -lt $SAMPLES ]]; then
    echo "," >> "$JSON"
  else
    echo >> "$JSON"
  fi
done
echo "]" >> "$JSON"

echo "Benchmark summary written to $JSON" >&2
