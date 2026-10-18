#!/usr/bin/env bash
set -euo pipefail
root=$1
work=$(mktemp -d "$root/build/checksum-inventory.XXXXXX")
trap 'rm -rf -- "$work"' EXIT
mkdir -p "$work/scripts" "$work/dist" "$work/build/package-state"
cp "$root/scripts/"{package-checksums.sh,package-verify.sh,verify-artifact-runtime.sh,release_version.sh,require-host-bash.sh,require-workspace.sh,release-targets.sh} "$work/scripts/"
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
cp "$root/Makefile" "$work/"
cp "$work/dist/softline-9.8.7-CHECKSUMS" "$work/complete-manifest"
reject_manifest() {
  if SOFTLINE_CHECKSUM_FILE="$work/invalid-manifest" "$work/scripts/package-verify.sh" > "$work/error" 2>&1; then
    echo 'Invalid checksum inventory accepted' >&2; exit 1
  fi
  grep -Fq "$1" "$work/error" || { cat "$work/error"; exit 1; }
}
cp "$work/complete-manifest" "$work/invalid-manifest"
head -1 "$work/complete-manifest" >> "$work/invalid-manifest"
reject_manifest 'duplicate checksum artifact'
printf extra > "$work/dist/extra.txt"
cp "$work/complete-manifest" "$work/invalid-manifest"
(cd "$work/dist"; sha256sum extra.txt) >> "$work/invalid-manifest"
reject_manifest 'unexpected checksum artifact'
# A valid source-only scoped fixture reaches successful verification on the old
# scanner, so an unterminated extra/duplicate record must itself cause failure.
cp "$work/dist/softline-9.8.7.tar.gz" "$work/saved-source"
mkdir -p "$work/source-fixture/softline-9.8.7"
printf fixture > "$work/source-fixture/softline-9.8.7/README"
tar -czf "$work/dist/softline-9.8.7.tar.gz" -C "$work/source-fixture" softline-9.8.7
source_record=$(cd "$work/dist"; sha256sum softline-9.8.7.tar.gz)
extra_record=$(cd "$work/dist"; sha256sum extra.txt)
printf '%s' "$source_record" > "$work/invalid-manifest"
SOFTLINE_CHECKSUM_FILE="$work/invalid-manifest" "$work/scripts/package-verify.sh"
# Manifest and dist overrides retain their meaning across the verifier's cd.
verify_from() {
  (
    cd "$1"
    SOFTLINE_CHECKSUM_FILE="$2" SOFTLINE_DIST_DIR="$3" "$work/scripts/package-verify.sh"
  ) > "$work/path-verification.log" 2>&1 || { cat "$work/path-verification.log"; exit 1; }
  grep -q 'Checking softline-9.8.7' "$work/path-verification.log"
  grep -q 'Artifact privacy and ELF runtime verification passed' "$work/path-verification.log"
}
verify_from "$root" "${work#$root/}/invalid-manifest" "$work/dist"
mkdir "$work/caller with spaces"
cp "$work/invalid-manifest" "$work/caller with spaces/CHECKSUMS with spaces"
verify_from "$work/caller with spaces" 'CHECKSUMS with spaces' '../dist'
verify_from "$work/caller with spaces" "$work/invalid-manifest" '../dist'
printf '%s\n%s' "$source_record" "$extra_record" > "$work/invalid-manifest"
reject_manifest 'unexpected checksum artifact'
printf '%s\n%s' "$source_record" "$source_record" > "$work/invalid-manifest"
reject_manifest 'duplicate checksum artifact'
mv "$work/saved-source" "$work/dist/softline-9.8.7.tar.gz"
rm "$work/dist/extra.txt"
printf 'invalid-digest  softline-9.8.7.tar.gz\n' > "$work/invalid-manifest"
reject_manifest 'invalid checksum entry'
sdk=softline-9.8.7-x86_64-linux-gnu.tar.gz
cp "$work/dist/$sdk" "$work/saved-sdk"
mkdir -p "$work/bad-sdk/${sdk%.tar.gz}" "$work/bad-sdk/.hidden"
tar -czf "$work/dist/$sdk" -C "$work/bad-sdk" "${sdk%.tar.gz}" .hidden
(cd "$work/dist"; sha256sum "$sdk") > "$work/invalid-manifest"
reject_manifest 'unexpected SDK archive entry'
mv "$work/saved-sdk" "$work/dist/$sdk"
mv "$work/dist/$sdk" "$work/sdk-input"
ln -s "$work/sdk-input" "$work/dist/$sdk"
(cd "$work/dist"; sha256sum "$sdk") > "$work/invalid-manifest"
reject_manifest 'missing or unsafe checksum artifact'
rm "$work/dist/$sdk"
mv "$work/sdk-input" "$work/dist/$sdk"
printf 'x86_64-linux-gnu\n' > "$work/build/package-state/targets"
"$work/scripts/package-checksums.sh" binary
for omitted in softline-9.8.7.tar.gz softline-lua-9.8.7.tar.gz softline-9.8.7-1.rockspec softline-9.8.7-1.src.rock softline-9.8.7-aarch64-linux-gnu.tar.gz; do
  cp "$work/dist/$omitted" "$work/saved-payload"
  printf corrupt > "$work/dist/$omitted"
  if (cd "$work"; SOFTLINE_CHECKSUM_FILE="$work/build/package-state/softline-9.8.7-BINARY-CHECKSUMS" make --no-print-directory verify-release-archives) > "$work/error" 2>&1; then
    echo 'Complete archive target accepted corrupted omitted payload' >&2; exit 1
  fi
  grep -Fq "$omitted: FAILED" "$work/error"
  cmp "$work/complete-manifest" "$work/dist/softline-9.8.7-CHECKSUMS"
  mv "$work/saved-payload" "$work/dist/$omitted"
done
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
