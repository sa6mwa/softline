#!/bin/sh
set -eu

ROOT_DIR="${SL_VERSION_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}"

normalize_version() {
  raw_version="$1"
  version="${raw_version#v}"

  case "${version}" in
    ""|*[!0-9.]*|.*|*.)
      echo "ERROR: version must be X.Y.Z or vX.Y.Z: ${raw_version}" >&2
      exit 1
      ;;
  esac

  old_ifs="${IFS}"
  IFS=.
  set -- ${version}
  IFS="${old_ifs}"

  if [ "$#" -ne 3 ]; then
    echo "ERROR: version must be X.Y.Z or vX.Y.Z: ${raw_version}" >&2
    exit 1
  fi

  for part in "$1" "$2" "$3"; do
    case "${part}" in
      ""|*[!0-9]*)
        echo "ERROR: version must be X.Y.Z or vX.Y.Z: ${raw_version}" >&2
        exit 1
        ;;
    esac
  done

  echo "${version}"
}

is_git_worktree() {
  if ! command -v git >/dev/null 2>&1; then
    return 1
  fi
  git_root="$(git -C "${ROOT_DIR}" rev-parse --show-toplevel 2>/dev/null || true)"
  [ -n "${git_root}" ] && [ "$(cd "${git_root}" && pwd)" = "$(cd "${ROOT_DIR}" && pwd)" ]
}

is_exact_version_tag() {
  printf '%s\n' "$1" | grep -Eq '^v[0-9]+\.[0-9]+\.[0-9]+$'
}

exact_lightweight_version_tag() {
  tags="$(git -C "${ROOT_DIR}" tag --points-at HEAD 2>/dev/null || true)"
  found=""
  for tag in ${tags}; do
    if is_exact_version_tag "${tag}" &&
      [ "$(git -C "${ROOT_DIR}" cat-file -t "refs/tags/${tag}" 2>/dev/null || true)" = "commit" ]; then
      if [ -n "${found}" ]; then
        echo "ERROR: multiple exact lightweight version tags on HEAD: ${found} ${tag}" >&2
        exit 1
      fi
      found="${tag}"
    fi
  done
  if [ -n "${found}" ]; then
    normalize_version "${found}"
  fi
}

if [ -n "${SL_VERSION_OVERRIDE:-}" ]; then
  normalize_version "${SL_VERSION_OVERRIDE}"
  exit 0
fi

if is_git_worktree; then
  tag="$(exact_lightweight_version_tag)"
  if [ -n "${tag}" ]; then
    echo "${tag}"
    exit 0
  fi
  echo "0.0.0"
  exit 0
fi

if [ -f "${ROOT_DIR}/VERSION" ]; then
  normalize_version "$(head -1 "${ROOT_DIR}/VERSION")"
  exit 0
fi

echo "0.0.0"
