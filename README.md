# softline

softline is a C89 multiline prompt editor derived from [linenoise](https://github.com/antirez/linenoise).
It is meant for shells, chat prompts, REPLs, and other readline-like
interfaces where Enter submits and `Ctrl-J` inserts a newline.

The project is a handle-oriented editor core. It is usable for simple
multiline prompts and terminal-native chat, but it is not a
complete GNU Readline replacement and does not provide Readline API or ABI
compatibility. The current-state and gap spec lives in `docs/softline-spec.md`.

The public API is handle based. Use a constructor to create an `sl_t`, call
methods on that receiver for instance behavior, and destroy it when done. Each
handle owns its own line buffer, history, terminal state, render cache, and
callbacks; softline keeps no global editor state.

```c
#include "softline/softline.h"

sl_t *sl = sl_create();
char *line = sl->readline(sl, "softline> ");
if (line) {
  sl->history_add(sl, line);
  sl->free_string(sl, line);
} else if (sl->last_readline_status(sl) == SL_READLINE_EOF) {
  /* input ended */
}
sl->destroy(sl);
```

Pass `NULL` as the prompt to use the default prompt, `"> "`.

Receiver methods and `sl_*` free-function wrappers have the same behavior. Use
receiver methods when that reads naturally in handle-oriented code, and wrappers
when a conventional C function-call style is clearer.

## Behavior

Currently implemented:

- Enter submits the current buffer.
- `Ctrl-J` inserts a newline.
- `Ctrl-D` on an empty buffer returns `NULL`; inside text it deletes forward.
- `Ctrl-C` remains a terminal interrupt instead of being swallowed.
- `last_readline_status()` distinguishes submitted text, EOF, cancellation,
  interrupt, and failures after `readline()` returns.
- `Ctrl-U`, `Ctrl-K`, word deletion, movement, history recall (`Up`/`Down` or
  `Ctrl-P`/`Ctrl-N`), and delete keys operate across the whole multiline buffer.
- `Ctrl-R` starts reverse incremental history search over the handle's current
  in-memory history, including entries loaded before `readline()`.
- `Ctrl-V` reads PNG or JPEG image data from the X11 `CLIPBOARD` selection and
  inserts the literal path to an owner-only cache image file at the cursor.
  It speaks X11 directly through `DISPLAY`, including an SSH-forwarded display,
  without a host X11 library or clipboard command. The file remains
  after the editor exits so the host can consume it. By default the path is
  `${XDG_CACHE_HOME}/softline/{xid}.png` (or `.jpeg`), falling back to
  `$HOME/.cache/softline/`; `xid` is a 20-character lowercase base32hex ID
  compatible with [rs/xid](https://github.com/rs/xid), implemented inside
  Softline without an added library dependency.
  `sl_config_t.image_paste_path_template` or
  `sl_set_image_paste_path_template()` changes the path. Its single `*` becomes
  the xid; `.png` or `.jpeg` is always appended, so
  `/tmp/softline/*/image` creates `/tmp/softline/{xid}/image.png`. Parent
  directories are created recursively. Templates accept `~/`, `{{HOME}}`,
  `{{home}}`, `{{XDG_CACHE_HOME}}`, and `{{xdg_cache_home}}`; the XDG token
  follows the same fallback rule. Set `disable_image_paste = 1` to disable
  built-in capture (the draft stays unchanged). A missing or unsupported
  image leaves the draft unchanged and sets `last_error()`. X11 image paste is
  currently available on Linux when `DISPLAY` is present;
  the transfer limit is 32 MiB. A `Ctrl-V` key binding takes precedence.
- Long input wraps by words where possible and reflows after terminal resize.
- Bracketed paste is enabled while editing so pasted carriage returns become
  buffer content instead of submitting the prompt.
- Editing uses a plain silent line reader when either standard input or output
  is not a TTY and does not emit prompts; streamed output uses LF line endings
  instead of terminal CRLF.
- While Softline owns terminal raw mode, kernel tab expansion
  (`TAB3`/`OXTABS`, such as `stty -tabs`) is disabled so tabs reach the terminal
  unchanged. OPOST/ONLCR and other output flags are preserved. Original flags
  are restored on release, including for separate input and output TTYs.
  Native stream writes after that release temporarily disable tab expansion
  for the write and restore it before returning.
- Keys such as TAB, Enter, function keys, and Alt-letter combinations can be
  bound per handle. A binding may handle the key, pass through to the built-in
  behavior, submit, cancel, interrupt, or mutate the active buffer.
  `SL_KEY_ALT_ENTER` represents the broadly supported Escape-plus-Return
  sequence. `SL_KEY_CTRL_ENTER` remains available when the terminal sends a
  distinguishable Ctrl-Enter sequence.
- Chat-like prompts can opt into a bounded FIFO prompt queue with public C and
  Lua inspection/mutation APIs. The default profile keeps Tab queueing and
  Alt-E edit-newest behavior, while the queued-turns profile binds
  Enter-to-queue to Softline's busy lifecycle. Either profile can auto-deliver
  ordinary queued work or leave it for explicit host delivery. The renderer ships
  default, plain, accent, Dracula, Gruvbox, monochrome, monogreen, Outrun,
  Riced, and Synthwave prompt themes. Optional status lines use the selected
  palette. The chat examples add sent nonempty prompts to their history, so
  `Up`/`Down` and `Ctrl-P`/`Ctrl-N` recall sent prompts while Alt-E remains
  reserved for unsent queued drafts.
- Interactive handles can watch application-owned file descriptors. Softline
  waits for terminal and watch readiness together, then invokes the watch
  callback on the editor owner thread so streamed output can redraw above a
  live draft without cross-thread handle access. Ready watches are visited
  round-robin in bounded batches so terminal input remains responsive.
- UTF-8 input is preserved, common Unicode clusters are kept intact by
  cursor/delete operations, and rendering accounts for combining marks, East
  Asian wide characters, and common emoji widths.

Not currently implemented:

- completion menus or completion provider APIs
- inline hints, autosuggestions, or syntax highlighting
- forward history search or history filtering hooks
- kill ring, undo/redo, vi mode, or full readline-style command sets
- full Unicode Text Segmentation, locale-specific ambiguous-width handling, or
  normalization
- terminfo/termcap capability lookup
- Windows console backend
- Readline-compatible headers, globals, `.inputrc`, or ABI

## Examples

`example_simple` is the normal terminal prompt. Output is printed after each
submitted line and the next prompt proceeds below it like an ordinary REPL.

`example_chat` is the C streaming Markdown demo. It links libmdf only as an
example dependency: libsoftline and its installed package remain independent
of libmdf. The composer gives libmdf a two-column left margin and its default
ANSI palette for terminal output (escape-free when redirected), updating both
renderers' geometry on resize and dropping the margin on very narrow terminals. Softline's quoted-prompt helper writes each
submitted prompt directly into the output session as a literal, italic quote.
It repeats the configurable `> ` prefix after wrapping and supplies the line
breaks needed for one visible empty row on each side. Markdown punctuation in
the prompt stays literal; only response text goes through libmdf. A
worker emits one source character every 20 ms; the owner-thread watch callback
feeds libmdf incrementally and forwards each sink fragment directly into a
Softline output session. The responses mix headings, subheadings, italic, bold,
code, and paragraphs while input remains editable.

While available, Enter dispatches a turn; while its operation is running,
Enter queues a follow-up in Softline. While busy, Alt-Enter queues a steer
entry or marks the newest queued entry as steer when the editor is empty. The
example takes steers after a Markdown block boundary, then continues the
response stream. A steer that arrives after the last boundary starts the next
simulated response. While idle, Alt-Enter submits a draft or promotes the
newest queued entry. Alt-E edits the newest queued draft. On completion, the
example starts the oldest remaining queue entry as the next turn. The
example uses the status spinner only as a presentation of its application-owned
busy state. Escape or Ctrl-C returns cancellation to the application; the C chat
example uses it to stop the active simulated operation, retain its queue, and keep the
chat open. Automatic FIFO release stays stopped until the user submits a new
turn or manually promotes a queued one.
The queue UI, status line, and simulated operation stream activate only when
both standard input and output are terminals; piped input or redirected output
produces plain libmdf-rendered responses with Softline-quoted prompts. The
separate Lua chat example remains a plain queued-turn demonstration; its facade
exposes the same generic output-session API for Lua applications composing an
external renderer.

```sh
make run-simple
make run-chat
make run-chat-default
make run-chat-plain
make run-chat-riced
make run-chat-monogreen
make run-chat-monochrome
make run-chat-synthwave
make run-chat-without-delay
make run-chat-without-delay-riced
```

The chat convenience targets build the C example before launching it.
The two `without-delay` targets set `SOFTLINE_CHAT_CHAR_MS=0`, removing the
example producer's simulated typing delay, with default and riced themes.
`run-chat` uses Gruvbox by default; pass `THEME=...` to override it. Both C
and Lua examples accept
`SOFTLINE_PROMPT_THEME=default`, `plain`, `accent`, `dracula`, `gruvbox`, `monochrome`,
`monogreen`, `outrun`, `riced`, or `synthwave`; the generic Make targets also
expose that as `THEME=...`. The simple and chat examples default to `default`;
`make run-chat` supplies Gruvbox. Both chat examples use one persistent output
session with the native terminal layout by default.

## Terminal-native chat

A full-terminal output session starts the transcript at the current terminal
cursor. The editable prompt starts at the bottom. Physical resize preserves
the terminal's position for an unchanged prompt, including newly exposed rows
below it. Only prompt layout changes update its cells.
A VT scroll region ends immediately above the current prompt frame. Producer
bytes, including libmdf styles and wrapping, pass through unchanged. Softline keeps
parser state and the output cursor; it does not cache, pad, rewrap, clear, or
replay the native transcript.

Physical resize can race an in-flight VT batch and misplace even ASCII output.
This is an [accepted concurrency limit](docs/softline-mdf-stream-design.md#review-exception-concurrent-physical-resize).
The unpaused VTE diagnostic remains available as `--streaming-race`; regression
gates pause their private producer at resize boundaries.
After readline returns, a retained prompt has no input-owned resize observation.
The immediate feed fallback can misplace ASCII continuation when earlier lines
reflow or preserved terminal history differs from its estimate. See the
[unobserved resize limit](docs/softline-mdf-stream-design.md#review-exception-unobserved-retained-frame-resize).
The active chat loop observes resize before dispatching producer watches.

Before a prompt frame exists, output uses the full terminal and keeps the
producer cursor in place between writes. Native autowrap and Unicode clusters
survive chunk boundaries. Full-height output follows terminal resize without
another margin command. Cursor reports refresh the producer position at
resize and cursor handoffs outside feed calls. Feed writes have **no intentional
latency**: no sleeps, timed coalescing or terminal cursor-position round trips,
even during resize. Complete UTF-8/SGR units are emitted before the call returns;
only an incomplete sequence is retained until its remaining bytes arrive.
Cursor advances use local scalar cell widths shared with prompt layout.
Terminal Unicode versions and emoji/cluster shaping can differ from the
Unicode 16.0 scalar accounting.
Producer bytes, wrapping and scrollback remain producer/terminal-owned; there
is no transcript replay or glyph-span reconstruction.
At a known ASCII right edge with an active prompt, a later chunk uses a hard
row advance after the cursor handoff. This avoids overwriting the last cell,
but that row may not join on width growth and a Unicode cluster split there
may lose its attachment. See the scoped
[review exception](docs/softline-mdf-stream-design.md#review-exception-active-prompt-right-edge-continuation).

Softline retains the previous prompt frame and patches only changed cells.
Feeding output leaves unchanged prompt cells intact and restores the editor
cursor. The output region follows the current frame height: only visible
queue entries, nonempty status messages, status lines, and editor rows occupy
prompt space. Growth scrolls existing output cells only enough to fit when
needed; shrink returns freed rows to output without moving transcript cells.
The editor pages only when the frame exceeds the terminal's available height.
Resize leaves transcript cells to the terminal. Softline queries the live input
cursor once when reconciling changed geometry and applies its movement to the
tracked output position. Cursor positions are stored as distances from the
bottom; no terminal cursor save/restore sequences are used. Unchanged prompt
rows that still fit are preserved. Width changes rebuild only prompt layout;
transcript reflow belongs to the terminal. The prompt retains its logical row
layout across successive resizes, even when a physical row temporarily wraps.
A scalar cell counter handles plain ASCII lines. Unicode advances use local
scalar widths; Unicode and tabs have no span tables or reflow reconstruction. Their tracked column
is retained, clamping only an offscreen column after resize. Continuation after
width reflow of an unfinished Unicode/tab line can therefore resume at a
different cell, including a CR-overwritten tail. This is an intentional
[opaque-feed limitation](docs/softline-mdf-stream-design.md#review-exception-opaque-feed).
Feed state is bounded independently of line length. If output scrolls at the
bottom of the output margin, a later width change can reflow that boundary
differently across terminal emulators; exact continuation at that seam is not
guaranteed without replaying the producer's text.
Rapid tmux resizes may update its visible grid before delivering the matching
PTY size; output emitted during that interval can be corrupted. The ordinary
resize tests wait for each size notification before proceeding.
After a carriage return, Softline tracks the cursor separately from the
still-visible end of that line so resize does not place new output in its tail.
Unicode/tab tails follow the opaque-feed limitation above. A clipped producer
cursor can also resume on a different row. See the
[review exception](docs/softline-mdf-stream-design.md#review-exception-output-clipped-behind-a-bottom-anchored-prompt).
Every batch that positions the output cursor establishes its scroll margin
alongside the producer bytes. Physical resize itself updates geometry;
an unchanged prompt needs no cursor movement or repaint. Updating the margin
does not scroll the transcript again. Softline does not recover or replay output
that leaves the visible screen. An unfinished line that moves into scrollback
continues at the first visible output row. If the clipped position was at a hard
line boundary, new output starts next to the prompt. For example, with no
prior scrollback, a bottom-anchored prompt can keep the hardware cursor at
the bottom during a height shrink while a short CR-overwritten output line
scrolls above the screen. Its continuation cannot resume at the original
offscreen cell; it follows the clipped-line rule. Existing scrollback,
including native blank rows, stays intact. A promptless stream ending after CR
advances below the still-visible line. Ending chat restores the full scroll
region, clears only the input rows, and keeps queue and status rows visible.
The cursor remains at column zero on the current input row, with no final
newline or scroll. Set
`sl_config_t.clear_prompt_on_exit = 1` to clear the whole prompt area and return
below the transcript instead. Ending a stream inside an active editor keeps
the prompt and scroll region for later finite output or another stream;
destroying the handle always closes native chat. Native feeds do not toggle
cursor visibility. Feed calls do not wait for cursor-position replies;
bounded chunks are batched with cursor restoration, retrying short writes as
needed.

Chat uses the full terminal width and needs at least three rows: two for the
VT100 scroll region and one for the prompt. Rectangular viewports are unsupported.
Softline never enters or leaves the alternate screen itself.

For ordinary finite `print_above()` calls without a persistent output
session, `sl_set_live_scroll_region()` remains available to configure the
readline scrollback editor. It acquires ownership at startup and retries
between producer callbacks as output reaches the bottom. Feed calls remain
immediate, including multiple feeds in one callback. Chat examples use the
persistent session API.

## Persistent output session

Open a session once, forward each external renderer sink fragment, and close it
when the conversation ends. The receiver and free-function C APIs are
equivalent; Lua exposes `sl:output_stream_begin()`,
`sl:output_stream_write(bytes)`, and `sl:output_stream_end()`.

```c
sl->output_stream_begin(sl);
/* Called on the editor owner thread for each external sink emission. */
sl->output_stream_write(sl, bytes, length);
sl->output_stream_end(sl);
```

Completed sessions retain their visible TTY rows. The next output starts on a
fresh row if the previous session ended mid-row. Finite `print_above()` output
between sessions continues in ordinary terminal scrollback.

Each write is visible before it returns, including while `next_prompt()` is
active. Chunk boundaries add no content or document separators; ANSI SGR and
UTF-8 sequences may cross calls. End rejects an incomplete sequence. Softline
does not automatically append submitted editor text to the transcript;
applications render it explicitly. Use
`sl_output_stream_write_quoted_prompt(sl, submitted)` between complete renderer
segments for a literal, italic prompt. Softline wraps it at the current output
width, repeats `> ` on each visible row, and supplies missing line breaks for
one blank row on each side. The prefix and its colour can be configured
independently with `sl_set_quoted_prompt_prefix()` and
`sl_set_quoted_prompt_style()`; NULL restores the theme defaults. The Lua
facade exposes matching methods. The composer
updates its external renderer width on the owner thread when the terminal
changes. Softline detects terminal dimensions and redraws only the prompt;
it never replays the transcript. A watched FD is the usual way to
deliver producer events without blocking editor input. See the
[composition contract](docs/softline-mdf-stream-design.md) for limits and
failure semantics.

## Prompt queueing

Prompt queueing is available for interactive chat-like prompts, including
normal scrollback terminals. It does not alter non-TTY input. Enable it in the
handle configuration or after construction, and read application work through
`next_prompt()`:

```c
sl_prompt_source_t source;

sl->set_prompt_queue(sl, 1, 64, 3);
sl->set_prompt_theme(sl, SL_PROMPT_THEME_DEFAULT);

for (;;) {
  char *line = sl->next_prompt(sl, NULL, &source);
  if (!line)
    break;
  /* source is SL_PROMPT_SOURCE_DIRECT or SL_PROMPT_SOURCE_QUEUED. */
  sl->free_string(sl, line);
}
```

Tab queues a nonempty active editor and leaves a FIFO preview panel above the
current input; Tab on an empty editor is a no-op. The panel uses the themed
`Q 1. preview` layout, shows oldest-first previews, and adds `... N more` when
entries exceed the configured preview count. Alt-E removes the newest queued
entry and restores it to the editor. Explicit key bindings continue to override
these defaults. Prompt appearance is renderer-owned so it remains safe with
layout: `default` uses only standard ANSI colours: a bold bright-white marker,
normal terminal-colour input, subdued dark-gray queue text and separators,
standard-colour status elements, and red/green busy markers. `plain` keeps the
editor and queue uncoloured while quoted prompts and status messages use
neutral ANSI styling. `accent`, Dracula, Gruvbox, monochrome, monogreen,
Outrun, Riced, and Synthwave use their embedded palettes. The selected theme
applies to every interactive prompt, including normal readline prompts,
status lines, and queue panels. Prompt markers reset before typed text;
monochrome and monogreen additionally colour typed text as defined by their
palettes.

### Queue control API

The queue remains renderer-owned, but an embedding application can inspect and
mutate it without synthetic terminal input or a parallel queue. Queue indexes
are oldest-first and returned strings are released with `sl_free_string()`.

```c
sl->set_prompt_queue(sl, 1, 64, 3);
sl->set_prompt_queue_profile(sl, SL_PROMPT_QUEUE_PROFILE_QUEUED_TURNS);
sl->set_prompt_queue_delivery(sl, SL_PROMPT_QUEUE_DELIVERY_MANUAL);

/* An operation starts: Enter now queues nonempty drafts. */
sl->set_status_busy(sl, 1);

/* At a response seam, inspect modes and take a STEER entry if desired.
 * At completion, return to idle and choose the next queue entry. */
sl->set_status_busy(sl, 0);
```

`SL_PROMPT_QUEUE_PROFILE_DEFAULT` preserves Tab, Alt-E, and automatic FIFO
delivery by default. `SL_PROMPT_QUEUE_PROFILE_QUEUED_TURNS` is the native
turn lifecycle preset: while `set_status_busy(sl, 1)` is active, Enter queues
a nonempty draft; with automatic delivery, returning idle delivers the oldest
ordinary queued turn. Starting that turn sets busy again, so later entries
stay visibly queued. In idle mode, Enter submits normally
and an empty Enter is ignored. While busy, Alt-Enter queues a nonempty draft
with `SL_PROMPT_QUEUE_MODE_STEER`; with an empty draft it marks the newest
queued entry as steer. The queue preview labels these entries `S`. The host
uses `sl_prompt_queue_get_mode()` to choose a steer entry and
`sl_prompt_queue_take()` to consume it at a safe response seam. Ordinary
queued entries retain `SL_PROMPT_QUEUE_MODE_QUEUED`. Set
`SL_PROMPT_QUEUE_DELIVERY_MANUAL` to let the host choose both kinds of entry
and when to take them; the C and Lua chat examples do this. With automatic
delivery, ordinary queued entries are released when the operation becomes
idle. In idle mode, Alt-Enter submits a nonempty draft or promotes the newest
entry immediately. Explicit
`sl_bind_key()` bindings always take precedence over these built-ins.
Cancelling a queued-turns editor keeps queued drafts visible but stops automatic
FIFO release; a subsequent direct submission or manual promotion resumes it.
The chat examples recognize `/quit` when they receive it. An ordinary queued
`/quit` runs after earlier FIFO entries; a steered `/quit` runs when the example
takes steers at its next response boundary. An idle Alt-Enter promotion delivers
it directly. Entries behind `/quit` are not processed.
The simple examples also use `/quit` to leave.

## External events while editing

Register an application-owned nonblocking wake FD before entering an
interactive `readline()` or `next_prompt()` call. Softline never reads, closes,
or changes that FD. Its callback runs on the same thread that owns the editor,
where it drains bounded application work and may call `print_above()`, update
status, or complete a queued-turn operation safely.

```c
sl_watch_id_t watch;

sl->watch_add(sl, wake_fd, SL_WATCH_READ | SL_WATCH_HANGUP,
              on_application_wake, app, &watch);
```

## Status lines

Status lines are opt-in renderer-owned live rows between queue previews and the
editor. Set their elements in bulk or update an individual element from an idle
or key callback. Element text must be valid UTF-8 without C0/C1 controls or
DEL. Every colour theme has eight element colours. Element zero is
the first application element (typically the model name), and the selected
starting index wraps modulo eight: an offset of 15 therefore uses slot 7 for
the first element and slot 0 for the second.

```c
static const char *const status[] = {
    "gpt-5.6-terra high", "ctx 36%", "demo/project", "feat/prompt-queue"};

sl->set_statusline(sl, 1, 0);
sl->set_status_elements(sl, status, 4);
sl->set_status_message(sl, "Thinking..."); /* update during live output */
sl->set_status_message(sl, "Reasoning...");
sl->set_status_message(sl, NULL);          /* clear */
sl->set_status_idle_marker(sl, '-');  /* optional green idle marker */
sl->set_status_busy(sl, 1);    /* x by default, or /-\\| with spinner enabled */
sl->set_status_spinner(sl, 1);
```

Idle uses a green `+` by default. Set one printable ASCII character with
`sl_set_status_idle_marker()` to choose another green marker, or pass `'\0'`
to leave its reserved two-column marker slot blank while preserving alignment
with busy markers. Busy uses red `x`; the busy spinner uses that same red, is
off by default, and advances every 500ms only when both spinner and busy are
enabled. Elements wrap between elements when possible; an oversized element
wraps by text. Softline retains at most 32 elements. A longer bulk update keeps
the first 31 and renders `...` as the final element.

A nonempty status message occupies rows immediately above the status line.
`sl_set_status_message()` accepts printable single-line UTF-8,
wraps long text at words with continuation rows indented to the prefix width,
and redraws immediately while the editor is
active. The default prefix is `! ` in the theme's muted colour; the message
text is italic in the theme's secondary colour. Use
`sl_set_status_message_prefix(sl, "? ")` to change the prefix, `""` to hide it,
or `NULL` to restore `! `. Use `sl_set_status_message_colors(sl, prefix_color,
text_color)` to select palette roles independently, such as
`SL_THEME_COLOR_MUTED` and `SL_THEME_COLOR_ELEMENT_2`. Clearing the message
removes its rows and returns that space to the output region.

The output callback is chunk based. Return `SL_OK` with `*chunk` and `*len` set
for each chunk; return `SL_OK` with `*len == 0` to end the stream.

```c
struct chunks {
  const char **parts;
};

static int next_chunk(sl_t *sl, void *userdata,
                      const char **chunk, size_t *len) {
  struct chunks *stream = userdata;
  (void)sl;
  if (!stream->parts[0]) {
    *chunk = NULL;
    *len = 0;
    return SL_OK;
  }
  *chunk = stream->parts[0];
  *len = strlen(stream->parts[0]);
  stream->parts++;
  return SL_OK;
}
```

## Build and test

```sh
make build
make test
make test-terminal-cache
make deps DEPENDENCY=libmdf
make deps DEPENDENCY=lua PRESET=debug-lua
make asan
make valgrind
make package-consumer-smoke
make lua-test
make prerelease
```

On native x86_64 Linux, `make test` provisions checksum-pinned GTK/VTE,
Xvfb, keyboard, font and clipboard test tools from the lifecycle's shared
verified archive cache. `make deps-terminal-tests` prepares them independently.
The cache is selected by `CPKT_DEPENDENCY_CACHE`, otherwise
`${XDG_CACHE_HOME:-$HOME/.cache}/c.pkt.systems/deps`; every reused archive is
hashed. Repository-local extraction and runtime staging are disposable under
`.cache/deps-build/x86_64-linux-gnu/terminal-tests` and
`.cache/deps/x86_64-linux-gnu/terminal-tests`. No package manager installation
or maintainer scripts run, and none of these tools enter the shipped libraries.

The pinned native tools use official Ubuntu package archives, an explicit
source exception for test tools recorded with versions, URLs and SHA-256 hashes
in `cmake/terminal-tests/archives.json`, including their libc, compiler runtime
and desktop library closure. Provisioning verifies every ELF dependency and
imported libc symbol version is staged. Host GTK/GLib/X11 installs are not
required. Configured CTest tests retain the prepared environment, including for
direct `ctest --preset debug` runs. Terminal tools and the test interpreter run
through the cached loader;
fixture children, host build tools and shell utilities retain their own runtime.
The staged Xvfb keyboard compiler lookup is relocated to the staged `PATH`;
the verified archive remains immutable. Font caches and temporary test workspaces
live under `build/terminal-test-tools`. `make test-terminal-cache` checks offline
reconstruction, corrupt archives, interrupted staging, runtime isolation and
rejection of missing runtime libraries or libc symbol versions.
Transient archive transfer failures are retried up to three times; partial or
unverified downloads never become cache entries. Other hosts retain existing
native test-tool discovery.

Every project-owned C target is compiled as C89 with POSIX terminal APIs.
Shared builds use `SOFTLINE_ABI_VERSION=0` for SONAME/SOVERSION during current
development. Softline and its sole consumer change together; API changes in
this development phase do not establish a new ABI compatibility commitment.
Both are rebuilt together; older compiled consumers are unsupported. The
[development ABI exception](docs/softline-mdf-stream-design.md#review-exception-development-abi-0)
records this explicit decision and its scope.

Ordinary Linux debug, sanitizer, Valgrind, package-consumer, and release package
builds use the pinned native GNU Bootlin toolchain. The current pinned Bootlin
compiler executables are x86_64-hosted, so ordinary Linux lifecycle work is
supported on native x86_64 Linux hosts. Unsupported Linux hosts fail closed
rather than selecting host compilers or binutils. The release matrix ships the supported Linux target artifacts
(`x86_64`, `aarch64`, and `armhf`, each GNU and musl) from those pinned
toolchains. Fuzzing remains native `x86_64-linux-gnu` only.
Host `clangd`, `clang-format`, Valgrind, Lua 5.5, LuaRocks, and packaging
utilities are development tools; the C compiler, linker, archiver, and sysroot
for project-owned Linux release builds come from the lifecycle toolchain
resolver.

Local tests, examples, fuzzers, and temporary SDK consumers on supported x86-64
Linux hosts select the
Bootlin ELF interpreter and runtime libraries at link time, including in Release
builds. Run these executables directly. Their private runtime paths also cover
child executables and indirect library dependencies without exporting
`LD_LIBRARY_PATH` to host tools. Shipped SDK libraries and exported metadata do
not contain these local runtime settings.

The release guard inspects every ELF payload by file signature, including nested
archives, and rejects non-system interpreters, non-`$ORIGIN` runtime paths, and
embedded local or Bootlin collection paths. Malformed archives or failed ELF
inspection block verification. `make test-artifact-runtime` exercises positive
and negative packaged fixtures; it is part of `make test-all`.

Lua verification and examples use a local Lua 5.5.1 interpreter built with the
same selected collection. Its checksum-pinned upstream source archive is reused
from the shared dependency cache; disposable extraction state stays under
`.cache/deps-build/`, while the interpreter build remains under
`build/local-lua` (or `build/local-lua-debug` for debug workflows).
LuaRocks remains a host packaging tool. This runtime selection is not hermetic
execution and does not establish compatibility with older deployment libcs.

`make prerelease` is the deterministic local gate: formatting, debug and
sanitizer tests, native Valgrind, Lua, toolchain, editor, header, and
install-tree consumer checks. `make release-matrix` builds the standard Linux GNU/musl target matrix,
generates binary and Lua release artifacts, writes checksums, and verifies
package layout, runtime loader metadata, and release privacy. The rehearsal
matrix may skip Darwin when osxcross is unavailable. `make release` is stricter:
it requires the Darwin toolchain and a verified Darwin artifact, then creates
and reconstructs the source archive; packaged Darwin artifacts require
target-correct Mach-O inspection.

Linux clipboard protocol tests run without an X server. They check request and
reply sequence wraparound, I/O failures, and byte preservation for a 17 MiB PNG
transferred in small INCR chunks. The optional X11 tests also exercise real
clipboard owners and forwarded displays.

Package smoke links fully static GNU and musl consumers with fatal linker
warnings. Linux X11 hostname lookup uses privately bundled MIT-licensed c-ares
1.34.8 (with its BSD-3-Clause sorting helper) in both static and shared libraries:
literal IPv4/IPv6 addresses (including IPv6 interface qualifiers), `/etc/hosts`,
then DNS configured by `/etc/resolv.conf` (including search
domains). It does not use libc NSS or load hostname modules. No additional
library is needed when linking `libsoftline.a`. Resolver symbols are private
and namespaced so callers may also link their own c-ares. Source provenance,
the small numeric-service patch, and the license are in `vendor/c-ares/`;
Linux SDKs install notices and provenance under `share/softline/licenses/c-ares/`.
Resolver sources are checked in; configuring/building never downloads c-ares.

Source archive smoke extracts and builds under the repository's `build/`,
independently of the caller's working directory or `TMPDIR`, and removes its
workspace on success, failure, or interruption.

`cmake/softline.exports` is the source-controlled dynamic export contract for
`libsoftline`. Build and extracted-package checks fail if a public symbol is
missing or an accidental symbol becomes linkable. The lifecycle migration record
is maintained in [docs/lifecycle-migration.md](docs/lifecycle-migration.md).

`v99.99.99` is permanently reserved for the release-version contract check and
is never a valid softline release tag. The Lua facade supports Lua 5.5 only;
LuaRocks builds must select Lua 5.5.

Installed CMake consumers should use the canonical imported target:

```cmake
find_package(softline REQUIRED CONFIG)
target_link_libraries(app PRIVATE softline::softline)
```

Plain C consumers can use the installed pkg-config metadata:

```sh
eval "$(./scripts/cpkt-toolchains.sh env x86_64-linux-gnu)"
"$CC" $(pkg-config --cflags softline) app.c $(pkg-config --libs softline)
```

Lua 5.5 consumers can use the Lua facade from the LuaRocks package:

```lua
local softline = require("softline")
local sl = softline.new()
local line = sl:readline("softline> ")
sl:close()
```

For local development:

```sh
make lua-test
softline_lua_env="$(make lua-env)" && eval "${softline_lua_env}"
lua examples/simple.lua
```

To run the Lua facade and examples against the in-tree debug library:

```sh
make lua-debug-test
make lua-debug-simple
make lua-debug-chat
make lua-debug-chat THEME=riced
```

For a fuller status and gap list, see `docs/softline-spec.md`.

## Lineage

softline no longer vendors linenoise as a separate source file, but its design
and some code are derived from linenoise. The inherited BSD 2-Clause notice is
included in `LICENSE`.
