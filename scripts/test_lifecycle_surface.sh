#!/bin/sh
set -eu

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

require_make_target() {
  target="$1"
  if ! grep -Eq "^${target}:" "${ROOT_DIR}/Makefile"; then
    echo "ERROR: missing Make target: ${target}" >&2
    exit 1
  fi
}

for target in \
  help deps-debug deps-release build build-debug build-release \
  test test-debug test-all asan package package-source \
  package-source-smoke package-consumer-smoke package-checksums package-verify \
  verify-release-archives verify-release-privacy release-matrix \
  finalize-slice prerelease prerelease-hardening release \
  print-release-version format clean clean-dist lua-rock lua-test lua-env \
  lua-test-lib64 lua-debug-test lua-debug-env lua-debug-simple lua-debug-chat \
  release-lua-artifacts validate-luarocks test-tool-discovery \
  test-darwin-linker-route test-release-version test-lifecycle-surface test-clangd \
  test-public-header-docs; do
  require_make_target "${target}"
done

if [ ! -f "${ROOT_DIR}/.clang-format" ]; then
  echo "ERROR: missing .clang-format" >&2
  exit 1
fi

if [ ! -f "${ROOT_DIR}/.clangd" ]; then
  echo "ERROR: missing .clangd" >&2
  exit 1
fi

if ! grep -Eq '^[[:space:]]+CompilationDatabase:\s+build/debug$' "${ROOT_DIR}/.clangd"; then
  echo "ERROR: .clangd must point clangd at build/debug compile_commands.json" >&2
  exit 1
fi

if ! grep -Eq '^[[:space:]]+- -Iinclude$' "${ROOT_DIR}/.clangd" ||
   ! grep -Eq '^[[:space:]]+- -Isrc$' "${ROOT_DIR}/.clangd"; then
  echo "ERROR: .clangd must provide include/src fallback flags" >&2
  exit 1
fi

python3 - "${ROOT_DIR}/CMakePresets.json" <<'PY'
import json
import sys

path = sys.argv[1]
with open(path, "r", encoding="utf-8") as f:
    data = json.load(f)

configure = {p["name"]: p for p in data.get("configurePresets", [])}
build = {p["name"]: p for p in data.get("buildPresets", [])}
tests = {p["name"]: p for p in data.get("testPresets", [])}

required_configure = [
    "base",
    "debug",
    "asan",
    "x86_64-linux-gnu-release",
    "x86_64-linux-musl-release",
    "aarch64-linux-gnu-release",
    "aarch64-linux-musl-release",
    "armhf-linux-gnu-release",
    "armhf-linux-musl-release",
    "arm64-apple-darwin-release",
]
for name in required_configure:
    if name not in configure:
        raise SystemExit(f"ERROR: missing configure preset: {name}")

for name in required_configure:
    if name == "base":
        continue
    if name not in build:
        raise SystemExit(f"ERROR: missing build preset: {name}")

for name in ["debug", "asan"]:
    if name not in tests:
        raise SystemExit(f"ERROR: missing test preset: {name}")

for name, preset in configure.items():
    if not name.endswith("-release"):
        continue
    cache = preset.get("cacheVariables", {})
    if cache.get("SL_TARGET_ID") != name.removesuffix("-release"):
        raise SystemExit(f"ERROR: {name} has wrong or missing SL_TARGET_ID")
    if "SL_DIST_DIR" not in cache:
        raise SystemExit(f"ERROR: {name} missing SL_DIST_DIR")
    if "CMAKE_INSTALL_PREFIX" not in cache:
        raise SystemExit(f"ERROR: {name} missing CMAKE_INSTALL_PREFIX")

darwin = configure["arm64-apple-darwin-release"].get("cacheVariables", {})
if "CMAKE_TOOLCHAIN_FILE" not in darwin:
    raise SystemExit("ERROR: Darwin release preset must use a toolchain file")

print("Lifecycle surface tests passed.")
PY
