#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
ANALYZER="${ROOT_DIR}/scripts/dynarec/analyze-s03a-log.sh"

usage() {
  cat <<'EOF'
usage: finalize-s03-log.sh [--delete-raw] <current-log> [baseline-log]

Runs S-03 parser, validates required fields, writes compact summary file, and
optionally deletes the large raw current log only after validation passes.
EOF
}

DELETE_RAW=0
if [ "${1:-}" = "--delete-raw" ]; then
  DELETE_RAW=1
  shift
fi

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
  usage >&2
  exit 2
fi

CURRENT_LOG="$1"
BASELINE_LOG="${2:-}"
CURRENT_DIR="$(cd "$(dirname "$CURRENT_LOG")" && pwd)"
STAMP="$(date +%Y-%m-%d_%H-%M-%S)"
SUMMARY_OUT="${CURRENT_DIR}/s03-summary-${STAMP}.txt"

if [ ! -f "$CURRENT_LOG" ]; then
  echo "error: current log not found: $CURRENT_LOG" >&2
  exit 1
fi
if [ -n "$BASELINE_LOG" ] && [ ! -f "$BASELINE_LOG" ]; then
  echo "error: baseline log not found: $BASELINE_LOG" >&2
  exit 1
fi

if [ -n "$BASELINE_LOG" ]; then
  "$ANALYZER" "$CURRENT_LOG" "$BASELINE_LOG" > "$SUMMARY_OUT"
else
  "$ANALYZER" "$CURRENT_LOG" > "$SUMMARY_OUT"
fi

# Safety gate: required evidence must exist before raw log cleanup is allowed.
required_patterns=(
  "^S03_LOG current$"
  "^  summary_seen=yes$"
  "^  dirty_list_hits="
  "^  promote_byte_mask="
  "^  promote_no_immediates="
  "^  transitions_action_byte_mask="
  "^  transitions_action_no_immediates="
  "^S03_TAGS current$"
  "^S03_RETRIES_HIST current before$"
  "^S03_RETRIES_HIST current after$"
  "^  ratio_promote_no_immediates_per_dirty_hit="
)

for pat in "${required_patterns[@]}"; do
  if ! rg -q "$pat" "$SUMMARY_OUT"; then
    echo "error: summary missing required field pattern: $pat" >&2
    echo "summary_file=$SUMMARY_OUT" >&2
    echo "raw_log_kept=1" >&2
    exit 1
  fi
done

if [ -n "$BASELINE_LOG" ]; then
  if ! rg -q "^S03_DELTA baseline_to_current$" "$SUMMARY_OUT"; then
    echo "error: baseline supplied but S03_DELTA block missing" >&2
    echo "summary_file=$SUMMARY_OUT" >&2
    echo "raw_log_kept=1" >&2
    exit 1
  fi
fi

echo "summary_file=$SUMMARY_OUT"
echo "summary_validated=1"
echo "current_log=$CURRENT_LOG"

if [ "$DELETE_RAW" -eq 1 ]; then
  rm -f "$CURRENT_LOG"
  echo "raw_log_deleted=1"
else
  echo "raw_log_deleted=0"
fi
