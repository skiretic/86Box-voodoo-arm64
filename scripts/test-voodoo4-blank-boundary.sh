#!/bin/bash

set -euo pipefail

APP="${APP:-build/src/86Box.app/Contents/MacOS/86Box}"
VM_PATH="${VM_PATH:-$HOME/Library/Application Support/86Box/Virtual Machines/v4}"
RUN_SECONDS="${RUN_SECONDS:-6}"
LOGFILE="${LOGFILE:-/tmp/voodoo4-blank-boundary.log}"

if [ ! -x "$APP" ]; then
    echo "Error: 86Box binary not found at $APP" >&2
    exit 1
fi

if [ ! -d "$VM_PATH" ]; then
    echo "Error: VM path not found at $VM_PATH" >&2
    exit 1
fi

rm -f "$LOGFILE"

"$APP" --vmpath "$VM_PATH" --logfile "$LOGFILE" >/dev/null 2>&1 &
EMU_PID=$!

cleanup() {
    if kill -0 "$EMU_PID" 2>/dev/null; then
        kill "$EMU_PID" 2>/dev/null || true
        wait "$EMU_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

for _ in $(seq "$RUN_SECONDS"); do
    sleep 1
    if [ -f "$LOGFILE" ] && rg -q "V4 trace: first PCI read 2f =" "$LOGFILE"; then
        break
    fi
    if ! kill -0 "$EMU_PID" 2>/dev/null; then
        break
    fi
done

if [ ! -f "$LOGFILE" ]; then
    echo "FAIL: logfile was not created at $LOGFILE" >&2
    exit 1
fi

if ! rg -q "V4 trace: first PCI read 2c =" "$LOGFILE"; then
    echo "FAIL: V4 trace did not reach subsystem-ID reads" >&2
    exit 1
fi

if rg -q "V4 trace: first PCI read 2c = 00" "$LOGFILE" \
    && rg -q "V4 trace: first PCI read 2d = 00" "$LOGFILE" \
    && rg -q "V4 trace: first PCI read 2e = 00" "$LOGFILE" \
    && rg -q "V4 trace: first PCI read 2f = 00" "$LOGFILE"; then
    echo "FAIL: Voodoo4 exposed an all-zero subsystem ID block at PCI 0x2c-0x2f" >&2
    exit 1
fi

echo "PASS: Voodoo4 exposed a nonzero subsystem ID block at PCI 0x2c-0x2f"

if rg -q "V4 trace: first ext" "$LOGFILE"; then
    echo "INFO: early ext-register traffic was observed"
else
    echo "INFO: no early ext-register traffic observed yet"
fi
