#!/bin/sh
set -eu

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DIST_DIR="${ROOT_DIR}/dist"

if [ ! -d "${DIST_DIR}" ] || [ -z "$(ls -A "${DIST_DIR}" 2>/dev/null)" ]; then
  echo "No packages to checksum in ${DIST_DIR}/"
  exit 1
fi

VERSION="$(sh "${ROOT_DIR}/scripts/release_version.sh")"
CHECKSUM_FILE="${DIST_DIR}/softline-${VERSION}-CHECKSUMS"

cd "${DIST_DIR}"
ARTIFACTS="$(find . -maxdepth 1 -type f \( \
  -name "softline-${VERSION}.tar.gz" -o \
  -name "softline-${VERSION}-*.tar.gz" -o \
  -name "softline-lua-${VERSION}.tar.gz" -o \
  -name "softline-${VERSION}-1.rockspec" -o \
  -name "softline-${VERSION}-1.src.rock" \
  \) | sed 's|^\./||' | sort)"
if [ -z "${ARTIFACTS}" ]; then
  echo "No release artifacts to checksum in ${DIST_DIR}/"
  exit 1
fi

if command -v sha256sum >/dev/null 2>&1; then
  sha256sum ${ARTIFACTS} > "${CHECKSUM_FILE}"
else
  shasum -a 256 ${ARTIFACTS} > "${CHECKSUM_FILE}"
fi

echo "Checksums written to ${CHECKSUM_FILE}"
