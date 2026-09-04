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
config table mirrors `sl_config_t`:

- `input_fd`
- `output_fd`
- `screen_x`
- `screen_y`
- `screen_width`
- `screen_height`
- `bounded`
- `live_scroll_region`
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
  cancellation, interrupt, or error.
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
- `sl:set_bounds(x, y, width, height)` enables bounded prompt rendering; zero
  width or height uses dynamic terminal bounds.
- `sl:set_screen_width(width)` sets normal prompt wrapping width; `0` returns
  to terminal-width probing.
- `sl:set_live_scroll_region(enabled)` opts an unbounded prompt into
  bottom-pinned scroll-region output after it reaches the terminal bottom.
  It is disabled by default.
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
- `sl:set_queue_delivery("auto" | "manual")` selects automatic FIFO delivery
  or host-controlled retention for the `default` profile;
  `sl:queue_delivery()` returns that mode.
- `sl:set_queue_profile("default" | "queued_turns")` selects the built-in
  keymap and queue policy. `queued_turns` maps Enter to enqueue while
  `sl:set_status_busy(true)` is active, Alt-E to edit-newest, and Alt-Enter
  to submit a nonempty draft or promote the newest queued entry when empty.
  `sl:set_status_busy(false)` releases exactly one oldest queued turn; starting
  that turn should set busy again, leaving later turns queued. `sl:queue_profile()`
  returns the selected profile.
  Alt-Enter returns a nonempty draft immediately with `PROMPT_SOURCE_DIRECT`,
  even while busy; an empty-editor promotion returns `PROMPT_SOURCE_PROMOTED`.
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
- `sl:set_status_elements(elements)` replaces all status elements. At most 32
  are retained; longer input uses the first 31 followed by `...`. The limit is
  exported as `STATUS_MAX_ELEMENTS`. Elements must be valid UTF-8 and cannot
  contain C0/C1 controls or DEL.
- `sl:set_status_element(index, value)` updates one zero-based element; pass
  `nil` as `value` to clear it.
- `sl:set_status_busy(busy)` selects the red busy `x` or spinner marker. With
  the `queued_turns` profile and queueing enabled, it is also the native turn
  lifecycle signal: busy retains turns; idle releases one oldest queued turn.
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
- `sl:print_above(source)` prints above the active prompt. Bounded prompts use
  their output region; normal prompts clear and redraw by default, or use an
  enabled live scroll region after reaching the terminal bottom. `source` may
  be a string, an array-like table of string chunks, or a function that
  receives a 1-based chunk index and returns the next string or `nil`.
- `sl:last_readline_status()` returns the last readline status code.
- `sl:last_error()` returns the last handle-owned diagnostic string, or `nil`.
- `sl:close()` destroys the handle.

Fallible methods other than `readline()` return `true` on success or
`nil, status` on failure. Status constants exported by the module are:

- `softline.READLINE_NONE`
- `softline.READLINE_SUBMITTED`
- `softline.READLINE_EOF`
- `softline.READLINE_CANCELLED`
- `softline.READLINE_INTERRUPTED`
- `softline.READLINE_ERROR`
- `softline.OK`
- `softline.PROMPT_SOURCE_NONE`
- `softline.PROMPT_SOURCE_DIRECT`
- `softline.PROMPT_SOURCE_QUEUED`
- `softline.PROMPT_SOURCE_PROMOTED`
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
- `softline.STATUS_MAX_ELEMENTS`
- `softline.KEY_CTRL_C`
- `softline.KEY_ESCAPE`
- `softline.KEY_TAB`
- `softline.KEY_ENTER`
- `softline.KEY_CTRL_ENTER`
- `softline.KEY_ALT_ENTER`
- `softline.KEY_CTRL_N`
- `softline.KEY_CTRL_P`
- `softline.KEY_UP`
- `softline.KEY_DOWN`
- `softline.KEY_ALT_E`
- `softline.KEY_ACTION_PASS`
- `softline.KEY_ACTION_HANDLED`
- `softline.KEY_ACTION_SUBMIT`
- `softline.KEY_ACTION_CANCEL`
- `softline.KEY_ACTION_INTERRUPT`

## Examples

The repository ships Lua examples equivalent to the C examples:

```sh
make lua-test
eval "$(make lua-env)"
lua examples/simple.lua
lua examples/chat.lua
```

`examples/chat.lua` accepts the same `SOFTLINE_PROMPT_THEME` values as the C
chat example. Set `SOFTLINE_LIVE_SCROLL_REGION=1` to demonstrate the optional
unbounded bottom-pinned scroll-region mode.

To run against the in-tree debug `libsoftline` instead of the installed local
SDK:

```sh
make lua-debug-test
make lua-debug-simple
make lua-debug-chat
```
