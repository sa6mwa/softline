#!/bin/sh
set -eu

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="$(SL_VERSION_OVERRIDE=0.0.0 sh "${ROOT_DIR}/scripts/release_version.sh")"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "${TMP_DIR}"' EXIT

write_good_artifacts() {
  dist_dir="$1"
  stage_dir="${TMP_DIR}/stage"
  rm -rf "${dist_dir}" "${stage_dir}"
  mkdir -p "${dist_dir}" "${stage_dir}/softline-lua-${VERSION}/lua" \
    "${stage_dir}/softline-lua-${VERSION}/include/softline"

  cat > "${dist_dir}/softline-${VERSION}-1.rockspec" <<EOF
package = "softline"
version = "${VERSION}-1"
source = {
   url = "softline-lua-${VERSION}.tar.gz",
   dir = "softline-lua-${VERSION}",
}
EOF
  cp "${dist_dir}/softline-${VERSION}-1.rockspec" \
    "${stage_dir}/softline-lua-${VERSION}/softline-${VERSION}-1.rockspec"
  printf '%s\n' 'int luaopen_softline(void);' \
    > "${stage_dir}/softline-lua-${VERSION}/lua/softline_lua.c"
  printf '%s\n' 'void sl_config_init(void);' \
    > "${stage_dir}/softline-lua-${VERSION}/include/softline/softline.h"
  printf '%s\n' \
    "lua/softline_lua.c" \
    "include/softline/softline.h" \
    "RELEASE_MANIFEST" \
    > "${stage_dir}/softline-lua-${VERSION}/RELEASE_MANIFEST"

  tar -czf "${dist_dir}/softline-lua-${VERSION}.tar.gz" \
    -C "${stage_dir}" "softline-lua-${VERSION}"
  (
    cd "${dist_dir}"
    zip -q "softline-${VERSION}-1.src.rock" \
      "softline-${VERSION}-1.rockspec" \
      "softline-lua-${VERSION}.tar.gz"
  )
}

expect_lua_validation_failure() {
  name="$1"
  expected="$2"
  dist_dir="${TMP_DIR}/${name}"
  log="${TMP_DIR}/${name}.log"
  write_good_artifacts "${dist_dir}"
  "$3" "${dist_dir}"

  if SOFTLINE_DIST_DIR="${dist_dir}" SL_VERSION_OVERRIDE="${VERSION}" \
      "${ROOT_DIR}/scripts/validate_luarocks.sh" >"${log}" 2>&1; then
    echo "ERROR: validate-luarocks accepted leaked ${name} artifact" >&2
    exit 1
  fi
  if ! grep -q "${expected}" "${log}"; then
    cat "${log}" >&2
    echo "ERROR: validate-luarocks failed ${name} for the wrong reason" >&2
    exit 1
  fi
}

leak_rockspec() {
  dist_dir="$1"
  printf '%s\n' "source = { url = \"file://${HOME}/softline-lua-${VERSION}.tar.gz\" }" \
    >> "${dist_dir}/softline-${VERSION}-1.rockspec"
}

leak_lua_archive() {
  dist_dir="$1"
  stage_dir="${TMP_DIR}/archive-leak"
  rm -rf "${stage_dir}"
  mkdir -p "${stage_dir}"
  tar -xzf "${dist_dir}/softline-lua-${VERSION}.tar.gz" -C "${stage_dir}"
  printf '%s\n' "source = \"file://${ROOT_DIR}\"" \
    > "${stage_dir}/softline-lua-${VERSION}/lua/leak.txt"
  tar -czf "${dist_dir}/softline-lua-${VERSION}.tar.gz" \
    -C "${stage_dir}" "softline-lua-${VERSION}"
}

leak_src_rock_nested_archive() {
  dist_dir="$1"
  stage_dir="${TMP_DIR}/srcrock-leak"
  rm -rf "${stage_dir}"
  mkdir -p "${stage_dir}"
  tar -xzf "${dist_dir}/softline-lua-${VERSION}.tar.gz" -C "${stage_dir}"
  printf '%s\n' "/tmp/luarocks_softline_build/source" \
    > "${stage_dir}/softline-lua-${VERSION}/lua/package-manager-tmp.txt"
  tar -czf "${dist_dir}/softline-lua-${VERSION}.tar.gz" \
    -C "${stage_dir}" "softline-lua-${VERSION}"
  (
    cd "${dist_dir}"
    rm -f "softline-${VERSION}-1.src.rock"
    zip -q "softline-${VERSION}-1.src.rock" \
      "softline-${VERSION}-1.rockspec" \
      "softline-lua-${VERSION}.tar.gz"
  )
}

expect_lua_validation_failure rockspec "local file URL" leak_rockspec
expect_lua_validation_failure lua-archive "live-worktree source URL" leak_lua_archive
expect_lua_validation_failure src-rock "package-manager temporary path" leak_src_rock_nested_archive

echo "Lua artifact privacy regression tests passed."
