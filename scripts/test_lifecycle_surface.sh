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
  help deps-debug deps-release deps-cross build build-debug build-release \
  test test-debug test-all asan valgrind valgrind-portable fuzz fuzz-smoke fuzz-portable fuzz-long package package-source \
  package-source-smoke package-consumer-smoke package-checksums package-verify \
  verify-release-archives verify-release-privacy release-matrix \
  finalize-slice prerelease prerelease-hardening release release-pipeline \
  lifecycle-version-contract \
  print-release-version format clean clean-dist lua-rock lua-test lua-env \
  lua-test-lib64 lua-debug-test lua-debug-env lua-debug-simple lua-debug-chat \
  release-lua-artifacts validate-luarocks test-lua-artifact-privacy test-tool-discovery \
  test-darwin-linker-route test-release-version test-package-source-worktree test-toolchain-contract test-lifecycle-surface test-clangd \
  test-public-header-docs; do
  require_make_target "${target}"
done

if ! grep -Eq '^test-all:.*valgrind-portable' "${ROOT_DIR}/Makefile"; then
  echo "ERROR: aggregate local gates must use the portable Valgrind wrapper" >&2
  exit 1
fi

if ! grep -q 'softline_reject_stale_linux_toolchain_cache' "${ROOT_DIR}/CMakeLists.txt" ||
   ! grep -q 'Stale or incompatible CMake cache' "${ROOT_DIR}/CMakeLists.txt"; then
  echo "ERROR: CMake must reject stale host-compiler caches after lifecycle toolchain cutover" >&2
  exit 1
fi

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
    "linux-base",
    "debug",
    "debug-lua",
    "asan",
    "fuzz",
    "valgrind",
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
    if name in ["base", "linux-base"]:
        continue
    if name not in build:
        raise SystemExit(f"ERROR: missing build preset: {name}")

base_cache = configure["base"].get("cacheVariables", {})
if "CMAKE_TOOLCHAIN_FILE" in base_cache:
    raise SystemExit("ERROR: native base preset must not select a cross toolchain")
linux_base_cache = configure["linux-base"].get("cacheVariables", {})
if linux_base_cache.get("CMAKE_TOOLCHAIN_FILE") != "${sourceDir}/cmake/toolchains/bootlin-linux.cmake":
    raise SystemExit("ERROR: linux-base preset must select the pinned Bootlin toolchain")

for name in ["debug", "debug-lua", "asan"]:
    if configure[name].get("inherits") not in ["linux-base", "debug"]:
        raise SystemExit(f"ERROR: {name} must inherit the native Linux toolchain preset")
    cache = configure[name].get("cacheVariables", {})
    effective_cache = dict(configure["debug"].get("cacheVariables", {}))
    effective_cache.update(cache)
    if "SL_TARGET_ID" in effective_cache:
        raise SystemExit(f"ERROR: {name} must not pin the native Bootlin target")
if configure["fuzz"].get("inherits") != "linux-base":
    raise SystemExit("ERROR: fuzz must inherit the pinned Linux toolchain")
if configure["valgrind"].get("inherits") != "linux-base":
    raise SystemExit("ERROR: valgrind must inherit the pinned Linux toolchain")
valgrind_cache = configure["valgrind"].get("cacheVariables", {})
if "SL_TARGET_ID" in valgrind_cache:
    raise SystemExit("ERROR: valgrind must not pin the native Bootlin target")
fuzz_cache = configure["fuzz"].get("cacheVariables", {})
if fuzz_cache.get("SL_TARGET_ID") != "x86_64-linux-gnu":
    raise SystemExit("ERROR: fuzz must remain native x86_64 Linux-only")

for name in ["debug", "debug-lua", "asan", "fuzz", "valgrind"]:
    if name not in tests:
        raise SystemExit(f"ERROR: missing test preset: {name}")

for name in ["debug", "debug-lua", "asan", "fuzz", "valgrind"]:
    preset = configure[name]
    cache = preset.get("cacheVariables", {})
    if "CMAKE_C_COMPILER" in cache:
        raise SystemExit(f"ERROR: {name} must not pin or fall back to a host compiler")

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
    if name != "arm64-apple-darwin-release" and "CMAKE_C_COMPILER" in cache:
        raise SystemExit(f"ERROR: {name} must not select a host compiler")

host_release = configure["x86_64-linux-gnu-release"].get("cacheVariables", {})
if host_release.get("SL_BUILD_TESTS") != "ON" or host_release.get("SL_BUILD_EXAMPLES") != "ON":
    raise SystemExit("ERROR: native release preset must build host-executable tests")

