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
- `Ctrl-U`, `Ctrl-K`, word deletion, movement, history recall, and delete keys
  operate across the whole multiline buffer.
- `Ctrl-R` starts reverse incremental history search over the handle's current
  in-memory history, including entries loaded before `readline()`.
- Long input wraps by words where possible and reflows after terminal resize.
- Bracketed paste is enabled while editing so pasted carriage returns become
  buffer content instead of submitting the prompt.
- Non-tty input uses a plain silent line reader and does not emit prompts;
  streamed output uses LF line endings instead of terminal CRLF.
- Keys such as TAB, Enter, function keys, and Alt-letter combinations can be
  bound per handle. A binding may handle the key, pass through to the built-in
  behavior, submit, cancel, interrupt, or mutate the active buffer.
  `SL_KEY_CTRL_ENTER` is available when the terminal sends a distinguishable
  Ctrl-Enter sequence.
- Chat-like prompts can opt into a FIFO prompt queue. Tab queues a nonempty
  draft, Alt-E recalls the newest queued draft for editing, and
  `next_prompt()` returns queued work before opening a direct editor while
  identifying whether the result was queued or direct. The renderer ships
  plain, accent, and riced prompt themes.
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
- nonblocking/event-loop session API
- Windows console backend
- Readline-compatible headers, globals, `.inputrc`, or ABI

## Examples

`example_simple` is the normal terminal prompt. Output is printed after each
submitted line and the next prompt proceeds below it like an ordinary REPL.

`example_chat` enters the alternate screen, keeps the prompt at the bottom of
the terminal, and prints output through the region above the prompt. It replies
to dispatched work as `[direct|queued] I read back: <prompt>` and emits a
random simulated peer message every two seconds while the editor is active.
Ctrl-C cancels the active editor and keeps the chat open. The alternate-screen,
queue UI, and simulated peer activate only when both standard input and standard
output are terminals, so piped use remains plain line-oriented input/output.

```sh
make run-simple
make run-chat
make run-chat THEME=riced
```

`run-simple` and `run-chat` build the C examples before launching them. Both C
and Lua examples accept `SOFTLINE_PROMPT_THEME=plain`, `accent`, or `riced`;
the Make targets expose that as `THEME=...`. The simple examples default to
`plain`; chat examples default to `accent`.

## Bounded prompts

Set `screen_width` and `screen_height` in `sl_config_t`, or call
`sl_set_bounds()`, to anchor the prompt inside a terminal box. In bounded mode
the prompt grows upward as input wraps while `sl_print_above()` pulls streamed
chunks from a callback and writes them through the region above the prompt.
Use `sl_set_bounds(sl, 0, 0, 0, 0)`, or set `bounded = 1` with zero config
bounds, for a dynamic full-terminal bottom prompt that tracks terminal resize
in softline. Bounded rendering keeps a retained view of the visible editor
rows: ordinary edits patch only changed cells, structural changes redraw the
affected rows, and `SIGWINCH` triggers a full reflow of the bounded box before
input resumes. During a bounded structural update or transcript dispatch,
softline hides the hardware cursor and restores it only at the final prompt
position, preventing visible cursor travel across the prompt area.

For a persistent bottom prompt, use this bounded mode as a full-screen terminal
UI on the alternate screen. That keeps the main scrollback intact and lets
softline manage the prompt box and transcript scroll region coherently.
Softline never enters or leaves the alternate screen itself: the embedding
application chooses normal scrollback or an alternate-screen UI.

## Prompt queueing

Prompt queueing is available for interactive chat-like prompts, including
normal scrollback terminals. It does not alter non-TTY input. Enable it in the
handle configuration or after construction, and read application work through
`next_prompt()`:

```c
sl_prompt_source_t source;

sl->set_prompt_queue(sl, 1, 64, 3);
sl->set_prompt_theme(sl, SL_PROMPT_THEME_ACCENT);

for (;;) {
  char *line = sl->next_prompt(sl, "chat> ", &source);
  if (!line)
    break;
  /* source is SL_PROMPT_SOURCE_DIRECT or SL_PROMPT_SOURCE_QUEUED. */
  sl->free_string(sl, line);
}
```

Tab queues a nonempty active editor and leaves a FIFO preview panel above the
current input; Tab on an empty editor is a no-op. The panel shows a total count
and a bounded number of oldest-first previews. Alt-E removes the newest queued
entry and restores it to the editor. Explicit key bindings continue to override
these defaults. Prompt appearance is renderer-owned so it remains safe with
layout: `plain` is uncoloured, `accent` uses cyan, and `riced` uses vivid
magenta styling. The selected theme applies to every interactive prompt,
including normal readline prompts and bounded queue panels.

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
make asan
make valgrind
make package-consumer-smoke
make lua-test
make prerelease
```

The core library is compiled as C89 with POSIX terminal APIs. Shared builds use
the separate CMake `SOFTLINE_ABI_VERSION`, currently `0`, for SONAME/SOVERSION.
That ABI version is bumped only for shared-library ABI breaks, not for every
project release-version bump.

On supported Linux development hosts, ordinary debug, sanitizer, Valgrind,
package-consumer, and release package builds use the pinned native GNU Bootlin
toolchain. The current pinned Bootlin compiler executables are x86_64-hosted, so
native lifecycle builds are selected automatically only on x86_64 Linux hosts.
The release matrix still ships the supported Linux target artifacts
(`x86_64`, `aarch64`, and `armhf`, each GNU and musl) from those pinned
toolchains. Fuzzing remains native `x86_64-linux-gnu` only. Unsupported native
architectures and non-Linux hosts may fall back to the host compiler for local
development presets, but explicit Linux package/release targets fail closed
instead of silently using host tools.
Host `clangd`, `clang-format`, Valgrind, Lua 5.5, LuaRocks, and packaging
utilities are development tools; the C compiler, linker, archiver, and sysroot
for project-owned Linux release builds come from the lifecycle toolchain
resolver.

`make prerelease` is the deterministic local gate: formatting, debug and
sanitizer tests, native Valgrind, Lua, toolchain, editor, header, and
install-tree consumer checks. `make release-matrix` builds the standard Linux GNU/musl target matrix,
generates source and Lua release artifacts, writes checksums, and verifies
package layout, runtime loader metadata, and release privacy. The rehearsal
matrix may skip Darwin when osxcross is unavailable. `make release` is stricter:
it requires the Darwin toolchain and a verified Darwin artifact; packaged Darwin
artifacts require target-correct Mach-O inspection.

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
eval "$(make lua-env)"
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
