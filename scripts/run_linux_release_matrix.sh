#!/bin/sh
set -eu

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

for target in x86_64-linux-gnu; do
  preset="${target}-release"
  echo "=== Building ${target} ==="
  cmake --preset "${preset}" -S "${ROOT_DIR}"
  cmake --build --preset "${preset}"
  echo "=== Packaging ${target} ==="
  "${ROOT_DIR}/scripts/package.sh"
done

echo "Release matrix build complete."