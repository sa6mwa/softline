#!/usr/bin/env bash
set -euo pipefail
root=$1
real_make=$(command -v make)
work=$(mktemp -d "$root/build/release-sequence.XXXXXX")
trap 'rm -rf -- "$work"' EXIT
fixture="$work/repo"
mkdir -p "$fixture/scripts" "$fixture/build" "$work/bin"
cp "$root/scripts/"{release.sh,lifecycle-version-contract.sh,release_version.sh,release-targets.sh,require-host-bash.sh,require-workspace.sh} "$fixture/scripts/"
mkdir -p "$fixture/cmake"
cp "$root/cmake/softline_workspace.cmake" "$fixture/cmake/"
cp "$root/Makefile" "$fixture/"
cat > "$fixture/scripts/cpkt-toolchains.sh" <<'SH'
#!/usr/bin/env bash
printf 'status=ready\n'
SH
cat > "$fixture/scripts/cpkt-aflpp.sh" <<'SH'
#!/usr/bin/env bash
exit 0
SH
for helper in package-source package-checksums package-verify release_lua_artifacts; do
  printf '#!/usr/bin/env bash\nprintf '\''%%s:%%s\\n'\'' %q "$*" >> "$CALL_LOG"\n' "$helper" > "$fixture/scripts/$helper.sh"
done
cat > "$fixture/scripts/package.sh" <<'SH'
#!/usr/bin/env bash
printf 'package:%s:%s:%s\n' "$0" "$SOFTLINE_PACKAGE_TARGETS" "$SOFTLINE_REQUIRE_DARWIN" >> "$CALL_LOG"
SH
cat > "$fixture/scripts/extract-release-source.sh" <<'SH'
#!/usr/bin/env bash
mkdir -p "$2/native/scripts"
root=$(cd "${BASH_SOURCE[0]%/*}/.." && pwd)
cp "$root/scripts/package.sh" "$2/native/scripts/"
printf 'extract\n' >> "$CALL_LOG"
printf '%s\n' "$2/native"
SH
cat > "$work/bin/make" <<'SH'
#!/usr/bin/env bash
printf 'make:%s\n' "$*" >> "$CALL_LOG"
SH
chmod +x "$fixture/scripts/"* "$work/bin/make"
printf '/build/\n/dist/\n' > "$fixture/.gitignore"
source "$root/tests/lifecycle/release-fixture.sh"
release_fixture_init "$fixture"
export PATH="$work/bin:$PATH" CALL_LOG="$work/calls"
unset SL_VERSION_OVERRIDE SL_VERSION_ROOT SOFTLINE_PACKAGE_TARGETS SOFTLINE_RELEASE_TARGETS SOFTLINE_DIST_DIR SOFTLINE_FORCE_TESTS SOFTLINE_CHECKSUM_FILE
: > "$CALL_LOG"
printf retained > "$fixture/build/retained"
if "$fixture/scripts/release.sh" preflight > "$work/error" 2>&1; then echo 'Untagged release accepted' >&2; exit 1; fi
[[ -f "$fixture/build/retained" && ! -s "$CALL_LOG" ]]
git -C "$fixture" tag v1.2.3
if "$fixture/scripts/release.sh" preflight > "$work/error" 2>&1; then echo 'Unsigned release accepted' >&2; exit 1; fi
[[ -f "$fixture/build/retained" && ! -s "$CALL_LOG" ]]
git -C "$fixture" tag -d v1.2.3 >/dev/null
release_fixture_sign "$fixture"
git -C "$fixture" tag v1.2.3
git -C "$fixture" checkout -qb feature/not-a-release
if "$fixture/scripts/release.sh" preflight > "$work/error" 2>&1; then echo 'Feature-branch release accepted' >&2; exit 1; fi
[[ -f "$fixture/build/retained" && ! -s "$CALL_LOG" ]]
git -C "$fixture" checkout -q trunk
"$fixture/scripts/release.sh" preflight
if SOFTLINE_RELEASE_TARGETS=x86_64-linux-gnu "$fixture/scripts/release.sh" preflight > "$work/error" 2>&1; then
  echo 'Partial final release accepted' >&2; exit 1
fi
"$fixture/scripts/release.sh"
mapfile -t calls < "$CALL_LOG"
[[ ${#calls[@]} == 8 ]]
[[ ${calls[0]} == "make:-C $fixture prerelease-checks" ]]
[[ ${calls[1]} == package-source: && ${calls[2]} == extract ]]
[[ ${calls[3]} == "package:$fixture/build/release-source/native/scripts/package.sh:x86_64-linux-gnu:0" ]]
[[ ${calls[4]} == "package:$fixture/scripts/package.sh:"*":1" ]]
[[ ${calls[4]} != *' x86_64-linux-gnu '* ]]
[[ ${calls[5]} == release_lua_artifacts: && ${calls[6]} == 'package-checksums:all' && ${calls[7]} == package-verify: ]]
[[ -f "$fixture/build/retained" ]]
: > "$CALL_LOG"
"$real_make" --no-print-directory -C "$fixture" release MAKE="$work/bin/make"
mapfile -t calls < "$CALL_LOG"
[[ ${#calls[@]} == 10 && ${calls[0]} == make:lifecycle-version-contract && ${calls[1]} == make:clean ]]
[[ ${calls[2]} == "make:-C $fixture prerelease-checks" && ${calls[9]} == package-verify: ]]
echo 'Read-only preflight preserves outputs; final sequence shares source-native and checkout-cross ownership.'
