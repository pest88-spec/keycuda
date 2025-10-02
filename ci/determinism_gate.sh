#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
SOLVER="$REPO_ROOT/build/Puzzle71Solver"

if [[ ! -x "$SOLVER" ]]; then
  cmake -S "$REPO_ROOT" -B "$REPO_ROOT/build" -DCMAKE_BUILD_TYPE=Release >/dev/null
  cmake --build "$REPO_ROOT/build" --target Puzzle71Solver --config Release >/dev/null
fi

KEYSPACE=${CI_DETERMINISM_KEYSPACE:-0x400000000000000000:0x4000000000000000FF}
DEVICES=${CI_DETERMINISM_DEVICES:-0}
TARGET=1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU

run_once() {
  local dir
  dir=$(mktemp -d)
  local cmd=("$SOLVER"
             --keyspace "$KEYSPACE"
             --target-address "$TARGET"
             --operator-id determinism
             --operator-purpose gate
             --telemetry-jsonl "$dir"
             --luck-file /dev/null)
  if [[ -n "$DEVICES" ]]; then
    cmd+=(--device "$DEVICES")
  fi
  "${cmd[@]}" >/dev/null 2>&1
  python3 - <<PY
import json, pathlib, sys
src = pathlib.Path("$dir/puzzle71solver.ndjson")
if not src.exists():
    print('[error] telemetry missing', file=sys.stderr)
    sys.exit(1)
data = []
for line in src.read_text().splitlines():
    if not line.strip():
        continue
    obj = json.loads(line)
    for transient in ('timestamp', 'elapsed_ms', 'keys_per_sec'):
        obj.pop(transient, None)
    data.append(json.dumps(obj, sort_keys=True))
pathlib.Path("$dir/normalized.jsonl").write_text("\n".join(data)+"\n")
PY
  echo "$dir"
}

dir1=$(run_once)
dir2=$(run_once)

if ! diff -u "$dir1/normalized.jsonl" "$dir2/normalized.jsonl" >/dev/null; then
  echo "[error] Telemetry outputs differ across identical runs" >&2
  diff -u "$dir1/normalized.jsonl" "$dir2/normalized.jsonl" || true
  rm -rf "$dir1" "$dir2"
  exit 1
fi

echo "[determinism] Telemetry outputs are identical" >&2
rm -rf "$dir1" "$dir2"
