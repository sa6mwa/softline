# softline Current State And Readline Gap Spec

## Purpose

softline is a small C89/POSIX prompt editor for multiline command entry. Its
current center of gravity is shells, REPLs, chat prompts, and terminal tools
where `Enter` submits the current buffer and `Ctrl-J` inserts a literal newline.

softline is not currently a complete GNU Readline replacement. It is a
handle-owned editor core with enough terminal rendering, history, key binding,
and bounded prompt support to build multiline readline-like interfaces without
global editor state.

## Current Public Shape

The public API is the installed header `include/softline/softline.h`.

- `sl_t` is a receiver-style handle. Constructors return a handle with method
  pointers plus an opaque implementation pointer.
- `sl_create()` creates a handle with default configuration.
- `sl_create_with_config()` accepts `sl_config_t` for input/output fds, prompt
  bounds, history length, and line length.
- `sl_config_init()` sets the documented defaults:
  - input fd: `STDIN_FILENO`
  - output fd: `STDOUT_FILENO`
  - unbounded prompt geometry
  - history max length: 100
  - line max length: 4096 bytes
- `readline()` returns a project-allocated string that must be released with
  `free_string()`.
- `last_readline_status()` classifies the most recent `readline()` outcome as
  submitted text, EOF, cancellation, interrupt, or error.
- Free-function wrappers mirror the receiver methods. Receiver methods are the
  primary handle-oriented style; wrappers provide conventional C call sites and
  NULL-argument guarding.

Each handle owns its own:

- editable line buffer
- cursor position
- history list
- terminal raw-mode state
- rendered prompt cache
- key bindings
- idle callback
- last-error text

softline does not keep global editor state.

## Current Editing Behavior

The editor supports these built-in behaviors:

- `Enter` submits the current buffer.
- `Ctrl-J` inserts a newline.
- `Ctrl-D` returns `NULL` on an empty buffer and deletes forward inside text.
- `Ctrl-C` restores terminal state and raises `SIGINT`.
- `Ctrl-A` / Home moves to the start of the buffer.
- `Ctrl-E` / End moves to the end of the buffer.
- `Ctrl-B` / Left and `Ctrl-F` / Right move by common UTF-8 cluster boundaries.
- Up and Down navigate visual rows first, then history where applicable.
  `Ctrl-P` and `Ctrl-N` navigate previous and next history entries directly.
- `Ctrl-R` starts reverse incremental search over the handle's current history.
  Typing updates the query, repeated `Ctrl-R` cycles older matches and wraps,
  Enter accepts the displayed match, and Escape or `Ctrl-G` restores the draft.
- `Alt-B` and `Alt-F` move by words.
- Backspace and Delete operate at the cursor.
- `Ctrl-U`, `Ctrl-K`, and `Ctrl-W` edit across the whole multiline buffer.
- Bracketed paste mode is enabled while editing; pasted carriage returns become
  buffer content instead of submitting the prompt.
- Input is limited by `line_max_len`.
- Non-tty input falls back to a plain line reader that does not emit prompts.

UTF-8 is preserved as bytes while rendering uses terminal-cell width for
combining marks, East Asian wide characters, and common emoji sequences.
Cursor and delete operations avoid splitting common UTF-8 clusters. Public
cursor offsets remain byte offsets.

## Current Rendering Behavior

softline renders multiline prompts directly to a terminal fd.

Current rendering guarantees:

- Long input wraps.
- Word runs are kept together where terminal width allows.
- Exact-width rows avoid accidental terminal autowrap prompt duplication.
- Prompts can grow at the bottom of a terminal without overwriting prior output.
- Resize is handled while `readline()` is idle.
- Render capacity is derived from the configured line length rather than a fixed
  row cap.
- Render failures during active editing return as errors, not as submitted
  input.

softline has two prompt modes:

- Normal mode: the prompt participates in the terminal's ordinary scrollback.
- Bounded mode: `set_bounds()` pins the prompt to a rectangular region. A
  `0,0,0,0` bounds configuration, or `sl_config_t` with `bounded = 1` and zero
  bounds, means a dynamic full-terminal bottom prompt.

Bounded mode is intended for alternate-screen applications such as chat-style
interfaces. In that mode, `print_above()` streams output through the area above
the prompt without overwriting the active input buffer.

### Bounded Prompt Queue

Bounded handles can opt into a prompt queue through configuration or
`set_prompt_queue()`. The queue is intentionally unavailable for normal and
non-TTY prompt paths. Tab moves a nonempty active editor into the queue and
clears the editor; Tab on an empty editor does nothing. Alt-E removes the most
recent queue entry and restores it to the active editor for editing.

