#!/bin/sh
set -eu

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DIST_DIR="${ROOT_DIR}/dist"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "${TMP_DIR}"' EXIT

if [ ! -d "${DIST_DIR}" ] || [ -z "$(ls -A "${DIST_DIR}" 2>/dev/null)" ]; then
  echo "No release artifacts to scan."
  exit 0
fi

echo "Scanning release artifacts for local paths..."

found=0

scan_path() {
  artifact="$1"
  path="$2"
  if grep -R -a -F "${ROOT_DIR}" "${path}" >/dev/null 2>&1; then
    echo "ERROR: ${artifact} contains repository path"
    found=1
  fi
  if grep -R -a -F "${HOME}" "${path}" >/dev/null 2>&1; then
    echo "ERROR: ${artifact} contains home path"
    found=1
  fi
  if grep -R -a -F "file://${HOME}" "${path}" >/dev/null 2>&1; then
    echo "ERROR: ${artifact} contains local file URL"
    found=1
  fi
}

expand_nested_archives() {
  root="$1"
  find "${root}" -type f \( -name '*.tar.gz' -o -name '*.tgz' -o -name '*.zip' -o -name '*.rock' -o -name '*.src.rock' \) | while IFS= read -r nested; do
    nested_dir="${nested}.expanded"
    mkdir -p "${nested_dir}"
    case "${nested}" in
      *.tar.gz|*.tgz)
        tar -xzf "${nested}" -C "${nested_dir}" 2>/dev/null || true
        ;;
      *.zip|*.rock|*.src.rock)
        unzip -q "${nested}" -d "${nested_dir}" 2>/dev/null || true
        ;;
    esac
  done
}

for artifact in "${DIST_DIR}"/*; do
  [ -f "${artifact}" ] || continue
  name="$(basename "${artifact}")"
  case "${name}" in
    *-CHECKSUMS|*.rockspec)
      scan_path "${name}" "${artifact}"
      ;;
    *.tar.gz|*.tgz)
      extract_dir="${TMP_DIR}/${name}.extracted"
      mkdir -p "${extract_dir}"
      tar -xzf "${artifact}" -C "${extract_dir}" 2>/dev/null || {
        echo "ERROR: unable to extract ${name}"
        found=1
        continue
      }
      expand_nested_archives "${extract_dir}"
      scan_path "${name}" "${extract_dir}"
      ;;
    *.rock|*.src.rock|*.zip)
      extract_dir="${TMP_DIR}/${name}.extracted"
      mkdir -p "${extract_dir}"
      unzip -q "${artifact}" -d "${extract_dir}" 2>/dev/null || {
        echo "ERROR: unable to extract ${name}"
        found=1
        continue
      }
      expand_nested_archives "${extract_dir}"
      scan_path "${name}" "${extract_dir}"
      ;;
  esac
done

if [ "${found}" -eq 1 ]; then
  echo "Privacy verification FAILED."
  exit 1
fi

echo "Privacy verification passed."
