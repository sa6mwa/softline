#!/usr/bin/env bash
set -euo pipefail
root=$1
cmake=$2
nm=$3
target=$4
toolchain=$5
compiler=$6
linker=$7
mkdir -p "$root/build"
work=$(mktemp -d "$root/build/export-reconfigure.XXXXXX")
trap 'rm -rf "$work"' EXIT
mkdir "$work/source"
cp "$root/CMakeLists.txt" "$work/source/"
cp -r "$root/"{src,include,cmake,scripts,vendor} "$work/source/"
case "$toolchain" in "$root/"*) toolchain="$work/source/${toolchain#$root/}" ;; esac
cat > "$work/launcher" <<'SH'
#!/bin/sh
printf 'compiler\n' >> "$COMPILER_CALLS"
exec "$@"
SH
chmod +x "$work/launcher"
export COMPILER_CALLS="$work/calls"
build="$work/source/build"
args=(-S "$work/source" -B "$build" -G Ninja "-DSL_TARGET_ID=$target"
  "-DCMAKE_C_COMPILER=$compiler" "-DCMAKE_LINKER=$linker" "-DCMAKE_TOOLCHAIN_FILE=$toolchain"
  -DCMAKE_LINK_DEPENDS_USE_LINKER=FALSE -DSL_BUILD_STATIC=OFF -DSL_BUILD_TESTS=OFF
  -DSL_BUILD_EXAMPLES=OFF -DSL_INSTALL=OFF "-DCMAKE_C_COMPILER_LAUNCHER=$work/launcher"
  "-DCMAKE_C_LINKER_LAUNCHER=$work/launcher")
"$cmake" "${args[@]}"
"$cmake" --build "$build" --parallel 4
symbol=sl_output_stream_write_quoted_prompt
map="$build/softline.exports.map"
library="$build/libsoftline.so"
grep -q "$symbol" "$map"
"$nm" -D --defined-only "$library" | grep "$symbol"
: > "$COMPILER_CALLS"
"$cmake" "${args[@]}"
"$cmake" --build "$build" --parallel 4
[[ ! -s "$COMPILER_CALLS" ]]
rm "$build/private-cares/softline_cares_namespace.h"
"$cmake" --build "$build" --parallel 4
[[ -f "$build/private-cares/softline_cares_namespace.h" ]]
sed -i "/^$symbol\$/d" "$work/source/cmake/softline.exports"
"$cmake" --build "$build" --parallel 1
if grep -q "$symbol" "$map"; then echo 'ERROR: map did not regenerate' >&2; exit 1; fi
if "$nm" -D --defined-only "$library" | grep "$symbol"; then echo 'ERROR: removed export is still public' >&2; exit 1; fi
echo 'No-op compiler/linker reuse, parallel generated-header ownership and export reconfiguration passed.'
