#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
SOLVER="${REPO_ROOT}/build/Puzzle71Solver"
OUTPUT_FILE="${REPO_ROOT}/benchmarks/latest.json"
BASELINE_FILE="${REPO_ROOT}/benchmarks/baseline/gpu_baselines_wsl2.json"

DEVICES="0"
WARMUP=1
SAMPLES=5
KEYSPACE="0x400000000000000000:0x40000000000000FFFF"
TARGET_ADDR="1PWo3JeB9jrGwfHDNpdGK54CRas7fsVzXU"
DRY_RUN=false

usage() {
  cat <<'EOF'
Usage: run-benchmarks.sh [options]
  --devices <csv>        Comma-separated CUDA device list (default: 0)
  --warmup <n>          Number of warm-up runs (default: 1)
  --samples <n>         Number of measurement samples (default: 5)
  --keyspace <start:end> Keyspace range to benchmark (default: puzzle71 slice)
  --target-address <addr> Override target address
  --dry-run-only        Invoke solver with --dry-run (logic exercised, throughput synthetic)
  --output <file>       Override output JSON file (default: benchmarks/latest.json)
  --baseline <file>     Override baseline JSON path
  -h, --help            Show this message
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --devices)
      DEVICES="$2"; shift 2;;
    --warmup)
      WARMUP="$2"; shift 2;;
    --samples)
      SAMPLES="$2"; shift 2;;
    --keyspace)
      KEYSPACE="$2"; shift 2;;
    --target-address)
      TARGET_ADDR="$2"; shift 2;;
    --dry-run-only)
      DRY_RUN=true; shift;;
    --output)
      OUTPUT_FILE="$2"; shift 2;;
    --baseline)
      BASELINE_FILE="$2"; shift 2;;
    -h|--help)
      usage; exit 0;;
    *)
      echo "Unknown option: $1" >&2
      usage
      exit 2;;
  esac
done

mkdir -p "${REPO_ROOT}/benchmarks"

if [[ ! -d "${REPO_ROOT}/build" ]] || [[ ! -x "$SOLVER" ]]; then
  cmake -S "$REPO_ROOT" -B "$REPO_ROOT/build" -DCMAKE_BUILD_TYPE=Release >/dev/null
  cmake --build "$REPO_ROOT/build" --target Puzzle71Solver --config Release >/dev/null
fi

if [[ ! -x "$SOLVER" ]]; then
  echo "[error] Solver binary not found at $SOLVER" >&2
  exit 1
fi

GPU_MODEL="Unknown"
if command -v nvidia-smi >/dev/null 2>&1; then
  set +e
  GPU_MODEL=$(nvidia-smi --query-gpu=name --format=csv,noheader | head -n1 2>/dev/null)
  [[ -z "$GPU_MODEL" ]] && GPU_MODEL="Unknown"
  set -e
fi

run_iteration() {
  local purpose="$1"
  local telemetry_dir
  telemetry_dir=$(mktemp -d)
  local cmd=("$SOLVER"
             --keyspace "$KEYSPACE"
             --target-address "$TARGET_ADDR"
             --operator-id benchmark
             --operator-purpose "$purpose"
             --telemetry-jsonl "$telemetry_dir"
             --luck-file /dev/null)
  if [[ -n "$DEVICES" ]]; then
    cmd+=(--device "$DEVICES")
  fi
  if [[ "$DRY_RUN" == true ]]; then
    cmd+=(--dry-run)
  fi

  "${cmd[@]}" >/dev/null 2>&1 || true

  local telemetry_file="$telemetry_dir/puzzle71solver.ndjson"
  local throughput
  throughput=$(python3 - <<PY
import json, pathlib
path = pathlib.Path("$telemetry_file")
if not path.exists():
    print('NaN')
else:
    lines = path.read_text().strip().splitlines()
    if not lines:
        print('NaN')
    else:
        data = json.loads(lines[-1])
        kps = data.get('keys_per_sec')
        if isinstance(kps, (int, float)):
            print(kps)
        else:
            processed = data.get('processed_keys')
            elapsed = data.get('elapsed_ms')
            if processed and elapsed:
                print((processed * 1000.0) / elapsed)
            else:
                print('NaN')
PY
)
  rm -rf "$telemetry_dir"
  echo "$throughput"
}

THROUGHPUTS=()

for ((i=1; i<=WARMUP; ++i)); do
  run_iteration "warmup" >/dev/null
  echo "[info] Warm-up $i/${WARMUP} complete" >&2
done

for ((i=1; i<=SAMPLES; ++i)); do
  value=$(run_iteration "measurement")
  if [[ "$value" == "NaN" ]]; then
    echo "[warn] Sample $i produced no telemetry; treating as 0" >&2
    value=0
  fi
  THROUGHPUTS+=("$value")
  printf '[info] Sample %d/%d: %.2f keys/sec\n' "$i" "$SAMPLES" "$value" >&2
done

THROUGHPUT_VALUES=$(printf '%s ' "${THROUGHPUTS[@]}")
export THROUGHPUT_VALUES DEVICES GPU_MODEL WARMUP KEYSPACE DRY_RUN OUTPUT_FILE BASELINE_FILE

python3 - <<PY
import json, os, statistics, sys, time
from pathlib import Path

values = [float(x) for x in os.environ['THROUGHPUT_VALUES'].split() if x.strip()]
if not values:
    print('[error] No throughput samples collected', file=sys.stderr)
    sys.exit(1)

median = statistics.median(values)
minimum = min(values)
maximum = max(values)
mean = statistics.mean(values)
stddev = statistics.stdev(values) if len(values) > 1 else 0.0

result = {
    "timestamp": time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
    "devices": os.environ.get('DEVICES', ''),
    "gpu_model": os.environ.get('GPU_MODEL', 'Unknown'),
    "warmup_runs": int(os.environ.get('WARMUP', '0')),
    "samples": len(values),
    "keyspace": os.environ.get('KEYSPACE'),
    "dry_run": os.environ.get('DRY_RUN', 'false') == 'true',
    "results": [
        {"index": idx + 1, "keys_per_sec": v}
        for idx, v in enumerate(values)
    ],
    "stats": {
        "min": minimum,
        "max": maximum,
        "median": median,
        "mean": mean,
        "stdev": stddev
    }
}

outfile = Path(os.environ['OUTPUT_FILE'])
outfile.parent.mkdir(parents=True, exist_ok=True)
outfile.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
print(f"[info] Benchmark results written to {outfile}")

baseline_path = Path(os.environ['BASELINE_FILE'])
if result['dry_run']:
    print('[warn] Dry-run mode enabled; skipping baseline comparison', file=sys.stderr)
elif baseline_path.exists():
    baseline_data = json.loads(baseline_path.read_text())
    gpu = result["gpu_model"]
    baselines = baseline_data.get("baselines", [])
    entry = next((item for item in baselines if item.get("gpu_model") == gpu), None)
    if entry is None:
        print(f"[warn] No baseline entry for GPU '{gpu}'", file=sys.stderr)
    else:
        baseline_min = float(entry.get("min_keys_per_sec", 0.0))
        threshold = baseline_min * 0.95
        if result["stats"]["median"] < threshold:
            print(f"[error] Median throughput {result['stats']['median']:.2f} below baseline threshold {threshold:.2f}", file=sys.stderr)
            sys.exit(1)
        else:
            print(f"[info] Median throughput {result['stats']['median']:.2f} meets baseline {baseline_min:.2f}")
else:
    print("[warn] Baseline file not found; skipping comparison", file=sys.stderr)
PY
