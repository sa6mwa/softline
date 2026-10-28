#!/usr/bin/env bash
set -euo pipefail
root=$1
[[ $(uname -s) == Linux ]] || { echo 'SKIP: Linux shared-library upgrade fixture'; exit 0; }
work=$(mktemp -d "$root/build/libmdf-upgrade.XXXXXX")
trap 'rm -rf "$work"' EXIT
version=$(sed -n 's/^set(SOFTLINE_TEST_LIBMDF_VERSION "\([^"]*\)")/\1/p' "$root/cmake/softline_test_libmdf.cmake")
sdk="$work/sdk/libmdf-$version-x86_64-linux-gnu"
mkdir -p "$work/producer" "$work/consumer" "$sdk/include/libmdf" "$sdk/lib/pkgconfig"
cat > "$work/producer/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.24)
project(fake_mdf C)
add_library(mdf SHARED mdf.c)
set_target_properties(mdf PROPERTIES SOVERSION "${GENERATION}"
  LIBRARY_OUTPUT_DIRECTORY "${SDK}/lib")
CMAKE
printf 'int mdf_fixture(void) { return 0; }\n' > "$work/producer/mdf.c"
cat > "$sdk/lib/pkgconfig/libmdf.pc" <<EOF
prefix=\${pcfiledir}/../..
Name: libmdf
Description: SDK upgrade fixture
Version: $version
Libs: -L\${prefix}/lib -lmdf
Cflags: -I\${prefix}/include
EOF
cat > "$work/consumer/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.24)
project(mdf_consumer C)
include("${ROOT}/cmake/softline_test_libmdf.cmake")
include("${ROOT}/cmake/softline_verified_archive.cmake")
# Supply synthetic SDK bytes locally; acquisition has its own fixtures.
function(softline_verified_archive component url digest name output)
  set(${output} "${ARCHIVE}" PARENT_SCOPE)
endfunction()
set(SL_TARGET_ID x86_64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/empty-sysroot")
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
add_executable(consumer main.c)
softline_enable_dev_libmdf(consumer)
CMAKE
cat > "$work/consumer/main.c" <<'C'
#include <libmdf/mdf.h>
#include <stdio.h>
int main(void) { printf("%d\n", MDF_FIXTURE_GENERATION); return mdf_fixture(); }
C
for generation in 1 2; do
  cmake -S "$work/producer" -B "$work/producer/build" -G Ninja \
    -DGENERATION="$generation" -DSDK="$sdk"
  cmake --build "$work/producer/build"
  if [[ $generation == 2 ]]; then
    cmake -E rm -- "$sdk/lib/libmdf.so.1"
  fi
  printf '#define MDF_FIXTURE_GENERATION %s\nint mdf_fixture(void);\n' "$generation" > "$sdk/include/libmdf/mdf.h"
  # Match the normalized timestamps in the published SDKs.
  touch -t 197001020000 "$sdk/include/libmdf/mdf.h" "$sdk/lib/libmdf.so.$generation"
  (cd "$work/sdk" && cmake -E tar czf "$work/sdk.tar.gz" "${sdk##*/}")
  if [[ $generation == 2 ]]; then
    printf 'outdated dependency\n' > "$work/consumer/.cache/dependency-contracts/x86_64-linux-gnu/libmdf.txt"
  fi
  cmake -S "$work/consumer" -B "$work/consumer/build" -G Ninja \
    -DROOT="$root" -DARCHIVE="$work/sdk.tar.gz"
  cmake --build "$work/consumer/build"
  actual=$("$work/consumer/build/consumer")
  [[ $actual == "$generation" ]] || {
    echo "SDK upgrade left stale consumer headers: expected $generation, got $actual" >&2
    exit 1
  }
done
echo 'SDK replacement rebuilt headers and relinked the changed shared-library ABI.'
