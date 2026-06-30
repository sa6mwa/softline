#!/bin/sh
set -eu

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
LUA_TREE="${ROOT_DIR}/build/luarocks-debug"
DEBUG_DIR="${ROOT_DIR}/build/debug"

eval "$(luarocks --tree "${LUA_TREE}" path --bin)"

quote() {
  printf '%s' "$1" | sed "s/'/'\\\\''/g" | sed "s/^/'/;s/$/'/"
}

printf 'export LUA_PATH=%s\n' "$(quote "${LUA_PATH}")"
printf 'export LUA_CPATH=%s\n' "$(quote "${LUA_CPATH}")"
printf 'export PATH=%s\n' "$(quote "${PATH}")"
printf 'export SOFTLINE_INCLUDE_DIR=%s\n' "$(quote "${ROOT_DIR}/include")"
printf 'export SOFTLINE_LIB_DIR=%s\n' "$(quote "${DEBUG_DIR}")"
printf 'export LD_LIBRARY_PATH=%s\n' \
  "$(quote "${DEBUG_DIR}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}")"
