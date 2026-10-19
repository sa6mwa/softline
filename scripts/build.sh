#!/usr/bin/env bash
case ${BASH_SOURCE[0]} in
  */*) source "${BASH_SOURCE[0]%/*}/require-host-bash.sh" ;;
  *) source ./require-host-bash.sh ;;
esac || exit $?
set -euo pipefail
source "${BASH_SOURCE[0]%/*}/require-workspace.sh" || exit $?
ROOT_DIR=$(cd "${BASH_SOURCE[0]%/*}/.." && pwd)
mode=${1:-build}
preset=${2:-debug}
if (($#)); then shift; fi
if (($#)); then shift; fi
case "$mode" in build|test) ;; *) echo 'Usage: build.sh [build|test] [preset] [CTest options]' >&2; exit 2 ;; esac
cd "$ROOT_DIR"
cmake --preset "$preset"
cmake --build --preset "$preset"
if [[ "$mode" == test ]]; then
  exec ctest --preset "$preset" --stop-on-failure --output-on-failure --no-tests=error "$@"
fi
