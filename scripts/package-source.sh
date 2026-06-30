#!/bin/sh
set -eu

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DIST_DIR="${ROOT_DIR}/dist"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf ${TMP_DIR}' EXIT

VERSION="$(sh "${ROOT_DIR}/scripts/release_version.sh")"
ARCHIVE="${DIST_DIR}/softline-${VERSION}.tar.gz"

mkdir -p "${DIST_DIR}"

STAGE_DIR="${TMP_DIR}/softline-${VERSION}"
mkdir -p "${STAGE_DIR}"

if command -v git >/dev/null 2>&1 && [ -d "${ROOT_DIR}/.git" ]; then
  tracked="$(git -C "${ROOT_DIR}" ls-files)"
  if [ -n "${tracked}" ]; then
    echo "${tracked}" | while read -r f; do
      mkdir -p "${STAGE_DIR}/$(dirname "${f}")"
      cp "${ROOT_DIR}/${f}" "${STAGE_DIR}/${f}"
    done
  fi
fi

if [ -z "$(ls -A "${STAGE_DIR}" 2>/dev/null)" ]; then
  for f in CMakeLists.txt CMakePresets.json Makefile .clang-format .gitignore \
           LICENSE README.md; do
    [ -f "${ROOT_DIR}/${f}" ] && cp "${ROOT_DIR}/${f}" "${STAGE_DIR}/${f}"
  done
  for d in cmake scripts include src tests examples docs lua; do
    if [ -d "${ROOT_DIR}/${d}" ]; then
      cp -r "${ROOT_DIR}/${d}" "${STAGE_DIR}/${d}"
    fi
  done
  [ -f "${ROOT_DIR}/softline.rockspec.in" ] && cp "${ROOT_DIR}/softline.rockspec.in" "${STAGE_DIR}/softline.rockspec.in"
fi

(
  cd "${STAGE_DIR}"
  find . -type f | sed 's|^\./||' | sort > RELEASE_MANIFEST
)
echo "${VERSION}" > "${STAGE_DIR}/VERSION"
printf '%s\n' VERSION >> "${STAGE_DIR}/RELEASE_MANIFEST"
printf '%s\n' RELEASE_MANIFEST >> "${STAGE_DIR}/RELEASE_MANIFEST"
sort -u "${STAGE_DIR}/RELEASE_MANIFEST" -o "${STAGE_DIR}/RELEASE_MANIFEST"

tar -czf "${ARCHIVE}" -C "${TMP_DIR}" "softline-${VERSION}"
echo "Source archive: ${ARCHIVE}"
