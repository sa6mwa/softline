#!/bin/sh
set -eu

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
. "${ROOT_DIR}/scripts/release-targets.sh"

target="arm64-apple-darwin"
osxcross_root="${OSXCROSS_ROOT:-${HOME}/.local/cross/osxcross}"
osxcross_host="${CPKT_OSXCROSS_HOST:-arm64-apple-darwin25}"
cc="$(softline_target_default_cc "${target}")"
ld="${osxcross_root}/bin/${osxcross_host}-ld"

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

fixed_out="$(dry_run "$(softline_target_path_prefix "${target}"):${PATH}")"
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
