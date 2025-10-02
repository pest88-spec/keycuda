#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
SOLVER="${REPO_ROOT}/build/Puzzle71Solver"
PROFILE_DIR="${REPO_ROOT}/docs/validation/evidence/nsight"
PROFILE_PREFIX="puzzle71_profile"

if [[ ! -x "$SOLVER" ]]; then
  echo "[error] Solver binary not found at $SOLVER" >&2
  echo "        Run: cmake --build build --target Puzzle71Solver" >&2
  exit 2
fi

if ! command -v nsight-cu-cli >/dev/null 2>&1; then
  echo "[warn] nsight-cu-cli not found; skipping profile run" >&2
  exit 0
fi

mkdir -p "$PROFILE_DIR"
OUTPUT_PATH="${PROFILE_DIR}/${PROFILE_PREFIX}"

KEYSPACE_START=0x400000000000000000
KEYSPACE_END=0x4000000000000000FF
TARGET_ADDR=1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU
OP_ID=profile
OP_PURPOSE=nsight
DEVICE_IDS=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --keyspace)
      if [[ "$2" != *:* ]]; then
        echo "[error] --keyspace expects start:end" >&2
        exit 2
      fi
      KEYSPACE_START="${2%%:*}"
      KEYSPACE_END="${2##*:}"
      shift 2
      ;;
    --keyspace-start)
      KEYSPACE_START="$2"
      shift 2
      ;;
    --keyspace-end)
      KEYSPACE_END="$2"
      shift 2
      ;;
    --target-address)
      TARGET_ADDR="$2"
      shift 2
      ;;
    --operator-id)
      OP_ID="$2"
      shift 2
      ;;
    --operator-purpose)
      OP_PURPOSE="$2"
      shift 2
      ;;
    --device)
      DEVICE_IDS="$2"
      shift 2
      ;;
    -h|--help)
      cat <<'EOF'
Usage: puzzle71_profile.sh [options]
  --keyspace <start:end>     Keyspace (default 0xa00:0xbff)
  --keyspace-start <hex>
  --keyspace-end <hex>
  --target-address <addr>    Target address (default Puzzle #71)
  --operator-id <id>         Operator ID (default profile)
  --operator-purpose <txt>   Operator purpose (default nsight)
  --device <ids>             Comma-separated device list passed to solver
EOF
      exit 0
      ;;
    *)
      echo "[error] Unknown option: $1" >&2
      exit 2
      ;;
  esac
done

METRICS="sm__inst_executed.sum,sm__sass_thread_inst_executed_op_integer.sum,sm__sass_thread_inst_executed_op_memory.sum,smsp__thread_inst_executed_per_warp_active.avg"

SOLVER_ARGS=(
  --keyspace "${KEYSPACE_START}:${KEYSPACE_END}"
  --target-address "$TARGET_ADDR"
  --operator-id "$OP_ID"
  --operator-purpose "$OP_PURPOSE"
  --telemetry-jsonl telemetry_nsight
  --enable-checkpoint
)

if [[ -n "$DEVICE_IDS" ]]; then
  SOLVER_ARGS+=(--device "$DEVICE_IDS")
fi

nsight-cu-cli \
  --profile-from-start on \
  --target-processes all \
  --metrics "$METRICS" \
  --set full \
  --launch-skip 0 \
  --launch-count 1 \
  --section SpeedOfLight \
  --csv \
  --export "$OUTPUT_PATH" \
  "$SOLVER" \
  "${SOLVER_ARGS[@]}"

REPORT="${OUTPUT_PATH}.ncu-rep"
if [[ -f "$REPORT" ]]; then
  echo "[info] Nsight profile captured: $REPORT" >&2
else
  echo "[warn] Nsight profile did not produce .ncu-rep file" >&2
fi
