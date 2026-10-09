#!/usr/bin/env bash
case ${BASH_SOURCE[0]} in
  */*) source "${BASH_SOURCE[0]%/*}/require-host-bash.sh" ;;
  *) source ./require-host-bash.sh ;;
esac || exit $?
set -euo pipefail
ROOT_DIR=$(cd "${BASH_SOURCE[0]%/*}/.." && pwd -P)
[[ -z ${SL_VERSION_OVERRIDE:-} ]] || {
  echo 'ERROR: final release requires an exact lightweight tag, without SL_VERSION_OVERRIDE' >&2; exit 1;
}
[[ $(git -C "$ROOT_DIR" rev-parse --show-toplevel) == "$ROOT_DIR" ]] || {
  echo 'ERROR: final release requires the owning Git checkout' >&2; exit 1;
}
[[ -z $(git -C "$ROOT_DIR" status --porcelain) ]] || {
  echo 'ERROR: final release requires a clean committed worktree' >&2; exit 1;
}
version=$("$ROOT_DIR/scripts/release_version.sh")
[[ "$version" != 0.0.0 ]] || {
  echo 'ERROR: final release requires one exact lightweight vX.Y.Z tag on HEAD' >&2; exit 1;
}
tag="refs/tags/v$version"
if git -C "$ROOT_DIR" symbolic-ref -q "$tag" >/dev/null; then
  echo 'ERROR: symbolic release tags are not lightweight release identities' >&2; exit 1
fi
[[ $(git -C "$ROOT_DIR" cat-file -t "$tag") == commit &&
   $(git -C "$ROOT_DIR" rev-parse "$tag") == $(git -C "$ROOT_DIR" rev-parse HEAD) ]] || {
  echo 'ERROR: release tag must identify HEAD directly' >&2; exit 1;
}
echo "Final release identity: v$version ($(git -C "$ROOT_DIR" rev-parse --short HEAD))"
