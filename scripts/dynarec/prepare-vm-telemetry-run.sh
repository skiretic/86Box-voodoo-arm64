#!/bin/sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
ARTIFACT_ROOT="${ARTIFACT_ROOT:-${ROOT_DIR}/docs/perf-artifacts/arm64-dynarec}"
VM_PATH="${VM_PATH:-/Users/anthony/Library/Application Support/86Box/Virtual Machines/Windows 98 Gaming PC}"
VM_NAME="${VM_NAME:-Windows 98 Gaming PC}"
RUN_TAG="${RUN_TAG:-s03a-telemetry}"
STAMP="$(date +%Y-%m-%d_%H-%M-%S)"
RUN_DIR="${ARTIFACT_ROOT}/${STAMP}-${VM_NAME}-${RUN_TAG}"
LOGFILE="${RUN_DIR}/86box.log"

mkdir -p "${RUN_DIR}"

env_or_unset() {
    value="$(printenv "$1" 2>/dev/null || true)"
    if [ -n "$value" ]; then
        printf '%s' "$value"
    else
        printf 'unset'
    fi
}

# Run metadata kept next to logfile so telemetry folders stay self-describing.
cat > "${RUN_DIR}/run-metadata.txt" <<EOF
branch=$(git -C "${ROOT_DIR}" rev-parse --abbrev-ref HEAD 2>/dev/null || printf unknown)
commit=$(git -C "${ROOT_DIR}" rev-parse HEAD 2>/dev/null || printf unknown)
build_binary=${ROOT_DIR}/build/src/86Box.app/Contents/MacOS/86Box
vm_name=${VM_NAME}
vm_path=${VM_PATH}
artifact_root=${ARTIFACT_ROOT}
run_tag=${RUN_TAG}
timestamp=${STAMP}
host_arch=$(uname -m)
repo_root=${ROOT_DIR}
env_86BOX_NEW_DYNAREC_STATS=$(env_or_unset 86BOX_NEW_DYNAREC_STATS)
env_86BOX_NEW_DYNAREC_TELEMETRY=$(env_or_unset 86BOX_NEW_DYNAREC_TELEMETRY)
env_86BOX_A013_TRACE=$(env_or_unset 86BOX_A013_TRACE)
env_86BOX_A013_TRACE_EFFECTIVE=${86BOX_A013_TRACE:-0}
env_86BOX_NEW_DYNAREC_IN_SCALAR=$(env_or_unset 86BOX_NEW_DYNAREC_IN_SCALAR)
EOF

# Output argv list one item per line so launcher can parse reliably.
printf '%s\n' "${ROOT_DIR}/build/src/86Box.app/Contents/MacOS/86Box" \
    "-P" "${VM_PATH}" \
    "-V" "${VM_NAME}" \
    "-L" "${LOGFILE}"
