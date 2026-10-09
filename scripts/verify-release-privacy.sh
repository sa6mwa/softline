#!/usr/bin/env bash
case ${BASH_SOURCE[0]} in
  */*) source "${BASH_SOURCE[0]%/*}/require-host-bash.sh" ;;
  *) source ./require-host-bash.sh ;;
esac || exit $?
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
args=()
[[ -z ${SOFTLINE_CHECKSUM_FILE:-} ]] || args=(--manifest "$SOFTLINE_CHECKSUM_FILE")
exec "${ROOT_DIR}/scripts/verify-artifact-runtime.sh" "${SOFTLINE_DIST_DIR:-${ROOT_DIR}/dist}" "${args[@]}"
