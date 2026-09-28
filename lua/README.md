# softline Lua facade

The `softline` Lua module is a thin facade over the installed C library. It
links with `libsoftline` and uses only the public `softline/softline.h` API.
Lua 5.5 is the supported Lua runtime for this facade.

```lua
local softline = require("softline")

local sl = softline.new()
local line, status = sl:readline("softline> ")
if line then
  sl:history_add(line)
else
  if status == softline.READLINE_EOF then
    -- input ended
  end
end
sl:close()
```

## Handles

`softline.new([config])` returns an independent editor handle. The optional
config table mirrors `sl_config_t`. Unknown fields are rejected, including
removed boxed-layout options (`bounded`, `screen_x`, `screen_y`, and
`screen_height`). Invalid config names or values raise a Lua error. Supported
fields are:

- `input_fd`
- `output_fd`
- `screen_width`
- `live_scroll_region`
- `clear_prompt_on_exit` (default `false`: clear input rows, keep queue and
  status rows, and return at column zero on the current input row; `true` clears
  the whole prompt area and returns below the transcript)
- `prompt_queue`
- `prompt_queue_max_entries`
- `prompt_queue_preview_entries`
- `prompt_theme`
- `statusline`
- `statusline_start_element`
- `status_spinner`
- `status_busy`
- `status_idle_marker`
- `history_max_len`
- `line_max_len`

Each handle owns its buffer, cursor, history, prompt state, and diagnostics.
Call `sl:close()` when done; the Lua finalizer also closes an unclosed handle.
Queueing and live scroll regions affect only editing with both input and output
attached to TTYs; mixed or piped streams remain plain line readers. Defaults
match `sl_config_init()`: queueing,
live scroll regions, status lines, and spinners are off; the theme is
`PROMPT_THEME_DEFAULT`; and the idle status marker is `+`.

## Methods

- `sl:readline([prompt])` returns a submitted string, or `nil, status` for EOF,
  cancellation, interrupt, or error. Ctrl-C restores terminal state before
  raising SIGINT; if a handler returns, an existing native output session
  remains available.
- `sl:next_prompt([prompt])` returns `line, source`. Automatic queue delivery
  dispatches FIFO entries before opening an editor; manual delivery retains
  them for explicit take or Alt-Enter promotion. `source` is
  `PROMPT_SOURCE_QUEUED`, `PROMPT_SOURCE_DIRECT`, or
  `PROMPT_SOURCE_PROMOTED`. On interactive handles it retains terminal input
  ownership between results; `close()` restores the terminal. On no result it
  returns `nil, status`.
- `sl:history_add(line)` adds one history entry.
- `sl:history_set_max_len(max_len)` changes the retained history cap; `0`
  clears and disables history.
- `sl:history_save(filename)` writes history with owner-only permissions.
- `sl:history_load(filename)` loads history entries into the handle.
- `sl:set_screen_width(width)` sets ordinary readline wrapping width; `0`
  returns to terminal-width probing. Native chat uses physical terminal width.
  Without an active readline, this hint does not move or resize native output.
- `sl:set_live_scroll_region(enabled)` opts an ordinary readline prompt into
  bottom-pinned scroll-region output after it reaches the terminal bottom.
  It is disabled by default; native chat always uses its own scroll region.
- `sl:set_idle_callback(callback)` registers a no-argument Lua callback that
  runs while an interactive editor is idle; pass `nil` to clear it. The
  callback may use methods such as `print_above`, `insert`, `submit`, or
  `cancel`. Closing that handle from its own idle callback is rejected. A Lua
  callback error ends the active `readline()` or `next_prompt()` with
  `READLINE_ERROR`; `sl:last_error()` returns the captured Lua error text.
