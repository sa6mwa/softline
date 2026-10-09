#!/usr/bin/env bash
set -euo pipefail
runner=$(realpath "$1")
simple=$(realpath "$2")
chat=$(realpath "$3")
build=$(realpath "$4")
work=$(mktemp -d "$build/example-history-check.XXXXXX")
trap 'rm -rf -- "$work"' EXIT
name=$(printf %s softline.examples.chat | sha256sum)
name=${name%% *}.history
for directory in "$work/home/.local/state/softline/history" "$work/state/softline/history" "$work/override"; do
  mkdir -p -- "$directory"
  chmod 700 "$directory"
  printf 'caller-owned prompt\n' > "$directory/$name"
  chmod 600 "$directory/$name"
done
snapshot() {
  (cd "$work" && find home state override -type f -print0 | sort -z | xargs -0 sha256sum)
}
for mode in default override; do
  snapshot > "$work/before"
  find "$build" -maxdepth 1 -type d -name 'example-history.*' | sort > "$work/existing"
  (
    cd "$work"
    export HOME="$work/home" XDG_STATE_HOME="$work/state"
    unset SOFTLINE_HISTORY_DIR
    if [[ $mode == override ]]; then export SOFTLINE_HISTORY_DIR="$work/override"; fi
    "$runner" "$simple" "$chat" bottom
  )
  snapshot > "$work/after"
  cmp "$work/before" "$work/after"
  find "$build" -maxdepth 1 -type d -name 'example-history.*' | sort > "$work/current"
  mapfile -t created < <(comm -13 "$work/existing" "$work/current")
  [[ ${#created[@]} == 1 ]] || { echo 'Runner must create one build-local history directory' >&2; exit 1; }
  [[ -s ${created[0]}/$name ]] || { echo 'History records missing' >&2; exit 1; }
  if grep -Fq 'caller-owned prompt' "${created[0]}/$name"; then
    echo 'Runner read caller history' >&2
    exit 1
  fi
done
