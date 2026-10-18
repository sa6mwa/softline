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
# Prefer the newest complete Darwin prefix; a broken newer installation must
# not shadow a usable older one. Explicit prefix selection never falls back.
(
  work=$(mktemp -d "$TMPDIR/darwin resolver.XXXXXXXX")
  trap 'rm -rf -- "$work"' EXIT
  export OSXCROSS_ROOT="$work/osxcross" CPKT_TOOLCHAIN_CACHE="$work/cache"
  unset CPKT_OSXCROSS_HOST
  mkdir -p "$OSXCROSS_ROOT/bin"
  older=arm64-apple-darwin25.2
  newer=arm64-apple-darwin25.4
  for prefix in "$older" "$newer"; do
    for tool in clang clang++ ld ar ranlib strip nm otool install_name_tool; do
      printf '#!/bin/sh\nexit 0\n' > "$OSXCROSS_ROOT/bin/$prefix-$tool"
      chmod +x "$OSXCROSS_ROOT/bin/$prefix-$tool"
    done
  done
  resolver="$ROOT_DIR/scripts/cpkt-toolchains.sh"
  description=$("$resolver" discover arm64-apple-darwin)
  revision=$(sed -n 's/^mig_revision=//p' <<< "$description")
  [[ -n "$revision" ]]
  mig="$CPKT_TOOLCHAIN_CACHE/roots/host-mig-puredarwin-$revision-x86_64-linux-gnu"
  mkdir -p "$mig/bin" "$mig/libexec"
  for tool in bin/mig bin/mig-upstream libexec/migcom; do
    printf '#!/bin/sh\nexit 0\n' > "$mig/$tool"
    chmod +x "$mig/$tool"
  done
  touch "$mig/TOOLCHAIN"
  check_prefix() {
    description=$("$resolver" discover arm64-apple-darwin)
    grep -Fxq status=ready <<< "$description"
    grep -Fxq "prefix=$1" <<< "$description" || {
      printf 'ERROR: expected Darwin prefix %s, got:\n%s\n' "$1" "$description" >&2; exit 1;
    }
    environment=$("$resolver" env arm64-apple-darwin)
    eval "$environment"
    [[ "$CC" == "$OSXCROSS_ROOT/bin/$1-clang" ]]
  }
  check_prefix "$newer"
  for mode in missing nonexecutable; do
    tool="$OSXCROSS_ROOT/bin/$newer-install_name_tool"
    if [[ $mode == missing ]]; then mv "$tool" "$tool.saved"
    else chmod -x "$tool"; fi
    check_prefix "$older"
    description=$(CPKT_OSXCROSS_HOST="$newer" "$resolver" discover arm64-apple-darwin)
    grep -Fxq status=missing <<< "$description"
    if CPKT_OSXCROSS_HOST="$newer" "$resolver" env arm64-apple-darwin > "$work/exports" 2> "$work/error"; then
      echo 'ERROR: incomplete pinned Darwin prefix accepted' >&2; exit 1
    fi
    [[ ! -s "$work/exports" ]]
    if [[ $mode == missing ]]; then mv "$tool.saved" "$tool"
    else chmod +x "$tool"; fi
  done
  rm "$OSXCROSS_ROOT/bin/$older-install_name_tool" "$OSXCROSS_ROOT/bin/$newer-install_name_tool"
  description=$("$resolver" discover arm64-apple-darwin)
  grep -Fxq status=missing <<< "$description"
  if "$resolver" env arm64-apple-darwin > "$work/exports" 2> "$work/error"; then
    echo 'ERROR: Darwin exports accepted without a complete prefix' >&2; exit 1
  fi
  [[ ! -s "$work/exports" ]]
)
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
