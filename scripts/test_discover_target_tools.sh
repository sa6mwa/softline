#!/usr/bin/env bash
case ${BASH_SOURCE[0]} in
  */*) source "${BASH_SOURCE[0]%/*}/require-host-bash.sh" ;;
  *) source ./require-host-bash.sh ;;
esac || exit $?
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "${ROOT_DIR}/build"
TMP_DIR="$(mktemp -d "${ROOT_DIR}/build/test_discover_target_tools.XXXXXX")"
trap 'rm -rf "${TMP_DIR}"' EXIT
mkdir -p "$TMP_DIR/source/scripts" "$TMP_DIR/home" "$TMP_DIR/path/bin"
cp "$ROOT_DIR/scripts/"{discover_target_tools.sh,require-host-bash.sh} "$TMP_DIR/source/scripts/"
discovery="$TMP_DIR/source/scripts/discover_target_tools.sh"
# Exercise the project adapter without an installed cpkt, skill or collection.
export HOME="$TMP_DIR/home" XDG_CACHE_HOME="$TMP_DIR/global"
export TEST_TOOLS="$TMP_DIR/collection/bin" TEST_CPKT_CALLS="$TMP_DIR/cpkt-calls"
cat > "$TMP_DIR/path/bin/cpkt" <<'SH'
#!/bin/sh
printf 'unexpected cpkt invocation\n' >> "$TEST_CPKT_CALLS"
exit 99
SH
chmod +x "$TMP_DIR/path/bin/cpkt"
export PATH="$TMP_DIR/path/bin:$PATH"
cat > "$TMP_DIR/source/scripts/cpkt-toolchains.sh" <<'SH'
#!/bin/bash
[[ ${TEST_RESOLVER_FAIL:-0} == 0 ]] || { echo 'selected collection missing' >&2; exit 7; }
printf 'export CC=%q LD=%q CPKT_TOOLCHAIN_READELF=%q CPKT_TOOLCHAIN_STRIP=%q CPKT_TOOLCHAIN_NM=%q\n' \
  "$TEST_TOOLS/target-gcc" "$TEST_TOOLS/target-ld" "$TEST_TOOLS/target-readelf" "$TEST_TOOLS/target-strip" "$TEST_TOOLS/target-nm"
printf 'export CPKT_TOOLCHAIN_ROOT=%q CPKT_TOOLCHAIN_PREFIX=target CPKT_TOOLCHAIN_OTOOL=%q\n' \
  "${TEST_TOOLS%/bin}" "$TEST_TOOLS/target-otool"
SH
chmod +x "$TMP_DIR/source/scripts/cpkt-toolchains.sh"

make_tool() {
  path="$1"
  mkdir -p "$(dirname "${path}")"
  printf '#!/bin/sh\nexit 0\n' > "${path}"
  chmod +x "${path}"
}

assert_contains() {
  text="$1"
  needle="$2"
  if ! printf '%s\n' "${text}" | grep -F "${needle}" >/dev/null; then
    echo "ERROR: expected discovery output to contain ${needle}" >&2
    printf '%s\n' "${text}" >&2
    exit 1
  fi
}

cache_dir="${TMP_DIR}/cache"
mkdir -p "${cache_dir}"
for tool in gcc ld readelf strip nm otool install_name_tool; do make_tool "$TEST_TOOLS/target-$tool"; done
make_tool "${TMP_DIR}/tools/cache-readelf"
cat > "${cache_dir}/CMakeCache.txt" <<EOF
CMAKE_C_COMPILER:FILEPATH=${TMP_DIR}/tools/cache-cc
CMAKE_READELF:FILEPATH=${TMP_DIR}/tools/cache-readelf
EOF
out="$("$discovery" "${cache_dir}" x86_64-linux-gnu)"
assert_contains "${out}" "READELF='${TMP_DIR}/tools/cache-readelf'"
make_tool "$TMP_DIR/tools/explicit-readelf"
out=$(SOFTLINE_READELF="$TMP_DIR/tools/explicit-readelf" "$discovery" "$cache_dir" x86_64-linux-gnu)
assert_contains "$out" "READELF='$TMP_DIR/tools/explicit-readelf'"
if SOFTLINE_READELF="$TMP_DIR/missing-readelf" "$discovery" "$cache_dir" x86_64-linux-gnu > "$TMP_DIR/out" 2> "$TMP_DIR/err"; then
  echo 'ERROR: unavailable explicit inspector accepted' >&2; exit 1
fi
[[ ! -s "$TMP_DIR/out" ]]
grep -q 'selected readelf is not executable' "$TMP_DIR/err"
for target in x86_64-linux-gnu arm64-apple-darwin; do
  if TEST_RESOLVER_FAIL=1 "$discovery" "$cache_dir" "$target" > "$TMP_DIR/out" 2> "$TMP_DIR/err"; then
    echo "ERROR: $target accepted a missing collection through cached/PATH tools" >&2; exit 1
  fi
  [[ ! -s "$TMP_DIR/out" ]]
  grep -q 'selected collection missing' "$TMP_DIR/err"
done

osx_dir="${TMP_DIR}/osxcross/bin"
make_tool "${osx_dir}/arm64-apple-darwin25-cc"
make_tool "${osx_dir}/arm64-apple-darwin25-ld"
make_tool "${osx_dir}/arm64-apple-darwin25-otool"
cat > "${cache_dir}/CMakeCache.txt" <<EOF
CMAKE_C_COMPILER:FILEPATH=${osx_dir}/arm64-apple-darwin25-cc
EOF
out="$("$discovery" "${cache_dir}" arm64-apple-darwin)"
assert_contains "${out}" "LINKER='${osx_dir}/arm64-apple-darwin25-ld'"
assert_contains "${out}" "OTOOL='${osx_dir}/arm64-apple-darwin25-otool'"
make_tool "$TMP_DIR/tools/configured-otool"
printf 'CMAKE_OTOOL:FILEPATH=%s/tools/configured-otool\n' "$TMP_DIR" >> "$cache_dir/CMakeCache.txt"
out="$("$discovery" "$cache_dir" arm64-apple-darwin)"
assert_contains "$out" "OTOOL='$TMP_DIR/tools/configured-otool'"

sibling_dir="${TMP_DIR}/sibling/bin"
make_tool "${sibling_dir}/aarch64-linux-gnu-gcc"
make_tool "${sibling_dir}/aarch64-linux-gnu-readelf"
cat > "${cache_dir}/CMakeCache.txt" <<EOF
CMAKE_C_COMPILER:FILEPATH=${sibling_dir}/aarch64-linux-gnu-gcc
EOF
out="$("$discovery" "${cache_dir}" aarch64-linux-gnu)"
assert_contains "${out}" "READELF='${sibling_dir}/aarch64-linux-gnu-readelf'"

path_dir="${TMP_DIR}/path/bin"
make_tool "${path_dir}/readelf"
cat > "${cache_dir}/CMakeCache.txt" <<EOF
CMAKE_C_COMPILER:FILEPATH=cc
EOF
PATH="${path_dir}:${PATH}" out="$("$discovery" "${cache_dir}" x86_64-linux-gnu)"
assert_contains "${out}" "READELF='$TEST_TOOLS/target-readelf'"
if printf '%s\n' "${out}" | grep -F "${path_dir}/readelf" >/dev/null; then
  echo "ERROR: Linux discovery must not fall back to ambient PATH readelf" >&2
  printf '%s\n' "${out}" >&2
  exit 1
fi
[[ ! -e "$TEST_CPKT_CALLS" ]]

echo "Target tool discovery tests passed."
