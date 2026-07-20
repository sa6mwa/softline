#!/bin/sh
set -eu

usage() {
  echo "usage: $0 <build-dir> <target-id>" >&2
  exit 2
}

if [ "$#" -ne 2 ]; then
  usage
fi

BUILD_DIR="$1"
TARGET_ID="$2"
CACHE="${BUILD_DIR}/CMakeCache.txt"
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
TARGET_OS="unknown"
case "${TARGET_ID}" in
  *-apple-darwin) TARGET_OS="darwin" ;;
  *-linux-*) TARGET_OS="linux" ;;
esac

cache_get() {
  key="$1"
  if [ -f "${CACHE}" ]; then
    sed -n "s|^${key}:[^=]*=||p" "${CACHE}" | tail -1
  fi
}

is_executable_path() {
  [ -n "$1" ] && [ -x "$1" ]
}

command_path() {
  if [ -n "$1" ]; then
    command -v "$1" 2>/dev/null || true
  fi
}

dirname_of() {
  case "$1" in
    */*) dirname "$1" ;;
    *) echo "" ;;
  esac
}

basename_of() {
  case "$1" in
    */*) basename "$1" ;;
    *) echo "$1" ;;
  esac
}

derive_host_prefix() {
  compiler="$1"
  base="$(basename_of "${compiler}")"
  case "${base}" in
    *-cc) echo "${base%-cc}" ;;
    *-gcc) echo "${base%-gcc}" ;;
    *-clang) echo "${base%-clang}" ;;
    *) echo "${CPKT_OSXCROSS_HOST:-arm64-apple-darwin25}" ;;
  esac
}

find_sibling_tool() {
  compiler="$1"
  tool="$2"
  dir="$(dirname_of "${compiler}")"
  host="$(derive_host_prefix "${compiler}")"
  if [ -n "${dir}" ]; then
    for candidate in "${dir}/${host}-${tool}" "${dir}/${tool}"; do
      if is_executable_path "${candidate}"; then
        echo "${candidate}"
        return 0
      fi
    done
  fi
  return 1
}

find_tool() {
  override="$1"
  cache_key="$2"
  compiler="$3"
  tool="$4"
  default_path="$5"
  allow_path="$6"
  path_tool=""

  if is_executable_path "${override}"; then
    echo "${override}"
    return 0
  fi
  cache_value="$(cache_get "${cache_key}")"
  if is_executable_path "${cache_value}"; then
    echo "${cache_value}"
    return 0
  fi
  if [ -n "${compiler}" ] && find_sibling_tool "${compiler}" "${tool}" >/dev/null 2>&1; then
    find_sibling_tool "${compiler}" "${tool}"
    return 0
  fi
  if is_executable_path "${default_path}"; then
    echo "${default_path}"
    return 0
  fi
  if [ "${allow_path}" = "1" ]; then
    path_tool="$(command_path "${tool}")"
    if [ -n "${path_tool}" ]; then
      echo "${path_tool}"
      return 0
    fi
  fi
  echo ""
}

shell_quote() {
  printf "%s" "$1" | sed "s/'/'\\\\''/g"
}

print_assignment() {
  key="$1"
  value="$(shell_quote "$2")"
  printf "%s='%s'\n" "${key}" "${value}"
}

CC_VALUE="$(cache_get CMAKE_C_COMPILER)"
DEFAULT_CC=""
DEFAULT_LINKER=""
DEFAULT_READELF=""
DEFAULT_STRIP=""
ALLOW_PATH_FALLBACK=0

case "${TARGET_OS}" in
  linux)
    lifecycle_env="$("${ROOT_DIR}/scripts/cpkt-toolchains.sh" env "${TARGET_ID}" 2>/dev/null || true)"
    if [ -n "${lifecycle_env}" ]; then
      eval "${lifecycle_env}"
      DEFAULT_CC="${CPKT_TOOLCHAIN_CC:-${CC:-}}"
      DEFAULT_LINKER="${CPKT_TOOLCHAIN_LD:-${LD:-}}"
      DEFAULT_READELF="${CPKT_TOOLCHAIN_READELF:-${READELF:-}}"
      DEFAULT_STRIP="${CPKT_TOOLCHAIN_STRIP:-${STRIP:-}}"
    fi
    ;;
  darwin)
    ALLOW_PATH_FALLBACK=1
    ;;