`next_prompt()` is the dispatch boundary. It removes an existing queue entry in
FIFO order before opening the editor, reporting `SL_PROMPT_SOURCE_QUEUED`; when
the queue is empty it acts like `readline()` and reports
`SL_PROMPT_SOURCE_DIRECT` for submitted text. Existing `readline()` callers
remain direct-only.

While a bounded editor is active, the renderer owns a queue panel above it. The
panel shows a total count plus a capped oldest-first preview list, and supports
all built-in prompt themes, including the ANSI-only `default` theme. The
selected prompt theme
also styles the active editor in both normal and bounded modes. The panel is
not built with `print_above()` because queue entries must be removable and must
reflow with the active editor. Application key bindings retain precedence over
the Tab and Alt-E defaults.

## Current Extension Points

### Key Bindings

`bind_key()` attaches a per-handle callback to a key. The callback may:

- handle the key completely
- pass through to built-in behavior
- submit
- cancel
- interrupt
- mutate the active buffer with public methods such as `insert()`,
  `set_buffer()`, `set_cursor()`, `submit()`, and `cancel()`

Current key coverage includes common control keys, arrows, Home/End, Delete,
Paste Begin/End, F1-F10, distinguishable Ctrl-Enter sequences, and Alt-letter
keys represented through `SL_KEY_ALT_BASE`.

### Idle Callback

`set_idle_callback()` registers a callback invoked while `readline()` is active
and no input byte is available. It can update the buffer, print above the
prompt, submit, or cancel through public methods.

This is the current hook for timer-like or async-adjacent behavior. It is not a
general event loop integration API.

### Streaming Output Above Prompt

`print_above()` accepts a chunk callback and writes all chunks above the active
prompt. The callback returns `SL_OK` with a non-empty chunk to continue, or
`SL_OK` with length `0` to finish.

For an unbounded prompt, output uses normal clear-and-redraw scrollback by
default. Set `live_scroll_region = 1` in `sl_config_t` or call
`sl_set_live_scroll_region()` to opt into a cursor-position probe once the
prompt reaches the terminal bottom. When supported, softline temporarily
scrolls the full-width region above the retained prompt, avoiding a prompt
repaint while keeping queue, status, wrapping, and resize reflow aligned to the
bottom. The terminal scroll region is reset on every completion, cancellation, error,
and handle teardown. If the terminal does not answer the probe, output uses the
compatible clear-and-redraw path.

## Current History Behavior

History is per handle.

Supported behavior:

- add entries manually
- cap history length
- navigate history during editing
- preserve an edited draft while moving through history
- reverse incremental search over manually added or explicitly loaded entries
- save and load history files
- encode multiline history entries
- reject oversized history records without splitting them
- force saved history files to owner-only permissions

softline does not currently provide a global history registry, timestamped
history, duplicate suppression policy, forward search, history expansion, or
shell-style history metadata.

## Current Error And Cleanup Behavior

- Most fallible methods return `SL_OK` or a negative `sl_status_t`.
- `readline()` returns `NULL` for EOF, cancel, interrupt, or failure.
- `last_readline_status()` distinguishes those `NULL` outcomes and also records
  successful submitted text.
- `last_error()` exposes the most recent diagnostic string.
- `destroy(NULL)` is safe.
- Public string results are released through `free_string()`.
- Terminal raw mode and bracketed paste are restored on normal return, cancel,
  interrupt, and tested error paths.

The return value remains optimized for the common case: a non-`NULL` result is
submitted text, while `NULL` means no submitted text. Callers that need precise
classification should read `last_readline_status()` before the next
`readline()` call.

## Current Packaging And Portability

softline currently targets C89 plus POSIX terminal APIs.

Current build surfaces include:

- CMake presets
- static and shared library builds
- shared-library ABI/SOVERSION policy through `SOFTLINE_ABI_VERSION`, currently
  `0`, decoupled from the project release version
- installed CMake package exports with canonical target `softline::softline`
- installed pkg-config metadata
- Lua 5.5 facade packaged as LuaRocks source artifacts
- example binaries
- Lua examples matching the simple and chat workflows
- CTest unit/example coverage
- Lua facade smoke coverage
- sanitizer test target
- package verification scripts
- external installed-package consumer smoke test

The library is not currently a Windows console implementation, a terminfo
database consumer, or a curses-style screen abstraction.

## Gap List Toward A More Complete Multiline Readline Alternative

This section is a product and engineering backlog, not a promise that every item
belongs in softline. Each item should get tests before being considered part of
the public contract.

### Completion

Missing:

- completion callback API
- completion menus
- common-prefix insertion
- completion replacement spans
- multiline-aware completion display
- async completion lifecycle
- completion cancellation
- completion tests across wrapped prompts and bounded mode

Likely API shape:

- register a completion provider per handle
- pass immutable buffer, cursor, and prompt context to the provider
- return owned completion candidates plus optional replacement range
- keep rendering policy in softline rather than forcing callbacks to draw

