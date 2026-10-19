#!/usr/bin/env bash
set -euo pipefail
root=$1
args=()
if [[ $(uname -s) != Darwin ]]; then
  description=$("$root/scripts/cpkt-toolchains.sh" discover arm64-apple-darwin)
  if ! grep -qx status=ready <<< "$description"; then
    echo 'SKIP: optional local Lua Darwin configuration requires prepared osxcross'; exit 0
  fi
  args=("-DCMAKE_TOOLCHAIN_FILE=$root/cmake/toolchains/osxcross-darwin.cmake")
fi
mkdir -p "$root/build"
work=$(mktemp -d "$root/build/lua-platform.XXXXXX")
trap 'rm -rf "$work"' EXIT
cmake -S "$root/cmake/local-lua" -B "$work" -G Ninja -DSL_TARGET_ID=arm64-apple-darwin "${args[@]}"
cmake --build "$work"
grep -q LUA_USE_MACOSX "$work/build.ninja"
if grep -qE 'LUA_USE_LINUX|--export-dynamic|--dynamic-linker| -ldl' "$work/build.ninja"; then
  echo 'ERROR: Linux Lua platform flags entered Darwin configuration' >&2; exit 1
fi
[[ -f "$work/lua" ]]
if [[ $(uname -s) == Darwin ]]; then "$work/lua" -e 'assert(io.popen("echo ok"):read("l") == "ok")'; fi
echo 'Darwin local Lua build/platform selection passed.'
