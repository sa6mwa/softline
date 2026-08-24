#!/bin/sh
set -eu

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DIST_DIR="${ROOT_DIR}/dist"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "${TMP_DIR}"' EXIT

if [ ! -d "${DIST_DIR}" ] || [ -z "$(ls -A "${DIST_DIR}" 2>/dev/null)" ]; then
  echo "No packages to verify in ${DIST_DIR}/"
  exit 0
fi

VERSION="$(sh "${ROOT_DIR}/scripts/release_version.sh")"
CHECKSUM_FILE="${DIST_DIR}/softline-${VERSION}-CHECKSUMS"
SOFTLINE_ABI_VERSION="${SOFTLINE_ABI_VERSION:-1}"

if [ ! -f "${CHECKSUM_FILE}" ]; then
  echo "ERROR: missing checksum manifest ${CHECKSUM_FILE}"
  exit 1
fi

if [ -e "${DIST_DIR}/SHA256SUMS" ]; then
  echo "ERROR: deprecated checksum manifest present: ${DIST_DIR}/SHA256SUMS"
  exit 1
fi

for checksum in "${DIST_DIR}"/softline-*-CHECKSUMS; do
  [ -f "${checksum}" ] || continue
  if [ "${checksum}" != "${CHECKSUM_FILE}" ]; then
    echo "ERROR: stale checksum manifest present: ${checksum}"
    exit 1
  fi
done

target_from_basename() {
  basename="$1"
  echo "${basename#softline-${VERSION}-}"
}

load_tools() {
  target="$1"
  build_dir="${ROOT_DIR}/build/${target}-release"
  tool_env=""
  TARGET_ID=""
  TARGET_OS=""
  CC=""
  LD=""
  LINKER=""
  READELF=""
  OTOOL=""
  INSTALL_NAME_TOOL=""
  STRIP=""
  if tool_env="$("${ROOT_DIR}/scripts/cpkt-toolchains.sh" env "${target}" 2>/dev/null)"; then
    eval "${tool_env}"
    TARGET_ID="${target}"
    case "${target}" in
      *-apple-darwin) TARGET_OS="darwin" ;;
      *-linux-*) TARGET_OS="linux" ;;
      *) TARGET_OS="unknown" ;;
    esac
    LINKER="${LD:-${CPKT_TOOLCHAIN_LD:-}}"
    OTOOL="${CPKT_TOOLCHAIN_OTOOL:-}"
    INSTALL_NAME_TOOL="${CPKT_TOOLCHAIN_INSTALL_NAME_TOOL:-}"
    STRIP="${CPKT_TOOLCHAIN_STRIP:-}"
    return
  fi
  tools="$("${ROOT_DIR}/scripts/discover_target_tools.sh" "${build_dir}" "${target}")"
  eval "${tools}"
}

fail_local_path() {
  artifact="$1"
  file="$2"
  echo "ERROR: ${artifact} contains local path in ${file}"
  exit 1
}

scan_for_local_paths() {
  artifact="$1"
  root="$2"
  if grep -R -a -F "${ROOT_DIR}" "${root}" >/dev/null 2>&1; then
    fail_local_path "${artifact}" "${root}"
  fi
  if grep -R -a -F "${HOME}" "${root}" >/dev/null 2>&1; then
    fail_local_path "${artifact}" "${root}"
  fi
  if grep -R -a -F "file://${HOME}" "${root}" >/dev/null 2>&1; then
    fail_local_path "${artifact}" "${root}"
  fi
}

