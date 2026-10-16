#!/usr/bin/env bash
set -euo pipefail
root=$1
work=$(mktemp -d "$root/build/checksum-inventory.XXXXXX")
trap 'rm -rf -- "$work"' EXIT
mkdir -p "$work/scripts" "$work/dist" "$work/build/package-state"
cp "$root/scripts/"{package-checksums.sh,package-verify.sh,release_version.sh,require-host-bash.sh,require-workspace.sh,release-targets.sh} "$work/scripts/"
mkdir -p "$work/cmake"
cp "$root/cmake/softline_workspace.cmake" "$work/cmake/"
printf '9.8.7\n' > "$work/VERSION"
source "$work/scripts/release-targets.sh"
for target in $SOFTLINE_RELEASE_TARGETS; do printf 'SDK %s\n' "$target" > "$work/dist/softline-9.8.7-$target.tar.gz"; done
for name in softline-9.8.7.tar.gz softline-lua-9.8.7.tar.gz softline-9.8.7-1.rockspec softline-9.8.7-1.src.rock; do
  printf '%s\n' "$name" > "$work/dist/$name"
done
unset SOFTLINE_DIST_DIR SL_VERSION_OVERRIDE SL_VERSION_ROOT
"$work/scripts/package-checksums.sh"
(cd "$work/dist"; sha256sum -c softline-9.8.7-CHECKSUMS)
[[ $(wc -l < "$work/dist/softline-9.8.7-CHECKSUMS") == 11 ]]
reject() {
  if "$work/scripts/package-checksums.sh" "$@" > "$work/error" 2>&1; then echo 'Incomplete/invalid inventory accepted' >&2; exit 1; fi
}
mv "$work/dist/softline-9.8.7-1.src.rock" "$work/saved"
reject
grep -q 'missing or unsafe required artifact' "$work/error"
if "$work/scripts/package-verify.sh" > "$work/error" 2>&1; then echo 'Verifier accepted a missing required payload' >&2; exit 1; fi
grep -q 'missing or unsafe required release payload' "$work/error"
mv "$work/saved" "$work/dist/softline-9.8.7-1.src.rock"
printf stale > "$work/dist/softline-9.8.6-x86_64-linux-gnu.tar.gz"
reject
grep -q 'stale or unlisted' "$work/error"
printf 'x86_64-linux-gnu\n' > "$work/build/package-state/targets"
"$work/scripts/package-checksums.sh" binary
[[ $(wc -l < "$work/build/package-state/softline-9.8.7-BINARY-CHECKSUMS") == 1 ]]
[[ -f "$work/dist/softline-9.8.6-x86_64-linux-gnu.tar.gz" ]]
printf 'x86_64-linux-gnu\nx86_64-linux-gnu\n' > "$work/build/package-state/targets"
reject binary
grep -q 'duplicate package target' "$work/error"
mkdir "$work/empty-dist"
if SOFTLINE_DIST_DIR="$work/empty-dist" "$work/scripts/package-verify.sh" > "$work/error" 2>&1; then
  echo 'Empty required verification succeeded' >&2; exit 1
fi
echo 'Complete inventory rejects missing/stale payloads; scoped binary inventory preserves unrelated artifacts.'
