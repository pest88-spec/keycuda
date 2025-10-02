#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

SHOW_HELP=false
DEVICES="0"
SAMPLES=3
KEYSPACE="0x400000000000000000:0x40000000000FFFFF"
DRY_RUN=false

while [[ $# -gt 0 ]]; do
  case "$1" in
    --devices)
      DEVICES="$2"
      shift 2
      ;;
    --samples)
      SAMPLES="$2"
      shift 2
      ;;
    --keyspace)
      KEYSPACE="$2"
      shift 2
      ;;
    --dry-run-only)
      DRY_RUN=true
      shift
      ;;
    -h|--help)
      SHOW_HELP=true
      shift
      ;;
    *)
      echo "Unknown option: $1" >&2
      exit 2
      ;;
  esac
done

if [[ "$SHOW_HELP" == true ]]; then
  cat <<'EOF'
Usage: run-benchmarks.sh [options]
  --devices <list>     Comma-separated CUDA devices (default: "0")
  --samples <n>        Number of benchmark samples (default: 3)
  --keyspace <range>   Keyspace range for each run (default: 0x400000000000000000:0x40000000000FFFFF)
  --dry-run-only       Invoke solver with --dry-run to skip full scan
  -h, --help           Show this help message
EOF
  exit 0
fi

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
  CMD=("$SOLVER"
       --keyspace "$KEYSPACE"
       --target-address 1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU
       --operator-id benchmark
       --operator-purpose smoke
       --device "$DEVICES")
  if [[ "$DRY_RUN" == true ]]; then
    CMD+=(--dry-run)
  fi
  "${CMD[@]}" >/dev/null 2>&1 || true
  printf '  {"timestamp": "%s", "devices": "%s"}' "$ts" "$DEVICES" >> "$JSON"
  if [[ $i -lt $SAMPLES ]]; then
    echo "," >> "$JSON"
  else
    echo >> "$JSON"
  fi
done
echo "]" >> "$JSON"

echo "Benchmark summary written to $JSON" >&2
