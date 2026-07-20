#!/bin/sh
set -eu

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
LUA_TREE="${ROOT_DIR}/build/luarocks"
SDK_DIR="${ROOT_DIR}/build/lua-sdk"
ROCKSPEC="${ROOT_DIR}/build/lua-rockspec/softline-$(sh "${ROOT_DIR}/scripts/release_version.sh")-1.rockspec"
INSTALL_LIBDIR="${SOFTLINE_LUA_INSTALL_LIBDIR:-}"
INSTALL_LIBDIR_ARG=""

if [ -n "${INSTALL_LIBDIR}" ]; then
  INSTALL_LIBDIR_ARG="-DCMAKE_INSTALL_LIBDIR=${INSTALL_LIBDIR}"
fi

(
  cd "${ROOT_DIR}"
  cmake --preset debug-lua ${INSTALL_LIBDIR_ARG}
  cmake --build --preset debug-lua
)
rm -rf "${SDK_DIR}"
cmake --install "${ROOT_DIR}/build/debug-lua" --prefix "${SDK_DIR}"

if [ -d "${SDK_DIR}/lib/pkgconfig" ]; then
  SDK_LIB_DIR="${SDK_DIR}/lib"
elif [ -d "${SDK_DIR}/lib64/pkgconfig" ]; then
  SDK_LIB_DIR="${SDK_DIR}/lib64"
else
  echo "ERROR: Lua SDK install missing lib/pkgconfig or lib64/pkgconfig" >&2
  exit 1
fi

"${ROOT_DIR}/scripts/render_lua_rockspec.sh" "${ROCKSPEC}" >/dev/null
rm -rf "${LUA_TREE}"
if NATIVE_TARGET="$("${ROOT_DIR}/scripts/cpkt-toolchains.sh" native-linux-target 2>/dev/null)"; then
  "${ROOT_DIR}/scripts/cpkt-toolchains.sh" ensure "${NATIVE_TARGET}" >/dev/null
  eval "$("${ROOT_DIR}/scripts/cpkt-toolchains.sh" env "${NATIVE_TARGET}")"
  SOFTLINE_LUA_CC="${CC}"
  export CC LD AR RANLIB SOFTLINE_LUA_CC
fi
(
  cd "${ROOT_DIR}"
  PKG_CONFIG_PATH="${SDK_LIB_DIR}/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}" \
    SOFTLINE_DIR="${SDK_DIR}" \
    luarocks --tree "${LUA_TREE}" make "${ROCKSPEC}"
)

eval "$("${ROOT_DIR}/scripts/lua-env.sh")"
lua -e 'local s=require("softline"); assert(s.new); print(_VERSION)'
lua "${ROOT_DIR}/tests/lua_smoke.lua"
printf 'hello\n' | lua "${ROOT_DIR}/tests/lua_readline.lua"
python3 "${ROOT_DIR}/tests/lua_chat_ctrl_c.py" "${ROOT_DIR}"

echo "Lua facade tests passed."
