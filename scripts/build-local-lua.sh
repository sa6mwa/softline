#!/bin/sh
set -eu
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_NAME="${2:-local-lua}"
case "${BUILD_NAME}" in
  local-lua|local-lua-debug) ;;
  *) echo "ERROR: invalid local Lua build name" >&2; exit 2 ;;
esac
cmake -S "${ROOT_DIR}/cmake/local-lua" -B "${ROOT_DIR}/build/${BUILD_NAME}" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="${ROOT_DIR}/cmake/toolchains/bootlin-linux.cmake" \
  -DSOFTLINE_LOCAL_LIBRARY_DIR="$1"
cmake --build "${ROOT_DIR}/build/${BUILD_NAME}"
