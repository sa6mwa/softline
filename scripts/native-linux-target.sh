#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}/require-host-bash.sh" || exit $?
set -euo pipefail
# Project host support is separate from the unmodified lifecycle resolver.
system=${SOFTLINE_HOST_SYSTEM:-$(uname -s)}
machine=${SOFTLINE_HOST_MACHINE:-$(uname -m)}
[[ "$system" == Linux ]] || { echo "ERROR: native Bootlin target unavailable for non-Linux host: $system" >&2; exit 1; }
case "$machine" in
  x86_64|amd64) printf '%s\n' x86_64-linux-gnu ;;
  *) echo "ERROR: unsupported Linux host architecture: $machine; pinned Bootlin compiler executables are x86_64-hosted" >&2; exit 1 ;;
esac
