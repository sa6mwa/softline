#!/usr/bin/env bash
case ${BASH_SOURCE[0]} in
  */*) source "${BASH_SOURCE[0]%/*}/require-host-bash.sh" ;;
  *) source ./require-host-bash.sh ;;
esac || exit $?
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DIST_DIR="${SOFTLINE_DIST_DIR:-${ROOT_DIR}/dist}"
BUILD_DIR="${ROOT_DIR}/build/lua-release"
VERSION="$(bash "${ROOT_DIR}/scripts/release_version.sh")"
STAGE_ROOT="${BUILD_DIR}/softline-lua-${VERSION}"
SOURCE_ARCHIVE="${DIST_DIR}/softline-lua-${VERSION}.tar.gz"
ROCKSPEC="${DIST_DIR}/softline-${VERSION}-1.rockspec"

mkdir -p "${DIST_DIR}" "${BUILD_DIR}"
"${ROOT_DIR}/scripts/stage_lua_rock_sources.sh" "${STAGE_ROOT}" >/dev/null
"${ROOT_DIR}/scripts/render_release_rockspec.sh" >/dev/null
cp "${ROCKSPEC}" "${STAGE_ROOT}/softline-${VERSION}-1.rockspec"
printf '%s\n' "softline-${VERSION}-1.rockspec" >> "${STAGE_ROOT}/RELEASE_MANIFEST"
LC_ALL=C sort -u "${STAGE_ROOT}/RELEASE_MANIFEST" -o "${STAGE_ROOT}/RELEASE_MANIFEST"

tar --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner \
  --mode='u+rwX,go+rX,go-w' -cf - -C "${BUILD_DIR}" "softline-lua-${VERSION}" | gzip -n > "${SOURCE_ARCHIVE}"
rm -f "${DIST_DIR}/softline-${VERSION}-1.src.rock"
# ZIP metadata has a 1980 floor. Normalize copies in owned build scratch, so
# entry timestamps/permissions never depend on the caller's umask or timezone.
ZIP_STAGE="$(mktemp -d "${BUILD_DIR}/source-rock.XXXXXX")"
trap 'rm -rf "${ZIP_STAGE}"' EXIT
cp "${ROCKSPEC}" "${SOURCE_ARCHIVE}" "${ZIP_STAGE}/"
chmod 644 "${ZIP_STAGE}/"*
TZ=UTC touch -t 198001010000 "${ZIP_STAGE}/"*
SRC_ROCK="$(cd "${DIST_DIR}" && pwd)/softline-${VERSION}-1.src.rock"
(
  cd "${ZIP_STAGE}"
  TZ=UTC zip -X -q "${SRC_ROCK}" \
    "softline-${VERSION}-1.rockspec" \
    "softline-lua-${VERSION}.tar.gz"
)

echo "Lua artifacts:"
echo "  ${SOURCE_ARCHIVE}"
echo "  ${ROCKSPEC}"
echo "  ${DIST_DIR}/softline-${VERSION}-1.src.rock"
