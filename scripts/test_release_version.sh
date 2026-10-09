#!/usr/bin/env bash
case ${BASH_SOURCE[0]} in
  */*) source "${BASH_SOURCE[0]%/*}/require-host-bash.sh" ;;
  *) source ./require-host-bash.sh ;;
esac || exit $?
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "${ROOT_DIR}/build"
TMP_DIR="$(mktemp -d "${ROOT_DIR}/build/test_release_version.XXXXXX")"
trap 'rm -rf "${TMP_DIR}"' EXIT

make_fixture() {
  fixture="$1"
  mkdir -p "${fixture}/scripts"
  cp "${ROOT_DIR}/scripts/release_version.sh" "${fixture}/scripts/"
cp "${ROOT_DIR}/scripts/require-host-bash.sh" "${fixture}/scripts/"
}

make_cmake_fixture() {
  fixture="$1"
  make_fixture "${fixture}"
  mkdir -p "${fixture}/cmake"
  cp "${ROOT_DIR}/cmake/softline_version.cmake" "${fixture}/cmake/"
  cp "${ROOT_DIR}/cmake/softline_version.h.in" "${fixture}/cmake/"
  cat > "${fixture}/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.16)
project(softline_version_probe VERSION 0.0.0 LANGUAGES NONE)
include(cmake/softline_version.cmake)
file(WRITE "${CMAKE_BINARY_DIR}/detected-version.txt" "${PROJECT_VERSION}\n")
EOF
}

init_git_fixture() {
  fixture="$1"
  make_fixture "${fixture}"
  (
    cd "${fixture}"
    git init -q
    git config user.email test@example.invalid
    git config user.name "Test User"
    echo "ignored in git worktrees" > VERSION
    git add scripts/release_version.sh
    git commit -q -m "test: seed"
  )
}

assert_eq() {
  expected="$1"
  actual="$2"
  label="$3"
  if [ "${expected}" != "${actual}" ]; then
    echo "ERROR: ${label}: expected ${expected}, got ${actual}" >&2
    exit 1
  fi
}

plain="${TMP_DIR}/plain"
make_fixture "${plain}"
echo "1.2.3" > "${plain}/VERSION"
assert_eq "1.2.3" "$(SL_VERSION_ROOT="${plain}" bash "${plain}/scripts/release_version.sh")" "source VERSION"

repo="${TMP_DIR}/repo"
init_git_fixture "${repo}"
assert_eq "0.0.0" "$(SL_VERSION_ROOT="${repo}" bash "${repo}/scripts/release_version.sh")" "git worktree without exact tag"

(
  cd "${repo}"
  git tag -a v9.9.9 -m "annotated tag ignored"
)
assert_eq "0.0.0" "$(SL_VERSION_ROOT="${repo}" bash "${repo}/scripts/release_version.sh")" "annotated tag ignored"

prerelease_repo="${TMP_DIR}/prerelease"
init_git_fixture "${prerelease_repo}"
(
  cd "${prerelease_repo}"
  git tag v1.2.3-rc1
)
assert_eq "0.0.0" "$(SL_VERSION_ROOT="${prerelease_repo}" bash "${prerelease_repo}/scripts/release_version.sh")" "prerelease-like lightweight tag ignored"

parent_repo="${TMP_DIR}/parent"
child_archive="${parent_repo}/softline-archive"
mkdir -p "${parent_repo}"
(
  cd "${parent_repo}"
  git init -q
  git config user.email test@example.invalid
  git config user.name "Test User"
  echo "parent" > README
  git add README
  git commit -q -m "test: parent"
  git tag v9.9.9
)
make_cmake_fixture "${child_archive}"
echo "3.4.5" > "${child_archive}/VERSION"
assert_eq "3.4.5" "$(SL_VERSION_ROOT="${child_archive}" bash "${child_archive}/scripts/release_version.sh")" "source VERSION under parent git repo"

if command -v cmake >/dev/null 2>&1; then
  cmake -S "${child_archive}" -B "${TMP_DIR}/cmake-build" >/dev/null
  assert_eq "3.4.5" "$(cat "${TMP_DIR}/cmake-build/detected-version.txt")" "CMake source VERSION under parent git repo"
fi

(
  cd "${repo}"
  git tag v1.2.3-rc1
  git tag v1.2.4
)
assert_eq "1.2.4" "$(SL_VERSION_ROOT="${repo}" bash "${repo}/scripts/release_version.sh")" "lightweight tag"
assert_eq "2.0.0" "$(SL_VERSION_ROOT="${repo}" SL_VERSION_OVERRIDE=v2.0.0 bash "${repo}/scripts/release_version.sh")" "override"

echo "Release version tests passed."
