#!/usr/bin/env bash
case ${BASH_SOURCE[0]} in
  */*) source "${BASH_SOURCE[0]%/*}/require-host-bash.sh" ;;
  *) source ./require-host-bash.sh ;;
esac || exit $?
set -euo pipefail
source "${BASH_SOURCE[0]%/*}/require-workspace.sh" || exit $?
[[ $# == 3 || $# == 4 ]] || { echo 'Usage: verify-public-exports.sh nm library allowlist [elf|macho]' >&2; exit 2; }
nm=$1
library=$2
allowlist=$3
format=${4:-elf}
root=$(cd "${BASH_SOURCE[0]%/*}/.." && pwd)
mkdir -p "$root/build"
scratch=$(mktemp -d "$root/build/exports.XXXXXX")
trap 'rm -rf -- "$scratch"' EXIT
sed '/^[[:space:]]*#/d; /^[[:space:]]*$/d' "$allowlist" | sort > "$scratch/expected"
case "$format" in
  elf)
    "$nm" -D --defined-only --format=posix "$library" | awk '
      $1 == "SOFTLINE_1" && $2 == "A" { next }
      { sub(/@.*/, "", $1); print $1 }
    ' | sort -u > "$scratch/actual"
    ;;
  macho) "$nm" -gU -j "$library" | sed 's/^_//' | sort -u > "$scratch/actual" ;;
  *) echo 'ERROR: expected elf or macho export format' >&2; exit 2 ;;
esac
diff -u "$scratch/expected" "$scratch/actual"
echo 'Exact public dynamic exports passed.'
