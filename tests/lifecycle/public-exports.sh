#!/usr/bin/env bash
set -euo pipefail
root=$1
work=$(mktemp -d "$root/build/export-gate.XXXXXX")
trap 'rm -rf -- "$work"' EXIT
printf 'sl_config_init\n' > "$work/allowlist"
cat > "$work/nm" <<'SH'
#!/usr/bin/env bash
[[ "$EXPORT_CASE" != failure ]] || exit 3
if [[ "$1" == -D ]]; then
  printf 'sl_config_init@@SOFTLINE_1 T 123\nSOFTLINE_1 A 0\n'
  [[ "$EXPORT_CASE" != data ]] || printf 'sl_private_data D 456\n'
else
  printf '_sl_config_init\n'
  [[ "$EXPORT_CASE" != data ]] || printf '_sl_private_data\n'
fi
exit 0
SH
chmod +x "$work/nm"
for format in elf macho; do
  for state in good data failure; do
    if EXPORT_CASE=$state bash "$root/scripts/verify-public-exports.sh" "$work/nm" \
        "$work/library" "$work/allowlist" "$format" > "$work/log" 2>&1; then
      [[ "$state" == good ]] || { echo "Export gate accepted $format $state" >&2; exit 1; }
    else
      [[ "$state" != good ]] || { cat "$work/log"; exit 1; }
    fi
  done
done
echo 'ELF and Mach-O export gates reject extra data exports and failed inspection.'
