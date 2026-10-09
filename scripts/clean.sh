#!/usr/bin/env bash
case ${BASH_SOURCE[0]} in
  */*) source "${BASH_SOURCE[0]%/*}/require-host-bash.sh" ;;
  *) source ./require-host-bash.sh ;;
esac || exit $?
set -euo pipefail
ROOT_DIR=$(cd "${BASH_SOURCE[0]%/*}/.." && pwd)
rm -rf -- "$ROOT_DIR/build" "$ROOT_DIR/dist" "$ROOT_DIR/.cache"
echo 'Clean complete; shared archive/toolchain caches preserved.'
