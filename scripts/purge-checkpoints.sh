#!/usr/bin/env bash
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

usage() {
  cat <<'EOF'
Usage: purge-checkpoints.sh [--dry-run] [--keep-days <N>]
  --dry-run         Show files that would be removed without deleting them
  --keep-days <N>   Retain checkpoint artifacts newer than N days (default: 30)
EOF
}

KEEP_DAYS=30
DRY_RUN=false

while [[ $# -gt 0 ]]; do
  case "$1" in
    --dry-run)
      DRY_RUN=true
      shift
      ;;
    --keep-days)
      if [[ -z "${2:-}" || ! "$2" =~ ^[0-9]+$ ]]; then
        echo "--keep-days requires an integer" >&2
        exit 2
      fi
      KEEP_DAYS="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage
      exit 2
      ;;
  esac
done

CUTOFF=$(date -d "-${KEEP_DAYS} days" +%s)

CHECKPOINT_DIR="${REPO_ROOT}/checkpoints"

mapfile -t PAYLOADS < <(find "$CHECKPOINT_DIR" -type f -name 'payload-*.chk') || true

if [[ ${#PAYLOADS[@]} -eq 0 ]]; then
  echo "No payloads found"
  exit 0
fi

DRY_COUNT=0
REMOVED_PAYLOADS=0
REMOVED_MANIFESTS=0

for payload in "${PAYLOADS[@]}"; do
  MTIME=$(stat -c %Y "$payload")
  if (( MTIME < CUTOFF )); then
    manifest="${payload%.chk}.json"
    manifest="${manifest//\/payload-/\/manifest-}"
    if [[ "$DRY_RUN" == true ]]; then
      echo "[dry-run] would remove $payload"
      if [[ -f "$manifest" ]]; then
        echo "[dry-run] would remove $manifest"
      fi
      ((DRY_COUNT++))
    else
      rm -f "$payload"
      ((REMOVED_PAYLOADS++))
      if [[ -f "$manifest" ]]; then
        rm -f "$manifest"
        ((REMOVED_MANIFESTS++))
        echo "Removed $payload and $manifest"
      else
        echo "Removed $payload"
      fi
    fi
  fi
done

mapfile -t MANIFESTS < <(find "$CHECKPOINT_DIR" -type f -name 'manifest-*.json') || true
for manifest in "${MANIFESTS[@]}"; do
  MTIME=$(stat -c %Y "$manifest")
  payload="${manifest//\/manifest-/\/payload-}"
  payload="${payload%.json}.chk"
  if (( MTIME < CUTOFF )) && [[ ! -f "$payload" ]]; then
    if [[ "$DRY_RUN" == true ]]; then
      echo "[dry-run] would remove orphan manifest $manifest"
    else
      rm -f "$manifest"
      ((REMOVED_MANIFESTS++))
      echo "Removed orphan manifest $manifest"
    fi
  fi
done

if [[ "$DRY_RUN" == true ]]; then
  echo "[dry-run] Identified $DRY_COUNT payload(s) older than ${KEEP_DAYS} days"
else
  echo "Removed $REMOVED_PAYLOADS payload(s) and $REMOVED_MANIFESTS manifest(s) older than ${KEEP_DAYS} days"
fi
