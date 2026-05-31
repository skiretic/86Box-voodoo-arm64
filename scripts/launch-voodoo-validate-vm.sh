#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_PATH="${APP_PATH:-$REPO_ROOT/build/src/86Box.app}"
BIN_PATH="$APP_PATH/Contents/MacOS/86Box"
VM_PATH="${VM_PATH:-$HOME/Library/Application Support/86Box/Virtual Machines/Windows 98 Gaming PC}"
LOG_DIR="${LOG_DIR:-/tmp/86box-voodoo-validate}"
VALIDATE_MODE="${VOODOO_VALIDATE:-verify}"
VALIDATE_LIMIT="${VOODOO_VALIDATE_LIMIT:-409600000}"
VALIDATE_LOG_LIMIT="${VOODOO_VALIDATE_LOG_LIMIT:-32}"
JIT_METRICS="${VOODOO_ARM64_JIT_METRICS:-1}"
ALLOW_EXISTING=0
LOG_PATH=""

usage() {
    cat <<EOF
Usage: $(basename "$0") [options]

Launch the Windows 98 Gaming PC VM with Voodoo validator logging enabled.

Options:
  --vm PATH             VM directory. Default: $VM_PATH
  --log PATH            Log file path. Default: $LOG_DIR/voodoo-validate-<timestamp>.log
  --log-dir PATH        Log directory. Default: $LOG_DIR
  --limit N             VOODOO_VALIDATE_LIMIT. Default: $VALIDATE_LIMIT
  --log-limit N         VOODOO_VALIDATE_LOG_LIMIT. Default: $VALIDATE_LOG_LIMIT
  --mode MODE           VOODOO_VALIDATE. Default: $VALIDATE_MODE
  --metrics 0|1         VOODOO_ARM64_JIT_METRICS. Default: $JIT_METRICS
  --allow-existing      Do not refuse when another 86Box from this checkout is running.
  -h, --help            Show this help.
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --vm)
            VM_PATH="$2"
            shift 2
            ;;
        --log)
            LOG_PATH="$2"
            shift 2
            ;;
        --log-dir)
            LOG_DIR="$2"
            shift 2
            ;;
        --limit)
            VALIDATE_LIMIT="$2"
            shift 2
            ;;
        --log-limit)
            VALIDATE_LOG_LIMIT="$2"
            shift 2
            ;;
        --mode)
            VALIDATE_MODE="$2"
            shift 2
            ;;
        --metrics)
            JIT_METRICS="$2"
            shift 2
            ;;
        --allow-existing)
            ALLOW_EXISTING=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [ ! -x "$BIN_PATH" ]; then
    echo "Missing 86Box binary: $BIN_PATH" >&2
    echo "Run: ./scripts/setup-and-build.sh build" >&2
    exit 1
fi

if [ ! -d "$VM_PATH" ]; then
    echo "Missing VM path: $VM_PATH" >&2
    exit 1
fi

if [ "$ALLOW_EXISTING" -eq 0 ]; then
    existing="$(pgrep -f "$BIN_PATH" || true)"
    if [ -n "$existing" ]; then
        echo "86Box already running from this checkout: $existing" >&2
        echo "Quit it first, or pass --allow-existing." >&2
        exit 1
    fi
fi

mkdir -p "$LOG_DIR"
if [ -z "$LOG_PATH" ]; then
    LOG_PATH="$LOG_DIR/voodoo-validate-$(date +%Y%m%d-%H%M%S).log"
fi
: > "$LOG_PATH"

launchctl setenv VOODOO_VALIDATE "$VALIDATE_MODE"
launchctl setenv VOODOO_VALIDATE_LIMIT "$VALIDATE_LIMIT"
launchctl setenv VOODOO_VALIDATE_LOG_LIMIT "$VALIDATE_LOG_LIMIT"
launchctl setenv VOODOO_ARM64_JIT_METRICS "$JIT_METRICS"

open -n "$APP_PATH" --args -L "$LOG_PATH" --vmpath "$VM_PATH"

pid=""
for _ in $(seq 1 40); do
    pid="$(pgrep -n -f "$BIN_PATH.*-L $LOG_PATH" || true)"
    if [ -n "$pid" ]; then
        break
    fi
    sleep 0.25
done

launchctl unsetenv VOODOO_VALIDATE || true
launchctl unsetenv VOODOO_VALIDATE_LIMIT || true
launchctl unsetenv VOODOO_VALIDATE_LOG_LIMIT || true
launchctl unsetenv VOODOO_ARM64_JIT_METRICS || true

if [ -z "$pid" ]; then
    echo "Launch failed: no 86Box process found for log $LOG_PATH" >&2
    echo "Log file: $LOG_PATH" >&2
    exit 1
fi

sleep 1
if ! ps -p "$pid" >/dev/null 2>&1; then
    echo "Launch failed: 86Box exited. Log file: $LOG_PATH" >&2
    exit 1
fi

echo "PID: $pid"
echo "Log: $LOG_PATH"
echo "VOODOO_VALIDATE=$VALIDATE_MODE"
echo "VOODOO_VALIDATE_LIMIT=$VALIDATE_LIMIT"
echo "VOODOO_VALIDATE_LOG_LIMIT=$VALIDATE_LOG_LIMIT"
echo "VOODOO_ARM64_JIT_METRICS=$JIT_METRICS"
