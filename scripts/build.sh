#!/bin/sh
set -eu

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build/debug"

cmake --preset debug -S "${ROOT_DIR}"
cmake --build --preset debug
echo "Build complete: ${BUILD_DIR}"
