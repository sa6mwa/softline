#!/usr/bin/env bash
set -euo pipefail
root=$1
mkdir -p "$root/build"
work=$(mktemp -d "$root/build/release-identity.XXXXXX")
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/scripts"
cp "$root/scripts/"{require-host-bash.sh,release_version.sh,lifecycle-version-contract.sh} "$work/scripts/"
source "$root/tests/lifecycle/release-fixture.sh"
release_fixture_init "$work"
reject() {
  local before after
  before=$(git -C "$work" for-each-ref --format='%(refname) %(objectname)' refs/tags)
  if "$work/scripts/lifecycle-version-contract.sh" > "$root/build/release-identity.log" 2>&1; then
    echo 'ERROR: invalid release identity was accepted' >&2; exit 1
  fi
  after=$(git -C "$work" for-each-ref --format='%(refname) %(objectname)' refs/tags)
  [[ "$before" == "$after" ]]
}
reject
git -C "$work" tag v1.2.3
reject # Unsigned default-branch commit must not become releasable by tagging.
git -C "$work" tag -d v1.2.3 >/dev/null
release_fixture_sign "$work"
git -C "$work" tag -a v1.2.3 -m fixture
reject
git -C "$work" tag -d v1.2.3 >/dev/null
git -C "$work" tag v1.2.3
"$work/scripts/lifecycle-version-contract.sh"
git -C "$work" checkout -qb feature/not-a-release
reject
git -C "$work" checkout -q --detach
reject
git -C "$work" checkout -q trunk
git -C "$work" branch -m stable
git -C "$work" symbolic-ref refs/remotes/origin/HEAD refs/remotes/origin/stable
"$work/scripts/lifecycle-version-contract.sh"
git -C "$work" symbolic-ref --delete refs/remotes/origin/HEAD
reject
git -C "$work" symbolic-ref refs/remotes/origin/HEAD refs/remotes/origin/stable
SL_VERSION_OVERRIDE=2.3.4 reject
printf dirty > "$work/untracked"
reject
rm "$work/untracked"
git -C "$work" tag v1.2.4
reject
git -C "$work" tag -d v1.2.4 >/dev/null
git -C "$work" symbolic-ref refs/tags/v1.2.4 refs/tags/v1.2.3
reject
echo 'Read-only release identity requires a signed attached default branch and rejects invalid tags/dirty/override states.'
