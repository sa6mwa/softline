#!/usr/bin/env bash
case ${BASH_SOURCE[0]} in
  */*) source "${BASH_SOURCE[0]%/*}/require-host-bash.sh" ;;
  *) source ./require-host-bash.sh ;;
esac || exit $?
set -euo pipefail
ROOT_DIR=$(cd "${BASH_SOURCE[0]%/*}/.." && pwd)
mode=${1:-smoke}
case "$mode" in smoke|long) ;; *) echo 'Usage: fuzz.sh [smoke|long]' >&2; exit 2 ;; esac
# Validate owned inputs before compiler discovery or provisioning.
[[ -s "$ROOT_DIR/fuzz/corpus/basic" ]] || { echo 'ERROR: fuzz/corpus/basic must be a nonempty seed' >&2; exit 1; }
cd "$ROOT_DIR"
resolved_env=$("$ROOT_DIR/scripts/cpkt-aflpp.sh" env) || exit $?
eval "$resolved_env"
CPKT_AFL_FUZZ="$CPKT_AFLPP_ROOT/bin/afl-fuzz"
CPKT_AFL_SHOWMAP="$CPKT_AFLPP_ROOT/bin/afl-showmap"
cmake --preset fuzz
cmake --build --preset fuzz
if [[ "$mode" == long ]]; then
  exec "$CPKT_AFL_FUZZ" -i fuzz/corpus -o build/fuzz-findings -- ./build/fuzz/softline_stdin_fuzz
fi
timeout 15 "$CPKT_AFL_SHOWMAP" -q -o /dev/null -- ./build/fuzz/softline_stdin_fuzz < fuzz/corpus/basic