### Hints And Autosuggestions

Missing:

- right-side hints
- inline ghost text
- validation messages
- async suggestion updates
- style policy for hints in narrow terminals

Important constraint: hints must never become submitted buffer content unless the
user accepts them explicitly.

### Searchable History

Supported:

- reverse incremental search
- search prompt rendering inside multiline/bounded mode
- accept/cancel search state

Missing:

- forward search
- history filtering hooks
- duplicate suppression options

### Editing Model

Missing or incomplete compared with mature readline-style editors:

- kill ring
- yank / yank-pop
- undo / redo
- transpose character / word
- case operations
- region/mark operations
- configurable word-boundary policy
- full Emacs-style command set
- vi insert/normal mode
- keymap layers

softline currently has a small fixed editing model plus per-key callbacks.

### Unicode And Width Semantics

Current behavior is UTF-8-preserving and terminal-width-aware for common
interactive prompt input. It is not a complete Unicode Text Segmentation or
locale policy implementation.

Missing:

- full Unicode grapheme-cluster segmentation
- locale-specific ambiguous-width policy
- Unicode normalization policy
- invalid UTF-8 policy
- locale-aware width tests
- callback APIs that expose both byte offsets and character-cell positions

This is one of the larger gaps for a production-grade multiline readline
alternative.

### Terminal Capability Handling

Current behavior emits common ANSI/VT-style escape sequences.

Missing:

- terminfo/termcap capability lookup
- terminal feature detection
- fallback strategies for limited terminals
- configurable bracketed paste support
- richer key sequence decoding
- mouse/pointer policy, if ever needed
- robust behavior under terminal multiplexers with unusual sequences

### Event Loop And Async Integration

The idle callback is useful but not a full integration model.

Missing:

- nonblocking readline session API
- explicit poll fd exposure
- step/tick API
- integration with `select`, `poll`, `epoll`, or external loops
- cancellation from another thread or signal-safe wakeups
- async output queueing above the prompt
- deterministic ownership rules for async callbacks

This would likely require a new session-oriented API rather than extending
`readline()` alone.

### Output And Transcript Management

`print_above()` handles chunked output above the active prompt, but softline does
not own a transcript model.

Missing:

- scrollback buffer abstraction for alternate-screen applications
- transcript redraw after resize
- line wrapping policy for transcript output
- styled output
- damage tracking across prompt plus transcript
- application-level viewport controls

For now, bounded mode is a prompt manager, not a full terminal UI toolkit.

### Styling

Missing:

- prompt styling API
- syntax highlighting callback
- completion/hint style policy
- active selection style
- terminal color capability handling
- style reset guarantees around callbacks

Any styling API must preserve byte ownership and terminal reset safety.

### Persistence And History Policy

Current history persistence is intentionally simple.

Missing:

- append-only history save
- atomic save/rename option
- lock policy for shared history files
- configurable permissions
- duplicate filtering
- timestamp or metadata records
- migration/version marker for richer history files

The current encoded line format is enough for multiline entries but not a rich
history database.

### Diagnostics

Missing:

- structured error object
- callback failure propagation detail
- optional diagnostic callback

This is important if softline becomes a lower-level component for larger
interactive applications.

### Portability

Missing:

- Windows console backend
- non-POSIX fallback strategy
- broader OS matrix tests
- terminal emulator compatibility matrix
- package smoke tests on more targets

### Security And Robustness

Areas that need continued hardening:

- malformed escape sequence handling
- very large configured line limits
- hostile terminal dimensions
- repeated resize storms
- callback reentrancy policy
- signal interaction beyond Ctrl-C
- file permission and atomicity policy for history

### Compatibility With Readline Expectations

softline does not currently aim for Readline ABI or API compatibility.

Missing if that goal changes:

- Readline-compatible headers or adapter layer
- Readline-style global variables
- `.inputrc` parsing
- named commands
- macro recording/playback
- completion API compatibility
- history API compatibility
- redisplay hooks

The current architecture is handle-oriented and intentionally not Readline's
global-state model. A compatibility layer should be considered a separate
facade, not a reason to weaken the core handle model.

## Suggested Near-Term Milestones

1. Completion provider API

   Add synchronous completion with replacement spans and bounded-mode tests.

2. Unicode width policy

   Decide whether softline remains byte-oriented with documented limitations or
   adopts a width/grapheme dependency.

3. Event-loop-friendly session API

   Split `readline()` into create session, feed input, render, poll, and finish
   operations while keeping the existing blocking API as a facade.

4. Searchable history

   Extend current reverse search with forward search, filtering hooks, and any
   richer search lifecycle API that later callers need.

5. Structured diagnostics

   Preserve `last_error()` and `last_readline_status()` but add richer
   machine-readable detail for advanced callers.
