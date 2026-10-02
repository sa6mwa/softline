#!/bin/sh
# Isolate cached terminal libraries to this process; fixture children and host
# shell/build commands continue using their own interpreter and library paths.
set -eu
ROOT_DIR="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
install="${SOFTLINE_TERMINAL_TEST_ROOT:-${ROOT_DIR}/.cache/deps/x86_64-linux-gnu/terminal-tests/install}"
export SOFTLINE_TERMINAL_TEST_ROOT="${install}"
exec "${install}/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2" \
  --library-path "${install}/usr/lib/x86_64-linux-gnu:${install}/lib/x86_64-linux-gnu" "$@"
