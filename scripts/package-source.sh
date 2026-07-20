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

GIT_ROOT=""
if command -v git >/dev/null 2>&1; then
  GIT_ROOT="$(git -C "${ROOT_DIR}" rev-parse --show-toplevel 2>/dev/null || true)"
fi
if [ "${GIT_ROOT}" = "${ROOT_DIR}" ]; then
  RELEASE_FILES="$(git -C "${ROOT_DIR}" ls-files)"
else
  if [ ! -f "${ROOT_DIR}/RELEASE_MANIFEST" ]; then
    echo "ERROR: source packaging outside git requires RELEASE_MANIFEST" >&2
    exit 1
  fi
  RELEASE_FILES="$(sed '/^$/d' "${ROOT_DIR}/RELEASE_MANIFEST")"
fi

if [ -z "${RELEASE_FILES}" ]; then
  echo "ERROR: source release manifest is empty" >&2
  exit 1
fi

printf '%s\n' "${RELEASE_FILES}" | while IFS= read -r f; do
  case "${f}" in
    VERSION|RELEASE_MANIFEST) continue ;;
    /*|../*|*/../*|.)
      echo "ERROR: invalid source release manifest path: ${f}" >&2
      exit 1
      ;;
  esac
  if [ ! -f "${ROOT_DIR}/${f}" ]; then
    echo "ERROR: source release manifest file is missing: ${f}" >&2
    exit 1
  fi
  mkdir -p "${STAGE_DIR}/$(dirname "${f}")"
  cp "${ROOT_DIR}/${f}" "${STAGE_DIR}/${f}"
done

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