darwin = configure["arm64-apple-darwin-release"].get("cacheVariables", {})
if "CMAKE_TOOLCHAIN_FILE" not in darwin:
    raise SystemExit("ERROR: Darwin release preset must use a toolchain file")

print("Lifecycle surface tests passed.")
PY

for path in \
  scripts/cpkt-toolchains.sh scripts/cpkt-aflpp.sh \
  cmake/toolchains/bootlin-linux.cmake cmake/toolchains/aflpp-linux.cmake \
  fuzz/softline_stdin_fuzz.c fuzz/corpus/basic; do
  if [ ! -e "${ROOT_DIR}/${path}" ]; then
    echo "ERROR: missing lifecycle asset: ${path}" >&2
    exit 1
  fi
done

if ! grep -A3 '^release:' "${ROOT_DIR}/Makefile" | grep -q 'lifecycle-version-contract'; then
  echo "ERROR: release must run lifecycle-version-contract first" >&2
  exit 1
fi
if ! grep -A4 '^release:' "${ROOT_DIR}/Makefile" | grep -q '$(MAKE) clean'; then
  echo "ERROR: release must clean before its shared proof graph" >&2
  exit 1
fi
if ! grep -A5 '^release:' "${ROOT_DIR}/Makefile" | grep -q 'SOFTLINE_REQUIRE_DARWIN=1'; then
  echo "ERROR: release must require a Darwin artifact" >&2
  exit 1
fi
if ! grep -A2 '^prerelease:' "${ROOT_DIR}/Makefile" | grep -q '$(MAKE) release-pipeline'; then
  echo "ERROR: prerelease must run the shared release-pipeline" >&2
  exit 1
fi
if ! grep -q 'Darwin artifact is required for make release' "${ROOT_DIR}/scripts/package.sh"; then
  echo "ERROR: package flow must fail final releases without Darwin" >&2
  exit 1
fi
if ! grep -q 'ctest --test-dir "${build_dir}" --output-on-failure' "${ROOT_DIR}/scripts/package.sh" ||
   ! grep -q 'release tests failed' "${ROOT_DIR}/scripts/package.sh"; then
  echo "ERROR: release matrix must run host-executable release tests" >&2
  exit 1
fi
if grep -q "name 'softline-\\*.tar.gz'" "${ROOT_DIR}/scripts/package-checksums.sh" ||
   ! grep -q 'softline-${VERSION}-\*.tar.gz' "${ROOT_DIR}/scripts/package-checksums.sh"; then
  echo "ERROR: checksum generation must use current-version-qualified release artifact patterns" >&2
  exit 1
fi
if ! grep -q 'deprecated checksum manifest present' "${ROOT_DIR}/scripts/package-verify.sh" ||
   ! grep -q 'SHA256SUMS' "${ROOT_DIR}/scripts/package-verify.sh"; then
  echo "ERROR: package verification must reject deprecated checksum manifests" >&2
  exit 1
fi
if grep -q 'xargs .* -r' "${ROOT_DIR}/Makefile" ||
   grep -q 'xargs -0 -r' "${ROOT_DIR}/Makefile"; then
  echo "ERROR: format target must not depend on GNU-only xargs -r" >&2
  exit 1
fi
if ! grep -Eq '^package-source-smoke: package-source([[:space:]]|$)' "${ROOT_DIR}/Makefile"; then
  echo "ERROR: package-source-smoke must depend on package-source" >&2
  exit 1
fi
if ! grep -q 'cmake/toolchains/bootlin-linux.cmake' "${ROOT_DIR}/scripts/package-source-smoke.sh" ||
   grep -q 'SL_TARGET_ID=x86_64-linux-gnu' "${ROOT_DIR}/scripts/package-source-smoke.sh"; then
  echo "ERROR: source archive smoke must build with the native resolved Bootlin toolchain" >&2
  exit 1
fi
if grep -q 'project(softline_version_probe VERSION 0.0.0 LANGUAGES C)' "${ROOT_DIR}/scripts/test_release_version.sh"; then
  echo "ERROR: release-version CMake fixture must not invoke ambient C compiler discovery" >&2
  exit 1
fi
if ! grep -q 'lifecycle readelf is required' "${ROOT_DIR}/scripts/package-consumer-smoke.sh" ||
   ! grep -q 'command -v readelf' "${ROOT_DIR}/scripts/package-consumer-smoke.sh" ||
   ! grep -q 'SKIP: shared SONAME check requires readelf' "${ROOT_DIR}/scripts/package-consumer-smoke.sh"; then
  echo "ERROR: package consumer smoke must require lifecycle readelf for Bootlin builds and preserve host fallback" >&2
  exit 1