esac

if [ -z "${CC_VALUE}" ]; then
  CC_VALUE="${DEFAULT_CC}"
fi

OSXCROSS_ROOT_VALUE="${OSXCROSS_ROOT:-${HOME}/.local/cross/osxcross}"
OSXCROSS_HOST_VALUE="${CPKT_OSXCROSS_HOST:-arm64-apple-darwin25}"
DEFAULT_OTOOL="${OSXCROSS_ROOT_VALUE}/bin/${OSXCROSS_HOST_VALUE}-otool"
DEFAULT_INSTALL_NAME_TOOL="${OSXCROSS_ROOT_VALUE}/bin/${OSXCROSS_HOST_VALUE}-install_name_tool"
DEFAULT_DARWIN_STRIP="${OSXCROSS_ROOT_VALUE}/bin/${OSXCROSS_HOST_VALUE}-strip"
DEFAULT_DARWIN_LINKER="${OSXCROSS_ROOT_VALUE}/bin/${OSXCROSS_HOST_VALUE}-ld"

case "${TARGET_OS}" in
  linux) DEFAULT_LINKER_PATH="${DEFAULT_LINKER}" ;;
  *) DEFAULT_LINKER_PATH="${DEFAULT_DARWIN_LINKER}" ;;
esac

LINKER_VALUE="$(find_tool "${SOFTLINE_LINKER:-}" CMAKE_LINKER "${CC_VALUE}" ld "${DEFAULT_LINKER_PATH}" "${ALLOW_PATH_FALLBACK}")"
READELF_VALUE="$(find_tool "${SOFTLINE_READELF:-}" CMAKE_READELF "${CC_VALUE}" readelf "${DEFAULT_READELF}" "${ALLOW_PATH_FALLBACK}")"
OTOOL_VALUE="$(find_tool "${SOFTLINE_OTOOL:-}" CPKT_OTOOL "${CC_VALUE}" otool "${DEFAULT_OTOOL}" "${ALLOW_PATH_FALLBACK}")"
if [ -z "${OTOOL_VALUE}" ]; then
  OTOOL_VALUE="$(find_tool "" CMAKE_OTOOL "${CC_VALUE}" otool "${DEFAULT_OTOOL}" "${ALLOW_PATH_FALLBACK}")"
fi
INSTALL_NAME_TOOL_VALUE="$(find_tool "${SOFTLINE_INSTALL_NAME_TOOL:-}" CMAKE_INSTALL_NAME_TOOL "${CC_VALUE}" install_name_tool "${DEFAULT_INSTALL_NAME_TOOL}" "${ALLOW_PATH_FALLBACK}")"
case "${TARGET_OS}" in
  linux) DEFAULT_STRIP_PATH="${DEFAULT_STRIP}" ;;
  *) DEFAULT_STRIP_PATH="${DEFAULT_DARWIN_STRIP}" ;;
esac
STRIP_VALUE="$(find_tool "${SOFTLINE_STRIP:-}" CMAKE_STRIP "${CC_VALUE}" strip "${DEFAULT_STRIP_PATH}" "${ALLOW_PATH_FALLBACK}")"

print_assignment TARGET_ID "${TARGET_ID}"
print_assignment TARGET_OS "${TARGET_OS}"
print_assignment CC "${CC_VALUE}"
print_assignment LINKER "${LINKER_VALUE}"
print_assignment READELF "${READELF_VALUE}"
print_assignment OTOOL "${OTOOL_VALUE}"
print_assignment INSTALL_NAME_TOOL "${INSTALL_NAME_TOOL_VALUE}"
print_assignment STRIP "${STRIP_VALUE}"
