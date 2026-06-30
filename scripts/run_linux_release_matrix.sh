#!/bin/sh
set -eu

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

"${ROOT_DIR}/scripts/package.sh"
"${ROOT_DIR}/scripts/package-source.sh"
"${ROOT_DIR}/scripts/release_lua_artifacts.sh"
"${ROOT_DIR}/scripts/package-checksums.sh"
"${ROOT_DIR}/scripts/package-verify.sh"

echo "Release matrix build, artifact generation, and verification complete."
