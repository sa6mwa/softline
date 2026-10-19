#!/usr/bin/env bash
set -euo pipefail
root=$1
mkdir -p "$root/build"
work=$(mktemp -d "$root/build/cache-test.XXXXXX")
server=
cleanup() {
  if [[ -n "$server" ]]; then kill "$server" 2>/dev/null || true; wait "$server" 2>/dev/null || true; fi
  rm -rf "$work"
}
trap cleanup EXIT
python3 "$root/tests/fixtures/archive_server.py" "$work" &
server=$!
deadline=$((SECONDS + 5))
until [[ -f "$work/address" ]]; do
  kill -0 "$server"
  ((SECONDS < deadline)) || { echo 'HTTP data fixture did not become ready' >&2; exit 1; }
  sleep 0.01
done
url=$(cat "$work/address")
printf 'verified archive fixture' > "$work/payload"
digest=$(sha256sum "$work/payload" | awk '{print $1}')
args=(-DROOT="$root" -DCPKT_DEPENDENCY_CACHE="$work/cache" -DDIGEST="$digest")
cmake "${args[@]}" -DURL="$url" -DNAME=fixture.tar -P "$root/tests/lifecycle/cache_case.cmake"
[[ $(cat "$work/requests") == 3 ]]
cmake "${args[@]}" -DURL="$url/renamed" -DNAME=renamed.tar -P "$root/tests/lifecycle/cache_case.cmake"
[[ $(cat "$work/requests") == 3 ]]
# A fresh source consumer retains the same verified shared archive bytes.
kill "$server"; wait "$server" 2>/dev/null || true; server=
cmake "${args[@]}" -DURL="$url/offline" -DNAME=offline.tar -P "$root/tests/lifecycle/cache_case.cmake"
printf corrupt > "$work/cache/archives/sha256/$digest/fixture.tar"
if cmake "${args[@]}" -DURL=file://"$work/missing" -DNAME=fixture.tar \
    -P "$root/tests/lifecycle/cache_case.cmake" > "$work/failure" 2>&1; then
  echo 'ERROR: corrupt archive was accepted' >&2; exit 1
fi
grep -q 'cannot acquire' "$work/failure"
[[ -z $(find "$work/cache/archives/sha256/$digest" -type f -print) ]]
echo 'Archive retry, digest alias, offline reuse and corruption gates passed.'
