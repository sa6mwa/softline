# Retained-frame resize regression

The standalone C fixture and real tmux cell assertions reproduce the reported
stale status suffix after a settled resize between output writes. The fixture
links only Softline, with no renderer, provider or adjacent checkout dependency.
Its producer acknowledges a FIFO barrier after all 500 partial-line characters
have been emitted. The driver verifies those cells before resizing and releasing
the next write, excluding a resize racing the producer.

Run from this checkout with its prepared native toolchain and tmux:

```sh
cmake --preset debug
cmake --build --preset debug --target retained_resize_fixture
cmake -S cmake/retained-resize-tests -B build/retained-resize-tests -G Ninja \
  -DSOFTLINE_RESIZE_FIXTURE="$PWD/build/debug/tests/retained_resize_fixture"
ctest --test-dir build/retained-resize-tests --output-on-failure --no-tests=error
```

This is an explicitly selected diagnostic suite. It asserts correct cells and
currently returns failure; cases are neither disabled nor inverted with
`WILL_FAIL`. The existing required native suite remains separate because the
current public contract documents this unobserved retained-frame limitation.
Failed cases retain before/after-resize cells, final screen and scrollback under
the printed owned build directory. Successful cases remove their scratch.

## Observations

| Case | Result on the unchanged library |
| --- | --- |
| Retained, partial line, 120x30 to 60x18 | Fails: `* Edited snake.cize reproduction` |
| Retained, complete line, 120x30 to 60x18 | Fails: first patch row contains stale status text |
| Retained, partial line, width-only shrink to 60x30 | Fails: contaminated heading |
| Retained, partial line, height-only shrink to 120x18 | Passes |
| Watch/yield ownership, partial/complete lines, early/late draft controls | Four cases pass |
| Input-owned step before continuation, each of the three failing variants | Three cases pass |

The complete diagnostic inventory is eight passing controls and three genuine
failures. Existing feed-latency, native-resize and tmux producer-resize checks
also pass (3/3); no library source or public API has been changed.

## Fix direction

`sl_output_stream_write_method` sets `output_write_active` before
`sl_prepare_native_output` reconciles retained geometry. `sl_query_cursor_row`
refuses observations while that flag is set, preserving the zero-wait feed
contract. The fallback in `sl_native_reconcile_rendered_frame` therefore estimates
where retained status/input cells moved. A width shrink can expose stale status
suffixes in subsequent producer rows, even without a simultaneous height change.
This localizes the path; it does not prove one arithmetic expression is the cause.

An experimental control re-enters the existing input owner through `sl_readline`
and cancels that input step before continuing output. All three failing variants
then pass. This establishes an input-owned reconciliation/repaint as a promising
repair path, not a production workaround or proof that a cursor query alone is
sufficient.

The recommended public design is an explicit owner-thread reconciliation
operation between producer writes: acquire a current terminal observation,
reconcile/repaint only the owned frame, preserve queued input and terminal modes,
then return control to the producer. Feed writes must continue to emit immediately
without cursor-report waits, transcript buffering/replay or caller-specific erases.
The API boundary and revised guarantee need agreement before implementation;
receiver/Lua parity and any ABI implications must be assessed against the released
baseline. An already-open `sl_output_stream_begin` currently returns
`SL_ERROR_INVALID`, so it cannot silently serve as this operation under today's
contract. CAI's dependency update is separate work outside this repository.
