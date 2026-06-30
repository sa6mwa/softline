#!/bin/sh
set -eu

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
LUA_TREE="${ROOT_DIR}/build/luarocks-debug"
ROCKSPEC="${ROOT_DIR}/build/lua-debug-rockspec/softline-$(sh "${ROOT_DIR}/scripts/release_version.sh")-1.rockspec"
MODE="${1:-test}"

if [ "${MODE}" = "env" ]; then
  exec "${ROOT_DIR}/scripts/lua-debug-env.sh"
fi

cmake --preset debug
cmake --build --preset debug

"${ROOT_DIR}/scripts/render_lua_rockspec.sh" "${ROCKSPEC}" >/dev/null
rm -rf "${LUA_TREE}"
SOFTLINE_INCLUDE_DIR="${ROOT_DIR}/include" \
  SOFTLINE_LIB_DIR="${ROOT_DIR}/build/debug" \
  luarocks --tree "${LUA_TREE}" make "${ROCKSPEC}"

eval "$("${ROOT_DIR}/scripts/lua-debug-env.sh")"

case "${MODE}" in
  test)
    lua -e 'local s=require("softline"); assert(s.new); print(_VERSION)'
    lua "${ROOT_DIR}/tests/lua_smoke.lua"
    printf 'hello\n' | lua "${ROOT_DIR}/tests/lua_readline.lua"
    printf 'exit\n' | lua "${ROOT_DIR}/examples/simple.lua" >/dev/null
    printf 'exit\n' | lua "${ROOT_DIR}/examples/chat.lua" >/dev/null
    echo "Lua debug facade and examples passed."
    ;;
  simple)
    exec lua "${ROOT_DIR}/examples/simple.lua"
    ;;
  chat)
    exec lua "${ROOT_DIR}/examples/chat.lua"
    ;;
  *)
    echo "usage: $0 [env|test|simple|chat]" >&2
    exit 2
    ;;
esac
