#!/bin/sh
set -eu

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

echo "Cleaning build artifacts..."
rm -rf "${BUILD_DIR}"
echo "Cleaning dist artifacts..."
rm -rf "${ROOT_DIR}/dist"
echo "Cleaning cache..."
rm -rf "${ROOT_DIR}/.cache"
echo "Clean complete."