- `sl:watch_add(fd_or_file, events, callback)` registers an integer descriptor
  or open Lua file handle for owner-thread wakeups while editing. The callback
  receives `id, fd, events`; it must drain bounded application work. Use
  `sl:watch_modify(id, events)`, `sl:watch_remove(id)`, or
  `sl:watch_clear()` to manage registrations. Watches require interactive TTY
  handles and callbacks may not recursively enter `readline` or `next_prompt`.
  Softline retains a supplied file handle until its watch is removed, cleared,
  or the Softline handle closes; do not close that file while registered.
  Ready watches are visited round-robin, with at most eight callbacks before
  terminal input is handled again.
- `sl:set_prompt_queue(enabled, max_entries, preview_entries)` enables the
  chat queue. The default profile maps Tab to queue a nonempty editor and
  Alt-E to edit the newest queued entry. Disabling the queue clears it;
  reducing `max_entries` below the current queue length fails.
- `sl:queue_count()` and `sl:queue_capacity()` return the queue state.
- `sl:queue_peek(index)`, `sl:queue_insert(index, text)`,
  `sl:queue_append(text)`, `sl:queue_replace(index, text)`, and
  `sl:queue_take(index)` inspect or mutate oldest-first queue entries. Lua
  indexes are one-based; `peek` and `take` return ordinary Lua strings.
- `sl:queue_clear()` clears queued entries without changing the active draft;
  `sl:queue_draft()` atomically queues an active nonempty draft.
- `sl:queue_mode(index)` returns `softline.QUEUE_MODE_QUEUED` or
  `softline.QUEUE_MODE_STEER`; `sl:queue_set_mode(index, mode)` changes an
  entry's delivery intent. The host chooses which steer to take at a safe seam.
- `sl:set_queue_delivery("auto" | "manual")` selects automatic FIFO delivery
  or host-controlled retention. Busy `queued_turns` retains entries in either
  mode; manual mode lets the host choose both steer and ordinary turns;
  `sl:queue_delivery()` returns that mode.
- `sl:set_queue_profile("default" | "queued_turns")` selects the built-in
  keymap and queue policy. `queued_turns` maps Enter to enqueue while
  `sl:set_status_busy(true)` is active, Alt-E to edit-newest, and Alt-Enter
  to queue a steer draft while busy or mark the newest queued entry as steer
  when the editor is empty.
  With automatic delivery, `sl:set_status_busy(false)` releases one oldest
  ordinary queued turn; manual delivery leaves it for the host to take.
  `sl:queue_profile()`
  returns the selected profile.
  While idle, Alt-Enter submits a nonempty draft immediately with
  `PROMPT_SOURCE_DIRECT`, or promotes the newest queued entry with
  `PROMPT_SOURCE_PROMOTED`.
  Cancellation retains queued drafts and stops automatic FIFO release until a
  direct submission or manual promotion resumes it.
- `sl:queue_keys()` returns a table with `enqueue_draft`, `edit_newest`, and
  `submit_or_promote_newest`; pass a table with those fields to
  `sl:set_queue_keys(keys)` to override built-in actions. Use `nil` for a
  disabled action. Explicit `sl:bind_key()` callbacks still take precedence.
- `sl:set_prompt_theme(theme)` selects one of `PROMPT_THEME_DEFAULT`, `PROMPT_THEME_PLAIN`,
  `PROMPT_THEME_ACCENT`, `PROMPT_THEME_DRACULA`, `PROMPT_THEME_GRUVBOX`,
  `PROMPT_THEME_MONOCHROME`, `PROMPT_THEME_MONOGREEN`, `PROMPT_THEME_OUTRUN`,
  `PROMPT_THEME_RICED`, or `PROMPT_THEME_SYNTHWAVE` for the whole interactive
  prompt UI, including status lines and queue panels.
- `sl:set_statusline(enabled, starting_element)` enables the optional status
  line and chooses the palette slot for its first element. Themes provide
  eight element colours, and subsequent elements cycle through those colours.
