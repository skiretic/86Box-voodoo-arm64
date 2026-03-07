#!/bin/bash
# Launch 86Box with the Windows 98 Low End test VM.
#
# Use LaunchServices (`open -n -a ... --args`) rather than invoking the Mach-O
# directly from automation. In this workspace that is the reliable way to start
# the visible GUI instance with a VM path, and it also preserves optional
# logfile/stats arguments.
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
APP_BUNDLE="$REPO_ROOT/build/src/86Box.app"
VM_PATH="$HOME/Library/Application Support/86Box/Virtual Machines/Windows 98 Low End copy"
LOG_PATH="${86BOX_LOG_PATH:-}"
ENABLE_STATS="${86BOX_NEW_DYNAREC_STATS_ENABLE:-0}"

if [ ! -d "$VM_PATH" ]; then
    echo "Error: VM not found at $VM_PATH"
    exit 1
fi

if [ ! -d "$APP_BUNDLE" ]; then
    echo "Error: 86Box not built. Run ./scripts/clean-build-and-sign.sh first"
    exit 1
fi

echo "Launching 86Box with VM: Windows 98 Low End copy"

if [ -n "$LOG_PATH" ]; then
    echo "Logging to: $LOG_PATH"
fi

if [ "$ENABLE_STATS" = "1" ]; then
    echo "CPU dynarec stats logging: enabled"
    if [ -n "$LOG_PATH" ]; then
        env 86BOX_NEW_DYNAREC_STATS=1 open -n -a "$APP_BUNDLE" --args -L "$LOG_PATH" -P "$VM_PATH"
    else
        env 86BOX_NEW_DYNAREC_STATS=1 open -n -a "$APP_BUNDLE" --args -P "$VM_PATH"
    fi
else
    if [ -n "$LOG_PATH" ]; then
        open -n -a "$APP_BUNDLE" --args -L "$LOG_PATH" -P "$VM_PATH"
    else
        open -n -a "$APP_BUNDLE" --args -P "$VM_PATH"
    fi
fi
