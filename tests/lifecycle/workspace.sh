#!/usr/bin/env bash
set -euo pipefail
root=$1
work=$(mktemp -d "$root/build/workspace.XXXXXX")
trap 'rm -rf -- "$work"' EXIT
fixture="$work/source with spaces"
mkdir -p "$fixture/scripts" "$fixture/cmake" "$fixture/build" "$fixture/dist" "$fixture/.cache" "$work/borrowed"
cp "$root/scripts/"{require-workspace.sh,clean.sh,require-host-bash.sh,stage_lua_rock_sources.sh,release_version.sh} "$fixture/scripts/"
cp "$root/cmake/softline_workspace.cmake" "$fixture/cmake/"
printf keep > "$work/borrowed/sentinel"
export XDG_CACHE_HOME="$work/global"
unset CPKT_TOOLCHAIN_CACHE CPKT_DEPENDENCY_CACHE
check() { cmake "-DSOFTLINE_WORKSPACE_ROOT=$fixture" -P "$fixture/cmake/softline_workspace.cmake"; }
reject() {
  if "$@" > "$work/error" 2>&1; then echo 'Borrowed workspace/cache accepted' >&2; exit 1; fi
  [[ $(cat "$work/borrowed/sentinel") == keep ]]
}
check
CPKT_DEPENDENCY_CACHE="$fixture/build/archives" check
CPKT_TOOLCHAIN_CACHE="$fixture/.cache/toolchains" check
CPKT_DEPENDENCY_CACHE="$XDG_CACHE_HOME/cpkt/deps" check
CPKT_TOOLCHAIN_CACHE="$work/borrowed" reject check
CPKT_DEPENDENCY_CACHE="$work/borrowed" reject check
ln -s "$work/borrowed" "$fixture/build/link"
CPKT_DEPENDENCY_CACHE="$fixture/build/link/cache" reject check
reject "$fixture/scripts/stage_lua_rock_sources.sh" "$work/borrowed"
reject "$fixture/scripts/stage_lua_rock_sources.sh" "$fixture/build/link/stage"
[[ ! -e "$work/borrowed/stage" ]]
for area in build dist .cache; do
  mv "$fixture/$area" "$fixture/saved"
  ln -s "$work/borrowed" "$fixture/$area"
  reject check
  rm "$fixture/$area"
  mv "$fixture/saved" "$fixture/$area"
done
# Every direct configuration entrypoint must reject the same borrowed cache
# before invoking its own generators or compiler checks.
for lane in . cmake/host-tests cmake/lua-tests cmake/retained-resize-tests cmake/local-lua cmake/terminal-tests; do
  mkdir -p "$fixture/$lane"
  cp "$root/$lane/CMakeLists.txt" "$fixture/$lane/"
  CPKT_DEPENDENCY_CACHE="$work/borrowed" reject cmake -S "$fixture/$lane" -B "$fixture/build/configure-${lane//\//-}"
  grep -q 'CPKT_DEPENDENCY_CACHE must use' "$work/error"
done
# Full clean removes hidden local state and preserves the shared global cache.
mkdir -p "$XDG_CACHE_HOME/cpkt/deps" "$XDG_CACHE_HOME/cpkt/toolchains"
printf keep > "$XDG_CACHE_HOME/cpkt/deps/sentinel"
printf keep > "$XDG_CACHE_HOME/cpkt/toolchains/sentinel"
for area in build dist .cache; do printf local > "$fixture/$area/.hidden"; done
"$fixture/scripts/clean.sh" dist
[[ ! -e "$fixture/dist" && -e "$fixture/build/.hidden" && -e "$fixture/.cache/.hidden" ]]
"$fixture/scripts/clean.sh"
[[ ! -e "$fixture/build" && ! -e "$fixture/dist" && ! -e "$fixture/.cache" ]]
[[ $(cat "$XDG_CACHE_HOME/cpkt/deps/sentinel") == keep && $(cat "$XDG_CACHE_HOME/cpkt/toolchains/sentinel") == keep ]]
echo 'Owned workspace/cache checks and scoped/full cleanup passed.'
