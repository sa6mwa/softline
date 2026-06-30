#!/bin/sh
set -eu

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
LUA_TREE="${ROOT_DIR}/build/luarocks"
SDK_DIR="${ROOT_DIR}/build/lua-sdk"
if [ -d "${SDK_DIR}/lib/pkgconfig" ]; then
  SDK_LIB_DIR="${SDK_DIR}/lib"
elif [ -d "${SDK_DIR}/lib64/pkgconfig" ]; then
  SDK_LIB_DIR="${SDK_DIR}/lib64"
else
  SDK_LIB_DIR="${SDK_DIR}/lib"
fi

eval "$(luarocks --tree "${LUA_TREE}" path --bin)"

quote() {
  printf '%s' "$1" | sed "s/'/'\\\\''/g" | sed "s/^/'/;s/$/'/"
}

printf 'export LUA_PATH=%s\n' "$(quote "${LUA_PATH}")"
printf 'export LUA_CPATH=%s\n' "$(quote "${LUA_CPATH}")"
printf 'export PATH=%s\n' "$(quote "${PATH}")"
printf 'export PKG_CONFIG_PATH=%s\n' \
  "$(quote "${SDK_LIB_DIR}/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}")"
printf 'export LD_LIBRARY_PATH=%s\n' \
  "$(quote "${SDK_LIB_DIR}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}")"
printf 'export SOFTLINE_DIR=%s\n' "$(quote "${SDK_DIR}")"
