#!/usr/bin/env bash
set -euo pipefail

# Pinned native AFL++ GCC-plugin tooling bound to the x86_64 Bootlin release.
version=5.02c
revision=5
archive_name="AFLplusplus-${version}.tar.gz"
archive_sha256=118415843e5d289d63bd6d8f2252c18212978f15ac9e86acbbc75766cd45acde
ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
bootlin="$ROOT_DIR/scripts/cpkt-toolchains.sh"
cleanup_path=
die() { printf 'cpkt-aflpp: %s\n' "$*" >&2; exit 1; }
cleanup() { [[ -z "${cleanup_path:-}" ]] || rm -rf "$cleanup_path"; }
cleanup_file() { [[ -z "${cleanup_path:-}" ]] || rm -f "$cleanup_path"; }
cache() { if [[ -n "${CPKT_TOOLCHAIN_CACHE:-}" ]]; then printf '%s\n' "$CPKT_TOOLCHAIN_CACHE"; elif [[ -n "${XDG_CACHE_HOME:-}" ]]; then printf '%s/c.pkt.systems/toolchains\n' "$XDG_CACHE_HOME"; elif [[ -n "${HOME:-}" ]]; then printf '%s/.cache/c.pkt.systems/toolchains\n' "$HOME"; else die 'cache root unavailable'; fi; }
value() { sed -n "s/^$1=//p" <<<"$2" | tail -1; }
lock_run() { local f=$1 fd; shift; command -v flock >/dev/null || die 'flock is required'; mkdir -p "$(dirname "$f")"; exec {fd}>"$f"; flock -w "${CPKT_TOOLCHAIN_LOCK_TIMEOUT:-600}" "$fd" || die "timeout on $f"; "$@"; flock -u "$fd"; eval "exec ${fd}>&-"; }
description() { "$bootlin" ensure x86_64-linux-gnu >/dev/null; "$bootlin" discover x86_64-linux-gnu; }
collection_id() { basename -- "$1"; }
root() { printf '%s/roots/aflplusplus-%s-x86_64-linux-gnu-%s\n' "$(cache)" "$version" "$1"; }
ready() { local r=$1 id=$2; [[ -x "$r/bin/afl-fuzz" && -x "$r/bin/afl-showmap" && -x "$r/bin/cpkt-afl-gcc" && -x "$r/bin/cpkt-afl-g++" && -f "$r/lib/afl/afl-gcc-pass.so" && -f "$r/lib/afl/afl-compiler-rt.o" && -f "$r/lib/afl/dynamic_list.txt" && -f "$r/.cpkt-aflpp-revision-$revision-$id" ]]; }
ensure_locked() {
  local d br id r c cc cxx strip archive tmp src helper doc_path
  d=$(description); br=$(value root "$d"); id=$(collection_id "$br"); r=$(root "$id"); c=$(cache); ready "$r" "$id" && return
  cc=$(value cc "$d"); cxx=$(value cxx "$d"); strip=$(value strip "$d"); [[ -f "$br/include/gmp.h" ]] || die 'Bootlin GCC plugin headers are incomplete'
  mkdir -p "$c/archives"; archive="$c/archives/$archive_name"
  if [[ ! -f "$archive" ]] || ! printf '%s  %s\n' "$archive_sha256" "$archive" | sha256sum -c - >/dev/null 2>&1; then
    rm -f "$archive"; tmp="$archive.tmp.$$"; cleanup_path=$tmp; trap cleanup_file EXIT HUP INT TERM
    if command -v curl >/dev/null; then curl -fL --retry 3 --connect-timeout 20 -o "$tmp" "https://github.com/AFLplusplus/AFLplusplus/archive/refs/tags/v${version}.tar.gz"; elif command -v wget >/dev/null; then wget -O "$tmp" "https://github.com/AFLplusplus/AFLplusplus/archive/refs/tags/v${version}.tar.gz"; else die 'curl or wget is required'; fi
    printf '%s  %s\n' "$archive_sha256" "$tmp" | sha256sum -c - >/dev/null || die 'AFL++ checksum mismatch'; mv "$tmp" "$archive"; cleanup_path=; trap - EXIT HUP INT TERM
  fi
  tmp="$c/.aflplusplus.$$"; cleanup_path=$tmp; trap cleanup EXIT HUP INT TERM; helper="$r/lib/afl"; doc_path="$r/share/doc/afl"; mkdir -p "$tmp/extract" "$tmp/root/bin" "$tmp/root/lib/afl" "$tmp/root/include/afl" "$tmp/root/share/doc/afl"
  tar -xzf "$archive" -C "$tmp/extract"; src="$tmp/extract/AFLplusplus-$version"; [[ -d "$src" ]] || die 'unexpected AFL++ archive layout'
  (
    cd "$src"
    make -j1 NO_PYTHON=1 CC="$cc" CXX="$cxx" PREFIX="$tmp/root" HELPER_PATH="$helper" BIN_PATH="$r/bin" DOC_PATH="$doc_path" afl-fuzz afl-showmap
    "$cc" -O3 -funroll-loops -fPIC -Wall -g -Wno-cast-qual -Wno-variadic-macros -Wno-pointer-sign -I ./include/ -I ./instrumentation/ "-DAFL_PATH=\"$helper\"" "-DBIN_PATH=\"$r/bin\"" "-DVERSION=\"++$version\"" -Wno-unused-function -Wno-deprecated -c src/afl-common.c -o instrumentation/afl-common.o
    "$cc" -O3 -funroll-loops -fPIC -Wall -g -Wno-cast-qual -Wno-variadic-macros -Wno-pointer-sign -I ./include/ -I ./instrumentation/ "-DAFL_PATH=\"$helper\"" "-DBIN_PATH=\"$r/bin\"" "-DVERSION=\"++$version\"" -Wno-unused-function -Wno-deprecated "-DAFL_INCLUDE_PATH=\"$r/include/afl\"" src/afl-cc.c instrumentation/afl-common.o -o afl-cc -DLLVM_MINOR=0 -DLLVM_MAJOR=0 "-DLLVM_VERSION=\"0.0.0\"" "-DLLVM_BINDIR=\"\"" "-DLLVM_LIBDIR=\"\"" "-DCLANG_BIN=\"\"" "-DCLANGPP_BIN=\"\"" "-DAFL_CLANG_FLTO=\"\"" "-DAFL_REAL_LD=\"\"" -DUSE_BINDIR=0 "-DCFLAGS_OPT=\"\"" -lm
    make -j1 -f GNUmakefile.gcc_plugin CC="$cc" CXX="$cxx" PREFIX="$tmp/root" HELPER_PATH="$helper" BIN_PATH="$r/bin" DOC_PATH="$doc_path" CXXFLAGS="-O3 -fPIC -I$br/include" LDFLAGS="-L$br/lib -Wl,-rpath,$br/lib"
    install -m755 afl-fuzz afl-showmap afl-cc "$tmp/root/bin/"
    ln -sf afl-cc "$tmp/root/bin/afl-gcc-fast"; ln -sf afl-cc "$tmp/root/bin/afl-g++-fast"
    install -m755 afl-gcc-pass.so "$tmp/root/lib/afl/"; install -m644 afl-compiler-rt.o dynamic_list.txt "$tmp/root/lib/afl/"
    install -m644 include/*.h "$tmp/root/include/afl/"
  )
  "$strip" "$tmp/root/bin/afl-fuzz" "$tmp/root/bin/afl-showmap" "$tmp/root/bin/afl-cc" "$tmp/root/lib/afl/afl-gcc-pass.so"
  printf '#!/usr/bin/env bash\nexport AFL_PATH=%q\nexport AFL_CC=%q\nexec %q "$@"\n' "$r/lib/afl" "$cc" "$r/bin/afl-gcc-fast" > "$tmp/root/bin/cpkt-afl-gcc"
  printf '#!/usr/bin/env bash\nexport AFL_PATH=%q\nexport AFL_CC=%q\nexport AFL_CXX=%q\nexec %q "$@"\n' "$r/lib/afl" "$cc" "$cxx" "$r/bin/afl-g++-fast" > "$tmp/root/bin/cpkt-afl-g++"
  chmod +x "$tmp/root/bin/cpkt-afl-gcc" "$tmp/root/bin/cpkt-afl-g++"; touch "$tmp/root/.cpkt-aflpp-revision-$revision-$id"
  ready "$tmp/root" "$id" || die 'incomplete AFL++ build'; rm -rf "$r"; mv "$tmp/root" "$r"; rm -rf "$tmp"; cleanup_path=; trap - EXIT HUP INT TERM
}
ensure() { [[ "$(uname -s)" = Linux ]] || die 'AFL++ fuzzing is native Linux-only'; case "$(uname -m)" in x86_64|amd64) ;; *) die 'native x86_64 is required';; esac; local d id; d=$(description); id=$(collection_id "$(value root "$d")"); ready "$(root "$id")" "$id" || lock_run "$(cache)/locks/aflplusplus-${version}-x86_64-linux-gnu.lock" ensure_locked; }
report() { local d id r; ensure; d=$(description); id=$(collection_id "$(value root "$d")"); r=$(root "$id"); printf 'version=%s\ncache=%s\nsource=aflplusplus\nroot=%s\nafl_fuzz=%s\nafl_showmap=%s\ncc=%s\ncxx=%s\nhelper=%s\n' "$version" "$(cache)" "$r" "$r/bin/afl-fuzz" "$r/bin/afl-showmap" "$r/bin/cpkt-afl-gcc" "$r/bin/cpkt-afl-g++" "$r/lib/afl"; }
env_out() {
  local d
  d=$(report)
  printf 'export AFL_PATH=%q\n' "$(value helper "$d")"
  printf 'export CPKT_AFL_FUZZ=%q\n' "$(value afl_fuzz "$d")"
  printf 'export CPKT_AFL_SHOWMAP=%q\n' "$(value afl_showmap "$d")"
  printf 'export CC=%q\n' "$(value cc "$d")"
  printf 'export CXX=%q\n' "$(value cxx "$d")"
}
case "${1:-}" in ensure) [[ $# -eq 1 ]] || die 'usage: ensure'; ensure;; discover) [[ $# -eq 1 ]] || die 'usage: discover'; report;; env) [[ $# -eq 1 ]] || die 'usage: env'; env_out;; *) die 'usage: {ensure|discover|env}';; esac
