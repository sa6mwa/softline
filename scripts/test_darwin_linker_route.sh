#!/bin/sh
set -eu

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
target="arm64-apple-darwin"
description="$("${ROOT_DIR}/scripts/cpkt-toolchains.sh" discover "${target}")"
cc="$(printf '%s\n' "${description}" | sed -n 's/^cc=//p')"
ld="$(printf '%s\n' "${description}" | sed -n 's/^ld=//p')"
osxcross_root="$(printf '%s\n' "${description}" | sed -n 's/^root=//p')"
osxcross_host="$(printf '%s\n' "${description}" | sed -n 's/^prefix=//p')"

if [ ! -x "${cc}" ] || [ ! -x "${ld}" ]; then
  echo "SKIP: Darwin linker route test: osxcross compiler/linker unavailable"
  exit 0
fi

dry_run() {
  PATH_VALUE="$1"
  printf 'int main(void) { return 0; }\n' |
    PATH="${PATH_VALUE}" "${cc}" -x c - -### -o /dev/null 2>&1
}

ambient_out="$(dry_run "${PATH}")"
case "${ambient_out}" in
  *'"/usr/bin/ld"'*|*' /usr/bin/ld '*)
    echo "Observed unfixed Darwin route selecting host /usr/bin/ld"
    ;;
  *"${ld}"*)
    echo "Ambient Darwin route already selects target linker"
    ;;
  *)
    echo "ERROR: unable to classify ambient Darwin linker route" >&2
    printf '%s\n' "${ambient_out}" >&2
    exit 1
    ;;
esac

fixed_out="$(dry_run "$(dirname "${cc}"):${PATH}")"
case "${fixed_out}" in
  *"${ld}"*) ;;
  *)
    echo "ERROR: lifecycle Darwin route did not select ${ld}" >&2
    printf '%s\n' "${fixed_out}" >&2
    exit 1
    ;;
esac

build_dir="${ROOT_DIR}/build/${target}-release"
if [ -f "${build_dir}/CMakeCache.txt" ]; then
  tools="$("${ROOT_DIR}/scripts/discover_target_tools.sh" "${build_dir}" "${target}")"
  eval "${tools}"
  if [ "${LINKER:-}" != "${ld}" ]; then
    echo "ERROR: discovered Darwin linker ${LINKER:-} does not match ${ld}" >&2
    exit 1
  fi
fi

echo "Darwin linker route test passed."
