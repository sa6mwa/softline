#!/usr/bin/env bash
case ${BASH_SOURCE[0]} in
  */*) source "${BASH_SOURCE[0]%/*}/require-host-bash.sh" ;;
  *) source ./require-host-bash.sh ;;
esac || exit $?
set -euo pipefail
ROOT_DIR=$(cd "${BASH_SOURCE[0]%/*}/.." && pwd -P)
(($# == 2)) || { echo 'Usage: extract-release-source.sh archive build-workspace' >&2; exit 2; }
archive=$1
workspace=$2
case "$workspace" in "$ROOT_DIR"/build/*) ;; *) echo 'ERROR: source workspace must be under repository build/' >&2; exit 1 ;; esac
[[ "$workspace" != *'/../'* && "$workspace" != */.. && ! -L "$workspace" ]] || { echo 'ERROR: unsafe source workspace' >&2; exit 1; }
# Owned extraction scratch uses physical paths with no symlink parents.
parent="$ROOT_DIR/build"
[[ ! -L "$parent" ]] || { echo 'ERROR: source workspace must not have symlink parents' >&2; exit 1; }
IFS=/ read -r -a components <<< "${workspace#"$ROOT_DIR/build/"}"
for component in "${components[@]}"; do
  [[ -n "$component" && "$component" != . && "$component" != .. ]] || { echo 'ERROR: unsafe source workspace' >&2; exit 1; }
  parent+="/$component"
  [[ ! -L "$parent" ]] || { echo 'ERROR: source workspace must not have symlink parents' >&2; exit 1; }
done
[[ ! -d "$workspace" || -z $(find "$workspace" -mindepth 1 -print -quit) ]] || { echo 'ERROR: source workspace must be empty' >&2; exit 1; }
version=$("$ROOT_DIR/scripts/release_version.sh")
[[ -f "$archive" ]] || { echo "ERROR: source archive not found: $archive" >&2; exit 1; }
mkdir -p "$workspace"
# Reject traversal, unrelated roots and generated scratch (including empty dirs)
# before extraction. tar must never place archive data outside our build root.
members=$(tar -tzf "$archive")
[[ -n "$members" ]] || { echo 'ERROR: source archive is empty' >&2; exit 1; }
while IFS= read -r member; do
  case "$member" in
    "softline-$version"|"softline-$version/"|"softline-$version/"*) ;;
    *) echo "ERROR: unexpected source archive entry: $member" >&2; exit 1 ;;
  esac
  case "/$member/" in
    *'/../'*|*'/./'*|*'/build/'*|*'/.cache/'*|*'/.git/'*|*'/dist/'*|*'/stash/'*)
      echo "ERROR: forbidden source archive entry: $member" >&2; exit 1 ;;
  esac
done <<< "$members"
# Source manifests contain files, never symlink/hardlink payloads.
details=$(tar -tvzf "$archive")
if awk 'substr($0,1,1) != "-" && substr($0,1,1) != "d" { bad=1 } END { exit !bad }' <<< "$details"; then
  echo 'ERROR: source archive contains a non-file/directory entry' >&2; exit 1
fi
tar -xzf "$archive" -C "$workspace"
source="$workspace/softline-$version"
[[ $(head -1 "$source/VERSION") == "$version" ]] || { echo 'ERROR: source VERSION mismatch' >&2; exit 1; }
[[ -f "$source/RELEASE_MANIFEST" ]] || { echo 'ERROR: source archive missing RELEASE_MANIFEST' >&2; exit 1; }
(cd "$source"; find . -type f | sed 's|^\./||' | sort) > "$workspace/actual-manifest"
sort "$source/RELEASE_MANIFEST" > "$workspace/expected-manifest"
cmp "$workspace/actual-manifest" "$workspace/expected-manifest" || { echo 'ERROR: source RELEASE_MANIFEST differs from payload' >&2; exit 1; }
printf '%s\n' "$source"
