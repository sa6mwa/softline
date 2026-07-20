#!/usr/bin/env sh
set -eu

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
# This impossible project version is permanently reserved for this contract
# check. It is never a releasable softline version. Deleting it recovers an
# interrupted prior contract run before exact-tag detection or artifact work.
RESERVED_TAG="v99.99.99"

cleanup() {
  # Deliberately remove only the permanently reserved test-only tag.
  git -C "${ROOT_DIR}" tag -d "${RESERVED_TAG}" >/dev/null 2>&1 || true
}
trap cleanup EXIT HUP INT TERM

git -C "${ROOT_DIR}" rev-parse --is-inside-work-tree >/dev/null
git -C "${ROOT_DIR}" tag -d "${RESERVED_TAG}" >/dev/null 2>&1 || true

exact_tag=""
for tag in $(git -C "${ROOT_DIR}" tag --points-at HEAD); do
  if ! printf '%s\n' "${tag}" | grep -Eq '^v[0-9]+\.[0-9]+\.[0-9]+$'; then
    continue
  fi
  if [ "$(git -C "${ROOT_DIR}" cat-file -t "refs/tags/${tag}")" != commit ]; then
    echo "ERROR: exact release tag ${tag} must be lightweight" >&2
    exit 1
  fi
  if [ -n "${exact_tag}" ]; then
    echo "ERROR: multiple exact lightweight release tags on HEAD: ${exact_tag} ${tag}" >&2
    exit 1
  fi
  exact_tag="${tag}"
done

if [ -n "${exact_tag}" ]; then
  expected="${exact_tag#v}"
else
  git -C "${ROOT_DIR}" -c tag.gpgSign=false tag "${RESERVED_TAG}"
  if [ "$(git -C "${ROOT_DIR}" cat-file -t "refs/tags/${RESERVED_TAG}")" != commit ]; then
    echo "ERROR: ${RESERVED_TAG} must be a lightweight tag" >&2
    exit 1
  fi
  expected="${RESERVED_TAG#v}"
fi

actual="$(make -s -C "${ROOT_DIR}" print-release-version)"
if [ "${actual}" != "${expected}" ]; then
  echo "ERROR: release version contract expected ${expected}, got ${actual}" >&2
  exit 1
fi

echo "Lifecycle version contract passed: ${expected}"
