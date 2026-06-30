#!/bin/sh
set -eu

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="$(sh "${ROOT_DIR}/scripts/release_version.sh")"
OUT="${1:-${ROOT_DIR}/build/luarocks/softline-${VERSION}-1.rockspec}"
SOURCE_URL="${SOURCE_URL:-file://.}"
SOURCE_DIR="${SOURCE_DIR:-softline-${VERSION}}"

mkdir -p "$(dirname "${OUT}")"
sed \
  -e "s|@VERSION@|${VERSION}|g" \
  -e "s|@SOURCE_URL@|${SOURCE_URL}|g" \
  -e "s|@SOURCE_DIR@|${SOURCE_DIR}|g" \
  "${ROOT_DIR}/softline.rockspec.in" > "${OUT}"
echo "${OUT}"
