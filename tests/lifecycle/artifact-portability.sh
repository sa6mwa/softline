#!/usr/bin/env bash
set -euo pipefail
root=$1
mkdir -p "$root/build"
work=$(mktemp -d "$root/build/artifact-portable.XXXXXX")
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/scripts" "$work/stage/sdk" "$work/build/x86_64-linux-gnu-release"
cp "$root/scripts/"{verify-artifact-runtime.sh,require-host-bash.sh} "$work/scripts/"
# No producing configuration or resolver exists in this source-only fixture.
printf '%s\n' 'cc=$cache/roots/x86-64--glibc--stable-2026.08-1/bin/x86_64-linux-gcc' > "$work/stage/sdk/payload"
tar -czf "$work/fixture.tar.gz" -C "$work/stage" sdk
"$work/scripts/verify-artifact-runtime.sh" "$work/fixture.tar.gz"
printf '%s' "$HOME" > "$work/stage/sdk/payload"
tar -czf "$work/fixture.tar.gz" -C "$work/stage" sdk
if "$work/scripts/verify-artifact-runtime.sh" "$work/fixture.tar.gz" > "$work/log" 2>&1; then
  echo 'ERROR: actual private source path accepted' >&2; exit 1
fi
grep -q 'forbidden local/cache path' "$work/log"
printf '\177ELFbroken' > "$work/stage/sdk/payload"
tar -czf "$work/fixture.tar.gz" -C "$work/stage" sdk
if "$work/scripts/verify-artifact-runtime.sh" "$work/fixture.tar.gz" > "$work/log" 2>&1; then
  echo 'ERROR: ELF without a producing inspector was accepted' >&2; exit 1
fi
grep -q 'readelf required' "$work/log"
printf 'CMAKE_READELF:FILEPATH=%s/missing-readelf\n' "$work" > "$work/build/x86_64-linux-gnu-release/CMakeCache.txt"
if "$work/scripts/verify-artifact-runtime.sh" "$work/fixture.tar.gz" > "$work/log" 2>&1; then
  echo 'ERROR: unavailable configured inspector was accepted' >&2; exit 1
fi
grep -q 'ELF inspection failed' "$work/log"
printf ordinary > "$work/stage/sdk/payload"
tar -czf "$work/fixture.tar.gz" -C "$work/stage" sdk
"$work/scripts/verify-artifact-runtime.sh" "$work/fixture.tar.gz"
echo 'Source-only scans do not require ELF tooling; encountered ELF fails without its inspector.'
