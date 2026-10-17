#!/usr/bin/env bash
set -euo pipefail
root=$1
real_cmake=$(command -v cmake)
mkdir -p "$root/build"
work=$(mktemp -d "$root/build/target-test.XXXXXX")
trap 'rm -rf "$work"' EXIT
cp "$root/Makefile" "$work/"
mkdir -p "$work/scripts" "$work/bin" "$work/build/debug/examples"
cp "$root/scripts/"{require-host-bash.sh,require-workspace.sh,deps.sh,build.sh} "$work/scripts/"
mkdir -p "$work/cmake"
cp "$root/cmake/softline_workspace.cmake" "$work/cmake/"
cat > "$work/bin/cmake" <<'SH'
#!/bin/sh
case "$1" in -DSOFTLINE_WORKSPACE_ROOT=*) exec "$REAL_CMAKE" "$@" ;; esac
printf 'cmake %s\n' "$*" >> "$CALL_LOG"
SH
cat > "$work/scripts/build-local-lua.sh" <<'SH'
#!/bin/sh
printf 'lua %s\n' "$*" >> "$CALL_LOG"
SH
cat > "$work/build/debug/examples/example_chat" <<'SH'
#!/bin/sh
printf '%s %s\n' "$SOFTLINE_CHAT_CHAR_MS" "$SOFTLINE_PROMPT_THEME"
SH
chmod +x "$work/bin/cmake" "$work/scripts/build-local-lua.sh" "$work/build/debug/examples/example_chat"
export CALL_LOG="$work/calls" PATH="$work/bin:$PATH" REAL_CMAKE="$real_cmake"
cd "$work"
: > "$CALL_LOG"
make --no-print-directory deps DEPENDENCY=libmdf
grep -qx 'cmake --preset debug' "$CALL_LOG"
: > "$CALL_LOG"
make --no-print-directory deps DEPENDENCY=lua PRESET=debug
printf 'cmake --preset debug\nlua %s/build/debug\n' "$work" > "$work/expected"
cmp "$CALL_LOG" "$work/expected"
: > "$CALL_LOG"
if make deps DEPENDENCY=bad > "$work/error" 2>&1; then echo 'Invalid dependency accepted' >&2; exit 1; fi
[[ ! -s "$CALL_LOG" ]]
for theme in default riced; do
  if [[ "$theme" == default ]]; then target=run-chat-without-delay; else target=run-chat-without-delay-riced; fi
  output=$(SOFTLINE_CHAT_CHAR_MS=999 SOFTLINE_PROMPT_THEME=plain make --no-print-directory "$target")
  [[ "$output" == "0 $theme" ]]
done
echo 'Dependency selection and no-delay example targets passed.'
