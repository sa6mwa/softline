#!/usr/bin/env bash
set -euo pipefail
lua=$1
root=$2
case "$3" in
  history_auto) printf 'auto\nreadline\n' | "$lua" "$root/tests/lua_history_auto.lua" ;;
  readline) printf 'hello\n' | "$lua" "$root/tests/lua_readline.lua" ;;
  simple) printf '/quit\n' | "$lua" "$root/examples/simple.lua" >/dev/null ;;
  chat)
    output=$(printf 'hello\n/quit\n' | "$lua" "$root/examples/chat.lua")
    [[ "$output" == '[turn] hello' ]] || { echo "ERROR: non-tty Lua chat output mismatch: $output" >&2; exit 1; }
    ;;
  *) echo 'ERROR: unknown Lua console fixture' >&2; exit 2 ;;
esac
