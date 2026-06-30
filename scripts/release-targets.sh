#!/bin/sh

SOFTLINE_RELEASE_TARGETS="${SOFTLINE_RELEASE_TARGETS:-x86_64-linux-gnu x86_64-linux-musl aarch64-linux-gnu aarch64-linux-musl armhf-linux-gnu armhf-linux-musl arm64-apple-darwin}"

softline_target_preset() {
  echo "$1-release"
}

softline_target_os() {
  case "$1" in
    *-apple-darwin) echo "darwin" ;;
    *-linux-*) echo "linux" ;;
    *) echo "unknown" ;;
  esac
}

softline_target_default_cc() {
  target="$1"
  case "${target}" in
    x86_64-linux-gnu) echo "${CC:-cc}" ;;
    x86_64-linux-musl) echo "${SOFTLINE_X86_64_LINUX_MUSL_CC:-x86_64-linux-musl-gcc}" ;;
    aarch64-linux-gnu) echo "${SOFTLINE_AARCH64_LINUX_GNU_CC:-aarch64-linux-gnu-gcc}" ;;
    aarch64-linux-musl) echo "${SOFTLINE_AARCH64_LINUX_MUSL_CC:-aarch64-linux-musl-gcc}" ;;
    armhf-linux-gnu) echo "${SOFTLINE_ARMHF_LINUX_GNU_CC:-arm-linux-gnueabihf-gcc}" ;;
    armhf-linux-musl) echo "${SOFTLINE_ARMHF_LINUX_MUSL_CC:-arm-linux-musleabihf-gcc}" ;;
    arm64-apple-darwin)
      osxcross_root="${OSXCROSS_ROOT:-${HOME}/.local/cross/osxcross}"
      osxcross_host="${CPKT_OSXCROSS_HOST:-arm64-apple-darwin25}"
      echo "${SOFTLINE_ARM64_APPLE_DARWIN_CC:-${osxcross_root}/bin/${osxcross_host}-clang}"
      ;;
    *) echo "" ;;
  esac
}

softline_target_path_prefix() {
  target="$1"
  case "${target}" in
    arm64-apple-darwin)
      osxcross_root="${OSXCROSS_ROOT:-${HOME}/.local/cross/osxcross}"
      echo "${osxcross_root}/bin"
      ;;
    *) echo "" ;;
  esac
}
