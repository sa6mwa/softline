#!/usr/bin/env bash
case ${BASH_SOURCE[0]} in
  */*) source "${BASH_SOURCE[0]%/*}/require-host-bash.sh" ;;
  *) source ./require-host-bash.sh ;;
esac || exit $?
set -euo pipefail
ROOT_DIR=$(cd "${BASH_SOURCE[0]%/*}/.." && pwd -P)
case "${1:-all}" in
  all) rm -rf -- "$ROOT_DIR/build" "$ROOT_DIR/dist" "$ROOT_DIR/.cache" ;;
  dist) rm -rf -- "$ROOT_DIR/dist" ;;
  *) echo 'Usage: clean.sh [all|dist]' >&2; exit 2 ;;
esac
echo 'Clean complete; shared archive/toolchain caches preserved.'
