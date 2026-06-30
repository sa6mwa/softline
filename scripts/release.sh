#!/bin/sh
set -eu

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

make clean
make prerelease-hardening

VERSION="$(sh "${ROOT_DIR}/scripts/release_version.sh")"
echo "Release version: ${VERSION}"
echo "Release artifacts in ${ROOT_DIR}/dist/"
