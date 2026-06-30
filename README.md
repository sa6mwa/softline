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
- Non-tty input uses a plain silent line reader and does not emit prompts.
- Keys such as TAB, Enter, function keys, and Alt-letter combinations can be
  bound per handle. A binding may handle the key, pass through to the built-in
  behavior, submit, cancel, interrupt, or mutate the active buffer.
  `SL_KEY_CTRL_ENTER` is available when the terminal sends a distinguishable
  Ctrl-Enter sequence.
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
the terminal, and prints output through the region above the prompt.

```sh
cmake --preset debug
cmake --build --preset debug
build/debug/examples/example_simple
build/debug/examples/example_chat
```

## Bounded prompts

Set `screen_width` and `screen_height` in `sl_config_t`, or call
`sl_set_bounds()`, to anchor the prompt inside a terminal box. In bounded mode
the prompt grows upward as input wraps while `sl_print_above()` pulls streamed
chunks from a callback and writes them through the region above the prompt.
Use `sl_set_bounds(sl, 0, 0, 0, 0)`, or set `bounded = 1` with zero config
bounds, for a dynamic full-terminal bottom prompt that tracks terminal resize
in softline.

For a persistent bottom prompt, use this bounded mode as a full-screen terminal
UI on the alternate screen. That keeps the main scrollback intact and lets
softline manage the prompt box and transcript scroll region coherently.

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
make package-consumer-smoke
make lua-test
make release-matrix
```

The core library is compiled as C89 with POSIX terminal APIs. Shared builds use
the separate CMake `SOFTLINE_ABI_VERSION`, currently `0`, for SONAME/SOVERSION.
That ABI version is bumped only for shared-library ABI breaks, not for every
project release-version bump.

`make release-matrix` builds the standard Linux GNU/musl target matrix,
generates source and Lua release artifacts, writes checksums, and verifies
package layout, runtime loader metadata, and release privacy. The optional
Darwin target is part of the configured matrix and is packaged only when a
working osxcross toolchain is available; packaged Darwin artifacts require
target-correct Mach-O inspection.

Installed CMake consumers should use the canonical imported target:

```cmake
find_package(softline REQUIRED CONFIG)
target_link_libraries(app PRIVATE softline::softline)
```

Plain C consumers can use the installed pkg-config metadata:

```sh
cc $(pkg-config --cflags softline) app.c $(pkg-config --libs softline)
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
```

For a fuller status and gap list, see `docs/softline-spec.md`.

## Lineage

softline no longer vendors linenoise as a separate source file, but its design
and some code are derived from linenoise. The inherited BSD 2-Clause notice is
included in `LICENSE`.
