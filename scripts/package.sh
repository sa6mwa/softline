#!/bin/sh
set -eu

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DIST_DIR="${ROOT_DIR}/dist"
. "${ROOT_DIR}/scripts/release-targets.sh"

VERSION="$(sh "${ROOT_DIR}/scripts/release_version.sh")"
TARGETS="${SOFTLINE_PACKAGE_TARGETS:-${SOFTLINE_RELEASE_TARGETS}}"
BUILT_TARGETS=""
SKIPPED_TARGETS=""
MANDATORY_TARGETS="${SOFTLINE_MANDATORY_PACKAGE_TARGETS:-x86_64-linux-gnu}"

rm -rf "${DIST_DIR}"
mkdir -p "${DIST_DIR}"

tool_available() {
  tool="$1"
  case "${tool}" in
    /*|*/*) [ -x "${tool}" ] ;;
    *) command -v "${tool}" >/dev/null 2>&1 ;;
  esac
}

run_for_target() {
  target="$1"
  shift
  path_prefix="$(softline_target_path_prefix "${target}")"
  if [ -n "${path_prefix}" ]; then
    PATH="${path_prefix}:${PATH}" "$@"
  else
    "$@"
  fi
}

is_mandatory_target() {
  needle="$1"
  for mandatory in ${MANDATORY_TARGETS}; do
    if [ "${mandatory}" = "${needle}" ]; then
      return 0
    fi
  done
  return 1
}

for target in ${TARGETS}; do
  preset="${target}-release"
  build_dir="${ROOT_DIR}/build/${preset}"
  install_dir="${build_dir}/install"
  install_libdir=""
  cc="$(softline_target_default_cc "${target}")"

  if [ -n "${cc}" ] && ! tool_available "${cc}"; then
    echo "SKIP: ${target}: compiler unavailable: ${cc}"
    SKIPPED_TARGETS="${SKIPPED_TARGETS} ${target}"
    continue
  fi

  echo "Building ${target}..."
  if [ -n "${cc}" ]; then
    if ! run_for_target "${target}" cmake --preset "${preset}" -S "${ROOT_DIR}" -DCMAKE_C_COMPILER="${cc}"; then
      if is_mandatory_target "${target}"; then
        echo "ERROR: mandatory target ${target} failed to configure"
        exit 1
      fi
      echo "SKIP: ${target}: configure failed with compiler ${cc}"
      SKIPPED_TARGETS="${SKIPPED_TARGETS} ${target}"
      continue
    fi
  else
    if ! run_for_target "${target}" cmake --preset "${preset}" -S "${ROOT_DIR}"; then
      if is_mandatory_target "${target}"; then
        echo "ERROR: mandatory target ${target} failed to configure"
        exit 1
      fi
      echo "SKIP: ${target}: configure failed"
      SKIPPED_TARGETS="${SKIPPED_TARGETS} ${target}"
      continue
    fi
  fi
  if ! run_for_target "${target}" cmake --build --preset "${preset}"; then
    if is_mandatory_target "${target}"; then
      echo "ERROR: mandatory target ${target} failed to build"
      exit 1
    fi
    echo "SKIP: ${target}: build failed"
    SKIPPED_TARGETS="${SKIPPED_TARGETS} ${target}"
    continue
  fi
  rm -rf "${install_dir}"
  run_for_target "${target}" cmake --install "${build_dir}" --prefix "${install_dir}"

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

echo "Packages created in ${DIST_DIR}/"
ls -la "${DIST_DIR}/"
echo "Built targets:${BUILT_TARGETS}"
if [ -n "${SKIPPED_TARGETS}" ]; then
  echo "Skipped targets:${SKIPPED_TARGETS}"
fi
