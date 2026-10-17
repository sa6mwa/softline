#!/usr/bin/env bash
case ${BASH_SOURCE[0]} in
  */*) source "${BASH_SOURCE[0]%/*}/require-host-bash.sh" ;;
  *) source ./require-host-bash.sh ;;
esac || exit $?
set -euo pipefail
source "${BASH_SOURCE[0]%/*}/require-workspace.sh" || exit $?
ROOT_DIR=$(cd "${BASH_SOURCE[0]%/*}/.." && pwd)
component=${1:-}
preset=${2:-debug}
cd "$ROOT_DIR"
case "$component:$preset" in
  libmdf:debug) cmake --preset debug ;;
  lua:debug)
    cmake --preset "$preset"
    "$ROOT_DIR/scripts/build-local-lua.sh" "$ROOT_DIR/build/$preset"
    ;;
  *) echo 'ERROR: use DEPENDENCY=libmdf|lua PRESET=debug' >&2; exit 2 ;;
esac
