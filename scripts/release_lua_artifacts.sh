#!/bin/sh
set -eu

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DIST_DIR="${ROOT_DIR}/dist"
BUILD_DIR="${ROOT_DIR}/build/lua-release"
VERSION="$(sh "${ROOT_DIR}/scripts/release_version.sh")"
STAGE_ROOT="${BUILD_DIR}/softline-lua-${VERSION}"
SOURCE_ARCHIVE="${DIST_DIR}/softline-lua-${VERSION}.tar.gz"
ROCKSPEC="${DIST_DIR}/softline-${VERSION}-1.rockspec"

mkdir -p "${DIST_DIR}" "${BUILD_DIR}"
"${ROOT_DIR}/scripts/stage_lua_rock_sources.sh" "${STAGE_ROOT}" >/dev/null
"${ROOT_DIR}/scripts/render_release_rockspec.sh" >/dev/null
cp "${ROCKSPEC}" "${STAGE_ROOT}/softline-${VERSION}-1.rockspec"

tar -czf "${SOURCE_ARCHIVE}" -C "${BUILD_DIR}" "softline-lua-${VERSION}"
rm -f "${DIST_DIR}/softline-${VERSION}-1.src.rock"
(
  cd "${DIST_DIR}"
  zip -q "softline-${VERSION}-1.src.rock" \
    "softline-${VERSION}-1.rockspec" \
    "softline-lua-${VERSION}.tar.gz"
)

echo "Lua artifacts:"
echo "  ${SOURCE_ARCHIVE}"
echo "  ${ROCKSPEC}"
echo "  ${DIST_DIR}/softline-${VERSION}-1.src.rock"
