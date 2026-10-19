#!/usr/bin/env bash
case ${BASH_SOURCE[0]} in
  */*) source "${BASH_SOURCE[0]%/*}/require-host-bash.sh" ;;
  *) source ./require-host-bash.sh ;;
esac || exit $?
set -euo pipefail
ROOT_DIR=$(cd "${BASH_SOURCE[0]%/*}/.." && pwd)
mode=${1:-write}
case "$mode" in write|check) ;; *) echo 'Usage: format.sh [write|check]' >&2; exit 2 ;; esac
command -v clang-format >/dev/null || { echo 'ERROR: host clang-format is required' >&2; exit 1; }
args=(-i)
[[ "$mode" != check ]] || args=(--dry-run --Werror)
cd "$ROOT_DIR"
find include src tests examples lua \( -name '*.c' -o -name '*.h' \) \
  -exec clang-format "${args[@]}" -style=file --fallback-style=none {} +
