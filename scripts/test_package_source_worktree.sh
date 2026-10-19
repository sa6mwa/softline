#!/usr/bin/env bash
case ${BASH_SOURCE[0]} in
  */*) source "${BASH_SOURCE[0]%/*}/require-host-bash.sh" ;;
  *) source ./require-host-bash.sh ;;
esac || exit $?
set -euo pipefail

# The disposable worktree owns its version and output directory.
unset SL_VERSION_OVERRIDE SL_VERSION_ROOT SOFTLINE_DIST_DIR

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "${ROOT_DIR}/build"
TMP_DIR="$(mktemp -d "${ROOT_DIR}/build/test_package_source_worktree.XXXXXX")"
REPO_DIR="${TMP_DIR}/repo"
WORKTREE_DIR="${TMP_DIR}/linked-worktree"

cleanup() {
  if [ -d "${REPO_DIR}/.git" ]; then
    git -C "${REPO_DIR}" worktree remove --force "${WORKTREE_DIR}" >/dev/null 2>&1 || true
  fi
  rm -rf "${TMP_DIR}"
}
trap cleanup EXIT HUP INT TERM

mkdir -p "${REPO_DIR}/scripts"
cp "${ROOT_DIR}/scripts/package-source.sh" "${REPO_DIR}/scripts/"
cp "${ROOT_DIR}/scripts/release_version.sh" "${REPO_DIR}/scripts/"
cp "${ROOT_DIR}/scripts/require-host-bash.sh" "${REPO_DIR}/scripts/"
cp "${ROOT_DIR}/scripts/require-workspace.sh" "${REPO_DIR}/scripts/"
mkdir -p "${REPO_DIR}/cmake"
cp "${ROOT_DIR}/cmake/softline_workspace.cmake" "${REPO_DIR}/cmake/"
printf 'linked worktree fixture\n' > "${REPO_DIR}/README.md"

git -C "${REPO_DIR}" init -q
git -C "${REPO_DIR}" config user.email test@example.invalid
git -C "${REPO_DIR}" config user.name "Test User"
git -C "${REPO_DIR}" add README.md scripts cmake
git -C "${REPO_DIR}" commit -q -m "test: seed source package fixture"
git -C "${REPO_DIR}" worktree add --detach -q "${WORKTREE_DIR}" HEAD

"${WORKTREE_DIR}/scripts/package-source.sh" >/dev/null
test -f "${WORKTREE_DIR}/dist/softline-0.0.0.tar.gz"
ln -s "$WORKTREE_DIR" "$TMP_DIR/source-alias"
"$TMP_DIR/source-alias/scripts/package-source.sh" >/dev/null

echo "Linked worktree source package test passed."
