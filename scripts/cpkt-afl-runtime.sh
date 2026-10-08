#!/usr/bin/env bash
case ${BASH_SOURCE[0]} in
  */*) source "${BASH_SOURCE[0]%/*}/require-host-bash.sh" ;;
  *) source ./require-host-bash.sh ;;
esac || exit $?
# Private Bootlin runtime for AFL++ and the compiler backends loading its plugins.
# Sets cpkt_afl_runtime_{loader,library_path,link_options,cc,cxx} for the builder.
cpkt_afl_prepare_runtime() {
  local stage=$1 cc=$2 cxx=$3 sysroot=$4 collection=$5
  local library backend compiler name backend_dir
  collection=$(CDPATH= cd -- "$collection" && pwd -P) || return "$?"
  sysroot=$(CDPATH= cd -- "$sysroot" && pwd -P) || return "$?"
  [[ "$sysroot" == "$collection/"* ]] || die 'AFL++ requires the selected Bootlin sysroot'
  for compiler in "$cc" "$cxx"; do
    [[ -x "$compiler" && "$(readlink -f -- "$compiler")" == "$collection/"* ]] || die 'AFL++ requires the selected Bootlin compiler'
  done
  cpkt_afl_runtime_loader="$sysroot/lib/ld-linux-x86-64.so.2"
  [[ -x "$cpkt_afl_runtime_loader" ]] || die "Bootlin runtime loader is missing: $cpkt_afl_runtime_loader"
  library=$("$cxx" -print-file-name=libstdc++.so.6) || return "$?"
  library=$(readlink -f -- "$library") || return "$?"
  [[ -f "$library" && "$library" == "$collection/"* ]] || die 'Bootlin C++ runtime is missing or outside the selected collection'
  cpkt_afl_runtime_library_path="$sysroot/lib:$sysroot/usr/lib:$(dirname -- "$library"):$collection/lib"
  cpkt_afl_runtime_link_options=(
    "-Wl,--dynamic-linker,$cpkt_afl_runtime_loader"
    -Wl,--disable-new-dtags
    "-Wl,-rpath,$cpkt_afl_runtime_library_path")
  backend_dir="$stage/libexec/bootlin-runtime"
  mkdir -p "$stage/bin" "$backend_dir"
  for name in cc1 cc1plus; do
    compiler=$cc
    [[ "$name" != cc1plus ]] || compiler=$cxx
    backend=$("$compiler" -print-prog-name="$name") || return "$?"
    [[ -x "$backend" && "$(readlink -f -- "$backend")" == "$collection/"* ]] || die "Bootlin compiler backend is missing or outside the selected collection: $backend"
    printf '#!/usr/bin/env bash\nset -euo pipefail\nexec %q --library-path %q %q "$@"\n' \
      "$cpkt_afl_runtime_loader" "$cpkt_afl_runtime_library_path" "$backend" > "$backend_dir/$name"
    chmod +x "$backend_dir/$name"
  done
  for name in gcc g++; do
    compiler=$cc
    [[ "$name" != g++ ]] || compiler=$cxx
    {
      printf '#!/usr/bin/env bash\nset -euo pipefail\n'
      # Resolve relative to the wrapper so the verified staging tree can move.
      printf 'backend_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../libexec/bootlin-runtime" && pwd -P)\n'
      printf 'exec %q --library-path %q %q -B "$backend_dir/" "$@"\n' \
        "$cpkt_afl_runtime_loader" "$cpkt_afl_runtime_library_path" "$compiler"
    } > "$stage/bin/bootlin-$name"
    chmod +x "$stage/bin/bootlin-$name"
  done
  cpkt_afl_runtime_cc="$stage/bin/bootlin-gcc"
  cpkt_afl_runtime_cxx="$stage/bin/bootlin-g++"
}
