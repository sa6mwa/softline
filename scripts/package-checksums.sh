#!/usr/bin/env bash
case ${BASH_SOURCE[0]} in
  */*) source "${BASH_SOURCE[0]%/*}/require-host-bash.sh" ;;
  *) source ./require-host-bash.sh ;;
esac || exit $?
set -euo pipefail
ROOT_DIR=$(cd "${BASH_SOURCE[0]%/*}/.." && pwd -P)
DIST_DIR=${SOFTLINE_DIST_DIR:-$ROOT_DIR/dist}
VERSION=$("$ROOT_DIR/scripts/release_version.sh")
scope=${1:-all}
source "$ROOT_DIR/scripts/release-targets.sh"
artifacts=()
case "$scope" in
  all)
    manifest="$DIST_DIR/softline-$VERSION-CHECKSUMS"
    read -r -a targets <<< "$SOFTLINE_RELEASE_TARGETS"
    artifacts=("softline-$VERSION.tar.gz" "softline-lua-$VERSION.tar.gz"
      "softline-$VERSION-1.rockspec" "softline-$VERSION-1.src.rock")
    ;;
  binary)
    manifest="$ROOT_DIR/build/package-state/softline-$VERSION-BINARY-CHECKSUMS"
    mapfile -t targets < "$ROOT_DIR/build/package-state/targets"
    ;;
  *) echo 'Usage: package-checksums.sh [all|binary]' >&2; exit 2 ;;
esac
[[ ${#targets[@]} -gt 0 ]] || { echo 'ERROR: empty package target inventory' >&2; exit 1; }
declare -A seen=()
for target in "${targets[@]}"; do
  [[ " $SOFTLINE_RELEASE_TARGETS " == *" $target "* && -n $target && -z ${seen[$target]:-} ]] || {
    echo "ERROR: invalid or duplicate package target: $target" >&2; exit 1;
  }
  seen[$target]=1
  artifacts+=("softline-$VERSION-$target.tar.gz")
done
for artifact in "${artifacts[@]}"; do
  [[ -f "$DIST_DIR/$artifact" && ! -L "$DIST_DIR/$artifact" ]] || {
    echo "ERROR: missing or unsafe required artifact: $artifact" >&2; exit 1;
  }
done
if [[ $scope == all ]]; then
  for artifact in "$DIST_DIR"/softline-*.tar.gz "$DIST_DIR"/softline-*.rockspec "$DIST_DIR"/softline-*.rock "$DIST_DIR"/softline-*-CHECKSUMS; do
    [[ -e "$artifact" ]] || continue
    name=${artifact##*/}
    [[ "$artifact" == "$manifest" ]] && continue
    found=0
    for expected in "${artifacts[@]}"; do [[ "$name" != "$expected" ]] || found=1; done
    ((found)) || { echo "ERROR: stale or unlisted release artifact: $name" >&2; exit 1; }
  done
fi
[[ -d "$DIST_DIR" ]] || { echo "ERROR: missing dist directory: $DIST_DIR" >&2; exit 1; }
temporary="$manifest.tmp"
trap 'rm -f -- "$temporary"' EXIT
(
  cd "$DIST_DIR"
  if command -v sha256sum >/dev/null; then sha256sum "${artifacts[@]}";
  else shasum -a 256 "${artifacts[@]}"; fi
) > "$temporary"
LC_ALL=C sort -k2 "$temporary" -o "$temporary"
mv "$temporary" "$manifest"
echo "Checksums written to $manifest ($scope inventory)"
