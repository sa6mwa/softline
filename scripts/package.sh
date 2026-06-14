#!/bin/sh
set -eu

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DIST_DIR="${ROOT_DIR}/dist"

VERSION="$(sh "${ROOT_DIR}/scripts/release_version.sh")"
TARGETS="x86_64-linux-gnu"

rm -rf "${DIST_DIR}"
mkdir -p "${DIST_DIR}"

for target in ${TARGETS}; do
  preset="${target}-release"
  build_dir="${ROOT_DIR}/build/${preset}"
  install_dir="${build_dir}/install"

  echo "Building ${target}..."
  cmake --preset "${preset}" -S "${ROOT_DIR}"
  cmake --build --preset "${preset}"
  cmake --install "${build_dir}" --prefix "${install_dir}"

  pkg_dir="${DIST_DIR}/softline-${VERSION}-${target}"
  mkdir -p "${pkg_dir}/include/softline"
  mkdir -p "${pkg_dir}/lib"
  mkdir -p "${pkg_dir}/lib/cmake/softline"
  mkdir -p "${pkg_dir}/share/doc/softline"

  cp -r "${install_dir}/include/softline/"* "${pkg_dir}/include/softline/"
  if [ -d "${install_dir}/lib" ]; then
    cp -r "${install_dir}/lib/"* "${pkg_dir}/lib/"
  fi
  if [ -d "${install_dir}/lib64" ]; then
    cp -r "${install_dir}/lib64/"* "${pkg_dir}/lib/"
  fi
  cp "${install_dir}/share/doc/softline/LICENSE" "${pkg_dir}/share/doc/softline/" 2>/dev/null || true
  cp "${ROOT_DIR}/LICENSE" "${pkg_dir}/share/doc/softline/" 2>/dev/null || cp "${ROOT_DIR}/LICENSE" "${pkg_dir}/" 2>/dev/null || true

  echo "Packaging ${target}..."
  tar -czf "${DIST_DIR}/softline-${VERSION}-${target}.tar.gz" -C "${DIST_DIR}" "softline-${VERSION}-${target}"
  rm -rf "${pkg_dir}"
done

echo "Packages created in ${DIST_DIR}/"
ls -la "${DIST_DIR}/"