#!/usr/bin/env bash
set -euo pipefail
nm=$1
library=$2
allowlist=$3
root=$(cd "${BASH_SOURCE[0]%/*}/../.." && pwd)
mkdir -p "$root/build"
scratch=$(mktemp -d "$root/build/exports.XXXXXX")
trap 'rm -rf "$scratch"' EXIT
sed '/^[[:space:]]*#/d; /^[[:space:]]*$/d' "$allowlist" | sort > "$scratch/expected"
"$nm" -D --defined-only --format=posix "$library" | awk '
  $1 == "SOFTLINE_1" && $2 == "A" { next }
  { sub(/@.*/, "", $1); print $1 }
' | sort -u > "$scratch/actual"
diff -u "$scratch/expected" "$scratch/actual"
echo 'Exact public dynamic exports passed.'
