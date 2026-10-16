#!/usr/bin/env bash
# Inspect real tmux cells; success means the report's rendering assertions hold.
set -euo pipefail
tmux=$1 fixture=$2 build=$3
columns=$4 rows=$5 mode=$6 variant=$7 draft=$8
work=$(mktemp -d "$build/retained-resize.XXXXXX")
cd "$work"
unset TMUX TMUX_PANE
run() { "$tmux" -S terminal.sock -f /dev/null "$@"; }
cleanup() {
  local result=$?
  run kill-server >/dev/null 2>&1 || :
  if ((result)); then
    [[ ! -f screen ]] || cat screen >&2
    echo "Failure captures retained in $work" >&2
  else
    rm -rf -- "$work"
  fi
}
trap cleanup EXIT
fail() { echo "$*" >&2; exit 1; }
wait_for() {
  local deadline=$((SECONDS + 8))
  until "$@"; do
    ((SECONDS < deadline)) || fail "Timed out: $*"
    sleep .02
  done
}
mkfifo control ready
exec 8<>control 9<>ready
{
  printf '#!/usr/bin/env bash\nset -euo pipefail\n'
  for ((i=0; i<80; ++i)); do printf 'printf "HISTORY_%03d\\r\\n"\n' "$i"; done
  printf 'exec %q %q %q %q %q\n' "$fixture" "$work/control" "$work/ready" "$mode" "$variant"
} > runner
run new-session -d -s repro -x 120 -y 30 cat
run set-option -g status off
run set-option -g remain-on-exit on
run set-option -g remain-on-exit-format ''
printf -v launch 'exec %q %q' "$BASH" "$work/runner"
run respawn-pane -k -t repro:0.0 "$launch"
visible() {
  run capture-pane -p -t repro:0.0 > screen
  grep -Fq -- "$1" screen
}
wait_for visible 'Welcome to resize reproduction'
if [[ $mode == retained* ]]; then run send-keys -t repro:0.0 hold Enter; fi
if [[ $draft == early ]]; then
  run send-keys -t repro:0.0 'draft survives resize'
  wait_for visible '> draft survives resize'
fi
printf F >&8
IFS= read -r -N 1 -t 8 ready <&9 || fail 'Producer did not reach its resize barrier'
[[ $ready == R ]] || fail 'Unexpected producer acknowledgement'
partial_ready() {
  run capture-pane -p -t repro:0.0 > screen
  if [[ $variant != partial ]]; then grep -Fq 'RESIZE READY' screen; return; fi
  awk '
    /RESIZE READY / {sub(/^.*RESIZE READY /, ""); count += length; started=1; next}
    started && /^x+$/ {count += length}
    END {exit count != 500}' screen
}
wait_for partial_ready
run capture-pane -p -t repro:0.0 > before-resize
run resize-window -x "$columns" -y "$rows"
run capture-pane -p -t repro:0.0 > after-resize-before-write
printf P >&8
wait_for visible 'Worked for'
if [[ $draft != early ]]; then run send-keys -t repro:0.0 'draft survives resize'; fi
wait_for visible '> draft survives resize'
run capture-pane -p -S - -t repro:0.0 > transcript
grep -Fxq '* Edited snake.c' screen || fail 'Old source text contaminated the edit heading'
for ((i=0; i<6; ++i)); do
  expected=$(printf '%4d +PATCH_ROW_%03d' "$((i+1))" "$i")
  grep -Fxq "$expected" screen || fail "Patch row $i was misplaced or overwritten"
done
awk '
  /^> draft survives resize/ {prompt=NR; count++}
  /[^[:space:]]/ {last=NR}
  END {exit !(count == 1 && prompt == last)}' screen || fail 'Final prompt is misplaced or duplicated'
run send-keys -t repro:0.0 C-u /quit Enter
dead() { [[ $(run display-message -p -t repro:0.0 '#{pane_dead}') == 1 ]]; }
wait_for dead
[[ $(run display-message -p -t repro:0.0 '#{pane_dead_status}') == 0 ]]