- `sl:set_status_message(text)` sets or updates the status area above the
  status line. It displays `! ` in the theme's muted colour followed by italic
  text in the theme's secondary colour. Pass `nil` or `""` to clear it.
  Long text wraps at words, with continuation rows indented to the prefix
  width. The message may be changed while an output session is streaming.
- `sl:set_status_message_prefix(prefix)` changes `! `; pass `""` to hide the
  prefix or `nil` to restore it.
- `sl:set_status_message_colors(prefix_color, text_color)` selects independent
  theme roles, such as `softline.THEME_COLOR_MUTED` and
  `softline.THEME_COLOR_ELEMENT_2`.
- `sl:set_status_elements(elements)` replaces all status elements. At most 32
  are retained; longer input uses the first 31 followed by `...`. The limit is
  exported as `STATUS_MAX_ELEMENTS`. Elements must be valid UTF-8 and cannot
  contain C0/C1 controls or DEL.
- `sl:set_status_element(index, value)` updates one zero-based element; pass
  `nil` as `value` to clear it.
- `sl:set_status_busy(busy)` selects the red busy `x` or spinner marker. With
  the `queued_turns` profile and queueing enabled, it is also the native turn
  lifecycle signal: busy retains turns; idle releases one oldest ordinary turn
  only when automatic queue delivery is selected.
- `sl:set_status_spinner(enabled)` enables the 500ms `/ - \\ |` busy spinner.
- `sl:set_status_idle_marker(marker)` selects a one-byte printable ASCII green
  idle marker; it defaults to `+`. Pass `nil` to leave the reserved two-column
  idle slot blank.
- `sl:insert(text)` inserts text bytes at the active cursor.
- `sl:set_buffer(text)` replaces the active buffer and moves the cursor to the
  end.
- `sl:buffer()` returns the active buffer as a Lua string.
- `sl:cursor()` returns the cursor as a byte offset into `sl:buffer()`.
- `sl:set_cursor(offset)` clamps the byte offset to a valid UTF-8 cluster
  boundary when possible.
- `sl:submit()` submits the active buffer from a callback-driven edit.
- `sl:cancel()` cancels the active `readline()`.
- `sl:bind_key(key, callback)` binds a decoded key to a callback. The callback
  receives the key code and returns a `softline.KEY_ACTION_*` value, or `nil`
  to mark the key handled. Passing `nil` as the callback removes the binding.
- `sl:print_above(source)` prints above the active prompt. Ordinary prompts clear
  and redraw by default, or use an
  enabled live scroll region after reaching the terminal bottom. `source` may
  be a string, an array-like table of string chunks, or a function that
  receives a 1-based chunk index and returns the next string or `nil`.
  Native chat reconciles physical resize before each chunk, including resize
  while a source function is running.
  Failed native finite output discards partial ANSI/UTF-8 bytes so the next
  call starts cleanly.
- `sl:output_stream_begin()` opens one persistent output session above the
  prompt. Transcript output starts at the existing terminal cursor; the editable
  prompt is anchored at the bottom from its first frame. Producer bytes pass
  through unchanged. Before the first frame, output uses the full terminal and
  leaves its cursor in place between writes, preserving native autowrap and
  Unicode clusters. Cursor reports occur at ownership and resize boundaries,
  rather than per feed. Transcript reflow belongs to the terminal. The output
  margin follows the actual prompt height, including visible queue entries,
  nonempty status messages, status lines, and editor rows. Unused preview slots
  occupy no space. At physical capacity the editor pages, leaving at least two
  output rows. Native chat needs at least three terminal rows, uses physical
  terminal width, and supports no rectangular viewport.
  An unfinished line clipped into scrollback continues at the first visible
  output row without replay. If the clipped position was at a hard line
  boundary, new output starts next to the prompt. Existing scrollback cells
  and spacing are preserved.
  The application owns its renderer, wakeup, and
  response/document lifecycle; Softline has no Markdown dependency.
