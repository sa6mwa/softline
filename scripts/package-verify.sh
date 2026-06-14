#!/bin/sh
set -eu

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DIST_DIR="${ROOT_DIR}/dist"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf ${TMP_DIR}' EXIT

if [ ! -d "${DIST_DIR}" ] || [ -z "$(ls -A "${DIST_DIR}" 2>/dev/null)" ]; then
  echo "No packages to verify in ${DIST_DIR}/"
  exit 0
fi

VERSION="$(sh "${ROOT_DIR}/scripts/release_version.sh")"
CHECKSUM_FILE="${DIST_DIR}/softline-${VERSION}-CHECKSUMS"

if [ -f "${CHECKSUM_FILE}" ]; then
  echo "Verifying checksums..."
  cd "${DIST_DIR}"
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum -c "${CHECKSUM_FILE}"
  elif command -v shasum >/dev/null 2>&1; then
    shasum -a 256 -c "${CHECKSUM_FILE}"
  else
    echo "WARNING: no sha256sum or shasum found, skipping checksum verification"
  fi
fi

echo "Verifying package layouts..."
for archive in "${DIST_DIR}"/softline-*.tar.gz; do
  [ -f "${archive}" ] || continue
  basename="$(basename "${archive}" .tar.gz)"
  echo "  Checking ${basename}..."

  tar -xzf "${archive}" -C "${TMP_DIR}"

  root_count="$(ls -1 "${TMP_DIR}" | wc -l)"
  if [ "${root_count}" -ne 1 ]; then
    echo "ERROR: archive ${basename} has ${root_count} top-level entries (expected 1)"
    exit 1
  fi

  pkg_root="${TMP_DIR}/$(ls -1 "${TMP_DIR}")"

  if [ ! -d "${pkg_root}/include" ]; then
    echo "ERROR: ${basename} missing include/"
    exit 1
  fi

  if [ ! -d "${pkg_root}/lib" ]; then
    echo "ERROR: ${basename} missing lib/"
    exit 1
  fi

  echo "  ${basename}: OK"
done

echo "Package verification passed."