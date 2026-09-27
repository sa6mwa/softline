# Composable Live Output: Softline and libmdf

## Status and boundary

This is the acceptance contract for Softline's live output refactor. Softline
does not own, include, link, package, or expose libmdf. The application owns
its producer, event transport, Markdown renderer, renderer options (including
the two-column margin used by the chat examples), and document boundaries.
Only the chat examples and integration tests depend on libmdf. A released
libsoftline, its Lua facade, headers, CMake exports, and package metadata have
no libmdf dependency.

libmdf 0.13.0 has incremental feed, a bound sink, and in-document width and
margin changes through `set_geometry()`. The chat composer updates both
renderer geometries on the Softline owner thread before feeding later
Markdown; Softline reads the terminal dimensions for its native prompt.
The composer uses a two-column left margin except on very narrow terminals,
where it drops the margin to retain libmdf's three content columns. Neither
Softline nor the examples recreate or replay an unfinished Markdown document.
Softline never infers libmdf margins. The example supplies its borrowed stdout
fd to libmdf's destination-aware ANSI AUTO policy: terminal output is themed,
and redirected output is escape-free. Renderer handles are rebuilt against
libmdf 0.13.0's options layout; this private example dependency does not change
Softline ABI 0.

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
no newline, response separator, or space in producer content at a write
boundary. Terminal cursor and style controls surround native writes to isolate
the editor. Output position and producer style are tracked by Softline rather
than by terminal cursor save/restore sequences. Native LF positioning follows
the output TTY's OPOST/ONLCR settings; producer bytes and terminal settings
are unchanged. A failed native session startup restores raw mode only when
that startup acquired it, preserving raw mode already owned by the editor.
Ending a session does not finish a Markdown document. The application calls its
renderer’s document lifecycle itself. Each successful write accepts its entire
span; errors are reported on the handle and never silently discard accepted
data. A zero-length write is a no-op. Writes after end and overlapping begins
fail. Existing finite `sl_print_above()` remains available in native terminal scrollback. It cannot be interleaved with an open live session.

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
full-terminal transcript begins at the existing terminal cursor. The editable
prompt is anchored at the bottom from its first frame. The VT scroll region
ends immediately above the current prompt frame. Native producer bytes
are written unchanged, with only bounded partial ANSI/UTF-8 parser state and
an output cursor retained. Softline never rewraps, pads, clears, or replays
native transcript text. Positions are distances from the terminal bottom.
Resize reconciliation queries the live input cursor and combines its observed
movement with the known layout of the old prompt. Unchanged prompt rows that
fit the new width keep their cached cells. Width resize rebuilds only prompt
layout; transcript reflow remains owned by the terminal. The
producer tracks display cells since its last hard line boundary so growth can
restore the column of a previously wrapped line without storing its text.
When no prompt frame has been rendered, the live cursor belongs to the producer.
Writes leave that cursor in place, preserving native pending wrap and Unicode
clusters across chunks. No editor rows are reserved until a frame exists.
A full-height region follows terminal resize without another margin command.
Cursor reports refresh the producer's bottom-relative row and column at resize,
first editor handoff, finite-output completion, and session close; they are not
requested per feed. Moving to an editor or temporary parked cursor releases
producer ownership, so later writes use the stored output position. Rendering
a prompt sets its actual margin.
Updating margins after physical resize
must not scroll the transcript again; cells clipped off the top are not
recovered or replayed. When an unfinished line leaves the screen, its next
fragment resumes at the first visible output row. If the clipped position was
at a hard line boundary, new output starts next to the prompt. Neither case
changes cells or blank rows already in scrollback.
A cached previous prompt frame allows
patches of only changed cells; native feeds preserve unchanged prompt cells
and restore the editor cursor. Erasing an owned prompt row uses the default
background and erase-to-end from column zero. This avoids leaving a full-width
blank row that native terminal reflow can split when the width shrinks.
The output margin follows the current rendered
prompt height, with no empty queue slots or absent status-message rows. Growth
scrolls existing output cells only enough to fit; shrink returns freed rows
without moving transcript cells. The editor pages only at physical capacity.
Native feeds batch bounded chunks with cursor restoration, retrying short
writes as needed, without hiding the cursor or probing its position on
unchanged frames. Ending chat restores the full scroll
region and clears only input rows, preserving queue and status rows. The cursor
stays at column zero on the current input row without a newline or scroll.