- `sl:output_stream_write(bytes)` sends a Lua byte string immediately into the
  open session. Calls from a watch callback can alternate with prompt typing.
  Chunk boundaries add no newline or response separator. The stream accepts
  printable UTF-8, LF/CR/Tab, and ANSI SGR styling; malformed or unsupported
  terminal controls return `nil, status`. An empty string succeeds without
  changing the screen. No full response is buffered. Native LF positioning
  follows the output TTY's OPOST/ONLCR settings without changing those settings
  or producer bytes.
- `sl:output_stream_write_quoted_prompt(text)` writes submitted text between
  renderer segments as a wrapped, themed quote, repeating the prefix on every
  visible row and keeping one empty row on each side. The text is literal, so
  Markdown punctuation remains visible. `sl:set_quoted_prompt_prefix(prefix)`
  changes the default `> ` prefix; `nil` restores it.
  `sl:set_quoted_prompt_style({prefix={r,g,b}, text={r,g,b}})` overrides the
  theme colours; `nil` restores theme defaults. The prefix stays faded and the
  text stays italic.
- `sl:output_stream_end()` ends the producer session without finishing an
  external renderer document. Inside an active editor callback it retains the
  prompt and scroll region for later finite output or another stream. Otherwise
  native chat closes using `clear_prompt_on_exit`: by default it clears only
  input rows, keeps queue/status rows, and leaves the cursor at column zero on
  the current input row without a newline or scroll. With no rendered prompt
  it returns below output. Non-TTY output adds no teardown controls. If the
  last write left an ANSI or UTF-8 sequence incomplete, it returns
  `nil, status` and keeps the session
  open so the missing bytes can be supplied. Only one session may be open per
  editor. Output-session and quoted-prompt methods run on the Lua/editor owner
  thread; foreign producers should notify a watched descriptor instead of
  calling Lua.
- Physical resize redraws the prompt without replaying transcript output.
  The application updates its external renderer width separately. Softline
  does not cache or repaint the transcript.
- `sl:last_readline_status()` returns the last readline status code.
- `sl:last_error()` returns the last handle-owned diagnostic string, or `nil`.
- `sl:close()` destroys the handle and restores terminal state. Native chat
  cleanup uses the same `clear_prompt_on_exit` policy as `output_stream_end()`.

Fallible setter and operation methods return `true` on success or
`nil, status` on failure; getters and prompt reads return their documented
values. The module exports the C status, prompt-source,
queue-mode, theme, theme-colour, watch-event, and key constants. Queue delivery
and profile use the documented Lua strings instead of C enum integers.

`KEY_NONE` disables a configurable queue action. For Alt-letter bindings
without a named constant, add the letter byte to `softline.KEY_ALT_BASE`.

