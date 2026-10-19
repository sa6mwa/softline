#!/usr/bin/env bash
set -euo pipefail
root=$1
work=$(mktemp -d "$root/build/lua-sdk-contract.XXXXXX")
trap 'rm -rf -- "$work"' EXIT
fixture="$work/source with spaces"
sdk="$work/selected SDK"
mkdir -p "$fixture/scripts" "$fixture/lua" "$sdk/include" "$sdk/lib/pkgconfig" "$work/lua"
cp "$root/scripts/"{build_lua_rock.sh,require-host-bash.sh} "$fixture/scripts/"
printf '#define LUA_VERSION_NUM 505\n' > "$work/lua/lua.h"
printf 'int fixture;\n' > "$fixture/lua/softline_lua.c"
cat > "$work/compiler" <<'SH'
#!/usr/bin/env bash
printf '%s\n' "$@" >> "$COMPILER_ARGS"
SH
chmod +x "$work/compiler"
printf 'prefix=${pcfiledir}/../..\nincludedir=${prefix}/include\nlibdir=${prefix}/lib\nName: softline\nDescription: fixture\nVersion: 0.0.0\nLibs: -L${libdir} -lsoftline\nCflags: -I${includedir}\n' > "$sdk/lib/pkgconfig/softline.pc"
export PKG_CONFIG_PATH="$sdk/lib/pkgconfig" PKG_CONFIG_LIBDIR="$sdk/lib/pkgconfig" COMPILER_ARGS="$work/args"
unset SOFTLINE_LUA_CC SOFTLINE_LUA_INCDIR SOFTLINE_DIR SOFTLINE_INCLUDE_DIR SOFTLINE_LIB_DIR
for selection in directories prefix pkgconfig; do
  unset SOFTLINE_DIR SOFTLINE_INCLUDE_DIR SOFTLINE_LIB_DIR
  case "$selection" in
    directories) export SOFTLINE_INCLUDE_DIR="$sdk/include" SOFTLINE_LIB_DIR="$sdk/lib" ;;
    prefix) export SOFTLINE_DIR="$sdk" ;;
  esac
  for mode in static shared both; do
    for file in "$sdk/lib/libsoftline.a" "$sdk/lib/libsoftline.so"; do [[ ! -e "$file" ]] || unlink "$file"; done
    [[ "$mode" == shared ]] || printf static > "$sdk/lib/libsoftline.a"
    [[ "$mode" == static ]] || printf shared > "$sdk/lib/libsoftline.so"
    : > "$COMPILER_ARGS"
    if "$fixture/scripts/build_lua_rock.sh" "$work/compiler" '-fPIC' '-shared' o so "$work/lua" > "$work/log" 2>&1; then
      [[ "$mode" != static ]] || { echo 'Lua facade accepted a static-only SDK' >&2; exit 1; }
      if [[ "$selection" == pkgconfig ]]; then
        grep -Fxq -- "-I$sdk/lib/pkgconfig/../../include" "$COMPILER_ARGS"
        grep -Fxq -- "$sdk/lib/pkgconfig/../../lib/libsoftline.so" "$COMPILER_ARGS"
      else
        grep -Fxq -- "-I$sdk/include" "$COMPILER_ARGS"
        grep -Fxq -- "$sdk/lib/libsoftline.so" "$COMPILER_ARGS"
      fi
    else
      [[ "$mode" == static ]] || { cat "$work/log"; exit 1; }
      grep -q 'public shared libsoftline' "$work/log"
      [[ ! -s "$COMPILER_ARGS" ]]
    fi
  done
done
echo 'Lua facade requires the selected public shared SDK and preserves path arguments.'
