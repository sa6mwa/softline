#!/usr/bin/env bash
set -euo pipefail
root=$1
mkdir -p "$root/build"
work=$(mktemp -d "$root/build/release-identity.XXXXXX")
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/scripts"
cp "$root/scripts/"{require-host-bash.sh,release_version.sh,lifecycle-version-contract.sh} "$work/scripts/"
git -C "$work" init -q
git -C "$work" config user.name 'Lifecycle fixture'
git -C "$work" config user.email fixture@example.invalid
git -C "$work" config commit.gpgSign false
git -C "$work" config tag.gpgSign false
git -C "$work" config core.hooksPath /dev/null
git -C "$work" add scripts
git -C "$work" commit -q -m 'test(lifecycle): seed disposable version fixture'
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
git -C "$work" tag -a v1.2.3 -m fixture
reject
git -C "$work" tag -d v1.2.3 >/dev/null
git -C "$work" tag v1.2.3
"$work/scripts/lifecycle-version-contract.sh"
SL_VERSION_OVERRIDE=2.3.4 reject
printf dirty > "$work/untracked"
reject
rm "$work/untracked"
git -C "$work" tag v1.2.4
reject
git -C "$work" tag -d v1.2.4 >/dev/null
git -C "$work" symbolic-ref refs/tags/v1.2.4 refs/tags/v1.2.3
reject
echo 'Read-only release identity rejects untagged/annotated/symbolic/ambiguous/dirty/override states.'
