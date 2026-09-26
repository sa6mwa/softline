# Composable Live Output: Softline and libmdf

## Status and boundary

This is the acceptance contract for Softline's live output refactor. Softline
does not own, include, link, package, or expose libmdf. The application owns
its producer, event transport, Markdown renderer, renderer options (including
the two-column margin used by the chat examples), and document boundaries.
Only the chat examples and integration tests depend on libmdf. A released
libsoftline, its Lua facade, headers, CMake exports, and package metadata have
no libmdf dependency.

libmdf 0.12.0 has incremental feed, a bound sink, and in-document width and
margin changes through `set_geometry()`. The chat composer updates both
renderer geometries on the Softline owner thread before feeding later
Markdown; Softline reads the terminal dimensions for its native prompt.
The composer uses a two-column left margin except on very narrow terminals,
where it drops the margin to retain libmdf's three content columns. Neither
Softline nor the examples recreate or replay an unfinished Markdown document.
Softline never infers libmdf margins.

## Generic Softline output session

One editor may have one open live output session. Public C receiver methods
and equivalent free functions begin the session, write a byte span, and end
the session. Lua exposes the same lifecycle on its editor userdata. The API
uses Softline-owned names and byte strings only, no renderer-specific types.

The session is owner-thread-only. A producer thread/process hands fragments to
the application through a pipe or another bounded transport. The application
registers its descriptor with `sl_watch_add()` (Lua: `watch_add()`), drains it
on the editor thread, feeds its renderer, and forwards each sink emission
directly into the Softline session. Softline does not buffer whole messages,
interpret Markdown, own the transport, or run a worker. A sink write is visible
before the corresponding Softline write returns; no finish/EOF or next prompt
is needed to make an already-decided fragment appear.

Chunk boundaries have no display semantics. In particular, Softline inserts
no newline, response separator, reset, or space at a write boundary. Ending a
session does not finish a Markdown document. The application calls its
renderer’s document lifecycle itself. Each successful write accepts its entire
span; errors are reported on the handle and never silently discard accepted
data. A zero-length write is a no-op. Writes after end and overlapping begins
fail. Existing finite `sl_print_above()` remains available; narrow or offset
bounds use the same bounded viewport while full-width finite output may retain
VT scrolling. It cannot be interleaved with an open live session.

The quoted-prompt helper writes submitted text literally into an open session
between external renderer segments. It wraps at the current output width,
repeats a configurable prefix on each visible row, styles the prefix and
italic text independently, and adds only missing line breaks for one empty
row on either side. Prompt text does not pass through libmdf. The optional
status message sits above the status line, may change during a live session,
and wraps continuation rows under its configurable first-row prefix. Both
features are exposed through the C receiver/free functions and Lua methods.

## Terminal ownership and geometry

The output session and editor prompt have one serial terminal owner. A
full-terminal session begins at the existing terminal cursor. The prompt
follows initial and later output downward until it parks at the bottom. The
VT scroll region ends above the reserved prompt rows. Native producer bytes
are written unchanged, with only bounded partial ANSI/UTF-8 parser state and
an output cursor retained. Softline never rewraps, pads, clears, or replays
native transcript text. Terminal resize leaves transcript reflow to the
terminal and redraws only the prompt. Prompt growth uses free rows below it
before parking; after parking, wrapping and queue/status changes page within
the reservation without moving transcript rows. Ending chat clears the prompt,
restores the full scroll region, shows the cursor, and returns below output.

Every valid explicit bound, including a shorter, offset, or narrower rectangle,
is supported. These boxes retain only visible cells and partial parser state,
clip output to the viewport, and repaint their own cells when resized. Neither
path buffers the producer's complete response.

The application may call `sl_set_bounds()` and `sl_set_screen_width()` (or
their receiver and Lua equivalents) while a session is open or while the
prompt is being edited. The next render reconciles the prompt and transcript
inside the new bounds without clearing unrelated terminal cells. Input bytes,
cursor position in the editable buffer, queue contents, pending output, and
already emitted transcript order are preserved. Dynamic dimensions continue
to use `TIOCGWINSZ`; Softline does not take ownership of `SIGWINCH`. An
application wanting synchronous resize coordination can register its own
signal/self-pipe or other event source, then update Softline and its renderer
in the same owner-thread callback. Softline may redraw its prompt on the first
setter call, but no Markdown is fed between the two geometry updates.

The bounded output layout may not rely on a VT scroll region limited to the
terminal's full width: standard scroll margins are vertical and would alter
cells outside a narrow chat box. Softline's visible-viewport state and
repaint path must keep those outside cells untouched. The memory bound is
proportional to visible terminal cells and bounded parser state, not response
length. The stream must continue to accept one-byte fragments indefinitely.

## Compatibility and public surfaces

New C receiver pointers append after the existing receiver tail; old method
offsets remain unchanged. Equivalent free functions validate NULL handles.
Public declarations document ownership, thread context, lifecycle, errors,
and chunk semantics for clangd. Lua methods document the same contract in the
Lua reference and adjacent public binding comments. Lua cannot invoke its VM
from a foreign producer thread; a watched descriptor supplies owner-thread
events. The core and Lua libraries do not import libmdf.

## Verification

- A PTY test writes `Hello`, pauses, and observes it above an editable prompt
  before EOF. Later writes of ` world` continue the same line. Typing and
  queue edits remain responsive between writes.
- Identical output in one-byte and larger chunks yields the same visible
  screen, including styled ANSI and UTF-8 split across write boundaries.
- Prompt growth, queue/status rows, offset/narrow bounds, mid-prompt
  `set_bounds()`, `set_screen_width()`, and terminal resize retain transcript
  order and never modify cells outside the configured rectangle.
- Sink/write failure and session teardown leave a usable editor and report
  an actionable diagnostic. Repeated begin/end and invalid calls are tested.
- The C chat example composes a libmdf incremental renderer with the generic
  session. The composer configures default palette and a two-column
  left margin, passes submitted prompts to Softline's themed quoted-prompt
  helper between libmdf output segments, and emits varied Markdown response
  text one source character per 20 ms while the user may type, queue, promote,
  and cancel.
- The Lua facade tests the generic session lifecycle and byte forwarding;
  the separate Lua chat example remains a plain queued-turn demonstration.
- Packaging and artifact checks prove that libsoftline, its Lua facade, and
  installed package metadata have no libmdf link/runtime dependency. Only
  example and integration-test targets link the pinned libmdf SDK.
