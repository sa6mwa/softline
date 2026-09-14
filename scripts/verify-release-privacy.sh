#!/bin/sh
set -eu
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
exec python3 "${ROOT_DIR}/scripts/verify_artifact_runtime.py" "${SOFTLINE_DIST_DIR:-${ROOT_DIR}/dist}"
