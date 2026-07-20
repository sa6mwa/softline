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

if [ ! -f "${src_dir}/VERSION" ] || [ "$(head -1 "${src_dir}/VERSION")" != "${VERSION}" ]; then
  echo "ERROR: source archive VERSION does not match ${VERSION}"
  exit 1
fi

if [ ! -f "${src_dir}/RELEASE_MANIFEST" ]; then
  echo "ERROR: source archive missing RELEASE_MANIFEST"
  exit 1
fi

(
  cd "${src_dir}"
  find . -type f | sed 's|^\./||' | sort > "${TMP_DIR}/actual-manifest"
)
sort "${src_dir}/RELEASE_MANIFEST" > "${TMP_DIR}/expected-manifest"
if ! cmp -s "${TMP_DIR}/expected-manifest" "${TMP_DIR}/actual-manifest"; then
  echo "ERROR: source archive RELEASE_MANIFEST does not match payload"
  diff -u "${TMP_DIR}/expected-manifest" "${TMP_DIR}/actual-manifest" || true
  exit 1
fi

cmake -S "${src_dir}" -B "${src_dir}/build" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="${src_dir}/cmake/toolchains/bootlin-linux.cmake" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DSL_BUILD_TESTS=ON \
  -DSL_BUILD_EXAMPLES=ON
cmake --build "${src_dir}/build"

cd "${src_dir}/build" && ctest --output-on-failure

echo "Source archive smoke test passed."