fi
if ! awk '/verify_shared_abi\(\)/ { in_func = 1; next } in_func && /^}/ { exit } in_func && /libsoftline.dylib/ { dylib = NR } in_func && /shared install missing libsoftline.so/ { so = NR } END { exit !(dylib && so && dylib < so) }' "${ROOT_DIR}/scripts/package-consumer-smoke.sh"; then
  echo "ERROR: package consumer smoke must handle Darwin dylib host fallback before requiring ELF soname layout" >&2
  exit 1
fi
if ! awk '/for target in \$\{TARGETS\}/ { in_loop = 1; next } in_loop && /cmake --preset "\$\{preset\}"/ { cmake = NR } in_loop && /rm -rf "\$\{build_dir\}"/ { clean = NR } END { exit !(clean && cmake && clean < cmake) }' "${ROOT_DIR}/scripts/package.sh"; then
  echo "ERROR: package builds must invalidate stale CMake caches before configuring release targets" >&2
  exit 1
fi
if grep -q 'BOOTLIN_CMAKE_ARGS=' "${ROOT_DIR}/scripts/package-consumer-smoke.sh" ||
   ! grep -Fq '${BOOTLIN_TOOLCHAIN_ARG:+"${BOOTLIN_TOOLCHAIN_ARG}"}' "${ROOT_DIR}/scripts/package-consumer-smoke.sh" ||
   ! grep -Fq '${BOOTLIN_TARGET_ARG:+"${BOOTLIN_TARGET_ARG}"}' "${ROOT_DIR}/scripts/package-consumer-smoke.sh"; then
  echo "ERROR: package consumer smoke must pass optional Bootlin CMake args as quoted arguments" >&2
  exit 1
fi
if ! grep -A4 'cd "${ROOT_DIR}"' "${ROOT_DIR}/scripts/lua-test.sh" | grep -q 'cmake --preset debug-lua' ||
   ! grep -A4 'cd "${ROOT_DIR}"' "${ROOT_DIR}/scripts/lua-debug.sh" | grep -q 'cmake --preset debug' ||
   ! awk '/cd "\$\{ROOT_DIR\}"/ { cd_seen = 1 } cd_seen && /luarocks --tree "\$\{LUA_TREE\}" make/ { found = 1 } END { exit !found }' "${ROOT_DIR}/scripts/lua-test.sh" ||
   ! awk '/cd "\$\{ROOT_DIR\}"/ { cd_seen = 1 } cd_seen && /luarocks --tree "\$\{LUA_TREE\}" make/ { found = 1 } END { exit !found }' "${ROOT_DIR}/scripts/lua-debug.sh"; then
  echo "ERROR: Lua preset scripts must resolve presets from the repository root" >&2
  exit 1
fi
if ! grep -A4 'valgrind --leak-check=full' "${ROOT_DIR}/Makefile" | grep -q -- '--trace-children=yes'; then
  echo "ERROR: Valgrind example gate must trace spawned example processes" >&2
  exit 1
fi
if ! awk '/^valgrind:/ { in_target = 1; next } in_target && /^[^[:space:]].*:/ { exit } in_target && /cmake --preset valgrind/ { found = 1 } END { exit !found }' "${ROOT_DIR}/Makefile"; then
  echo "ERROR: Valgrind gate must configure with the pinned-toolchain preset" >&2
  exit 1
fi
if ! grep -q 'CACHE FILEPATH "Darwin C compiler" FORCE' "${ROOT_DIR}/cmake/toolchains/osxcross-darwin.cmake" ||
   ! grep -q 'CACHE FILEPATH "Darwin linker" FORCE' "${ROOT_DIR}/cmake/toolchains/osxcross-darwin.cmake" ||
   ! grep -q 'CACHE PATH "Darwin SDK" FORCE' "${ROOT_DIR}/cmake/toolchains/osxcross-darwin.cmake" ||
   ! grep -q 'REGEX REPLACE "(^| )--ld-path=' "${ROOT_DIR}/cmake/toolchains/osxcross-darwin.cmake"; then
  echo "ERROR: Darwin toolchain must refresh cached osxcross tools, SDK, and linker route" >&2
  exit 1
