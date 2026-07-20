#!/bin/sh
set -eu

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DIST_DIR="${ROOT_DIR}/dist"
. "${ROOT_DIR}/scripts/release-targets.sh"

VERSION="$(sh "${ROOT_DIR}/scripts/release_version.sh")"
REQUIRE_DARWIN="${SOFTLINE_REQUIRE_DARWIN:-0}"
if [ "${REQUIRE_DARWIN}" = "1" ]; then
  TARGETS="${SOFTLINE_RELEASE_TARGETS}"
else
  TARGETS="${SOFTLINE_PACKAGE_TARGETS:-${SOFTLINE_RELEASE_TARGETS}}"
fi
BUILT_TARGETS=""
SKIPPED_TARGETS=""

if [ "${REQUIRE_DARWIN}" = "1" ]; then
  case " ${TARGETS} " in
    *" arm64-apple-darwin "*) ;;
    *)
      echo "ERROR: Darwin artifact is required for make release" >&2
      exit 1
      ;;
  esac
  if ! "${ROOT_DIR}/scripts/cpkt-toolchains.sh" discover arm64-apple-darwin |
       grep -qx 'status=ready'; then
    echo "ERROR: Darwin artifact is required for make release, but osxcross is unavailable" >&2
    exit 1
  fi
fi

rm -rf "${DIST_DIR}"
mkdir -p "${DIST_DIR}"

for target in ${TARGETS}; do
  preset="${target}-release"
  build_dir="${ROOT_DIR}/build/${preset}"
  install_dir="${build_dir}/install"
  install_libdir=""
  echo "Building ${target}..."
  rm -rf "${build_dir}"
  if [ "${target}" = "arm64-apple-darwin" ] &&
     ! "${ROOT_DIR}/scripts/cpkt-toolchains.sh" discover "${target}" | grep -qx 'status=ready'; then
    if [ "${REQUIRE_DARWIN}" = "1" ]; then
      echo "ERROR: Darwin artifact is required for make release, but osxcross is unavailable" >&2
      exit 1
    fi
    echo "SKIP: ${target}: optional osxcross toolchain unavailable"
    SKIPPED_TARGETS="${SKIPPED_TARGETS} ${target}"
    continue
  fi
  if ! cmake --preset "${preset}" -S "${ROOT_DIR}"; then
    echo "ERROR: ${target} failed to configure using its lifecycle toolchain"
    exit 1
  fi
  if ! cmake --build --preset "${preset}"; then
    echo "ERROR: ${target} failed to build"
    exit 1
  fi
  if [ "${target}" = "x86_64-linux-gnu" ]; then
    if ! ctest --test-dir "${build_dir}" --output-on-failure; then
      echo "ERROR: ${target} release tests failed"
      exit 1
    fi
  fi
  rm -rf "${install_dir}"
  cmake --install "${build_dir}" --prefix "${install_dir}"

  if [ -d "${install_dir}/lib" ]; then
    install_libdir="lib"
  elif [ -d "${install_dir}/lib64" ]; then
    install_libdir="lib64"
  else
    echo "ERROR: install tree missing lib/ or lib64/"
    exit 1
  fi

  pkg_dir="${DIST_DIR}/softline-${VERSION}-${target}"
  mkdir -p "${pkg_dir}/include/softline"
  mkdir -p "${pkg_dir}/${install_libdir}"
  mkdir -p "${pkg_dir}/share/doc/softline"
  mkdir -p "${pkg_dir}/share/softline"

  cp -r "${install_dir}/include/softline/"* "${pkg_dir}/include/softline/"
  cp -r "${install_dir}/${install_libdir}/"* "${pkg_dir}/${install_libdir}/"
  if [ -d "${install_dir}/share/doc/softline" ]; then
    cp -r "${install_dir}/share/doc/softline/"* "${pkg_dir}/share/doc/softline/"
  else
    cp "${ROOT_DIR}/LICENSE" "${pkg_dir}/share/doc/softline/" 2>/dev/null || cp "${ROOT_DIR}/LICENSE" "${pkg_dir}/" 2>/dev/null || true
    cp "${ROOT_DIR}/README.md" "${pkg_dir}/share/doc/softline/" 2>/dev/null || true
  fi
  if [ -d "${install_dir}/share/softline" ]; then
    cp -r "${install_dir}/share/softline/"* "${pkg_dir}/share/softline/"
  fi

  echo "Packaging ${target}..."
  tar -czf "${DIST_DIR}/softline-${VERSION}-${target}.tar.gz" -C "${DIST_DIR}" "softline-${VERSION}-${target}"
  rm -rf "${pkg_dir}"
  BUILT_TARGETS="${BUILT_TARGETS} ${target}"
done

if [ -z "${BUILT_TARGETS}" ]; then
  echo "ERROR: no release targets were packaged"
  exit 1
fi

if [ "${REQUIRE_DARWIN}" = "1" ] &&
   ! printf '%s\n' "${BUILT_TARGETS}" | grep -Eq '(^|[[:space:]])arm64-apple-darwin([[:space:]]|$)'; then
  echo "ERROR: Darwin artifact is required for make release" >&2
  exit 1
fi

echo "Packages created in ${DIST_DIR}/"
ls -la "${DIST_DIR}/"
echo "Built targets:${BUILT_TARGETS}"
if [ -n "${SKIPPED_TARGETS}" ]; then
  echo "Skipped targets:${SKIPPED_TARGETS}"
fi
