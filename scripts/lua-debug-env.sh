#!/bin/sh
set -eu

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
if [ ! -f "${ROOT_DIR}/build/local-lua-debug/lua" ] || [ ! -x "${ROOT_DIR}/build/local-lua-debug/lua" ]; then
  echo "ERROR: local Lua interpreter missing or not executable; run make lua-debug-test first" >&2
  exit 1
fi
LUA_TREE="${ROOT_DIR}/build/luarocks-debug"
DEBUG_DIR="${ROOT_DIR}/build/debug"

LUA_ROCKS_ENV="$(luarocks --tree "${LUA_TREE}" path --bin)"
eval "${LUA_ROCKS_ENV}"

quote() {
  printf '%s' "$1" | sed "s/'/'\\\\''/g" | sed "s/^/'/;s/$/'/"
}

printf 'export LUA_PATH=%s\n' "$(quote "${LUA_PATH}")"
printf 'export LUA_CPATH=%s\n' "$(quote "${LUA_CPATH}")"
printf 'export PATH=%s\n' "$(quote "${ROOT_DIR}/build/local-lua-debug:${PATH}")"
printf 'export SOFTLINE_INCLUDE_DIR=%s\n' "$(quote "${ROOT_DIR}/include")"
printf 'export SOFTLINE_LIB_DIR=%s\n' "$(quote "${DEBUG_DIR}")"
