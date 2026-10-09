#!/usr/bin/env bash
case ${BASH_SOURCE[0]} in
  */*) source "${BASH_SOURCE[0]%/*}/require-host-bash.sh" ;;
  *) source ./require-host-bash.sh ;;
esac || exit $?
set -euo pipefail
ROOT_DIR=$(cd "${BASH_SOURCE[0]%/*}/.." && pwd)
cmake -S "$ROOT_DIR/cmake/host-tests" -B "$ROOT_DIR/build/host-tests" -G Ninja
cmake --build "$ROOT_DIR/build/host-tests"
exec ctest --test-dir "$ROOT_DIR/build/host-tests" --stop-on-failure --output-on-failure --no-tests=error "$@"
