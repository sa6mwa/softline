#!/bin/sh
set -eu

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DIST_DIR="${ROOT_DIR}/dist"
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

if grep -F "${ROOT_DIR}" "${ROCKSPEC}" >/dev/null; then
  echo "ERROR: release rockspec contains repository path" >&2
  exit 1
fi
if grep -F "file://${HOME}" "${ROCKSPEC}" >/dev/null; then
  echo "ERROR: release rockspec contains local file URL" >&2
  exit 1
fi

tar -xzf "${LUA_ARCHIVE}" -C "${TMP_DIR}"
test -f "${TMP_DIR}/softline-lua-${VERSION}/lua/softline_lua.c"
test -f "${TMP_DIR}/softline-lua-${VERSION}/include/softline/softline.h"
test -f "${TMP_DIR}/softline-lua-${VERSION}/RELEASE_MANIFEST"
if grep -R -F "${ROOT_DIR}" "${TMP_DIR}/softline-lua-${VERSION}" >/dev/null; then
  echo "ERROR: Lua source package contains repository path" >&2
  exit 1
fi

(
  cd "${TMP_DIR}"
  unzip -q "${SRC_ROCK}" -d srcrock
  test -f "srcrock/softline-${VERSION}-1.rockspec"
  test -f "srcrock/softline-lua-${VERSION}.tar.gz"
  tar -xzf "srcrock/softline-lua-${VERSION}.tar.gz" -C srcrock
  test -f "srcrock/softline-lua-${VERSION}/lua/softline_lua.c"
)

echo "LuaRocks artifacts verified."