- `softline.READLINE_NONE`
- `softline.READLINE_SUBMITTED`
- `softline.READLINE_EOF`
- `softline.READLINE_CANCELLED`
- `softline.READLINE_INTERRUPTED`
- `softline.READLINE_ERROR`
- `softline.OK`
- `softline.ERROR`
- `softline.ERROR_INVALID`
- `softline.ERROR_NOMEM`
- `softline.ERROR_IO`
- `softline.PROMPT_SOURCE_NONE`
- `softline.PROMPT_SOURCE_DIRECT`
- `softline.PROMPT_SOURCE_QUEUED`
- `softline.PROMPT_SOURCE_PROMOTED`
- `softline.QUEUE_MODE_QUEUED`
- `softline.QUEUE_MODE_STEER`
- `softline.ERROR_FULL`
- `softline.WATCH_READ`
- `softline.WATCH_WRITE`
- `softline.WATCH_ERROR`
- `softline.WATCH_HANGUP`
- `softline.PROMPT_THEME_PLAIN`
- `softline.PROMPT_THEME_ACCENT`
- `softline.PROMPT_THEME_DRACULA`
- `softline.PROMPT_THEME_GRUVBOX`
- `softline.PROMPT_THEME_MONOCHROME`
- `softline.PROMPT_THEME_MONOGREEN`
- `softline.PROMPT_THEME_OUTRUN`
- `softline.PROMPT_THEME_RICED`
- `softline.PROMPT_THEME_SYNTHWAVE`
- `softline.PROMPT_THEME_DEFAULT`
- `softline.THEME_COLOR_MUTED`
- `softline.THEME_COLOR_SECONDARY`
- `softline.THEME_COLOR_PROMPT`
- `softline.THEME_COLOR_QUEUE`
- `softline.THEME_COLOR_INPUT`
- `softline.THEME_COLOR_ELEMENT_0`
- `softline.THEME_COLOR_ELEMENT_1`
- `softline.THEME_COLOR_ELEMENT_2`
- `softline.THEME_COLOR_ELEMENT_3`
- `softline.THEME_COLOR_ELEMENT_4`
- `softline.THEME_COLOR_ELEMENT_5`
- `softline.THEME_COLOR_ELEMENT_6`
- `softline.THEME_COLOR_ELEMENT_7`
- `softline.STATUS_MAX_ELEMENTS`
- `softline.KEY_NONE`
- `softline.KEY_CTRL_A`
- `softline.KEY_CTRL_B`
- `softline.KEY_CTRL_C`
- `softline.KEY_CTRL_D`
- `softline.KEY_CTRL_E`
- `softline.KEY_CTRL_F`
- `softline.KEY_CTRL_J`
- `softline.KEY_CTRL_K`
- `softline.KEY_CTRL_R`
- `softline.KEY_CTRL_U`
- `softline.KEY_CTRL_W`
- `softline.KEY_ESCAPE`
- `softline.KEY_BACKSPACE`
- `softline.KEY_TAB`
- `softline.KEY_ENTER`
- `softline.KEY_CTRL_ENTER`
- `softline.KEY_ALT_ENTER`
- `softline.KEY_CTRL_N`
- `softline.KEY_CTRL_P`
- `softline.KEY_UP`
- `softline.KEY_DOWN`
- `softline.KEY_LEFT`
- `softline.KEY_RIGHT`
- `softline.KEY_HOME`
- `softline.KEY_END`
- `softline.KEY_DELETE`
- `softline.KEY_ALT_B`
- `softline.KEY_ALT_F`
- `softline.KEY_UNKNOWN`
- `softline.KEY_PASTE_BEGIN`
- `softline.KEY_PASTE_END`
- `softline.KEY_F1`
- `softline.KEY_F2`
- `softline.KEY_F3`
- `softline.KEY_F4`
- `softline.KEY_F5`
- `softline.KEY_F6`
- `softline.KEY_F7`
- `softline.KEY_F8`
- `softline.KEY_F9`
- `softline.KEY_F10`
- `softline.KEY_ALT_E`
- `softline.KEY_ALT_BASE`
- `softline.KEY_ALT_M`
- `softline.KEY_ACTION_PASS`
- `softline.KEY_ACTION_HANDLED`
- `softline.KEY_ACTION_SUBMIT`
- `softline.KEY_ACTION_CANCEL`
- `softline.KEY_ACTION_INTERRUPT`

## Examples

The repository ships Lua examples for simple prompts and queued chat turns.
The Lua chat example demonstrates queue delivery without libmdf:

```sh
make lua-test
softline_lua_env="$(make lua-env)" && eval "${softline_lua_env}"
lua examples/simple.lua
lua examples/chat.lua
```

`examples/chat.lua` accepts the same `SOFTLINE_PROMPT_THEME` values as the C
chat example. It uses the default native persistent output session: transcript
output starts at the original cursor, while the prompt stays at the bottom.
Output feeds preserve unchanged prompt cells; prompt updates patch only cells
that differ from the previous frame.

To run against the in-tree debug `libsoftline` instead of the installed local
SDK:

```sh
make lua-debug-test
make lua-debug-simple
make lua-debug-chat
```
