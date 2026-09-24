# softline

softline is a C89 multiline prompt editor derived from [linenoise](https://github.com/antirez/linenoise).
It is meant for shells, chat prompts, REPLs, and other readline-like
interfaces where Enter submits and `Ctrl-J` inserts a newline.

The project is a handle-oriented editor core. It is usable for simple
multiline prompts and bounded bottom-prompt interfaces, but it is not a
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
- Long input wraps by words where possible and reflows after terminal resize.
- Bracketed paste is enabled while editing so pasted carriage returns become
  buffer content instead of submitting the prompt.
- Editing uses a plain silent line reader when either standard input or output
  is not a TTY and does not emit prompts; streamed output uses LF line endings
  instead of terminal CRLF.
- Keys such as TAB, Enter, function keys, and Alt-letter combinations can be
  bound per handle. A binding may handle the key, pass through to the built-in
  behavior, submit, cancel, interrupt, or mutate the active buffer.
  `SL_KEY_ALT_ENTER` represents the broadly supported Escape-plus-Return
  sequence. `SL_KEY_CTRL_ENTER` remains available when the terminal sends a
  distinguishable Ctrl-Enter sequence.
- Chat-like prompts can opt into a bounded FIFO prompt queue with public C and
  Lua inspection/mutation APIs. The default profile keeps Tab queueing and
  Alt-E edit-newest behavior, while the queued-turns profile binds
  Enter-to-queue and FIFO release to Softline's busy lifecycle. `next_prompt()`
  can auto-deliver FIFO work or the default profile can leave it local for
  explicit host delivery. The renderer ships
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
ANSI palette, updating both renderers' geometry on resize and dropping the
margin on very narrow terminals. Each submitted prompt becomes a Markdown
block quote in italics with blank lines around it, through a Markdown source
hook before libmdf. The hook passes prompt text through as Markdown source, so
libmdf interprets Markdown punctuation in the prompt. A worker emits one source
character every 20 ms; the owner-thread watch callback feeds libmdf incrementally and
forwards each sink fragment directly into a Softline output session. The
responses mix headings, subheadings, italic, bold, code, and paragraphs while
input remains editable.

While available, Enter dispatches a turn; while its operation is running,
Enter queues a follow-up in Softline. Alt-Enter
always dispatches a nonempty draft, and Alt-Enter on an empty editor promotes
the newest queued entry for immediate host delivery. Alt-E edits the newest
queued draft. On completion, queued FIFO work automatically dispatches. The
example uses the status spinner only as a presentation of its application-owned
busy state. Escape or Ctrl-C returns cancellation to the application; the C chat
example uses it to stop the active simulated operation, retain its queue, and keep the
chat open. Automatic FIFO release stays stopped until the user submits a new
turn or manually promotes a queued one.
The queue UI, status line, and simulated operation stream activate only when
both standard input and output are terminals; piped input or redirected output
produces plain libmdf-rendered text. The separate Lua chat example remains a
plain queued-turn demonstration; its facade exposes the same generic
output-session API for Lua applications composing an external renderer.

```sh
make run-simple
make run-chat
make run-chat-default
make run-chat-default-sr
make run-chat-accent-sr
make run-chat-dracula-sr
make run-chat-plain
make run-chat-plain-sr
make run-chat-riced
make run-chat-monogreen
make run-chat-monochrome
make run-chat-synthwave
```

The chat convenience targets build the C example before launching it.
`run-chat` uses Gruvbox by default; pass `THEME=...` to override it. Both C
and Lua examples accept
`SOFTLINE_PROMPT_THEME=default`, `plain`, `accent`, `dracula`, `gruvbox`, `monochrome`,
`monogreen`, `outrun`, `riced`, or `synthwave`; the generic Make targets also
expose that as `THEME=...`. The simple and chat examples default to `default`;
`make run-chat` supplies Gruvbox. The C live output session uses the main
terminal's scrollback for a full-width prompt; the `-sr` convenience targets
do not change that output layout. `SOFTLINE_LIVE_SCROLL_REGION=1` remains
available to the Lua chat example for finite `print_above()` calls.

## Bounded prompts

Set `screen_width` and `screen_height` in `sl_config_t`, or call
`sl_set_bounds()`, to anchor the prompt inside a terminal box. In bounded mode
the prompt grows upward as input wraps while `sl_print_above()` pulls streamed
chunks from a callback and writes them through the region above the prompt.
Full-width bounds can use terminal scrolling. Narrow or offset bounds use a
bounded cell viewport, not a VT scroll region that would alter outside columns.
A persistent `sl_output_stream_*()` session accepts later chunks without
waiting for EOF. Full-width sessions scroll the main terminal, preserving
native scrollback; narrow or offset sessions use the bounded viewport.
Use `sl_set_bounds(sl, 0, 0, 0, 0)`, or set `bounded = 1` with zero config
bounds, for a dynamic full-terminal bottom prompt that tracks terminal resize
in softline. Bounded rendering keeps a retained view of the visible editor
rows: ordinary edits patch only changed cells, structural changes redraw the
affected rows, and terminal geometry changes reflow the bounded box while
editing. During a bounded structural update or transcript dispatch,
softline hides the hardware cursor and restores it only at the final prompt
position, preventing visible cursor travel across the prompt area.

For a normal scrollback prompt, streamed output uses the compatible
clear-and-redraw path by default. Set `live_scroll_region = 1` in
`sl_config_t`, or call `sl_set_live_scroll_region(sl, 1)`, to opt into a
temporary full-width scroll region once the active prompt reaches the bottom
row. That avoids repainting the live prompt while output streams. Queue
previews, status lines, wrapping, and resize reflow change that region with the
prompt. Softline resets the region whenever the edit finishes; terminals that
do not answer the cursor-position report continue with clear-and-redraw.

For a persistent bottom prompt, use bounded mode in either the normal or
alternate screen. Softline never enters or leaves the alternate screen itself.
The live viewport retains only visible terminal cells and partial parser state.
Full-width main-screen sessions scroll those cells into native terminal
scrollback. Softline does not keep its own transcript history.

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

Each write is visible before it returns, including while `next_prompt()` is
active. Chunk boundaries add no content or document semantics; ANSI SGR and
UTF-8 sequences may cross calls. End rejects an incomplete sequence. The
composer updates Softline geometry (`set_bounds` or `set_screen_width`) and
renderer width on the owner thread when the terminal changes; neither library
owns the other's margins. Softline immediately reconciles the transcript and
editable prompt within its new bounds. A watched FD is the usual way to
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
standard-colour status elements, and red/green busy markers. `plain` is
uncoloured; `accent`, Dracula, Gruvbox, monochrome, monogreen, Outrun, Riced,
and Synthwave use their embedded palettes. The selected theme applies to every
interactive prompt, including normal readline prompts, status lines, and queue
panels. Prompt markers reset before typed text; monochrome and monogreen
additionally colour typed text as defined by their palettes.

### Queue control API

The queue remains renderer-owned, but an embedding application can inspect and
mutate it without synthetic terminal input or a parallel queue. Queue indexes
are oldest-first and returned strings are released with `sl_free_string()`.

```c
sl->set_prompt_queue(sl, 1, 64, 3);
sl->set_prompt_queue_profile(sl, SL_PROMPT_QUEUE_PROFILE_QUEUED_TURNS);

/* An operation starts: Enter now queues nonempty drafts. */
sl->set_status_busy(sl, 1);

/* An owner-thread completion callback returns to idle. If the editor is
 * empty, the active next_prompt() immediately receives one oldest queued
 * turn; starting that turn sets busy again, so later entries remain queued. */
sl->set_status_busy(sl, 0);
```

`SL_PROMPT_QUEUE_PROFILE_DEFAULT` preserves Tab, Alt-E, and automatic FIFO
delivery by default. `SL_PROMPT_QUEUE_PROFILE_QUEUED_TURNS` is the native
turn lifecycle preset: while `set_status_busy(sl, 1)` is active, Enter queues
a nonempty draft; when it returns idle, Softline delivers exactly one oldest
queued turn. Starting that turn sets busy again, so the remaining FIFO entries
stay visibly queued until its completion. In idle mode, Enter submits normally
and an empty Enter is ignored. Alt-Enter always submits a nonempty draft; on
an empty draft it promotes the newest queued entry regardless of busy state.
Both are returned to the application immediately, even while busy: a nonempty
draft has `SL_PROMPT_SOURCE_DIRECT` and a promotion has
`SL_PROMPT_SOURCE_PROMOTED`. Use `sl_set_prompt_queue_delivery()` only with
the default profile for an explicit host-controlled delivery policy. Explicit
`sl_bind_key()` bindings always take precedence over these built-ins.
Cancelling a queued-turns editor keeps queued drafts visible but stops automatic
FIFO release; a subsequent direct submission or manual promotion resumes it.

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
    "gpt-5.6-terra high", "ctx 36%", "~/g/softline", "feat/prompt-queue"};

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

Enabling the status line reserves a single row immediately above it for a
status message. `sl_set_status_message()` accepts printable single-line UTF-8,
clips long text to that row, and redraws immediately while the editor is
active. The row uses italic text and the selected theme's faded colour. Clearing
the message leaves the row blank, so the status line and editor stay in place.

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
make deps DEPENDENCY=libmdf
make deps DEPENDENCY=lua PRESET=debug-lua
make asan
make valgrind
make package-consumer-smoke
make lua-test
make prerelease
```

Every project-owned C target is compiled as C89 with POSIX terminal APIs. Shared builds use
the separate CMake `SOFTLINE_ABI_VERSION`, currently `1`, for SONAME/SOVERSION.
That ABI version is bumped only for shared-library ABI breaks, not for every
project release-version bump. The v0.3.0 receiver-shell architecture is
withdrawn as an architectural miss and is not a supported shared-library
upgrade baseline; the current event-driven architecture replaces it while
retaining ABI version `1`.

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
