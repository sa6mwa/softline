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