fi
if ! grep -A4 '^release-pipeline:' "${ROOT_DIR}/Makefile" | grep -q '$(MAKE) prerelease-checks' ||
   ! grep -A4 '^release-pipeline:' "${ROOT_DIR}/Makefile" | grep -q '$(MAKE) release-matrix'; then
  echo "ERROR: release-pipeline must run checks before release-matrix" >&2
  exit 1
fi
if ! grep -q 'permanently reserved test-only tag' "${ROOT_DIR}/scripts/lifecycle-version-contract.sh"; then
  echo "ERROR: lifecycle version contract must document v99.99.99 as test-only" >&2
  exit 1
fi
if ! grep -q 'LUA_INCDIR}/lua.h' "${ROOT_DIR}/scripts/build_lua_rock.sh" ||
   ! grep -q 'LUA_VERSION_MAJOR_N' "${ROOT_DIR}/scripts/build_lua_rock.sh"; then
  echo "ERROR: Lua facade build must validate LuaRocks-selected Lua headers" >&2
  exit 1
fi
if ! grep -q 'SOFTLINE_LUA_CC' "${ROOT_DIR}/scripts/build_lua_rock.sh" ||
   ! grep -q 'LUA_COMPILE_INCDIR' "${ROOT_DIR}/scripts/build_lua_rock.sh" ||
   ! grep -q 'SOFTLINE_LUA_CC="${CC}"' "${ROOT_DIR}/scripts/lua-test.sh" ||
   ! grep -q 'ensure "${NATIVE_TARGET}"' "${ROOT_DIR}/scripts/lua-test.sh" ||
   ! grep -q 'native-linux-target 2>/dev/null' "${ROOT_DIR}/scripts/lua-test.sh" ||
   ! grep -q 'export CC LD AR RANLIB SOFTLINE_LUA_CC' "${ROOT_DIR}/scripts/lua-test.sh"; then
  echo "ERROR: Lua facade builds must prefer the lifecycle compiler over LuaRocks compiler defaults" >&2
  exit 1
fi
if ! grep -q 'supports Lua 5.5 only' "${ROOT_DIR}/scripts/build_lua_rock.sh"; then
  echo "ERROR: Lua facade build must support Lua 5.5 only" >&2
  exit 1
fi
if ! grep -q 'SOFTLINE_DIST_DIR' "${ROOT_DIR}/scripts/validate_luarocks.sh" ||
   ! grep -q 'package-manager temporary path' "${ROOT_DIR}/scripts/validate_luarocks.sh" ||
   ! grep -q 'test-lua-artifact-privacy' "${ROOT_DIR}/Makefile"; then
  echo "ERROR: Lua release artifact validation must include negative privacy regression coverage" >&2
  exit 1
fi
if ! grep -q 'rev-parse --show-toplevel' "${ROOT_DIR}/scripts/package-source.sh" ||
   ! grep -q 'outside git requires RELEASE_MANIFEST' "${ROOT_DIR}/scripts/package-source.sh"; then
  echo "ERROR: source packaging must require an explicit non-git manifest" >&2
  exit 1
fi
if ! grep -q 'verify_extracted_consumer' "${ROOT_DIR}/scripts/package-verify.sh"; then
  echo "ERROR: package verification must smoke extracted SDK consumers" >&2
  exit 1
fi
if ! grep -q 'package-source-smoke.sh' "${ROOT_DIR}/scripts/package-verify.sh" ||
   grep -q 'source archive, skipped by binary package verifier' "${ROOT_DIR}/scripts/package-verify.sh"; then
  echo "ERROR: package verification must verify checksum-listed C source archives" >&2
  exit 1
fi
if ! grep -q 'cpkt-toolchains.sh" env' "${ROOT_DIR}/scripts/package-verify.sh"; then
  echo "ERROR: package verification must resolve lifecycle toolchains without preserved build caches" >&2
  exit 1
fi
if ! grep -q 'arm64-apple-darwin) ;;' "${ROOT_DIR}/scripts/package-verify.sh" ||
   ! grep -q -- '-DSL_TARGET_ID=${target}' "${ROOT_DIR}/scripts/package-verify.sh"; then
  echo "ERROR: package verification must keep Darwin consumer config warning-clean while preserving Linux target identity" >&2
  exit 1
fi
if ! grep -q -- '--ld-path="${LINKER}"' "${ROOT_DIR}/scripts/package-verify.sh"; then
  echo "ERROR: Darwin pkg-config consumers must route through the target linker" >&2
  exit 1
