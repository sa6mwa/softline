#!/usr/bin/env bash
set -euo pipefail
root=$1
# This fixture supplies its own source version and artifact directory.
unset SL_VERSION_OVERRIDE SL_VERSION_ROOT SOFTLINE_DIST_DIR
real_cmake=$(command -v cmake)
mkdir -p "$root/build"
work=$(mktemp -d "$root/build/source-workspace.XXXXXX")
trap 'rm -rf "$work"' EXIT
fixture="$work/source root"
mkdir -p "$fixture/scripts" "$fixture/dist" "$work/bin" "$work/outside" "$work/unused"
cp "$root/scripts/"{package-source-smoke.sh,release_version.sh,require-host-bash.sh,require-workspace.sh,extract-release-source.sh} "$fixture/scripts/"
mkdir -p "$fixture/cmake"
cp "$root/cmake/softline_workspace.cmake" "$fixture/cmake/"
printf '0.0.0\n' > "$fixture/VERSION"
mkdir -p "$work/stage/softline-0.0.0"
cp "$fixture/VERSION" "$work/stage/softline-0.0.0/"
printf 'RELEASE_MANIFEST\nVERSION\n' > "$work/stage/softline-0.0.0/RELEASE_MANIFEST"
tar -czf "$fixture/dist/softline-0.0.0.tar.gz" -C "$work/stage" softline-0.0.0
cat > "$work/bin/cmake" <<'SH'
#!/bin/bash
set -eu
if [[ "$1" == -DSOFTLINE_WORKSPACE_ROOT=* ]]; then exec "$REAL_CMAKE" "$@"; fi
printf 'cmake\n' >> "$CALL_LOG"
if [[ "$1" == -S ]]; then
  [[ "$2" == "$FIXTURE_ROOT/build/"* ]]
  [[ "$3" == -B ]]
  mkdir -p "$4"
fi
[[ "$FAIL_BUILD" == 0 ]] || exit 17
SH
cat > "$work/bin/ctest" <<'SH'
#!/bin/bash
set -eu
[[ "$1" == --test-dir && "$2" == "$FIXTURE_ROOT/build/"* ]]
printf 'ctest\n' >> "$CALL_LOG"
SH
chmod +x "$work/bin/"*
export PATH="$work/bin:$PATH" FIXTURE_ROOT="$fixture" CALL_LOG="$work/calls" TMPDIR="$work/unused" REAL_CMAKE="$real_cmake"
cd "$work/outside"
for area in build .cache; do
  for kind in DEPENDENCY TOOLCHAIN; do
    : > "$CALL_LOG"
    if env FAIL_BUILD=0 "CPKT_${kind}_CACHE=$fixture/$area/cache" bash "$fixture/scripts/package-source-smoke.sh" > "$work/out" 2>&1; then
      echo "Source smoke accepted a checkout-local $kind cache" >&2; exit 1
    fi
    grep -q 'survive make clean' "$work/out"
    [[ ! -s "$CALL_LOG" && ! -d "$fixture/build" ]]
  done
done
for mode in success failure; do
  : > "$CALL_LOG"
  export FAIL_BUILD=0
  if [[ "$mode" == failure ]]; then FAIL_BUILD=1; fi
  if bash "$fixture/scripts/package-source-smoke.sh" > "$work/out" 2>&1; then
    [[ "$mode" == success ]]
    printf 'cmake\ncmake\nctest\n' > "$work/expected"
  else
    [[ "$mode" == failure ]] || { cat "$work/out"; exit 1; }
    printf 'cmake\n' > "$work/expected"
  fi
  cmp "$CALL_LOG" "$work/expected"
  [[ -z $(find "$fixture/build" -mindepth 1 -print -quit) ]]
  [[ -z $(find "$work/unused" "$work/outside" -mindepth 1 -print -quit) ]]
done
echo 'Source smoke owns build scratch and cleans normal success/failure.'
