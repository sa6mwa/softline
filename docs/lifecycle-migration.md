# Lifecycle Migration

This ledger tracks the release lifecycle convergence work.

| Old behavior | New lifecycle surface | Verification |
| --- | --- | --- |
| `package.sh` built only `x86_64-linux-gnu` | `package.sh` uses the full standard target set from `scripts/release-targets.sh` and skips unavailable optional toolchains explicitly | `make package`, `make release-matrix` |
| Target tools were discovered ad hoc from `PATH` | `scripts/discover_target_tools.sh` reads configured CMake state, compiler siblings, osxcross defaults, then `PATH` | `make test-tool-discovery` |
| Darwin CMake configuration relied on an absolute compiler path to imply target linker routing | `cmake/toolchains/osxcross-darwin.cmake` prepends osxcross `bin`, sets target tool state, and injects absolute `-fuse-ld=${CMAKE_LINKER}` for generated CMake link lines | `make test-darwin-linker-route`, `cmake --build --preset arm64-apple-darwin-release` |
| Git worktree version detection could read a local `VERSION` file before tags | Git worktrees now use only explicit `SL_VERSION_OVERRIDE`, an exact lightweight `vX.Y.Z` tag on `HEAD`, or `0.0.0`; source archives still use injected `VERSION` outside git | `make test-release-version` |
| Source archives had an injected `VERSION` but no payload manifest check | Source archive staging now writes `RELEASE_MANIFEST`, and smoke verification checks manifest exactness plus version agreement before building tests | `make package-source-smoke` |
| Lifecycle command and preset coverage was documented but not checked | `test-lifecycle-surface` verifies standard Make targets, release presets, target IDs, dist dirs, install prefixes, and Darwin toolchain-file use | `make test-lifecycle-surface` |
| Clean release failed because `make test` assumed a preconfigured debug build | `build`, `build-release`, and `test` now configure their presets before building/running so clean gates are self-sufficient | `make release` |
| `release-matrix` only looped one Linux target | `release-matrix` builds C SDK artifacts, source archive, Lua artifacts, checksums, package verification, and privacy verification | `make release-matrix` |
| Runtime loader verification was Linux-only and minimal | `package-verify` is checksum-manifest driven, rejects stale or omitted release artifacts, scans privacy/relocatability, and inspects extracted ELF and Mach-O runtime metadata with discovered target tools | `make package-verify` |
| Binary SDKs shipped CMake and pkg-config metadata but no package metadata under `share/softline` | CMake now installs `share/softline/package-metadata.json`; package staging ships it and package verification asserts target/pkg-config identity | `make package-verify` |
| Project-owned builds allowed warnings without failing the gate | CMake applies warning flags plus `-Werror` by default to library, examples, and tests; CTest registrations now include explicit timeouts | `make prerelease` |

Remaining optional coverage depends on local cross toolchain availability. Missing optional cross compilers are reported as skipped targets; a packaged Darwin artifact requires target-correct `otool` verification.
