#!/usr/bin/env bash
set -euo pipefail

# Complete pinned Linux compiler collections.  This resolver is intentionally
# self-contained so source archives never depend on a workstation installation.
die() { printf 'cpkt-toolchains: %s\n' "$*" >&2; exit 1; }
cache_root() {
  if [[ -n "${CPKT_TOOLCHAIN_CACHE:-}" ]]; then printf '%s\n' "$CPKT_TOOLCHAIN_CACHE"
  elif [[ -n "${XDG_CACHE_HOME:-}" ]]; then printf '%s/c.pkt.systems/toolchains\n' "$XDG_CACHE_HOME"
  elif [[ -n "${HOME:-}" ]]; then printf '%s/.cache/c.pkt.systems/toolchains\n' "$HOME"
  else die 'HOME, XDG_CACHE_HOME, or CPKT_TOOLCHAIN_CACHE is required'; fi
}
sha256_file() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | awk '{print $1}'
  elif command -v shasum >/dev/null 2>&1; then shasum -a 256 "$1" | awk '{print $1}'
  else die 'sha256sum or shasum is required'; fi
}
download() {
  if command -v curl >/dev/null 2>&1; then curl -fL --retry 3 --connect-timeout 20 -o "$2" "$1"
  elif command -v wget >/dev/null 2>&1; then wget -O "$2" "$1"
  else die 'curl or wget is required to download a Bootlin toolchain'; fi
}
targets() { printf '%s\n' x86_64-linux-gnu x86_64-linux-musl aarch64-linux-gnu aarch64-linux-musl armhf-linux-gnu armhf-linux-musl arm64-apple-darwin; }
linux_target() { case "$1" in x86_64-linux-gnu|x86_64-linux-musl|aarch64-linux-gnu|aarch64-linux-musl|armhf-linux-gnu|armhf-linux-musl) return 0;; *) return 1;; esac; }
host_system() { printf '%s\n' "${CPKT_HOST_SYSTEM:-$(uname -s)}"; }
host_machine() { printf '%s\n' "${CPKT_HOST_MACHINE:-$(uname -m)}"; }
native_linux_target() {
  local system machine
  system=$(host_system)
  machine=$(host_machine)
  case "$system" in
    Linux) ;;
    *) die "native Bootlin target unavailable for non-Linux host: ${system}" ;;
  esac
  case "$machine" in
    x86_64|amd64) printf '%s\n' x86_64-linux-gnu ;;
    *)
      die "native Bootlin target unavailable for unsupported Linux host architecture: ${machine}; pinned Bootlin compiler executables are x86_64-hosted"
      ;;
  esac
}
meta() {
  case "$1" in
    x86_64-linux-gnu) printf '%s\n' 'x86-64|x86-64--glibc--stable-2026.08-1|cde893afab04ac7dcd15c46aac214ff550441b982536124c88a71146a0eeedd3|x86_64-linux|x86_64-buildroot-linux-gnu/sysroot';;
    x86_64-linux-musl) printf '%s\n' 'x86-64|x86-64--musl--stable-2026.08-1|78d3a4683d6ac47b5ee73bd5bce210b55eb93dff1b137c61298af97eb0d2b5a6|x86_64-linux|x86_64-buildroot-linux-musl/sysroot';;
    aarch64-linux-gnu) printf '%s\n' 'aarch64|aarch64--glibc--stable-2026.08-1|0213efac9b5577f20d58de9431960a191347ffc2257b27ffe7250522bf1f7867|aarch64-linux|aarch64-buildroot-linux-gnu/sysroot';;
    aarch64-linux-musl) printf '%s\n' 'aarch64|aarch64--musl--stable-2026.08-1|b388c480a48e8e9f9b99e3d14e69219c4d61e5a2424a82faecb88a015b781a60|aarch64-linux|aarch64-buildroot-linux-musl/sysroot';;
    armhf-linux-gnu) printf '%s\n' 'armv7-eabihf|armv7-eabihf--glibc--stable-2026.08-1|9b7e25a74e87dac1e05d399444295e254a3073a056101e3197a859490e5701cd|arm-linux|arm-buildroot-linux-gnueabihf/sysroot';;
    armhf-linux-musl) printf '%s\n' 'armv7-eabihf|armv7-eabihf--musl--stable-2026.08-1|9147bafae4aa272321a3c6440d04d83b7e23411b2d344f875541d84c4444ba9b|arm-linux|arm-buildroot-linux-musleabihf/sysroot';;
    *) die "unsupported target: $1";; esac
}
values() { local a n s p y; IFS='|' read -r a n s p y <<<"$(meta "$1")"; printf '%s|%s|%s|%s|%s|%s\n' "$a" "$n" "$s" "$p" "$y" "$(cache_root)/roots/$n"; }
compiler_file() { "$1" -print-file-name="$2"; }
existing_compiler_file() { local f; f=$(compiler_file "$1" "$2"); [[ "$f" != "$2" && -f "$f" ]] || return 1; printf '%s\n' "$f"; }
ready() {
  local r=$1 p=$2 s=$3 tool
  for tool in gcc g++ ld ar ranlib strip nm objcopy objdump addr2line gdb readelf; do [[ -x "$r/bin/$p-$tool" ]] || return 1; done
  [[ -f "$s/usr/include/stdio.h" || -f "$s/include/stdio.h" ]] || return 1
  [[ -e "$s/usr/lib/libc.so" || -e "$s/lib/libc.so" || -e "$s/lib/libc.so.6" ]] || return 1
  existing_compiler_file "$r/bin/$p-g++" libstdc++.a >/dev/null
  existing_compiler_file "$r/bin/$p-g++" libgcc.a >/dev/null
}
lock_run() { local f=$1 fd; shift; command -v flock >/dev/null || die 'flock is required'; mkdir -p "$(dirname "$f")"; exec {fd}>"$f"; flock -w "${CPKT_TOOLCHAIN_LOCK_TIMEOUT:-600}" "$fd" || die "timeout on $f"; "$@"; flock -u "$fd"; eval "exec ${fd}>&-"; }
install_locked() {
  local t=$1 a n want p sr r archive tmp extract actual
  IFS='|' read -r a n want p sr r <<<"$(values "$t")"
  ready "$r" "$p" "$r/$sr" && return
  mkdir -p "$(cache_root)/archives" "$(cache_root)/roots"
  archive="$(cache_root)/archives/$n.tar.xz"
  if [[ -f "$archive" ]] && [[ "$(sha256_file "$archive")" != "$want" ]]; then rm -f "$archive"; fi
  if [[ ! -f "$archive" ]]; then
    tmp="$archive.tmp.$$"; trap 'rm -f "$tmp"' EXIT HUP INT TERM
    download "https://toolchains.bootlin.com/downloads/releases/toolchains/$a/tarballs/$n.tar.xz" "$tmp"
    actual=$(sha256_file "$tmp"); [[ "$actual" == "$want" ]] || die "checksum mismatch for $n"
    mv "$tmp" "$archive"; trap - EXIT HUP INT TERM
  fi
  extract="$(cache_root)/roots/.extract-$n.$$"; trap 'rm -rf "$extract"' EXIT HUP INT TERM
  mkdir -p "$extract"; tar -C "$extract" -xf "$archive"; [[ -d "$extract/$n/bin" ]] || die "unexpected archive layout for $n"
  rm -rf "$r"; mv "$extract/$n" "$r"; trap - EXIT HUP INT TERM
  ready "$r" "$p" "$r/$sr" || die "incomplete extracted toolchain: $r"
}
ensure_linux() { local a n s p sr r; IFS='|' read -r a n s p sr r <<<"$(values "$1")"; ready "$r" "$p" "$r/$sr" || lock_run "$(cache_root)/locks/bootlin-$n.lock" install_locked "$1"; }
darwin() {
  local r="${OSXCROSS_ROOT:-${HOME:-}/.local/cross/osxcross}" p="${CPKT_OSXCROSS_HOST:-arm64-apple-darwin25}" tool sdk found_sdk=
  local sdks=()
  printf 'target=arm64-apple-darwin\ncache=%s\nsource=osxcross\ndownloadable=no\n' "$(cache_root)"
  shopt -s nullglob
  sdks=("$r"/SDK/MacOSX*.sdk)
  shopt -u nullglob
  if ((${#sdks[@]})); then
    found_sdk=$(
      printf '%s\n' "${sdks[@]}" |
        awk '{
          path = $0
          version = $0
          sub(/^.*MacOSX/, "", version)
          sub(/\.sdk$/, "", version)
          n = split(version, parts, ".")
          key = ""
          for (i = 1; i <= n; i++) key = key sprintf("%06d", parts[i])
          print key "\t" path
        }' |
        sort -k1,1r |
        sed -n '1s/^[^\t]*\t//p'
    )
  fi
  if [[ -z "$found_sdk" ]]; then printf 'status=missing\nnote=Configure OSXCROSS_ROOT with a complete local osxcross SDK toolchain.\n'; return; fi
  for tool in clang clang++ ld ar ranlib strip nm otool install_name_tool; do
    if [[ ! -x "$r/bin/$p-$tool" ]]; then printf 'status=missing\nnote=Configure OSXCROSS_ROOT with a complete local osxcross SDK toolchain.\n'; return; fi
  done
  printf 'status=ready\nroot=%s\nprefix=%s\nsdk=%s\ncc=%s\ncxx=%s\nld=%s\nar=%s\nranlib=%s\nstrip=%s\nnm=%s\notool=%s\ninstall_name_tool=%s\n' "$r" "$p" "$found_sdk" "$r/bin/$p-clang" "$r/bin/$p-clang++" "$r/bin/$p-ld" "$r/bin/$p-ar" "$r/bin/$p-ranlib" "$r/bin/$p-strip" "$r/bin/$p-nm" "$r/bin/$p-otool" "$r/bin/$p-install_name_tool"
}
report() {
  local t=$1 a n s p sr r cxx
  [[ "$t" == arm64-apple-darwin ]] && { darwin; return; }
  linux_target "$t" || die "unsupported target: $t"; IFS='|' read -r a n s p sr r <<<"$(values "$t")"
  printf 'target=%s\ncache=%s\nsource=bootlin\narchive=%s.tar.xz\n' "$t" "$(cache_root)" "$n"
  if ! ready "$r" "$p" "$r/$sr"; then printf 'status=missing\ndownloadable=yes\n'; return; fi
  cxx="$r/bin/$p-g++"
  printf 'status=ready\nroot=%s\nprefix=%s\nsysroot=%s\ncc=%s\ncxx=%s\nld=%s\nar=%s\nranlib=%s\nstrip=%s\nnm=%s\nobjcopy=%s\nobjdump=%s\naddr2line=%s\ngdb=%s\nreadelf=%s\nlibstdcxx_a=%s\nlibgcc_a=%s\n' "$r" "$p" "$r/$sr" "$r/bin/$p-gcc" "$cxx" "$r/bin/$p-ld" "$r/bin/$p-ar" "$r/bin/$p-ranlib" "$r/bin/$p-strip" "$r/bin/$p-nm" "$r/bin/$p-objcopy" "$r/bin/$p-objdump" "$r/bin/$p-addr2line" "$r/bin/$p-gdb" "$r/bin/$p-readelf" "$(existing_compiler_file "$cxx" libstdc++.a)" "$(existing_compiler_file "$cxx" libgcc.a)"
}
ensure() {
  local d
  if [[ "$1" != arm64-apple-darwin ]]; then
    ensure_linux "$1"
  fi
  d=$(report "$1")
  printf '%s\n' "$d"
  grep -qx 'status=ready' <<<"$d" || die "target is unavailable: $1"
}
env_out() {
  local d k upper v standard
  d=$(report "$1")
  grep -qx 'status=ready' <<<"$d" || die "target is missing; run $0 ensure $1"
  for k in root prefix sysroot sdk cc cxx ld ar ranlib strip nm objcopy objdump addr2line gdb readelf libstdcxx_a libgcc_a otool install_name_tool; do
    v=$(sed -n "s/^$k=//p" <<<"$d")
    [[ -z "$v" ]] && continue
    upper=$(printf '%s\n' "$k" | tr '[:lower:]' '[:upper:]')
    printf 'export CPKT_TOOLCHAIN_%s=%q\n' "$upper" "$v"
    case "$k" in
      cc) standard=CC ;;
      cxx) standard=CXX ;;
      ld) standard=LD ;;
      ar) standard=AR ;;
      ranlib) standard=RANLIB ;;
      strip) standard=STRIP ;;
      nm) standard=NM ;;
      objcopy) standard=OBJCOPY ;;
      objdump) standard=OBJDUMP ;;
      addr2line) standard=ADDR2LINE ;;
      readelf) standard=READELF ;;
      *) standard="" ;;
    esac
    [[ -z "$standard" ]] || printf 'export %s=%q\n' "$standard" "$v"
  done
  printf 'export CPKT_TARGET=%q\n' "$1"
}
case "${1:-}" in
  targets) [[ $# -eq 1 ]] || die 'usage: targets'; targets;;
  native-linux-target) [[ $# -eq 1 ]] || die 'usage: native-linux-target'; native_linux_target;;
  discover) if [[ $# -eq 2 ]]; then report "$2"; elif [[ $# -eq 1 ]]; then while read -r t; do report "$t"; done < <(targets); else die 'usage: discover [target]'; fi;;
  ensure) [[ $# -eq 2 ]] || die 'usage: ensure <target|all>'; if [[ "$2" == all ]]; then while read -r t; do [[ "$t" == arm64-apple-darwin ]] && report "$t" || ensure "$t"; done < <(targets); else ensure "$2"; fi;;
  env) [[ $# -eq 2 ]] || die 'usage: env <target>'; env_out "$2";;
  *) die 'usage: {targets|native-linux-target|discover [target]|ensure <target|all>|env <target>}';;
esac
