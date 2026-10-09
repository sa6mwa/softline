# Lifecycle Migration Ledger

Softline uses the native pkt.systems C/CMake lifecycle. This ledger records the
implemented contract; successful release evidence comes from the actual tagged
release run and its final checksum-selected artifact bytes.

## Commands and ownership

Make exposes short public recipes. Bash sequences concrete operations in the
foreground. CMake owns configuration, source/data generation, build and install;
CTest owns registered test execution and results. One operation owns each mutable
workspace. Sources, tool collections and configuration stay stable during checks;
ordinary timestamp changes invalidate native build dependencies. Restore/copy
operations or changed execution context require explicit invalidation.

The terminal or job runner cancels the entire operation/job. Parent-PID-only
signals and concurrent in-place changes are outside the lifecycle interface.
There is no helper supervisor, receipt service or active-tag recovery protocol.
Generated scratch stays under `build/`; `make clean` removes local generated
state and `dist/` while preserving shared verified caches.

## Tool and test cutover

- PATH-selected Bash is at least 4.4. Linux compilation uses complete pinned
  Bootlin collections without host compiler/binutils/libc/header fallback.
  Discovery/configuration consume prepared tools; explicit `ensure` provisions.
- Shared defaults are `~/.cache/cpkt/toolchains` and `~/.cache/cpkt/deps`, with
  explicit cache overrides. Verified archive bytes are reusable by digest across
  naming/URL aliases. Local dependency extraction stays under `.cache/`.
- Project C targets use C89 and warnings as errors. Vendored upstream Lua/c-ares
  keep their selected upstream compilation boundary and recorded licenses.
- Python only generates declared API, namespace or image facts/data, or supplies
  small HTTP/DNS inputs. Test assertions and orchestration use compiled fixtures
  and small Bash/CMake checks. Lua API semantics execute in the selected runtime.
- Debug and native Memcheck share compilation. Instrumented checks retain their
  explicit timeout profile. ASan/UBSan and AFL++ own distinct effective flags.
  Installed consumer variants reuse matching library objects and inspect real
  CMake/pkg-config imports, ABI metadata and execution.
- Export maps use unchanged-content native generation. The private resolver
  namespace is a CMake-declared output with explicit parallel build dependencies.
  `cmake/softline.exports` is the public dynamic boundary, checked on built and
  extracted shared libraries, including accidental data exports.

## Delivery and final release

Preparation is incremental. `make prerelease` combines missing local and binary/
Lua rehearsal gates; source reconstruction is separately available through
`make package-source-smoke`. Binary rehearsals write a selected inventory under
`build/package-state/`, preserving unrelated identified artifacts.

`make release` is the final exact lightweight `vX.Y.Z` tagged gate. Its read-only
identity/tool preflight runs before the retained `make clean`. Candidate overrides,
dirty worktrees and partial inventory selectors cannot enter that gate. Git tag
variations are checked only in disposable fixture repositories.

Final release starts once from clean local state, shares matching producers and
runs fresh required proof. Its delivered source reconstruction supplies the native
Release lane; cross configurations remain in the owning checkout. CMake install
stages the public SDK under `build/`, and deterministic archives normalize ordering,
timestamps, ownership, gzip/ZIP metadata and intentional modes.

The complete checksum inventory contains four source/Lua artifacts plus all seven
SDK targets. Required payloads must exist and verify; stale/unlisted release-looking
files are rejected. Verification inspects extracted layout, version, public exports,
private dependency notices, ELF/Mach-O metadata, relocatable imports and real
installed static/shared consumers. Publication is a separate explicitly authorized
operation. Release failure stops; repairs do not retag/rebuild/publish inside it.

## Product boundaries preserved

libmdf remains an example/integration-test dependency, outside `libsoftline` and
its SDK imports. Private bundled c-ares remains Linux resolver implementation;
there is no inferred cpkt SDK dependency. The public C/Lua behavior and ABI 0,
selected targets and artifact set remain unchanged by this migration. Cross
execution and hosted native checks are not implicitly opted in; cross lanes prove
build/link/package metadata, with runtime execution deferred where no selected
native runner exists.
