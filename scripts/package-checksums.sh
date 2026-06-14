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
sha256sum softline-*.tar.gz > "${CHECKSUM_FILE}" 2>/dev/null || \
  shasum -a 256 softline-*.tar.gz > "${CHECKSUM_FILE}"

echo "Checksums written to ${CHECKSUM_FILE}"