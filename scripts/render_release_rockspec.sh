#!/bin/sh
set -eu

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="$(sh "${ROOT_DIR}/scripts/release_version.sh")"
OUT="${ROOT_DIR}/dist/softline-${VERSION}-1.rockspec"
SOURCE_URL="${SOURCE_URL:-softline-lua-${VERSION}.tar.gz}"
SOURCE_DIR="${SOURCE_DIR:-softline-lua-${VERSION}}"

mkdir -p "${ROOT_DIR}/dist"
SOURCE_URL="${SOURCE_URL}" SOURCE_DIR="${SOURCE_DIR}" \
  "${ROOT_DIR}/scripts/render_lua_rockspec.sh" "${OUT}" >/dev/null
echo "${OUT}"
