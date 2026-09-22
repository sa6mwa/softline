# Lifecycle Migration Ledger

Softline is aligned to the current pkt.systems C/CMake lifecycle. This ledger
records the deliberate cutover so future lifecycle changes have one auditable
baseline.

## Completed cutover

- Linux configurations now fail closed unless the native pinned Bootlin target
  is available. Project targets never select a host compiler, linker, binutils,
  libc, or headers as a fallback.
- The CMake root resolves `CPKT_DEPENDENCY_CACHE` once. Verified archives remain
  in the shared cache; disposable libmdf and Lua extraction state is under
  `.cache/deps-build/<target>/` and `.cache/deps/<target>/` with narrow
  per-component contracts.
- Every project-owned C target uses direct C89 options: `-std=c89`, warning
  flags, and `-pedantic-errors`. Upstream Lua sources remain an intentional
  external implementation boundary.
- `cmake/softline.exports` is the exact public dynamic-export allowlist.
  Linux links through a version script, Darwin uses the generated exported
  symbol list, CTest inspects build outputs, and package verification repeats
  the check after extraction.
- `release-matrix` is binary-only. `make release` alone performs the final
  source archive creation and source smoke after the ordinary proof and binary
  matrix have completed.

## Verification

The lifecycle proof is executable:

```sh
make test-all
make prerelease
make release
```

`make release` is the clean final gate and requires the Darwin artifact. It is
not a repair loop: failures start a separate fix iteration.

## Intentional boundaries

- libmdf remains an integration-test dependency only. It is not linked into
  `libsoftline` or its installed metadata.
- The proposed native libmdf streaming companion is still blocked on the
  documented libmdf incremental ANSI geometry operation; see
  [softline-mdf-stream-design.md](softline-mdf-stream-design.md).
