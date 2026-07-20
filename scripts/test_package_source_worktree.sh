#!/bin/sh
set -eu

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
TMP_DIR="$(mktemp -d)"
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
printf 'linked worktree fixture\n' > "${REPO_DIR}/README.md"

git -C "${REPO_DIR}" init -q
git -C "${REPO_DIR}" config user.email test@example.invalid
git -C "${REPO_DIR}" config user.name "Test User"
git -C "${REPO_DIR}" add README.md scripts
git -C "${REPO_DIR}" commit -q -m "test: seed source package fixture"
git -C "${REPO_DIR}" worktree add --detach -q "${WORKTREE_DIR}" HEAD

"${WORKTREE_DIR}/scripts/package-source.sh" >/dev/null
test -f "${WORKTREE_DIR}/dist/softline-0.0.0.tar.gz"

echo "Linked worktree source package test passed."
