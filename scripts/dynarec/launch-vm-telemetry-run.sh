#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
RUN_TAG="${1:-s03a-telemetry}"
A013_TRACE_VALUE="$(printenv 86BOX_A013_TRACE 2>/dev/null || true)"

if [ -z "${A013_TRACE_VALUE}" ]; then
  A013_TRACE_VALUE=0
fi

cd "${ROOT_DIR}"

# Always launch signed repo build.
./scripts/build-and-sign.sh >/tmp/86box-build-sign.log 2>&1

CMD_LINES="$(RUN_TAG="${RUN_TAG}" ./scripts/dynarec/prepare-vm-telemetry-run.sh)"
BIN="$(printf '%s\n' "${CMD_LINES}" | sed -n '1p')"
VM_FLAG_1="$(printf '%s\n' "${CMD_LINES}" | sed -n '2p')"
VM_PATH="$(printf '%s\n' "${CMD_LINES}" | sed -n '3p')"
VM_FLAG_2="$(printf '%s\n' "${CMD_LINES}" | sed -n '4p')"
VM_NAME="$(printf '%s\n' "${CMD_LINES}" | sed -n '5p')"
LOG_FLAG="$(printf '%s\n' "${CMD_LINES}" | sed -n '6p')"
LOGFILE="$(printf '%s\n' "${CMD_LINES}" | sed -n '7p')"
RUN_DIR="$(dirname "${LOGFILE}")"
APP_PATH="$(dirname "$(dirname "$(dirname "${BIN}")")")"

/usr/bin/pkill -f "${ROOT_DIR}/build/src/86Box.app/Contents/MacOS/86Box" >/dev/null 2>&1 || true
/usr/bin/pkill -f '/Applications/86box/86Box.app/Contents/MacOS/86Box' >/dev/null 2>&1 || true

env \
  86BOX_NEW_DYNAREC_STATS=1 \
  86BOX_NEW_DYNAREC_TELEMETRY=1 \
  86BOX_A013_TRACE="${A013_TRACE_VALUE}" \
  open -n -a "${APP_PATH}" --args \
  "${VM_FLAG_1}" "${VM_PATH}" \
  "${VM_FLAG_2}" "${VM_NAME}" \
  "${LOG_FLAG}" "${LOGFILE}" \
  >/tmp/86box-launch-telemetry.log 2>&1

PID=""
for _ in 1 2 3 4 5 6 7 8 9 10; do
  PID="$(pgrep -f "${BIN}" | head -n1 || true)"
  if [ -n "${PID}" ]; then
    break
  fi
  sleep 1
done

if [ -z "${PID}" ]; then
  echo "launch_failed=1"
  echo "run_tag=${RUN_TAG}"
  echo "run_dir=${RUN_DIR}"
  echo "logfile=${LOGFILE}"
  echo "build_log=/tmp/86box-build-sign.log"
  echo "launch_log=/tmp/86box-launch-telemetry.log"
  echo "error=process_not_found"
  exit 1
fi

echo "launch_failed=0"
echo "run_tag=${RUN_TAG}"
echo "run_dir=${RUN_DIR}"
echo "logfile=${LOGFILE}"
echo "pid=${PID}"
