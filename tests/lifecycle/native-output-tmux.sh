#!/usr/bin/env bash
set -euo pipefail
tmux=$1
fixture=$2
build=$3
work=$(mktemp -d "$build/native-tmux.XXXXXX")
# Relative Unix socket paths stay below the kernel limit in extracted sources.
cd "$work"
socket=tmux.sock
cleanup() {
  "$tmux" -S "$socket" kill-server >/dev/null 2>&1 || :
  rm -rf -- "$work"
}
trap cleanup EXIT
run() { "$tmux" -S "$socket" -f /dev/null "$@"; }
render() {
  local text=$1 chunk=$2 destination=$3
  {
    printf '#!/usr/bin/env bash\nset -euo pipefail\n'
    if ((chunk)); then
      printf 'exec %q --chunks %q %q\n' "$fixture" "$text" "$chunk"
    else
      printf 'printf '\''%%s\\r\\n'\'' %q\n' "$text"
    fi
  } > "$work/runner"
  printf -v command 'exec %q %q' "$BASH" "$work/runner"
  run respawn-pane -k -t unicode:0.0 "$command"
  deadline=$((SECONDS + 5))
  while [[ $(run display-message -p -t unicode:0.0 '#{pane_dead}') != 1 ]]; do
    ((SECONDS < deadline)) || { echo 'Unicode fixture failed to finish' >&2; exit 1; }
    sleep .01
  done
  [[ $(run display-message -p -t unicode:0.0 '#{pane_dead_status}') == 0 ]] || {
    echo 'Unicode fixture failed' >&2; exit 1;
  }
  # Match the original terminal oracle: trim terminal padding, retain row breaks.
  captured=$(run capture-pane -p -S - -t unicode:0.0)
  printf '%s\n' "$captured" > "$destination"
  run display-message -p -t unicode:0.0 '#{cursor_y},#{cursor_x}' >> "$destination"
}
run new-session -d -s unicode -x 40 -y 8 cat
run set-option -g remain-on-exit on
run set-option -g remain-on-exit-format ''
run set-option -g status off
repeat() {
  local glyph=$1 count=$2 result= index
  for ((index=0; index<count; ++index)); do result+=$glyph; done
  printf '%s' "$result"
}
texts=("$(repeat a 40)X" "$(repeat '🇸🇪' 22)X" "$(repeat '👩‍💻' 10)X"
  "$(repeat '❤️' 22)X" "$(repeat '1️⃣' 22)X" "$(repeat a 39)éX" "$(repeat a 40)́X")
for text in "${texts[@]}"; do
  render "$text" 0 "$work/expected"
  # Baseline must display the sentinel before the separate cursor record.
  [[ $(tail -2 "$work/expected" | head -1) == *X ]] || { cat "$work/expected"; exit 1; }
  length=$(printf %s "$text" | wc -c)
  mapfile -t chunks < <(printf '%s\n' 1 4 8 "$length" | sort -nu)
  for chunk in "${chunks[@]}"; do
    render "$text" "$chunk" "$work/actual"
    cmp "$work/expected" "$work/actual" || { echo "tmux mismatch: chunk=$chunk source=$text" >&2; exit 1; }
  done
done
