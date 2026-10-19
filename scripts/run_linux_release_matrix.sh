#!/usr/bin/env bash
case ${BASH_SOURCE[0]} in
  */*) source "${BASH_SOURCE[0]%/*}/require-host-bash.sh" ;;
  *) source ./require-host-bash.sh ;;
esac || exit $?
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

"${ROOT_DIR}/scripts/package.sh"
"${ROOT_DIR}/scripts/package-checksums.sh" binary
version=$("${ROOT_DIR}/scripts/release_version.sh")
SOFTLINE_CHECKSUM_FILE="${ROOT_DIR}/build/package-state/softline-${version}-BINARY-CHECKSUMS" \
  "${ROOT_DIR}/scripts/package-verify.sh"

echo "Binary release matrix build, artifact generation, and verification complete."
