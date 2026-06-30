#!/bin/sh
set -eu

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "${TMP_DIR}"' EXIT

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
make_tool "${TMP_DIR}/tools/cache-readelf"
cat > "${cache_dir}/CMakeCache.txt" <<EOF
CMAKE_C_COMPILER:FILEPATH=${TMP_DIR}/tools/cache-cc
CMAKE_READELF:FILEPATH=${TMP_DIR}/tools/cache-readelf
EOF
out="$("${ROOT_DIR}/scripts/discover_target_tools.sh" "${cache_dir}" x86_64-linux-gnu)"
assert_contains "${out}" "READELF='${TMP_DIR}/tools/cache-readelf'"

osx_dir="${TMP_DIR}/osxcross/bin"
make_tool "${osx_dir}/arm64-apple-darwin25-cc"
make_tool "${osx_dir}/arm64-apple-darwin25-ld"
make_tool "${osx_dir}/arm64-apple-darwin25-otool"
cat > "${cache_dir}/CMakeCache.txt" <<EOF
CMAKE_C_COMPILER:FILEPATH=${osx_dir}/arm64-apple-darwin25-cc
EOF
out="$("${ROOT_DIR}/scripts/discover_target_tools.sh" "${cache_dir}" arm64-apple-darwin)"
assert_contains "${out}" "LINKER='${osx_dir}/arm64-apple-darwin25-ld'"
assert_contains "${out}" "OTOOL='${osx_dir}/arm64-apple-darwin25-otool'"

sibling_dir="${TMP_DIR}/sibling/bin"
make_tool "${sibling_dir}/aarch64-linux-gnu-gcc"
make_tool "${sibling_dir}/aarch64-linux-gnu-readelf"
cat > "${cache_dir}/CMakeCache.txt" <<EOF
CMAKE_C_COMPILER:FILEPATH=${sibling_dir}/aarch64-linux-gnu-gcc
EOF
out="$("${ROOT_DIR}/scripts/discover_target_tools.sh" "${cache_dir}" aarch64-linux-gnu)"
assert_contains "${out}" "READELF='${sibling_dir}/aarch64-linux-gnu-readelf'"

path_dir="${TMP_DIR}/path/bin"
make_tool "${path_dir}/readelf"
cat > "${cache_dir}/CMakeCache.txt" <<EOF
CMAKE_C_COMPILER:FILEPATH=cc
EOF
PATH="${path_dir}:${PATH}" out="$("${ROOT_DIR}/scripts/discover_target_tools.sh" "${cache_dir}" x86_64-linux-gnu)"
assert_contains "${out}" "READELF='${path_dir}/readelf'"

echo "Target tool discovery tests passed."
