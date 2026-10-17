#!/usr/bin/env bash
set -euo pipefail
root=$1
mkdir -p "$root/build"
work=$(mktemp -d "$root/build/lua-env.XXXXXX")
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/source with spaces/scripts" "$work/bin"
fixture="$work/source with spaces"
cp "$root/scripts/"{require-host-bash.sh,require-workspace.sh} "$fixture/scripts/"
mkdir -p "$fixture/cmake"
cp "$root/cmake/softline_workspace.cmake" "$fixture/cmake/"
cat > "$work/bin/luarocks" <<'SH'
#!/bin/sh
printf '%s\n' 'export LUA_PATH=fixture' 'export LUA_CPATH=fixture'
SH
chmod +x "$work/bin/luarocks"
export PATH="$work/bin:$PATH"
for mode in lua lua-debug; do
  cp "$root/scripts/$mode-env.sh" "$fixture/scripts/"
  if [[ "$mode" == lua ]]; then directory=local-lua; else directory=local-lua-debug; fi
  binary="$fixture/build/$directory/lua"
  mkdir -p "${binary%/*}"
  for state in missing nonexecutable directory broken; do
    case "$state" in
      nonexecutable) printf 'not executable\n' > "$binary" ;;
      directory) mkdir "$binary" ;;
      broken) ln -s missing "$binary" ;;
    esac
    if "$fixture/scripts/$mode-env.sh" > "$work/out" 2> "$work/err"; then
      echo "ERROR: $mode accepted $state interpreter" >&2; exit 1
    fi
    [[ ! -s "$work/out" ]]
    grep -q 'missing or not executable' "$work/err"
    if [[ -d "$binary" ]]; then rmdir "$binary"; else rm -f "$binary"; fi
  done
  printf '#!/bin/sh\nexit 0\n' > "$binary"
  chmod +x "$binary"
  exports=$("$fixture/scripts/$mode-env.sh")
  selected=$(bash -c 'eval "$1"; command -v lua' test "$exports")
  [[ "$selected" == "$binary" ]]
  mv "$binary" "$work/borrowed-lua"
  ln -s "$work/borrowed-lua" "$binary"
  if "$fixture/scripts/$mode-env.sh" > "$work/out" 2> "$work/err"; then
    echo "ERROR: $mode accepted another workspace's interpreter" >&2; exit 1
  fi
  [[ ! -s "$work/out" ]]
  grep -q 'Generated path must stay below this checkout' "$work/err"
  unlink "$binary"
  mv "$work/borrowed-lua" "$binary"
done
echo 'Lua environments reject missing/bad interpreters and select the local runtime.'
