#!/bin/sh
set -eu

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
TMP_DIR="$(mktemp -d)"
VERSION="$(sh "${ROOT_DIR}/scripts/release_version.sh")"
SOFTLINE_ABI_VERSION="${SOFTLINE_ABI_VERSION:-1}"
BOOTLIN_TOOLCHAIN="${ROOT_DIR}/cmake/toolchains/bootlin-linux.cmake"
BOOTLIN_TARGET=""
BOOTLIN_TOOLCHAIN_ARG=""
BOOTLIN_TARGET_ARG=""
CONSUMER_CC="${CC:-cc}"
trap 'rm -rf "${TMP_DIR}"' EXIT

if BOOTLIN_TARGET="$("${ROOT_DIR}/scripts/cpkt-toolchains.sh" native-linux-target 2>/dev/null)"; then
  "${ROOT_DIR}/scripts/cpkt-toolchains.sh" ensure "${BOOTLIN_TARGET}" >/dev/null
  eval "$("${ROOT_DIR}/scripts/cpkt-toolchains.sh" env "${BOOTLIN_TARGET}")"
  BOOTLIN_TOOLCHAIN_ARG="-DCMAKE_TOOLCHAIN_FILE=${BOOTLIN_TOOLCHAIN}"
  BOOTLIN_TARGET_ARG="-DSL_TARGET_ID=${BOOTLIN_TARGET}"
  CONSUMER_CC="${CC}"
else
  echo "No supported native Bootlin Linux target selected; using host toolchain for package consumer smoke" >&2
fi

APP_DIR="${TMP_DIR}/consumer"

mkdir -p "${APP_DIR}"
cat > "${APP_DIR}/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.16)
project(softline_consumer_smoke LANGUAGES C)

find_package(softline REQUIRED CONFIG)

add_executable(consumer main.c)
target_link_libraries(consumer PRIVATE softline::softline)
EOF

cat > "${APP_DIR}/main.c" <<'EOF'
#include "softline/softline.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

struct stream_state {
  const char *text;
  int done;
};

static int one_chunk(sl_t *sl, void *userdata, const char **chunk,
                     size_t *len) {
  struct stream_state *state;
  (void)sl;
  state = (struct stream_state *)userdata;
  if (!state || state->done) {
    *chunk = NULL;
    *len = 0;
    return SL_OK;
  }
  state->done = 1;
  *chunk = state->text;
  *len = strlen(state->text);
  return SL_OK;
}

int main(void) {
  int input_pipe[2];
  int output_pipe[2];
  sl_config_t cfg;
  sl_t *sl;
  char *line;
  struct stream_state stream;

  if (pipe(input_pipe) != 0 || pipe(output_pipe) != 0)
    return 2;
  if (write(input_pipe[1], "hello\n", 6) != 6)
    return 3;
  close(input_pipe[1]);

  sl_config_init(&cfg);
  cfg.input_fd = input_pipe[0];
  cfg.output_fd = output_pipe[1];
  sl = sl_create_with_config(&cfg);
  if (!sl)
    return 4;

  if (sl->last_readline_status(sl) != SL_READLINE_NONE)
    return 5;
  if (sl_history_add(sl, "history") != SL_OK)
    return 6;
  if (sl_set_buffer(sl, "draft") != SL_OK)
    return 7;
  if (strcmp(sl_buffer(sl), "draft") != 0)
    return 8;
  if (sl_set_cursor(sl, 2) != SL_OK || sl_cursor(sl) != 2)
    return 9;
  stream.text = "x";
  stream.done = 0;
  if (sl->print_above(sl, one_chunk, &stream) != SL_OK)
    return 10;

  line = sl->readline(sl, "p> ");
  if (!line)
    return 11;
  if (strcmp(line, "hello") != 0)
    return 12;
  if (sl_last_readline_status(sl) != SL_READLINE_SUBMITTED)
    return 13;

  sl_free_string(sl, line);
  sl_destroy(sl);
  close(input_pipe[0]);
  close(output_pipe[0]);
  close(output_pipe[1]);
  return 0;
}
EOF

install_disabled_smoke() {
  build_dir="${TMP_DIR}/softline-build-install-disabled"
  install_dir="${TMP_DIR}/install-disabled"

  cmake -S "${ROOT_DIR}" -B "${build_dir}" \
    -G Ninja \
    ${BOOTLIN_TOOLCHAIN_ARG:+"${BOOTLIN_TOOLCHAIN_ARG}"} \
    ${BOOTLIN_TARGET_ARG:+"${BOOTLIN_TARGET_ARG}"} \
    -DSL_BUILD_EXAMPLES=OFF \
    -DSL_BUILD_TESTS=OFF \
    -DSL_INSTALL=OFF \
    -DCMAKE_INSTALL_PREFIX="${install_dir}"
  cmake --build "${build_dir}"
  cmake --install "${build_dir}" --prefix "${install_dir}"
  if [ -n "$(find "${install_dir}" -type f -print 2>/dev/null)" ]; then
    echo "ERROR: SL_INSTALL=OFF installed files"
    exit 1
  fi

  echo "  install-disabled: OK"
}