verify_elf_metadata() {
  artifact="$1"
  root="$2"
  if [ -z "${READELF:-}" ]; then
    echo "ERROR: ${artifact}: readelf unavailable for ELF verification"
    exit 1
  fi
  find "${root}" -type f \( -name '*.so' -o -name '*.so.*' \) | while IFS= read -r file; do
    dynamic="$("${READELF}" -d "${file}" 2>/dev/null || true)"
    case "${file}" in
      */libsoftline.so|*/libsoftline.so.*)
        soname="$(printf '%s\n' "${dynamic}" |
          sed -n 's/.*Library soname: \[\(.*\)\].*/\1/p')"
        if [ -n "${soname}" ] &&
           [ "${soname}" != "libsoftline.so.${SOFTLINE_ABI_VERSION}" ]; then
          echo "ERROR: ${artifact}: ${file} SONAME ${soname} does not match ABI ${SOFTLINE_ABI_VERSION}"
          exit 1
        fi
        ;;
    esac
    runpaths="$(printf '%s\n' "${dynamic}" |
      sed -n 's/.*\(RPATH\|RUNPATH\).*: \[\(.*\)\].*/\2/p')"
    if [ -n "${runpaths}" ]; then
      old_ifs="${IFS}"
      IFS=:
      for runpath in ${runpaths}; do
        case "${runpath}" in
          '$ORIGIN'|'$ORIGIN/'*) ;;
          /*)
            echo "ERROR: ${artifact}: ${file} has absolute ELF runtime path ${runpath}"
            exit 1
            ;;
          *"${ROOT_DIR}"*|*"${HOME}"*)
            echo "ERROR: ${artifact}: ${file} has local ELF runtime path ${runpath}"
            exit 1
            ;;
        esac
      done
      IFS="${old_ifs}"
    fi
  done
}

verify_darwin_metadata() {
  artifact="$1"
  root="$2"
  if [ -z "${OTOOL:-}" ]; then
    echo "ERROR: ${artifact}: external-tool-unavailable: target-correct otool required for Mach-O verification"
    exit 1
  fi
  find "${root}" -type f \( -name '*.dylib' -o -name '*.so' \) | while IFS= read -r file; do
    id="$("${OTOOL}" -D "${file}" 2>/dev/null | sed -n '2p' || true)"
    case "${file}" in
      *.dylib)
        case "${id}" in
          @rpath/*) ;;
          "")
            echo "ERROR: ${artifact}: ${file} missing LC_ID_DYLIB"
            exit 1
            ;;
          *)
            echo "ERROR: ${artifact}: ${file} has non-@rpath install name ${id}"
            exit 1
            ;;
        esac
        ;;
    esac

    "${OTOOL}" -L "${file}" 2>/dev/null | sed '1d' | while IFS= read -r dep_line; do
      dep="$(printf '%s\n' "${dep_line}" | awk '{print $1}')"
      case "${dep}" in
        @rpath/*|@loader_path/*|@executable_path/*) ;;
        /usr/lib/*|/System/Library/*) ;;
        /*)
          echo "ERROR: ${artifact}: ${file} has non-system absolute Mach-O dependency ${dep}"
          exit 1
          ;;
      esac
    done

    "${OTOOL}" -l "${file}" 2>/dev/null |
      awk '
        $1 == "cmd" && $2 == "LC_RPATH" { in_rpath = 1; next }
        in_rpath && $1 == "path" { print $2; in_rpath = 0 }
      ' | while IFS= read -r rpath; do
        case "${rpath}" in
          @loader_path*|@executable_path*) ;;
          "")
            ;;
          *)
            echo "ERROR: ${artifact}: ${file} has non-relocatable Darwin rpath ${rpath}"
            exit 1
            ;;
        esac
      done
  done
}

verify_extracted_consumer() {
  target="$1"
  pkg_root="$2"
  pkg_lib_dir="$3"
  consumer_dir="${TMP_DIR}/consumer-${target}"
  cmake_toolchain="${ROOT_DIR}/cmake/toolchains/bootlin-linux.cmake"

  case "${target}" in
    arm64-apple-darwin)
      cmake_toolchain="${ROOT_DIR}/cmake/toolchains/osxcross-darwin.cmake"
      ;;
  esac

  rm -rf "${consumer_dir}"
  mkdir -p "${consumer_dir}"
  cat > "${consumer_dir}/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.16)
project(softline_extracted_consumer LANGUAGES C)
find_package(softline REQUIRED CONFIG)
add_executable(softline_extracted_consumer main.c)
target_link_libraries(softline_extracted_consumer PRIVATE softline::softline)
EOF
  cat > "${consumer_dir}/main.c" <<'EOF'
#include "softline/softline.h"

int main(void) {
  sl_config_t config;
  sl_config_init(&config);
  return config.input_fd == 0 ? 0 : 1;
}
EOF

  set -- "-DCMAKE_TOOLCHAIN_FILE=${cmake_toolchain}"
  case "${target}" in
    arm64-apple-darwin) ;;
    *) set -- "$@" "-DSL_TARGET_ID=${target}" ;;
  esac
  cmake -S "${consumer_dir}" -B "${consumer_dir}/cmake-build" -G Ninja \
    "$@" \
    -DCMAKE_PREFIX_PATH="${pkg_root}" \
    -Dsoftline_DIR="${pkg_lib_dir}/cmake/softline"
  cmake --build "${consumer_dir}/cmake-build"

  PKG_CONFIG_PATH="${pkg_lib_dir}/pkgconfig"
  export PKG_CONFIG_PATH
  if [ "${target}" = "arm64-apple-darwin" ]; then
    if [ -z "${LINKER:-}" ]; then
      echo "ERROR: ${target}: target linker unavailable for pkg-config consumer" >&2
      exit 1
    fi
    PATH="$(dirname "${LINKER}"):${PATH}" "${CC}" --ld-path="${LINKER}" \
      -std=c89 -Wall -Wextra -Wpedantic -Werror \
      $(pkg-config --cflags softline) "${consumer_dir}/main.c" \
      $(pkg-config --libs softline) -o "${consumer_dir}/pkg-config-consumer"
  else
    "${CC}" -std=c89 -Wall -Wextra -Wpedantic -Werror \
      $(pkg-config --cflags softline) "${consumer_dir}/main.c" \
      $(pkg-config --libs softline) -o "${consumer_dir}/pkg-config-consumer"
  fi

  if [ "${target}" = "x86_64-linux-gnu" ]; then
    LD_LIBRARY_PATH="${pkg_lib_dir}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
      "${consumer_dir}/cmake-build/softline_extracted_consumer"
    LD_LIBRARY_PATH="${pkg_lib_dir}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
      "${consumer_dir}/pkg-config-consumer"
  fi
}

echo "Verifying checksums..."
cd "${DIST_DIR}"
if command -v sha256sum >/dev/null 2>&1; then
  sha256sum -c "${CHECKSUM_FILE}"
elif command -v shasum >/dev/null 2>&1; then
  shasum -a 256 -c "${CHECKSUM_FILE}"
else
  echo "ERROR: sha256sum or shasum is required to verify checksums"
  exit 1
fi
cd "${ROOT_DIR}"

for artifact in "${DIST_DIR}"/softline-*; do
  [ -f "${artifact}" ] || continue
  name="$(basename "${artifact}")"
  case "${name}" in
    *-CHECKSUMS) continue ;;
    *.tar.gz|*.rockspec|*.src.rock) ;;
    *) continue ;;
  esac
  if ! awk '{print $NF}' "${CHECKSUM_FILE}" | grep -Fx "${name}" >/dev/null; then
    echo "ERROR: release artifact omitted from checksum manifest: ${name}"
    exit 1
  fi
done

echo "Verifying package layouts..."
for archive in "${DIST_DIR}"/softline-*.tar.gz; do
  [ -f "${archive}" ] || continue
  basename="$(basename "${archive}" .tar.gz)"
  echo "  Checking ${basename}..."
  if [ "${basename}" = "softline-lua-${VERSION}" ]; then
    echo "  ${basename}: Lua source archive, verified by validate-luarocks"
    continue
  fi
  if [ "${basename}" = "softline-${VERSION}" ]; then
    "${ROOT_DIR}/scripts/package-source-smoke.sh"
    echo "  ${basename}: OK"
    continue
  fi

  target="$(target_from_basename "${basename}")"
  load_tools "${target}"

  rm -rf "${TMP_DIR:?}"/*
  tar -xzf "${archive}" -C "${TMP_DIR}"

  root_count="$(ls -1 "${TMP_DIR}" | wc -l)"
  if [ "${root_count}" -ne 1 ]; then
    echo "ERROR: archive ${basename} has ${root_count} top-level entries (expected 1)"
    exit 1
  fi

  pkg_root="${TMP_DIR}/$(ls -1 "${TMP_DIR}")"
  scan_for_local_paths "${basename}" "${pkg_root}"

  if [ ! -d "${pkg_root}/include" ]; then
    echo "ERROR: ${basename} missing include/"
    exit 1
  fi

  if [ -d "${pkg_root}/lib" ]; then
    pkg_lib_dir="${pkg_root}/lib"
  elif [ -d "${pkg_root}/lib64" ]; then
    pkg_lib_dir="${pkg_root}/lib64"
  else
    echo "ERROR: ${basename} missing lib/ or lib64/"
    exit 1
  fi

  case "${TARGET_OS}" in
    linux) verify_elf_metadata "${basename}" "${pkg_root}" ;;
    darwin) verify_darwin_metadata "${basename}" "${pkg_root}" ;;
    *)
      echo "ERROR: ${basename}: unknown target OS for ${target}"
      exit 1
      ;;
  esac

  pkg_config_file="${pkg_lib_dir}/pkgconfig/softline.pc"
  if [ ! -f "${pkg_config_file}" ]; then
    echo "ERROR: ${basename} missing pkg-config metadata"
    exit 1
  fi

  if grep -F "${ROOT_DIR}" "${pkg_config_file}" >/dev/null; then
    echo "ERROR: ${basename} pkg-config metadata contains local build path"
    exit 1
  fi

  if ! command -v pkg-config >/dev/null 2>&1; then
    echo "ERROR: pkg-config is required to verify package metadata"
    exit 1
  fi
  PKG_CONFIG_PATH="${pkg_lib_dir}/pkgconfig"
  export PKG_CONFIG_PATH
  pkg-config --exists softline
  case "$(pkg-config --cflags softline)" in
    *"${pkg_root}"*) ;;
    *)
      echo "ERROR: ${basename} pkg-config cflags do not point inside package"
      exit 1
      ;;
  esac
  case "$(pkg-config --libs softline)" in
    *"${pkg_root}"*) ;;
    *)
      echo "ERROR: ${basename} pkg-config libs do not point inside package"
      exit 1
      ;;
  esac

  if [ ! -f "${pkg_root}/share/doc/softline/LICENSE" ]; then
    echo "ERROR: ${basename} missing softline license"
    exit 1
  fi

  if [ ! -f "${pkg_root}/share/doc/softline/README.md" ]; then
    echo "ERROR: ${basename} missing softline README"
    exit 1
  fi

  metadata_file="${pkg_root}/share/softline/package-metadata.json"
  if [ ! -f "${metadata_file}" ]; then
    echo "ERROR: ${basename} missing package metadata"
    exit 1
  fi
  if grep -F "${ROOT_DIR}" "${metadata_file}" >/dev/null; then
    echo "ERROR: ${basename} package metadata contains local build path"
    exit 1
  fi
  if ! grep -F "\"target_id\": \"${target}\"" "${metadata_file}" >/dev/null; then
    echo "ERROR: ${basename} package metadata target_id mismatch"
    exit 1
  fi
  if ! grep -F "\"pkg_config\": \"softline\"" "${metadata_file}" >/dev/null; then
    echo "ERROR: ${basename} package metadata missing pkg-config identity"
    exit 1
  fi

  verify_extracted_consumer "${target}" "${pkg_root}" "${pkg_lib_dir}"

  echo "  ${basename}: OK"
done

if [ -f "${DIST_DIR}/softline-${VERSION}-1.src.rock" ] ||
   [ -f "${DIST_DIR}/softline-${VERSION}-1.rockspec" ] ||
   [ -f "${DIST_DIR}/softline-lua-${VERSION}.tar.gz" ]; then
  "${ROOT_DIR}/scripts/validate_luarocks.sh"
fi

"${ROOT_DIR}/scripts/verify-release-privacy.sh"

echo "Package verification passed."
