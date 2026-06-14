#!/bin/sh
set -eu

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DIST_DIR="${ROOT_DIR}/dist"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf ${TMP_DIR}' EXIT

VERSION="$(sh "${ROOT_DIR}/scripts/release_version.sh")"
ARCHIVE="${DIST_DIR}/softline-${VERSION}.tar.gz"

if [ ! -f "${ARCHIVE}" ]; then
  echo "Source archive not found: ${ARCHIVE}"
  exit 1
fi

tar -xzf "${ARCHIVE}" -C "${TMP_DIR}"
src_dir="${TMP_DIR}/softline-${VERSION}"

cmake -S "${src_dir}" -B "${src_dir}/build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DSL_BUILD_TESTS=ON \
  -DSL_BUILD_EXAMPLES=ON
cmake --build "${src_dir}/build"

cd "${src_dir}/build" && ctest --output-on-failure

echo "Source archive smoke test passed."