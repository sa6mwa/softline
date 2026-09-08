#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
RESOLVER="${ROOT_DIR}/scripts/cpkt-toolchains.sh"

native_target="$(CPKT_HOST_SYSTEM=Linux CPKT_HOST_MACHINE=x86_64 "$RESOLVER" native-linux-target)"
if [ "${native_target}" != "x86_64-linux-gnu" ]; then
  echo "ERROR: x86_64 Linux hosts must select the x86_64 GNU Bootlin target" >&2
  exit 1
fi
if CPKT_HOST_SYSTEM=Linux CPKT_HOST_MACHINE=aarch64 "$RESOLVER" native-linux-target >/dev/null 2>&1; then
  echo "ERROR: aarch64 Linux hosts must not select x86_64-hosted Bootlin compilers as native" >&2
  exit 1
fi
if CPKT_HOST_SYSTEM=Linux CPKT_HOST_MACHINE=armv7l "$RESOLVER" native-linux-target >/dev/null 2>&1; then
  echo "ERROR: armv7 Linux hosts must not select x86_64-hosted Bootlin compilers as native" >&2
  exit 1
fi
if CPKT_HOST_SYSTEM=Linux CPKT_HOST_MACHINE=riscv64 "$RESOLVER" native-linux-target >/dev/null 2>&1; then
  echo "ERROR: unsupported Linux host architectures must not select a Bootlin fallback" >&2
  exit 1
fi
if CPKT_HOST_SYSTEM=Darwin CPKT_HOST_MACHINE=arm64 "$RESOLVER" native-linux-target >/dev/null 2>&1; then
  echo "ERROR: non-Linux hosts must not select a Bootlin Linux target" >&2
  exit 1
fi

if OSXCROSS_ROOT=/definitely/missing "$RESOLVER" ensure arm64-apple-darwin >/dev/null 2>&1; then
  echo "ERROR: explicit Darwin ensure must fail when osxcross is unavailable" >&2
  exit 1
fi
if grep -Eq '(^|[[:space:]])(mapfile|readarray)([[:space:]]|$)|\$\{[A-Za-z_][A-Za-z0-9_]*\^\^\}' "${RESOLVER}"; then
  echo "ERROR: toolchain resolver must remain compatible with stock macOS Bash 3.2" >&2
  exit 1
fi

tmp_osxcross="$(mktemp -d)"
stale_build="${ROOT_DIR}/build/toolchain-contract-stale-cache"
cleanup() {
  rm -rf "${tmp_osxcross}" "${stale_build}"
}
trap cleanup EXIT
darwin_bin="${tmp_osxcross}/bin"
darwin_sdk="${tmp_osxcross}/SDK/MacOSX15.sdk"
old_darwin_sdk="${tmp_osxcross}/SDK/MacOSX9.sdk"
darwin_host=arm64-apple-darwin99
mkdir -p "${darwin_bin}" "${darwin_sdk}" "${old_darwin_sdk}"
for tool in clang clang++ ld ar ranlib strip nm otool; do
  printf '#!/bin/sh\nexit 0\n' > "${darwin_bin}/${darwin_host}-${tool}"
  chmod +x "${darwin_bin}/${darwin_host}-${tool}"
done
if OSXCROSS_ROOT="${tmp_osxcross}" CPKT_OSXCROSS_HOST="${darwin_host}" "$RESOLVER" ensure arm64-apple-darwin >/dev/null 2>&1; then
  echo "ERROR: Darwin ensure must fail without install_name_tool" >&2
  exit 1
fi
printf '#!/bin/sh\nexit 0\n' > "${darwin_bin}/${darwin_host}-install_name_tool"
chmod +x "${darwin_bin}/${darwin_host}-install_name_tool"
darwin_description="$(OSXCROSS_ROOT="${tmp_osxcross}" CPKT_OSXCROSS_HOST="${darwin_host}" "$RESOLVER" ensure arm64-apple-darwin)"
for key in status root prefix sdk cc cxx ld ar ranlib strip nm otool install_name_tool; do
  if ! grep -q "^${key}=" <<<"${darwin_description}"; then
    echo "ERROR: Darwin resolver must report ${key}" >&2
    exit 1
  fi
done
if ! grep -qx "sdk=${darwin_sdk}" <<<"${darwin_description}"; then
  echo "ERROR: Darwin resolver must select the newest SDK using the CMake toolchain ordering" >&2
  exit 1
fi
cmake_probe="${tmp_osxcross}/probe"
mkdir -p "${cmake_probe}"
cat > "${cmake_probe}/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.16)
project(darwin_toolchain_probe LANGUAGES NONE)
file(WRITE "${CMAKE_BINARY_DIR}/sdk.txt" "${CMAKE_OSX_SYSROOT}\n")
EOF
OSXCROSS_ROOT="${tmp_osxcross}" CPKT_OSXCROSS_HOST="${darwin_host}" \
  cmake -S "${cmake_probe}" -B "${cmake_probe}/build" \
    -DCMAKE_TOOLCHAIN_FILE="${ROOT_DIR}/cmake/toolchains/osxcross-darwin.cmake" \
    >/dev/null
