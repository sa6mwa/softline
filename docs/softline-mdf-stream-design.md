# Native libmdf Stream: Implementation Design

## Status

Proposed. libmdf 0.10.0 supplies the required incremental document lifecycle,
but one additional libmdf operation is required before this design can meet the
resize contract. See [Required libmdf follow-up](#required-libmdf-follow-up).

## Decision

Softline needs an optional, released `softline_mdf` companion library. It owns
the producer-to-UI handoff, the libmdf renderer, and a persistent terminal
output surface above an active Softline editor.

This is deliberately not an application adapter around repeated
`sl_print_above()` calls:

- `sl_print_above()` consumes one finite callback stream synchronously and
  completes its output transaction before returning.
- Each invocation clears/redraws or recreates prompt/output terminal state.
  That is correct for an independent message, but not for a paragraph whose
  later Markdown decisions arrive after an arbitrary delay.
- libmdf's incremental renderer can preserve Markdown state between feeds;
  Softline must preserve the corresponding terminal-output state between those
  feeds.

The base `softline` library remains independent of libmdf. The companion is
the only Softline artifact that links libmdf, and applications that do not use
Markdown streaming retain the current dependency graph and API.

## Facts established by the current implementation

The intended implementation follows Softline's existing owner-thread model;
it does not replace it with a second UI thread.

- `sl_read_input_byte()` already polls the terminal and application watches
  together. It is the single natural place to add a Softline-owned wake
  descriptor and dispatch native stream work before the next keyboard read.
- Watch callbacks run on the UI/`readline()` owner thread. A producer must not
  call a receiver, a renderer, or `print_above()` from another thread.
- The current `sl_write_stream*()` helpers and `sl_print_above()` encode a
  finite transaction. They must not become a hidden persistent-stream API.
- libmdf 0.10.0 (ABI/SONAME 3) has `mdf_feed()`, `mdf_flush()`,
  `mdf_finish_document()`, and `mdf_begin_document()`. `feed()` synchronously
  emits every final Markdown decision; `finish_document()` is the sole EOF
  operation. `flush()` intentionally emits nothing and does not resolve an
  incomplete construct.

The existing `test_libmdf_stream` is therefore only a dependency-integration
test: it proves that incremental libmdf calls can produce a finite stream for
the existing `print_above()` API. It does not prove, or implement, a live
interactive Markdown stream.

## Product model

One `sl_mdf_stream_t` belongs to one `sl_t` and represents a sequence of
Markdown response documents. One editor permits one open native Markdown
stream at a time. A single stream supports many documents in order:

```text
producer thread                         Softline UI owner thread
---------------                         ------------------------
write("Hello") ─┐
write(" world") ├─ bounded FIFO ─wake─> drain event -> mdf_feed()
finish_document ─┘                                  -> persistent surface
                                                    -> prompt redraw
```

The FIFO is a bounded transport buffer, not a response buffer: Softline never
collects an answer, rendered ANSI, or document in full. libmdf may retain the
bounded partial constructs documented by libmdf itself; Softline does not add
another materialization layer.

The stream is useful only while the application has returned to
`next_prompt()`/`readline()` and Softline is driving the terminal. A turn-based
application starts its operation on a producer thread, immediately re-enters
`next_prompt()` on the owner thread, and lets the producer submit response
fragments. This is the required control flow for simultaneous typing and
output; an application that blocks the owner thread cannot receive either
terminal input or queued stream work.

## Public companion API

Install a separate header, `include/softline/softline_mdf.h`. Do not add
libmdf types or a libmdf include to `softline/softline.h`.

```c
typedef struct sl_mdf_stream sl_mdf_stream_t;

typedef enum sl_mdf_theme {
  SL_MDF_THEME_DEFAULT = 0,
  SL_MDF_THEME_PLAIN = 1
} sl_mdf_theme_t;

typedef struct sl_mdf_stream_config {
  /* Zero selects 65536. Each queued event costs at least one byte, including
   * a document-end marker, so control traffic is bounded too. */
  size_t pending_byte_capacity;
  /* ANSI left margin. Zero is valid. */
  size_t margin_left;
  sl_mdf_theme_t theme;
} sl_mdf_stream_config_t;

void sl_mdf_stream_config_init(sl_mdf_stream_config_t *config);
int sl_mdf_stream_open(sl_t *editor, const sl_mdf_stream_config_t *config,
                       sl_mdf_stream_t **out);
int sl_mdf_stream_write(sl_mdf_stream_t *stream, const char *data, size_t len);
int sl_mdf_stream_finish_document(sl_mdf_stream_t *stream);
int sl_mdf_stream_close(sl_mdf_stream_t *stream);
```

The first version intentionally exposes Softline-owned scalar configuration,
not `mdf_options`. In particular, libmdf options may contain borrowed pointers
and callbacks whose lifetime and thread ownership would be wrong for this
object. `DEFAULT` maps to libmdf's default ANSI theme; `PLAIN` maps to its
unstyled ANSI rendering. Additional named themes are a later explicit
Softline enum extension, not borrowed string pointers.

`open()` and `close()` are owner-thread operations. `write()` and
`finish_document()` are thread-safe producer operations. A producer call with
an accepted event copies its bytes before returning. It blocks on a condition
variable while the finite FIFO lacks capacity; it must never block the owner
thread. An owner-thread write drains directly when necessary rather than
waiting on itself. A fragment larger than the configured capacity returns
`SL_ERROR_FULL`; no partial fragment is accepted.

`close()` first closes admission and wakes blocked producers. Called by the
owner after producers have been stopped, it drains accepted work, finishes an
open document if necessary, releases the terminal surface, and destroys the
renderer. Calls after closure fail with `SL_ERROR_INVALID`. Destruction of an
editor marks every attached stream closed and wakes blocked producers before
the editor's terminal state is released; callers must still stop producer
threads before freeing their own stream references.

The companion uses free functions rather than fields appended to `struct sl`.
That keeps the established core receiver layout and `libsoftline` SONAME
unchanged. `libsoftline_mdf` begins with its own ABI generation.

## Mailbox and wakeup protocol

The mailbox stores FIFO records of two kinds: copied byte fragments and a
document-finish marker. Capacity is charged as `max(fragment_length, 1)` per
record so an unbounded sequence of zero-byte control markers cannot consume
unbounded memory.

The companion owns a mutex, two condition variables (space available and
closed), and a nonblocking self-pipe. The read end is private Softline event
state; it is not registered through the public application-watch API.

1. A producer waits for capacity, appends exactly one record, and schedules
   the pipe if no drain is already scheduled.
2. `sl_read_input_byte()` polls terminal input, application watches, and every
   active companion pipe. Terminal input retains priority after each bounded
   dispatch batch, exactly as it does for application watches.
3. The UI drain consumes a bounded amount of mailbox work, invokes libmdf, and
   reschedules itself if work remains. It signals waiting producers only after
   capacity is released.
4. Pipe state is protected by the same mutex as mailbox state. Draining the
   pipe before clearing the scheduled bit prevents a producer/UI race from
   losing a wakeup. `EAGAIN` while scheduling means a wake byte is already
   pending, not that data has been lost.

A raw public write-fd is not the preferred API. It would still need framing
for exact document boundaries, an explicit capacity/backpressure contract, EOF
versus close semantics, and ownership of the descriptor. The self-pipe is the
right internal wake primitive; the typed `write()`/`finish_document()` API is
the correct public stream protocol.

## Persistent output surface

The implementation must first refactor terminal output around an internal
`sl_output_surface` abstraction. It owns transcript geometry, current terminal
cursor position, deferred newline state, and lifecycle state. The editor
renderer and a surface use one serial owner-thread terminal writer.

`sl_print_above()` becomes a transient surface user: begin, consume its finite
callback, finish. The native Markdown stream holds one surface open across
many UI wakes. This avoids parallel terminal-writing paths and makes prompt
reflow, queue previews, status lines, and stream output part of the same
layout protocol.

For each libmdf sink emission, the native surface must:

1. establish or update the transcript scroll area above the current prompt;
2. append bytes at the retained transcript cursor without clearing an
   unterminated output row or inserting a response boundary;
3. preserve deferred newline and ANSI continuation state across emissions;
4. restore/redraw only the active editor rows and hardware cursor.

When the prompt expands, contracts, moves because queue/status rows change, or
the terminal is resized, surface reconciliation runs before the editor render.
It reserves the output region, scrolls transcript rows when prompt rows need
space, and updates the persistent cursor. It must not call the old
clear-and-redraw `print_above()` path.

At `finish_document`, the UI performs `mdf_finish_document()`, appends its
final sink output to the same surface, closes any pending line/ANSI state, and
ends that document cleanly. On the next data record it calls
`mdf_begin_document()` before `mdf_feed()`, keeping Markdown and terminal
style state isolated between responses.

Terminal write or libmdf failure latches the stream failed, displays an
actionable status-line error on the owner thread, rejects future producer
writes, wakes waiting producers, and leaves the input editor usable. No error
path may silently discard an accepted fragment.

## Required libmdf follow-up

libmdf 0.10.0 lacks a public operation that changes ANSI width or margins on
an active incremental renderer. `mdf_options` is supplied to `mdf_create()`;
destroying/recreating a renderer at resize would lose unfinished Markdown
state, and mutating a private renderer/options layout would be an ABI breach.

The required Softline guarantee is that later rendering observes the current
usable terminal width and configured left margin. Deferring geometry until the
next document would violate that guarantee. Therefore Softline must not ship
the companion until libmdf provides and documents an operation equivalent to:

```c
mdf_status mdf_set_ansi_geometry(mdf *renderer, int width,
                                 int margin_left, int margin_right);
```

It must be valid between incremental calls, preserve parser/document state,
affect only future decisions, reject invalid effective widths, and not replay
already-emitted output. Softline calls it during owner-thread surface
reconciliation before the next `mdf_feed()` or `mdf_finish_document()`.

`mdf_flush()` does not solve this: by contract it neither emits nor changes
retained parser state.

## Build, package, and Lua boundaries

`SL_BUILD_MDF` builds the optional companion and defaults on for normal
Softline development/release builds. Setting it off produces the current core
library with no libmdf dependency. The production companion consumes
checksum-pinned libmdf release SDKs for every shipped target; it requires the
new libmdf release with SONAME 3 or later as established by the geometry API.

The released package surface is:

- `softline::softline` / `libsoftline`: unchanged, no libmdf dependency.
- `softline::mdf` / `libsoftline_mdf`: optional companion; CMake and
  pkg-config metadata declare the required libmdf static and runtime closure.
- `softline-mdf.pc` and package provenance state the exact libmdf release and
  ABI requirement without embedding cache or build paths.

The Lua module links the companion when its Markdown facade is enabled and
adds `editor:mdf_stream(config)`. It returns a stream userdata with `write`,
`finish_document`, and idempotent `close` methods. Lua calls occur on the Lua
owner/UI thread and use the same native stream; Lua must not call its VM from a
foreign producer thread. Native producer threads use the C API.

The current test-only direct libmdf bridge is replaced by tests linked through
the companion target. libmdf is no longer merely a test dependency once this
feature ships, but it remains absent from the base Softline library.

## Validation plan

Tests must assert terminal-screen state through the existing PTY screen
emulator, not raw ANSI substrings alone.

- A producer writes `Hello`, pauses indefinitely, and the visible transcript
  shows `Hello` while the editable prompt and cursor remain present.
- `Hello`, a space, and `world` across separate wakeups remain one output
  line. One-byte chunks produce the same completed transcript as libmdf's
  one-shot rendering.
- Cross-chunk italic, bold, code, headings, lists, fenced code, and UTF-8
  constructs render correctly; finish is the only EOF decision.
- Two documents on one stream have independent Markdown and ANSI state.
- Resize during an unfinished document changes later rendering width and
  preserves the prompt/transcript layout.
- Continuous output races with typing, paste, queue edits, ordinary submit,
  and Alt-Enter promotion without lost/reordered input or unintended FIFO
  dispatch.
- Capacity saturation blocks an external producer, never busy-spins, resumes
  deterministically, and preserves all accepted records. Close/failure wakes
  every waiter.
- PTY tests cover bounded and ordinary interactive layouts, prompt/status
  reflow, terminal write errors, libmdf errors, and stream teardown. Thread
  handoff has TSAN coverage and the full path has leak checking.
- Lua PTY tests exercise `editor:mdf_stream()` while editing and queueing;
  they must not implement a Lua Markdown/ANSI forwarding bridge.

The terminal chat C and Lua examples become executable demonstrations of this
contract: default libmdf styling, `margin_left = 2`, submitted prompt rendered
as a Markdown block quote with blank lines around it, and a 20 ms-per-character
Markdown response simulation. Users can type, queue, promote, and cancel
turns while that response is live.

## Implementation sequence

1. Add the libmdf ANSI-geometry operation and release a pinned SDK containing
   it. Add libmdf tests for resize between incremental feeds.
2. Extract and test the internal Softline output-surface/layout primitive using
   existing finite `print_above()` behavior as the compatibility baseline.
3. Add the companion target, opaque stream, mailbox/self-pipe lifecycle, and
   UI drain integration. Keep `struct sl` and core receiver offsets unchanged.
4. Integrate the libmdf renderer and geometry reconciliation; replace the
   test-only bridge with companion integration tests.
5. Add the Lua facade and PTY/race/backpressure tests.
6. Refactor both chat examples to use the native stream and make their stream
   behavior part of the example test contract.

No implementation should begin at step 3 by forwarding libmdf sink writes to
`sl_print_above()`: that would recreate the rejected message-scoped design.
