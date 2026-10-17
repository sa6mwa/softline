#!/usr/bin/env bash
set -euo pipefail
root=$1
work=$(mktemp -d "$root/build/clangd-gate.XXXXXX")
trap 'rm -rf -- "$work"' EXIT
fixture="$work/source with spaces"
mkdir -p "$fixture/scripts" "$fixture/cmake" "$fixture/build/debug" "$work/bin"
cp "$root/scripts/"{test_clangd.sh,require-host-bash.sh,require-workspace.sh} "$fixture/scripts/"
cp "$root/cmake/softline_workspace.cmake" "$fixture/cmake/"
printf 'CompileFlags: {}\n' > "$fixture/.clangd"
printf '[]\n' > "$fixture/build/debug/compile_commands.json"
cat > "$work/bin/clangd" <<'SH'
#!/usr/bin/env bash
if [[ $CLANGD_CASE != missing_config ]]; then
  printf 'Loading config file at %s/.clangd\n' "$FIXTURE_ROOT"
fi
if [[ $CLANGD_CASE != missing_database ]]; then
  printf 'Loaded compilation database from %s/build/debug/compile_commands.json\n' "$FIXTURE_ROOT"
fi
case "$CLANGD_CASE" in
  success|missing_config|missing_database) echo 'All checks completed, 0 errors'; exit 0 ;;
  semantic) echo "[undeclared_var_use] Line 1: use of undeclared identifier 'missing'" ;;
  mixed) echo "[undeclared_var_use] Line 1: use of undeclared identifier 'missing'"; echo 'tweak: SwapBinaryOperands ==> FAIL' ;;
  tweak) echo 'tweak: SwapBinaryOperands ==> FAIL' ;;
  crash) exit 139 ;;
esac
echo 'All checks completed, 1 errors'
exit 1
SH
chmod +x "$work/bin/clangd"
export PATH="$work/bin:$PATH" FIXTURE_ROOT="$fixture"
for state in success semantic mixed tweak missing_config missing_database crash; do
  if CLANGD_CASE=$state "$fixture/scripts/test_clangd.sh" > "$work/log" 2>&1; then
    [[ $state == success ]] || { echo "clangd accepted $state failure" >&2; exit 1; }
  else
    [[ $state != success ]] || { cat "$work/log"; exit 1; }
  fi
done
echo 'clangd requires successful semantics and the selected configuration/database.'