verify_shared_abi() {
  libdir="$1"
  shared_lib="${libdir}/libsoftline.so"
  abi_lib="${libdir}/libsoftline.so.${SOFTLINE_ABI_VERSION}"
  soname=""

  if [ -z "${BOOTLIN_TARGET}" ] && [ -e "${libdir}/libsoftline.dylib" ]; then
    return
  fi
  if [ ! -e "${shared_lib}" ]; then
    echo "ERROR: shared install missing libsoftline.so"
    exit 1
  fi
  if [ ! -e "${abi_lib}" ]; then
    echo "ERROR: shared install missing ABI symlink ${abi_lib}"
    exit 1
  fi
  if [ -n "${BOOTLIN_TARGET}" ] && { [ -z "${READELF:-}" ] || [ ! -x "${READELF}" ]; }; then
    echo "ERROR: lifecycle readelf is required for package consumer ABI verification" >&2
    exit 1
  fi
  if [ -z "${READELF:-}" ] || [ ! -x "${READELF}" ]; then
    READELF="$(command -v readelf || true)"
  fi
  if [ -z "${READELF}" ]; then
    echo "SKIP: shared SONAME check requires readelf" >&2
    return
  fi
  soname="$("${READELF}" -d "${shared_lib}" |
    sed -n 's/.*Library soname: \[\(.*\)\].*/\1/p')"
  if [ "${soname}" != "libsoftline.so.${SOFTLINE_ABI_VERSION}" ]; then
    echo "ERROR: shared install SONAME ${soname} does not match ABI ${SOFTLINE_ABI_VERSION}"
    exit 1
  fi
}

smoke_mode() {
  mode="$1"
  build_static="$2"
  build_shared="$3"
  install_libdir_arg="${4:-}"
  install_dir="${TMP_DIR}/install-${mode}"
  build_dir="${TMP_DIR}/softline-build-${mode}"
  app_build_dir="${TMP_DIR}/consumer-build-${mode}"
  pkg_consumer="${TMP_DIR}/pkg-config-consumer-${mode}"
  configure_libdir_arg=""
  install_libdir=""

  if [ -n "${install_libdir_arg}" ]; then
    configure_libdir_arg="-DCMAKE_INSTALL_LIBDIR=${install_libdir_arg}"
  fi

  cmake -S "${ROOT_DIR}" -B "${build_dir}" \
    -G Ninja \
    ${BOOTLIN_TOOLCHAIN_ARG:+"${BOOTLIN_TOOLCHAIN_ARG}"} \
    ${BOOTLIN_TARGET_ARG:+"${BOOTLIN_TARGET_ARG}"} \
    -DSL_BUILD_EXAMPLES=OFF \
    -DSL_BUILD_TESTS=OFF \
    -DSL_BUILD_STATIC="${build_static}" \
    -DSL_BUILD_SHARED="${build_shared}" \
    -DSL_INSTALL=ON \
    -DCMAKE_INSTALL_PREFIX="${install_dir}" \
    ${configure_libdir_arg}
  cmake --build "${build_dir}"
  cmake --install "${build_dir}"

  if [ -d "${install_dir}/lib/pkgconfig" ]; then
    install_libdir="${install_dir}/lib"
  elif [ -d "${install_dir}/lib64/pkgconfig" ]; then
    install_libdir="${install_dir}/lib64"
  else
    echo "ERROR: install tree missing lib/pkgconfig or lib64/pkgconfig"
    exit 1
  fi
  if [ "${build_shared}" = "ON" ]; then
    verify_shared_abi "${install_libdir}"
  fi

  cmake -S "${APP_DIR}" -B "${app_build_dir}" \
    -G Ninja \
    ${BOOTLIN_TOOLCHAIN_ARG:+"${BOOTLIN_TOOLCHAIN_ARG}"} \
    ${BOOTLIN_TARGET_ARG:+"${BOOTLIN_TARGET_ARG}"} \
    -DCMAKE_PREFIX_PATH="${install_dir}" \
    -Dsoftline_DIR="${install_libdir}/cmake/softline"
  cmake --build "${app_build_dir}"
  "${app_build_dir}/consumer"

  if ! command -v pkg-config >/dev/null 2>&1; then
    echo "ERROR: pkg-config is required for package consumer smoke"
    exit 1
  fi
  PKG_CONFIG_PATH="${install_libdir}/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"
  export PKG_CONFIG_PATH
  pkg-config --exists softline
  test "$(pkg-config --modversion softline)" = "${VERSION}"
  "${CONSUMER_CC}" -std=c89 -Wall -Wextra -Wpedantic -Werror \
    $(pkg-config --cflags softline) "${APP_DIR}/main.c" \
    $(pkg-config --libs softline) -Wl,-rpath,"${install_libdir}" \
    -o "${pkg_consumer}"
  "${pkg_consumer}"

  echo "  ${mode}: OK"
}

install_disabled_smoke
smoke_mode both ON ON
if [ "${1:-}" = "--version-override-only" ]; then
  echo "Installed package consumer smoke test passed."
  exit 0
fi
smoke_mode static-only ON OFF
smoke_mode shared-only OFF ON
smoke_mode both-lib64 ON ON lib64
SL_VERSION_OVERRIDE=v1.2.3 sh "$0" --version-override-only

echo "Installed package consumer smoke test passed."
