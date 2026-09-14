#!/bin/sh
set -eu

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
if [ ! -f "${ROOT_DIR}/build/local-lua/lua" ] || [ ! -x "${ROOT_DIR}/build/local-lua/lua" ]; then
  echo "ERROR: local Lua interpreter missing or not executable; run make lua-test first" >&2
  exit 1
fi
LUA_TREE="${ROOT_DIR}/build/luarocks"
SDK_DIR="${ROOT_DIR}/build/lua-sdk"
if [ -d "${SDK_DIR}/lib/pkgconfig" ]; then
  SDK_LIB_DIR="${SDK_DIR}/lib"
elif [ -d "${SDK_DIR}/lib64/pkgconfig" ]; then
  SDK_LIB_DIR="${SDK_DIR}/lib64"
else
  SDK_LIB_DIR="${SDK_DIR}/lib"
fi

LUA_ROCKS_ENV="$(luarocks --tree "${LUA_TREE}" path --bin)"
eval "${LUA_ROCKS_ENV}"

quote() {
  printf '%s' "$1" | sed "s/'/'\\\\''/g" | sed "s/^/'/;s/$/'/"
}

printf 'export LUA_PATH=%s\n' "$(quote "${LUA_PATH}")"
printf 'export LUA_CPATH=%s\n' "$(quote "${LUA_CPATH}")"
printf 'export PATH=%s\n' "$(quote "${ROOT_DIR}/build/local-lua:${PATH}")"
printf 'export PKG_CONFIG_PATH=%s\n' \
  "$(quote "${SDK_LIB_DIR}/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}")"
printf 'export SOFTLINE_DIR=%s\n' "$(quote "${SDK_DIR}")"
