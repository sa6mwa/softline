#!/usr/bin/env bash
case ${BASH_SOURCE[0]} in
  */*) source "${BASH_SOURCE[0]%/*}/require-host-bash.sh" ;;
  *) source ./require-host-bash.sh ;;
esac || exit $?
set -euo pipefail

skill_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
mkdir -p "$skill_dir/build/toolchain-checks"
export TMPDIR="$skill_dir/build/toolchain-checks"
resolver="$skill_dir/scripts/cpkt-aflpp.sh"
fail() { printf 'test-cpkt-aflpp-resolver: %s\n' "$*" >&2; exit 1; }

[[ -x "$resolver" ]] || fail 'resolver is not executable'
bash -n "$resolver"
grep -Fq 'version=5.02c' "$resolver" || fail 'AFL++ version is not pinned'
grep -Fq 'archive_sha256=' "$resolver" || fail 'AFL++ checksum is not pinned'
grep -Fq 'cpkt-toolchains.sh' "$resolver" || fail 'resolver does not use the embedded Bootlin resolver'
grep -Fq 'install_cleanup_trap -f "$dl"' "$resolver" || fail 'resolver does not clean interrupted AFL++ downloads'
grep -Fq 'install_cleanup_trap -rf "$tmp"' "$resolver" || fail 'resolver does not clean failed staging state'
grep -Fq 'with_cache_lock "$c/locks/aflplusplus-${version}-x86_64-linux-gnu.lock" ensure_locked' "$resolver" || fail 'resolver does not serialize shared AFL++ publication'
grep -Fq 'collection_id()' "$resolver" || fail 'resolver does not key AFL++ caches by Bootlin collection identity'
grep -Fq '.cpkt-aflpp-revision-$revision-$id' "$resolver" || fail 'resolver readiness marker is not tied to the Bootlin collection identity'
grep -Fq 'ready "$r" "$id" && return' "$resolver" || fail 'resolver does not recheck collection-specific AFL++ readiness under the lock'
grep -Fq -- '-x "$r/bin/afl-showmap"' "$resolver" || fail 'resolver readiness does not require afl-showmap'
grep -Fq '"-DAFL_PATH=\"$helper\""' "$resolver" || fail 'resolver does not preserve AFL++ cache paths as one compiler argument'
grep -Fq 'export PATH=%q' "$resolver" || fail 'resolver env output does not prepend the pinned AFL++ bin directory'

fake_bin=$(mktemp -d "${TMPDIR:-/tmp}/cpkt-aflpp-test.XXXXXX")
trap 'rm -rf "$fake_bin"' EXIT HUP INT TERM
printf '#!/bin/sh\nprintf "aarch64\\n"\n' > "$fake_bin/uname"
chmod +x "$fake_bin/uname"
if PATH="$fake_bin:$PATH" "$resolver" ensure >/dev/null 2>&1; then
  fail 'resolver accepted a non-native host'
fi

if env -u HOME -u XDG_CACHE_HOME -u CPKT_TOOLCHAIN_CACHE "$resolver" discover >/dev/null 2>&1; then
  fail 'resolver accepted a missing cache root'
fi

if "$resolver" ensure extra >/dev/null 2>&1; then
  fail 'resolver accepted an invalid command shape'
fi

signal_root=$(mktemp -d "${TMPDIR:-/tmp}/cpkt-aflpp-signal-test.XXXXXX")
trap 'rm -rf "$fake_bin" "$signal_root"' EXIT HUP INT TERM
signal_skill="$signal_root/skill"
signal_bin="$signal_root/bin"
signal_cache="$signal_root/cache"
signal_bootlin="$signal_root/bootlin"
mkdir -p "$signal_skill/scripts" "$signal_bin" "$signal_bootlin/include"
cp "$resolver" "$signal_skill/scripts/cpkt-aflpp.sh"
cp "$skill_dir/scripts/cpkt-archive-cache.sh" "$signal_skill/scripts/cpkt-archive-cache.sh"
cp "$skill_dir/scripts/cpkt-afl-runtime.sh" "$signal_skill/scripts/cpkt-afl-runtime.sh"
cp "$skill_dir/scripts/require-host-bash.sh" "$signal_skill/scripts/require-host-bash.sh"
chmod +x "$signal_skill/scripts/cpkt-aflpp.sh"
touch "$signal_bootlin/include/gmp.h"
printf '#!/bin/sh\nexit 0\n' > "$signal_bin/cc"
printf '#!/bin/sh\nexit 0\n' > "$signal_bin/cxx"
cat > "$signal_skill/scripts/cpkt-toolchains.sh" <<EOF
#!/bin/sh
case "\$1" in
  ensure) printf 'ensure\\n' >> "\${CPKT_TEST_BOOTLIN_CALLS:?}"; exit 0 ;;
  discover)
    printf 'status=%s\\n' "\${CPKT_TEST_BOOTLIN_STATUS:-ready}"
    printf 'cc=%s\\n' '$signal_bin/cc'
    printf 'cxx=%s\\n' '$signal_bin/cxx'
    printf 'root=%s\\n' '$signal_bootlin'
    ;;
  *) exit 2 ;;
esac
EOF
cat > "$signal_bin/sha256sum" <<'EOF'
#!/bin/sh
cat >/dev/null
exit 0
EOF
cat > "$signal_bin/curl" <<'EOF'
#!/bin/sh
while [ "$#" -gt 0 ]; do
  case "$1" in
    -o) output=$2; shift 2 ;;
    *) shift ;;
  esac
done
mkdir -p "$(dirname -- "$output")"
: > "$output"
printf '%s\n' "$output" > "${CPKT_TEST_DOWNLOADER_MARKER:?}"
kill -TERM "$PPID"
printf 'signalled\n' >> "$CPKT_TEST_DOWNLOADER_MARKER"
EOF
chmod +x "$signal_skill/scripts/cpkt-toolchains.sh" "$signal_bin/cc" "$signal_bin/cxx" "$signal_bin/sha256sum" "$signal_bin/curl"

# Discovery and environment extraction must not provision missing prerequisites.
export CPKT_TEST_BOOTLIN_CALLS="$signal_root/bootlin-calls"
export CPKT_TEST_DOWNLOADER_MARKER="$signal_root/downloader-ran"
for mode in discover env; do
  if CPKT_TEST_BOOTLIN_STATUS=missing CPKT_TOOLCHAIN_CACHE="$signal_cache" \
    "$signal_skill/scripts/cpkt-aflpp.sh" "$mode" > "$signal_root/$mode.out" 2> "$signal_root/$mode.err"; then
    fail "$mode accepted missing Bootlin"
  fi
  grep -Fq 'ensure x86_64-linux-gnu' "$signal_root/$mode.err" || fail "$mode omitted Bootlin preparation command"
  if CPKT_TOOLCHAIN_CACHE="$signal_cache" "$signal_skill/scripts/cpkt-aflpp.sh" "$mode" \
    > "$signal_root/$mode.out" 2> "$signal_root/$mode.err"; then
    fail "$mode accepted missing AFL++"
  fi
  grep -Fq 'cpkt-aflpp.sh ensure' "$signal_root/$mode.err" || fail "$mode omitted AFL++ preparation command"
done
[[ ! -e "$CPKT_TEST_BOOTLIN_CALLS" && ! -e "$signal_cache" && ! -e "$CPKT_TEST_DOWNLOADER_MARKER" ]] || fail 'discovery/environment provisioned missing tools'

# Exercise usable, incomplete and wrong-collection prepared roots without builds.
prepared="$signal_cache/roots/aflplusplus-5.02c-x86_64-linux-gnu-bootlin-r2"
mkdir -p "$prepared/bin" "$prepared/lib/afl" "$prepared/libexec/bootlin-runtime"
for tool in afl-fuzz afl-showmap afl-cc cpkt-afl-gcc cpkt-afl-g++ bootlin-gcc bootlin-g++; do
  printf '#!/bin/sh\nexit 0\n' > "$prepared/bin/$tool"
  chmod +x "$prepared/bin/$tool"
done
ln -s afl-cc "$prepared/bin/afl-gcc-fast"
ln -s afl-cc "$prepared/bin/afl-g++-fast"
for tool in cc1 cc1plus; do
  printf '#!/bin/sh\nexit 0\n' > "$prepared/libexec/bootlin-runtime/$tool"
  chmod +x "$prepared/libexec/bootlin-runtime/$tool"
done
touch "$prepared/lib/afl/afl-gcc-pass.so" "$prepared/lib/afl/afl-compiler-rt.o" "$prepared/.cpkt-aflpp-revision-2-bootlin"
description=$(CPKT_TOOLCHAIN_CACHE="$signal_cache" "$signal_skill/scripts/cpkt-aflpp.sh" discover)
grep -Fqx 'status=ready' <<< "$description" || fail 'prepared discovery did not report readiness'
grep -Fqx "root=$prepared" <<< "$description" || fail 'prepared discovery reported wrong root'
environment=$(CPKT_TOOLCHAIN_CACHE="$signal_cache" "$signal_skill/scripts/cpkt-aflpp.sh" env)
foreign="$signal_cache/roots/aflplusplus-5.02c-x86_64-linux-gnu-bootlin"
mkdir -p "$foreign"
printf 'another resolver revision\n' > "$foreign/owned-by-other-resolver"
CPKT_TOOLCHAIN_CACHE="$signal_cache" "$signal_skill/scripts/cpkt-aflpp.sh" ensure
[[ $(cat "$foreign/owned-by-other-resolver") = 'another resolver revision' ]] || fail 'ensure replaced another resolver revision'
[[ ! -e "$CPKT_TEST_DOWNLOADER_MARKER" ]] || fail 'ensure rebuilt a prepared revision'
rm -f "$CPKT_TEST_BOOTLIN_CALLS"
(
  eval "$environment"
  [[ "$CC" == "$prepared/bin/cpkt-afl-gcc" && "$CXX" == "$prepared/bin/cpkt-afl-g++" &&
     "$AFL_CC" == "$prepared/bin/bootlin-gcc" && "$AFL_CXX" == "$prepared/bin/bootlin-g++" &&
     "$AFL_PATH" == "$prepared/lib/afl" && "$PATH" == "$prepared/bin:"* ]] || fail 'prepared environment selected incorrect tools'
)
for tool in afl-showmap afl-gcc-fast afl-g++-fast afl-cc bootlin-gcc bootlin-g++ ../libexec/bootlin-runtime/cc1 ../libexec/bootlin-runtime/cc1plus; do
  mv "$prepared/bin/$tool" "$prepared/bin/$tool.missing"
  for mode in discover env; do
    if CPKT_TOOLCHAIN_CACHE="$signal_cache" "$signal_skill/scripts/cpkt-aflpp.sh" "$mode" > /dev/null 2>&1; then
      fail "$mode accepted incomplete AFL++ without $tool"
    fi
  done
  [[ ! -e "$prepared/bin/$tool" ]] || fail "$mode repaired missing $tool"
  mv "$prepared/bin/$tool.missing" "$prepared/bin/$tool"
done
mv "$prepared/.cpkt-aflpp-revision-2-bootlin" "$prepared/.cpkt-aflpp-revision-2-other"
for mode in discover env; do
  if CPKT_TOOLCHAIN_CACHE="$signal_cache" "$signal_skill/scripts/cpkt-aflpp.sh" "$mode" > /dev/null 2>&1; then
    fail "$mode accepted a wrong-collection AFL++ marker"
  fi
done
[[ ! -e "$CPKT_TEST_BOOTLIN_CALLS" && ! -e "$CPKT_TEST_DOWNLOADER_MARKER" &&
   ! -e "$prepared/.cpkt-aflpp-revision-2-bootlin" ]] || fail 'discovery/environment repaired prepared state'
rm -rf "$signal_cache"

set +e
PATH="$signal_bin:$PATH" CPKT_TOOLCHAIN_CACHE="$signal_cache" "$signal_skill/scripts/cpkt-aflpp.sh" ensure >/dev/null 2>&1
signal_status=$?
set -e
[[ $signal_status -ne 0 ]] || fail 'resolver accepted an interrupted AFL++ download'
[[ -s "$CPKT_TEST_BOOTLIN_CALLS" ]] || fail 'ensure did not prepare Bootlin'
[[ -s "$CPKT_TEST_DOWNLOADER_MARKER" ]] || fail 'interruption fixture never reached the downloader'
grep -Fqx signalled "$CPKT_TEST_DOWNLOADER_MARKER" || fail 'interruption fixture did not signal the resolver'
[[ -d "$signal_cache/archives" ]] || fail 'interruption fixture did not create its archive directory'
leftovers=$(find "$signal_cache/archives" -maxdepth 1 -name 'AFLplusplus-5.02c.tar.gz.tmp.*' -print) || fail 'unable to inspect interrupted download cleanup'
[[ -z "$leftovers" ]] || fail 'interrupted AFL++ download left a temporary archive in the shared cache'

printf 'AFL++ resolver tests passed\n'