fi
if ! grep -q 'native-linux-target' "${ROOT_DIR}/scripts/cpkt-toolchains.sh" ||
   ! grep -q 'x86_64-hosted' "${ROOT_DIR}/scripts/cpkt-toolchains.sh" ||
   ! grep -q 'target is unavailable' "${ROOT_DIR}/scripts/cpkt-toolchains.sh" ||
   ! grep -q 'export %s=%q' "${ROOT_DIR}/scripts/cpkt-toolchains.sh"; then
  echo "ERROR: toolchain resolver must select native Linux targets, fail unavailable explicit ensures, and export active tools" >&2
  exit 1
fi
if ! grep -q 'native-linux-target' "${ROOT_DIR}/cmake/toolchains/bootlin-linux.cmake" ||
   ! grep -q 'No supported native Bootlin Linux target selected; using host toolchain' "${ROOT_DIR}/cmake/toolchains/bootlin-linux.cmake"; then
  echo "ERROR: Bootlin CMake toolchain must resolve supported native targets before host fallback" >&2
  exit 1
fi
if ! grep -q 'env_out()' "${ROOT_DIR}/scripts/cpkt-aflpp.sh"; then
  echo "ERROR: AFL++ resolver must expose an environment contract" >&2
  exit 1
fi
if ! awk '/^fuzz-smoke:/ { in_target = 1; next } in_target && /^[^[:space:]].*:/ { exit } in_target && /cpkt-aflpp.sh ensure/ { ensure = NR } in_target && /cmake --preset fuzz/ { preset = NR } END { exit !(ensure && preset && ensure < preset) }' "${ROOT_DIR}/Makefile"; then
  echo "ERROR: fuzz-smoke must bound AFL++ provisioning before CMake can discover it" >&2
  exit 1
fi
if ! awk '/^test-all:/ && /fuzz-portable/ { found = 1 } END { exit !found }' "${ROOT_DIR}/Makefile" ||
   ! awk '/^fuzz-portable:/ { in_target = 1; next } in_target && /^[^[:space:]].*:/ { exit } in_target && /fuzz-smoke requires native x86_64 Linux/ { found = 1 } END { exit !found }' "${ROOT_DIR}/Makefile"; then
  echo "ERROR: portable aggregate tests must skip x86_64-only fuzzing on unsupported hosts" >&2
  exit 1
fi
for script in scripts/lua-test.sh scripts/lua-debug.sh scripts/package-consumer-smoke.sh; do
  if ! grep -q 'native-linux-target' "${ROOT_DIR}/${script}" ||
     ! grep -q 'ensure "' "${ROOT_DIR}/${script}" ||
     grep -q 'env x86_64-linux-gnu' "${ROOT_DIR}/${script}"; then
    echo "ERROR: ${script} must provision and resolve the native Linux Bootlin target" >&2
    exit 1
  fi
done
if ! grep -q 'sort -k1,1r' "${ROOT_DIR}/scripts/cpkt-toolchains.sh"; then
  echo "ERROR: Darwin SDK discovery must sort by numeric SDK version" >&2
  exit 1
fi
if ! grep -Fq 'revision=5' "${ROOT_DIR}/scripts/cpkt-aflpp.sh" ||
   ! grep -Fq 'BIN_PATH="$r/bin"' "${ROOT_DIR}/scripts/cpkt-aflpp.sh" ||
   ! grep -Fq 'DOC_PATH="$doc_path"' "${ROOT_DIR}/scripts/cpkt-aflpp.sh" ||
   ! grep -Fq '$r/include/afl' "${ROOT_DIR}/scripts/cpkt-aflpp.sh" ||
   ! grep -Fq '"$strip" "$tmp/root/bin/afl-fuzz"' "${ROOT_DIR}/scripts/cpkt-aflpp.sh" ||
   ! grep -q '\$r/lib/afl.*\$r/bin/afl-gcc-fast.*> "\$tmp/root/bin/cpkt-afl-gcc"' "${ROOT_DIR}/scripts/cpkt-aflpp.sh" ||
   ! grep -q '\$r/lib/afl.*\$r/bin/afl-g++-fast.*> "\$tmp/root/bin/cpkt-afl-g++"' "${ROOT_DIR}/scripts/cpkt-aflpp.sh" ||
   ! grep -Fq 'rm -rf "$tmp"' "${ROOT_DIR}/scripts/cpkt-aflpp.sh" ||
   ! grep -Fq 'dynamic_list.txt' "${ROOT_DIR}/scripts/cpkt-aflpp.sh"; then
  echo "ERROR: AFL++ wrappers must publish final cache paths, clean staging, and invalidate stale wrappers" >&2
  exit 1
fi
