#!/bin/sh
set -eu

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

if [ -n "${SL_VERSION_OVERRIDE:-}" ]; then
  echo "${SL_VERSION_OVERRIDE}"
  exit 0
fi

if [ -f "${ROOT_DIR}/VERSION" ]; then
  head -1 "${ROOT_DIR}/VERSION"
  exit 0
fi

if command -v git >/dev/null 2>&1 && [ -d "${ROOT_DIR}/.git" ]; then
  tag="$(git -C "${ROOT_DIR}" describe --tags --exact-match HEAD 2>/dev/null || true)"
  if [ -n "${tag}" ]; then
    echo "${tag#v}"
    exit 0
  fi
fi

echo "0.0.0"