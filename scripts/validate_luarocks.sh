#!/bin/sh
set -eu

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DIST_DIR="${SOFTLINE_DIST_DIR:-${ROOT_DIR}/dist}"
VERSION="$(sh "${ROOT_DIR}/scripts/release_version.sh")"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "${TMP_DIR}"' EXIT

ROCKSPEC="${DIST_DIR}/softline-${VERSION}-1.rockspec"
LUA_ARCHIVE="${DIST_DIR}/softline-lua-${VERSION}.tar.gz"
SRC_ROCK="${DIST_DIR}/softline-${VERSION}-1.src.rock"

for f in "${ROCKSPEC}" "${LUA_ARCHIVE}" "${SRC_ROCK}"; do
  if [ ! -f "${f}" ]; then
    echo "ERROR: missing Lua artifact ${f}" >&2
    exit 1
  fi
done

scan_release_paths() {
  artifact="$1"
  path="$2"
  if grep -R -a -F "file://${ROOT_DIR}" "${path}" >/dev/null 2>&1; then
    echo "ERROR: ${artifact} contains live-worktree source URL" >&2
    exit 1
  fi
  if grep -R -a -F "file://${HOME}" "${path}" >/dev/null 2>&1; then
    echo "ERROR: ${artifact} contains local file URL" >&2
    exit 1
  fi
  if grep -R -a -F "${ROOT_DIR}" "${path}" >/dev/null 2>&1; then
    echo "ERROR: ${artifact} contains repository path" >&2
    exit 1
  fi
  if grep -R -a -F "${HOME}" "${path}" >/dev/null 2>&1; then
    echo "ERROR: ${artifact} contains home path" >&2
    exit 1
  fi
  if grep -R -a -E '(/tmp/[^[:space:]]*luarocks|luarocks-build-)' "${path}" >/dev/null 2>&1; then
    echo "ERROR: ${artifact} contains package-manager temporary path" >&2
    exit 1
  fi
}

scan_release_paths "$(basename "${ROCKSPEC}")" "${ROCKSPEC}"

tar -xzf "${LUA_ARCHIVE}" -C "${TMP_DIR}"
test -f "${TMP_DIR}/softline-lua-${VERSION}/lua/softline_lua.c"
test -f "${TMP_DIR}/softline-lua-${VERSION}/include/softline/softline.h"
test -f "${TMP_DIR}/softline-lua-${VERSION}/RELEASE_MANIFEST"
scan_release_paths "$(basename "${LUA_ARCHIVE}")" "${TMP_DIR}/softline-lua-${VERSION}"

(
  cd "${TMP_DIR}"
  unzip -q "${SRC_ROCK}" -d srcrock
  test -f "srcrock/softline-${VERSION}-1.rockspec"
  test -f "srcrock/softline-lua-${VERSION}.tar.gz"
  scan_release_paths "$(basename "${SRC_ROCK}")" "srcrock/softline-${VERSION}-1.rockspec"
  tar -xzf "srcrock/softline-lua-${VERSION}.tar.gz" -C srcrock
  test -f "srcrock/softline-lua-${VERSION}/lua/softline_lua.c"
  scan_release_paths "$(basename "${SRC_ROCK}")" "srcrock/softline-lua-${VERSION}"
)

echo "LuaRocks artifacts verified."
