#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="${ROOT_DIR}/scripts/dynarec/.analyze-s03a-log.bin"
SRC="${ROOT_DIR}/scripts/dynarec/analyze-s03a-log.c"
S_ONLY=0

if [ "${1:-}" = "--s-only" ]; then
  # S-lane prep/readability mode: keep S03 sections and drop A013 sections.
  S_ONLY=1
  shift
fi

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
  echo "usage: $0 [--s-only] <current-log> [baseline-log]" >&2
  exit 2
fi

TMP_FILES=""
cleanup() {
  if [ -n "${TMP_FILES}" ]; then
    # shellcheck disable=SC2086
    rm -f ${TMP_FILES}
  fi
}
trap cleanup EXIT

prepare_log() {
  local src="$1"
  local tmp

  case "${src}" in
    *.gz)
      tmp="$(mktemp -t analyze-s03a-log.XXXXXX)"
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
  if [ "${S_ONLY}" -eq 1 ]; then
    "${BIN}" "${CUR_LOG}" "${BASE_LOG}" | awk '
      /^S03_/ { print; in_s=1; next }
      /^A013_/ { in_s=0; next }
      /^  / && in_s { print; next }
      { in_s=0 }
    '
  else
    "${BIN}" "${CUR_LOG}" "${BASE_LOG}"
  fi
else
  if [ "${S_ONLY}" -eq 1 ]; then
    "${BIN}" "${CUR_LOG}" | awk '
      /^S03_/ { print; in_s=1; next }
      /^A013_/ { in_s=0; next }
      /^  / && in_s { print; next }
      { in_s=0 }
    '
  else
    "${BIN}" "${CUR_LOG}"
  fi
fi
