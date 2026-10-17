#!/usr/bin/env bash
set -euo pipefail
root=$1
[[ $(uname -s):$(uname -m) == Linux:x86_64 ]] || { echo 'SKIP: native terminal cache fixture'; exit 0; }
mkdir -p "$root/build"
work=$(mktemp -d "$root/build/terminal-cache.XXXXXX")
trap 'rm -rf "$work"' EXIT
fixture="$work/source"
mkdir -p "$fixture/cmake/terminal-tests" "$fixture/scripts" "$work/payload/usr/bin"
cp "$root/cmake/terminal-tests/CMakeLists.txt" "$fixture/cmake/terminal-tests/"
cp "$root/cmake/softline_verified_archive.cmake" "$fixture/cmake/"
cp "$root/cmake/softline_workspace.cmake" "$fixture/cmake/"
cp "$root/scripts/"{relocate-terminal-test-tools.sh,require-host-bash.sh,require-workspace.sh} "$fixture/scripts/"
printf 'test Xvfb /usr/bin\0 prefix' > "$work/payload/usr/bin/Xvfb"
chmod +x "$work/payload/usr/bin/Xvfb"
tar -czf "$work/data.tar.gz" -C "$work/payload" usr
(cd "$work"; ar qc tools.deb data.tar.gz)
digest=$(sha256sum "$work/tools.deb" | awk '{print $1}')
mkdir -p "$work/extra/usr/share"
printf done > "$work/extra/usr/share/terminal-fixture"
tar -czf "$work/data.tar.gz" -C "$work/extra" usr
(cd "$work"; ar qc extra.deb data.tar.gz)
extra_digest=$(sha256sum "$work/extra.deb" | awk '{print $1}')
cp "$work/extra.deb" "$work/extra.saved"
printf '{"archives":[{"name":"fixture","url":"file://%s/tools.deb","sha256":"%s","archive":"tools.deb"},{"name":"extra","url":"file://%s/extra.deb","sha256":"%s","archive":"extra.deb"}]}\n' \
  "$work" "$digest" "$work" "$extra_digest" > "$fixture/cmake/terminal-tests/archives.json"
export XDG_CACHE_HOME="$work/global"
cache="$XDG_CACHE_HOME/cpkt/deps"
args=(-S "$fixture/cmake/terminal-tests" -B "$fixture/build/terminal-tests" "-DCPKT_DEPENDENCY_CACHE=$cache")
export CPKT_DEPENDENCY_CACHE="$cache/unused-cache"
cmake "${args[@]}"
install="$fixture/.cache/deps/x86_64-linux-gnu/terminal-tests/install"
expected="$work/expected"
printf 'test Xvfb \0\0\0\0\0\0\0\0\0 prefix' > "$expected"
cmp "$expected" "$install/usr/bin/Xvfb"
[[ $(cat "$install/usr/share/terminal-fixture") == done ]]
rm "$install/usr/bin/Xvfb" "$install/usr/share/terminal-fixture" "$work/extra.deb"
printf corrupt > "$cache/archives/sha256/$extra_digest/extra.deb"
if cmake "${args[@]}" > "$work/failure" 2>&1; then echo 'ERROR: corrupt partial staging succeeded' >&2; exit 1; fi
[[ -f "$install/usr/bin/Xvfb" && ! -e "$install/usr/share/terminal-fixture" ]]
cp "$work/extra.saved" "$work/extra.deb"
cmake "${args[@]}"
[[ $(cat "$install/usr/share/terminal-fixture") == done ]]
rm "$work/tools.deb" "$work/extra.deb"
cmake -E rm -r "$fixture/.cache"
cmake "${args[@]}"
cmp "$expected" "$install/usr/bin/Xvfb"
[[ ! -e "$cache/unused-cache" ]]
printf corrupt > "$cache/archives/sha256/$digest/tools.deb"
if cmake "${args[@]}" > "$work/failure" 2>&1; then echo 'ERROR: corrupt offline archive accepted' >&2; exit 1; fi
grep -q 'cannot acquire' "$work/failure"
[[ ! -e "$cache/archives/sha256/$digest/tools.deb" ]]
# Runtime version imports are checked separately from definitions.
mkdir -p "$work/version/usr/lib/x86_64-linux-gnu"
printf '\177ELFfixture' > "$work/version/fixture.so"
cp "$work/version/fixture.so" "$work/version/usr/lib/x86_64-linux-gnu/libc.so.6"
cat > "$work/readelf" <<'SH'
#!/bin/sh
case "$1" in
  --version-info) printf '%s\n' 'Version definition section' 'Name: GLIBC_2.35' 'Version needs section' "Name: GLIBC_${TEST_GLIBC_VERSION}" ;;
esac
SH
chmod +x "$work/readelf"
SOFTLINE_READELF="$work/readelf" TEST_GLIBC_VERSION=2.35 "$root/scripts/relocate-terminal-test-tools.sh" "$work/version" --verify
if SOFTLINE_READELF="$work/readelf" TEST_GLIBC_VERSION=2.43 "$root/scripts/relocate-terminal-test-tools.sh" "$work/version" --verify > "$work/failure" 2>&1; then
  echo 'ERROR: unstaged imported libc version accepted' >&2; exit 1
fi
grep -q 'required libc version GLIBC_2.43' "$work/failure"
echo 'Terminal cache validates offline reconstruction, partial staging, corruption and libc imports.'
