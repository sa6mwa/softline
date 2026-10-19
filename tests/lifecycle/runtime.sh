#!/usr/bin/env bash
set -euo pipefail
sysroot=$1
readelf=$2
probe=$3
shift 3
collection=$(cd "$sysroot/../.." && pwd -P)
check() {
  local binary=$1 metadata loader listing library resolved
  metadata=$("$readelf" -l "$binary")
  loader=$(sed -n 's/.*Requesting program interpreter: \([^]]*\)\].*/\1/p' <<< "$metadata")
  [[ "$loader" == "$sysroot/"* ]] || return 1
  listing=$("$loader" --list "$binary")
  while IFS= read -r library; do
    resolved=$(readlink -f "$library")
    case "$library" in
      *libsoftline*) ;;
      *libmdf*) [[ "$resolved" == "${SOFTLINE_TEST_LIBMDF_RUNTIME_ROOT:-}/"* ]] || return 1 ;;
      *) [[ "$resolved" == "$collection/"* ]] || return 1 ;;
    esac
  done < <(awk '/=> \// { print $3 }' <<< "$listing")
}
check "$probe"
for binary in "$@"; do check "$binary"; done
if check /bin/true; then echo 'ERROR: host-linked executable accepted as target runtime' >&2; exit 1; fi
for mode in direct self; do
  args=(); [[ "$mode" == direct ]] || args=(self)
  maps=$("$probe" "${args[@]}")
  libc=$(grep libc.so <<< "$maps")
  [[ -n "$libc" ]]
  if grep -vF "$sysroot" <<< "$libc"; then echo 'ERROR: runtime escaped target sysroot' >&2; exit 1; fi
done
maps=$("$probe" host)
grep -q libc.so <<< "$maps"
if grep -Fq "$sysroot" <<< "$maps"; then echo 'ERROR: target runtime contaminated host subprocess' >&2; exit 1; fi
echo 'Selected loader/runtime, self-exec and independent host subprocess passed.'
