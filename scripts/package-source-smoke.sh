#!/usr/bin/env bash
case ${BASH_SOURCE[0]} in
  */*) source "${BASH_SOURCE[0]%/*}/require-host-bash.sh" ;;
  *) source ./require-host-bash.sh ;;
esac || exit $?
set -euo pipefail
ROOT_DIR=$(cd "${BASH_SOURCE[0]%/*}/.." && pwd)
DIST_DIR=${SOFTLINE_DIST_DIR:-$ROOT_DIR/dist}
mkdir -p "$ROOT_DIR/build"
TMP_DIR=$(mktemp -d "$ROOT_DIR/build/package-source-smoke.XXXXXX")
trap 'rm -rf "$TMP_DIR"' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
VERSION=$("$ROOT_DIR/scripts/release_version.sh")
source=$("$ROOT_DIR/scripts/extract-release-source.sh" "$DIST_DIR/softline-$VERSION.tar.gz" "$TMP_DIR")
cmake -S "$source" -B "$source/build" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$source/cmake/toolchains/bootlin-linux.cmake" \
  -DCMAKE_BUILD_TYPE=Release -DSL_BUILD_TESTS=ON -DSL_BUILD_EXAMPLES=ON
cmake --build "$source/build"
ctest --test-dir "$source/build" --stop-on-failure --output-on-failure --no-tests=error
echo 'Source archive Release reconstruction and tests passed.'
