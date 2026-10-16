#!/usr/bin/env bash
case ${BASH_SOURCE[0]} in
  */*) source "${BASH_SOURCE[0]%/*}/require-host-bash.sh" ;;
  *) source ./require-host-bash.sh ;;
esac || exit $?
set -euo pipefail
ROOT_DIR=$(cd "${BASH_SOURCE[0]%/*}/.." && pwd -P)
[[ $# == 3 ]] || { echo 'Usage: package-sdk.sh verified-build-dir target dist-dir' >&2; exit 2; }
build=$1
target=$2
dist=$3
source "$ROOT_DIR/scripts/release-targets.sh"
[[ " $SOFTLINE_RELEASE_TARGETS " == *" $target "* ]] || { echo 'ERROR: unknown SDK target' >&2; exit 2; }
[[ -f "$build/CMakeCache.txt" ]] || { echo 'ERROR: prepare and verify the SDK producer first' >&2; exit 1; }
version=$("$ROOT_DIR/scripts/release_version.sh")
stage="$ROOT_DIR/build/package-stage"
payload="$stage/softline-$version-$target"
SOFTLINE_GENERATED_PATH="$payload" source "${BASH_SOURCE[0]%/*}/require-workspace.sh" || exit $?
mkdir -p "$stage" "$dist"
trap 'rm -rf -- "$payload"' EXIT
rm -rf -- "$payload"
cmake --install "$build" --prefix "$payload"
[[ -f "$payload/include/softline/softline.h" && -f "$payload/share/doc/softline/LICENSE" &&
   -f "$payload/share/softline/package-metadata.json" ]] || { echo 'ERROR: incomplete installed SDK payload' >&2; exit 1; }
[[ -d "$payload/lib" || -d "$payload/lib64" ]] || { echo 'ERROR: installed SDK has no library directory' >&2; exit 1; }
tar --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner \
  --mode='u+rwX,go+rX,go-w' -cf - -C "$stage" "${payload##*/}" | \
  gzip -n > "$dist/softline-$version-$target.tar.gz"
echo "SDK archive: $dist/softline-$version-$target.tar.gz"
