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

cmake -S "${ROOT_DIR}" -B "${ROOT_DIR}/build/lua-sdk-build" \
  -DSL_BUILD_EXAMPLES=OFF \
  -DSL_BUILD_TESTS=OFF \
  -DSL_BUILD_STATIC=ON \
  -DSL_BUILD_SHARED=ON \
  -DSL_INSTALL=ON \
  -DCMAKE_INSTALL_PREFIX="${SDK_DIR}" \
  ${INSTALL_LIBDIR_ARG}
cmake --build "${ROOT_DIR}/build/lua-sdk-build"
rm -rf "${SDK_DIR}"
cmake --install "${ROOT_DIR}/build/lua-sdk-build" --prefix "${SDK_DIR}"

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
PKG_CONFIG_PATH="${SDK_LIB_DIR}/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}" \
  SOFTLINE_DIR="${SDK_DIR}" \
  luarocks --tree "${LUA_TREE}" make "${ROCKSPEC}"

eval "$("${ROOT_DIR}/scripts/lua-env.sh")"
lua -e 'local s=require("softline"); assert(s.new); print(_VERSION)'
lua "${ROOT_DIR}/tests/lua_smoke.lua"
printf 'hello\n' | lua "${ROOT_DIR}/tests/lua_readline.lua"
python3 "${ROOT_DIR}/tests/lua_chat_ctrl_c.py" "${ROOT_DIR}"

echo "Lua facade tests passed."