The geometry model follows the terminal's live cursor rather than a saved
terminal cursor slot. [tmux's resize implementation](https://github.com/tmux/tmux/blob/3.6/screen.c)
moves rows above the cursor into history on shrink and pulls history back on
growth; it resets scroll margins after height changes. The
[Codex history writer](https://github.com/openai/codex/blob/main/codex-rs/tui/src/insert_history.rs)
uses a full-width vertical scroll region above its editable viewport and
application-owned coordinates to return to the input cursor. Softline applies
that separation to incremental producer writes and retains only the prompt
frame for cell comparisons.
`clear_prompt_on_exit = 1` selects clearing the whole prompt area and returning
below the transcript. Ending a stream inside an active editor retains the
prompt and scroll region for later finite output or another stream. Closing
the handle always restores terminal state. Non-TTY sessions emit no teardown
controls. Ctrl-C restores unrestricted scroll margins, the bottom input cursor,
and termios before raising SIGINT. If a handler returns, the existing native
output session remains available and its scroll margin is reinstated.
Cursor queries outside an active raw readline temporarily acquire noncanonical,
non-echo input without flushing pending bytes, then restore the prior attributes.
Native chat needs at least three terminal rows: two for output and
one for the prompt.

Rectangular viewports and bounds configuration are unsupported. Native chat
always uses the physical terminal width. `sl_set_screen_width()` controls
ordinary readline wrapping; it cannot rewrap producer output in a chat session.
Without an active readline, changing the hint leaves native output geometry
untouched. The next write or session close reconciles any physical resize.
Softline detects dynamic dimensions through `TIOCGWINSZ` without taking
ownership of `SIGWINCH`. An application may register its own signal/self-pipe
or other event source to update its external renderer before feeding more data.
Input bytes, editor cursor position, queue contents and pending parser state
survive resize. Memory use is independent of transcript length. The stream
accepts one-byte fragments indefinitely.

## Compatibility and public surfaces

The C and Lua surfaces expose a single native chat layout. ABI 0 is retained
while Softline and its sole consumer evolve together. Equivalent free functions validate NULL handles.
Public declarations document ownership, thread context, lifecycle, errors,
and chunk semantics for clangd. Lua methods document the same contract in the
Lua reference and adjacent public binding comments. Lua cannot invoke its VM
from a foreign producer thread; a watched descriptor supplies owner-thread
events. The core and Lua libraries do not import libmdf.

## Verification

- Real VTE PTY responses and tmux compare chunked producer output and the exit
  cursor with direct terminal bytes. Cases cover exact-width ASCII, combining
  marks, flags, joined emoji, variation selectors, and keycaps. VTE also checks
  first editor handoff and live width/height transitions with empty terminals
  and 100 prior rows, preserving internal blank rows. Finite output in one-byte
  Unicode chunks ends at the native endpoint before another stream begins.
  Writes after a retained editor frame and repeated terminal growth resume on
  the producer's last line, including a width hint between resize and write,
  with and without prior scrollback.
  Ignored SIGINT and returning handlers leave the input mode restored before
  more output; subsequent cursor reports remain absent from the transcript and
  session close restores the original input attributes.
  After `readline` returns, shrinking rows and sometimes columns before session
  close clears the retained input row at its new location and exits at the new
  bottom row.
- When GTK3 VTE and `xvfb-run` are installed, a real-engine regression runs the
  chat example in an already filled terminal. It compares physical transcript
  rows across 97↔96 and stepwise 110↔90 column changes with three responses,
  three prompt themes, and two terminal heights. Blank rows count as part of
  the transcript, so an erased row wrapping into an extra gap fails the test.
  A second VTE check generates three varied responses continuously while
  changing both width and height, with an editable draft and existing
  scrollback, under plain and Gruvbox prompts. It verifies complete paragraphs
  remain in order and the draft remains visible.
- A PTY test writes `Hello`, pauses, and observes it above an editable prompt
  before EOF. Later writes of ` world` continue the same line. Typing and
  queue edits remain responsive between writes.
- Identical output in one-byte and larger chunks yields the same visible
  screen, including styled ANSI and UTF-8 split across write boundaries.
- Prompt growth, queue/status rows, width setters, and terminal resize retain
  native geometry without replaying or clearing producer output.
  Real-terminal reflow tests use a gated libmdf producer with short paragraphs,
  lists, quotes, code blocks, combining characters, and wide characters. Each
  content set runs both without resizing and through width/height transitions;
  the driver waits until the application observes each requested PTY size.
  Static resize checks pause the producer and compare surviving transcript
  cells before and after Softline handles the resize; the terminal's own
  reflow is the reference. They cover paragraphs, lists, code, Unicode, and
  queue rows. A separate PTY matrix rejects scroll commands during physical
  resize and verifies scrolling remains available for ordinary prompt growth.
  Combined row/column growth tests append to an unfinished wrapped line after
  native reflow, with and without pre-existing scrollback, and compare the
  entire retained transcript to ensure earlier output was not overwritten.
  A producer-only matrix appends after width, height and combined resize,
  with ASCII, hard line breaks and Unicode, with and without scrollback.
  It compares the complete transcript to the terminal's native endpoint and
  then checks the handoff to a bottom-anchored editable prompt.
  Clipped unfinished-line tests compare native scrollback before and after
  continuation and reject newly inserted gaps; blank rows already introduced
  by native resize are preserved. The clipped-line PTY check also covers new
  output at a hard line boundary.
- The tmux live-example check spaces resize commands so each PTY notification
  settles. [tmux queues notifications for 250 ms](https://github.com/tmux/tmux/blob/3.6/server-client.c#L2647-L2713)
  while changing its grid immediately. Faster successive resizes can leave the
  visible grid and reported PTY geometry different while output is emitted;
  that case can still corrupt output. The `--queued-resize` diagnostic in
  `tests/check_terminal_resize.py` retains this reproducer. The standard paced
  test does not prove that diagnostic passes. There is no tmux-specific
  production workaround or transcript replay.
- Sink/write failure and session teardown leave a usable editor and report
  an actionable diagnostic. Repeated begin/end and invalid calls are tested.
  Native finite-output regressions reject incomplete ANSI/UTF-8 and callback
  failures, then verify the next call emits its bytes without stale prefixes.
  Starting a live stream during ordinary readline pages an existing wrapped
  draft before creating its output surface, preserving two output rows and
  the complete editable buffer.
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
