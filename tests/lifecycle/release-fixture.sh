#!/usr/bin/env bash
# Disposable repository and signing authority; never use workstation keys/refs.
release_fixture_init() {
  local repo=$1
  git -C "$repo" init -q -b trunk
  git -C "$repo" config user.name 'Lifecycle fixture'
  git -C "$repo" config user.email fixture@example.invalid
  git -C "$repo" config commit.gpgSign false
  git -C "$repo" config tag.gpgSign false
  git -C "$repo" config core.hooksPath /dev/null
  git -C "$repo" symbolic-ref refs/remotes/origin/HEAD refs/remotes/origin/trunk
  ssh-keygen -q -t ed25519 -N '' -f "$repo/.git/fixture-key"
  printf 'fixture@example.invalid %s\n' "$(cat "$repo/.git/fixture-key.pub")" > "$repo/.git/allowed-signers"
  git -C "$repo" config gpg.format ssh
  git -C "$repo" config user.signingKey "$repo/.git/fixture-key"
  git -C "$repo" config gpg.ssh.allowedSignersFile "$repo/.git/allowed-signers"
  git -C "$repo" add .
  git -C "$repo" commit -qm 'test(lifecycle): seed disposable release fixture'
}

release_fixture_sign() {
  git -C "$1" -c commit.gpgSign=true commit --amend --no-edit -q
}
