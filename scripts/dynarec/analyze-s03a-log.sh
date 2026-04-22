#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="${ROOT_DIR}/scripts/dynarec/.analyze-s03a-log.bin"
SRC="${ROOT_DIR}/scripts/dynarec/analyze-s03a-log.c"

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
  echo "usage: $0 <current-log> [baseline-log]" >&2
  exit 2
fi

TMP_FILES=""
cleanup() {
  if [ -n "${TMP_FILES}" ]; then
    rm -f ${TMP_FILES}
  fi
}
trap cleanup EXIT

prepare_log() {
  local src="$1"
  local tmp

  case "${src}" in
    *.gz)
      tmp="$(mktemp /tmp/analyze-s03a-log.XXXXXX.log)"
      gzip -dc -- "${src}" > "${tmp}"
      TMP_FILES="${TMP_FILES} ${tmp}"
      printf '%s\n' "${tmp}"
      ;;
    *)
      printf '%s\n' "${src}"
      ;;
  esac
}

CUR_LOG="$(prepare_log "$1")"
BASE_LOG=""
if [ "$#" -eq 2 ]; then
  BASE_LOG="$(prepare_log "$2")"
fi

cc -O2 -std=c11 -Wall -Wextra -Werror -o "${BIN}" "${SRC}"
if [ -n "${BASE_LOG}" ]; then
  "${BIN}" "${CUR_LOG}" "${BASE_LOG}"
else
  "${BIN}" "${CUR_LOG}"
fi
