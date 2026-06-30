#!/bin/sh
set -eu

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
LOG_FILE="$(mktemp)"
trap 'rm -f "${LOG_FILE}"' EXIT

if ! command -v clangd >/dev/null 2>&1; then
  echo "SKIP: clangd not found"
  exit 0
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
clangd --check="${ROOT_DIR}/src/softline.c" >"${LOG_FILE}" 2>&1
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

if grep -E "unknown type name|Unknown type name|Error reading .*\\.clang-format|Failed to build (preamble|AST)" "${LOG_FILE}" >/dev/null; then
  echo "ERROR: clangd reported semantic or configuration failures" >&2
  cat "${LOG_FILE}" >&2
  exit 1
fi

if [ "${status}" -ne 0 ]; then
  if grep -F "tweak: SwapBinaryOperands ==> FAIL" "${LOG_FILE}" >/dev/null &&
     grep -F "All checks completed, " "${LOG_FILE}" >/dev/null; then
    echo "clangd semantic check passed; ignored clangd check-mode SwapBinaryOperands tweak failure."
    exit 0
  fi
  echo "ERROR: clangd --check failed" >&2
  cat "${LOG_FILE}" >&2
  exit "${status}"
fi

echo "clangd checks passed."
