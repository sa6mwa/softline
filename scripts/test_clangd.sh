#!/usr/bin/env bash
case ${BASH_SOURCE[0]} in
  */*) source "${BASH_SOURCE[0]%/*}/require-host-bash.sh" ;;
  *) source ./require-host-bash.sh ;;
esac || exit $?
set -euo pipefail
source "${BASH_SOURCE[0]%/*}/require-workspace.sh" || exit $?

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "${ROOT_DIR}/build"
LOG_FILE="$(mktemp "${ROOT_DIR}/build/clangd.XXXXXX")"
trap 'rm -f "${LOG_FILE}"' EXIT

if ! command -v clangd >/dev/null 2>&1; then
  echo "ERROR: clangd is required for the selected editor verification gate" >&2
  exit 1
fi

if [ ! -f "${ROOT_DIR}/.clangd" ]; then
  echo "ERROR: missing .clangd" >&2
  exit 1
fi

if [ ! -f "${ROOT_DIR}/build/debug/compile_commands.json" ]; then
  echo "ERROR: missing build/debug/compile_commands.json; run make deps-debug" >&2
  exit 1
fi

set +e
# Parse and index the complete translation unit. Per-token editor refactoring
# self-tests belong to clangd; their failures must never mask semantic errors.
clangd --check="${ROOT_DIR}/src/softline.c" --check-locations=false >"${LOG_FILE}" 2>&1
status=$?
set -e

if ! grep -F "Loading config file at ${ROOT_DIR}/.clangd" "${LOG_FILE}" >/dev/null; then
  echo "ERROR: clangd did not load project .clangd" >&2
  cat "${LOG_FILE}" >&2
  exit 1
fi

if ! grep -F "Loaded compilation database from ${ROOT_DIR}/build/debug/compile_commands.json" "${LOG_FILE}" >/dev/null; then
  echo "ERROR: clangd did not load build/debug compile_commands.json" >&2
  cat "${LOG_FILE}" >&2
  exit 1
fi

if [ "${status}" -ne 0 ]; then
  echo "ERROR: clangd --check failed" >&2
  cat "${LOG_FILE}" >&2
  exit "${status}"
fi

echo "clangd checks passed."