if [ "$(cat "${cmake_probe}/build/sdk.txt")" != "${darwin_sdk}" ]; then
  echo "ERROR: Darwin CMake toolchain must select the resolver SDK" >&2
  exit 1
fi

rm -rf "${stale_build}"
CPKT_HOST_SYSTEM=Darwin CPKT_HOST_MACHINE=arm64 \
  cmake -S "${ROOT_DIR}" -B "${stale_build}" -G Ninja \
    -DSL_BUILD_TESTS=OFF \
    -DSL_BUILD_EXAMPLES=OFF \
    -DSL_INSTALL=OFF \
    >/dev/null
if cmake -S "${ROOT_DIR}" -B "${stale_build}" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="${ROOT_DIR}/cmake/toolchains/bootlin-linux.cmake" \
  -DSL_BUILD_TESTS=OFF \
  -DSL_BUILD_EXAMPLES=OFF \
  -DSL_INSTALL=OFF \
  >/dev/null 2>"${stale_build}/reconfigure.err"; then
  echo "ERROR: stale host-compiler CMake caches must not survive the lifecycle toolchain cutover" >&2
  exit 1
fi
if ! grep -q 'Stale or incompatible CMake cache' "${stale_build}/reconfigure.err"; then
  echo "ERROR: stale CMake cache rejection must explain the lifecycle toolchain mismatch" >&2
  cat "${stale_build}/reconfigure.err" >&2
  exit 1
fi

sha256_file() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | awk '{print $1}'
  else
    shasum -a 256 "$1" | awk '{print $1}'
  fi
}

while IFS='|' read -r target archive expected_sha256; do
  description="$("$RESOLVER" ensure "${target}")"
  cache="$(sed -n 's/^cache=//p' <<<"${description}")"
  if ! grep -qx "archive=${archive}.tar.xz" <<<"${description}"; then
    echo "ERROR: ${target} must resolve ${archive}.tar.xz" >&2
    exit 1
  fi
  if [ "$(sha256_file "${cache}/archives/${archive}.tar.xz")" != "${expected_sha256}" ]; then
    echo "ERROR: ${target} must retain the pinned ${archive} checksum" >&2
    exit 1
  fi
done <<'EOF'
x86_64-linux-gnu|x86-64--glibc--stable-2026.08-1|cde893afab04ac7dcd15c46aac214ff550441b982536124c88a71146a0eeedd3
x86_64-linux-musl|x86-64--musl--stable-2026.08-1|78d3a4683d6ac47b5ee73bd5bce210b55eb93dff1b137c61298af97eb0d2b5a6
aarch64-linux-gnu|aarch64--glibc--stable-2026.08-1|0213efac9b5577f20d58de9431960a191347ffc2257b27ffe7250522bf1f7867
aarch64-linux-musl|aarch64--musl--stable-2026.08-1|b388c480a48e8e9f9b99e3d14e69219c4d61e5a2424a82faecb88a015b781a60
armhf-linux-gnu|armv7-eabihf--glibc--stable-2026.08-1|9b7e25a74e87dac1e05d399444295e254a3073a056101e3197a859490e5701cd
armhf-linux-musl|armv7-eabihf--musl--stable-2026.08-1|9147bafae4aa272321a3c6440d04d83b7e23411b2d344f875541d84c4444ba9b
EOF

description="$("$RESOLVER" ensure x86_64-linux-gnu)"
if ! grep -qx 'status=ready' <<<"${description}"; then
  echo "ERROR: x86_64-linux-gnu toolchain must be ready for the lifecycle contract" >&2
  exit 1
fi

expected_cc="$(sed -n 's/^cc=//p' <<<"${description}")"
expected_ld="$(sed -n 's/^ld=//p' <<<"${description}")"
expected_ar="$(sed -n 's/^ar=//p' <<<"${description}")"
expected_libstdcxx="$(sed -n 's/^libstdcxx_a=//p' <<<"${description}")"
expected_libgcc="$(sed -n 's/^libgcc_a=//p' <<<"${description}")"
if [ ! -f "${expected_libstdcxx}" ] || [ ! -f "${expected_libgcc}" ]; then
  echo "ERROR: toolchain resolver must report existing static runtime archives" >&2
  exit 1
fi
env_output="$("$RESOLVER" env x86_64-linux-gnu)"
(
  eval "${env_output}"
  [[ "${CC}" == "${expected_cc}" ]]
  [[ "${LD}" == "${expected_ld}" ]]
  [[ "${AR}" == "${expected_ar}" ]]
  [[ "${CPKT_TOOLCHAIN_CC}" == "${expected_cc}" ]]
  [[ "${CPKT_TOOLCHAIN_LIBSTDCXX_A}" == "${expected_libstdcxx}" ]]
  [[ "${CPKT_TOOLCHAIN_LIBGCC_A}" == "${expected_libgcc}" ]]
)

echo "Toolchain contract tests passed."
