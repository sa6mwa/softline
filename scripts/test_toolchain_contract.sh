#!/usr/bin/env bash
case ${BASH_SOURCE[0]} in
  */*) source "${BASH_SOURCE[0]%/*}/require-host-bash.sh" ;;
  *) source ./require-host-bash.sh ;;
esac || exit $?
set -euo pipefail
ROOT_DIR=$(cd "${BASH_SOURCE[0]%/*}/.." && pwd)
mkdir -p "$ROOT_DIR/build/toolchain-checks"
export TMPDIR="$ROOT_DIR/build/toolchain-checks"
"$ROOT_DIR/scripts/test-cpkt-toolchain-resolvers.sh"
"$ROOT_DIR/scripts/test-cpkt-aflpp-resolver.sh"
resolver="$ROOT_DIR/scripts/native-linux-target.sh"
[[ $(SOFTLINE_HOST_SYSTEM=Linux SOFTLINE_HOST_MACHINE=x86_64 "$resolver") == x86_64-linux-gnu ]]
for machine in aarch64 armv7l riscv64; do
  if SOFTLINE_HOST_SYSTEM=Linux SOFTLINE_HOST_MACHINE="$machine" "$resolver" >/dev/null 2>&1; then
    echo "ERROR: unsupported native compiler host accepted: $machine" >&2; exit 1
  fi
done
if SOFTLINE_HOST_SYSTEM=Darwin SOFTLINE_HOST_MACHINE=arm64 "$resolver" >/dev/null 2>&1; then
  echo 'ERROR: non-Linux host accepted as a native Bootlin host' >&2; exit 1
fi
echo 'Toolchain fixture contracts passed without acquiring real dependencies.'
