#!/bin/sh
set -eu

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build/debug"

if [ ! -d "${BUILD_DIR}" ]; then
  cmake --preset debug -S "${ROOT_DIR}"
  cmake --build --preset debug
fi

cd "${BUILD_DIR}" && ctest --output-on-failure "$@"