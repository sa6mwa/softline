#!/bin/sh
set -eu

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DIST_DIR="${ROOT_DIR}/dist"

if [ ! -d "${DIST_DIR}" ] || [ -z "$(ls -A "${DIST_DIR}" 2>/dev/null)" ]; then
  echo "No release artifacts to scan."
  exit 0
fi

echo "Scanning release artifacts for local paths..."

found=0
for archive in "${DIST_DIR}"/*.tar.gz "${DIST_DIR}"/*-CHECKSUMS; do
  [ -f "${archive}" ] || continue

  if echo "${archive}" | grep -q 'CHECKSUMS'; then
    if grep -q "${HOME}\|${ROOT_DIR}" "${archive}" 2>/dev/null; then
      echo "ERROR: checksum manifest contains local paths: ${archive}"
      found=1
    fi
  fi

  case "${archive}" in
    *.tar.gz)
      if tar -tzf "${archive}" 2>/dev/null | grep -q "${HOME}\|file://" 2>/dev/null; then
        echo "ERROR: archive contains local paths: ${archive}"
        found=1
      fi
      ;;
  esac
done

if [ "${found}" -eq 1 ]; then
  echo "Privacy verification FAILED."
  exit 1
fi

echo "Privacy verification passed."