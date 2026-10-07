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
- `disable_image_paste` (default `false`; disables built-in Ctrl-V image capture)
- `image_paste_path_template` (default `nil`; XDG cache path)
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
  Ctrl-R searches history. Enter accepts the displayed match into the normal
  editable prompt without submitting or queueing it; a subsequent Enter follows
  the normal keymap. Escape or Ctrl-G restores the original draft.
- While terminal raw mode is owned, kernel tab expansion (`TAB3`/`OXTABS`) is
  disabled so the terminal handles tabs. OPOST/ONLCR and other output flags are
  preserved; original flags are restored on release.
- `sl:next_prompt([prompt])` returns `line, source`. Automatic queue delivery
  dispatches FIFO entries before opening an editor; manual delivery retains
  them for explicit take or Alt-Enter promotion. `source` is
  `PROMPT_SOURCE_QUEUED`, `PROMPT_SOURCE_DIRECT`, or
  `PROMPT_SOURCE_PROMOTED`. On interactive handles it retains terminal input
  ownership between results; `close()` restores the terminal. On no result it
  returns `nil, status`.
- `sl:history_add(line)` adds one history entry and synchronously calls the
  attached append hook; empty and consecutive duplicate entries are ignored.
- `sl:history_set_max_len(max_len)` changes the retained history cap; `0`
  clears and disables history.
- `sl:history_save(filename)` writes history with owner-only permissions.
- `sl:history_load(filename)` imports history entries, bypassing append hooks.
- `sl:history_set_backend(key, hooks)` loads and attaches custom `load`/`append`
  callbacks; see [persistent history](#persistent-history).
- `sl:history_open(key, directory)` loads and attaches native keyed storage;
  omit `directory` for the XDG state default.
- `sl:history_close()` detaches storage without clearing recall or saving on exit.
- `sl:history_compact()` atomically retains the newest capped entries from the
  native shared file under its stable lock; unavailable for custom backends.
- `sl:history_set_auto_add(enabled)` opts into immediate recording at editor
  and queue acceptance. Disabled by default; delivery, promotion, Ctrl-R
  selection, cancellation and rejected drafts do not append.
- `sl:history_search_active()` returns a boolean indicating reverse-search
  mode, including a failed match. It is false after selection, cancellation,
  or editor exit. The query is read-only and runs on the editor-owner thread.
  Key callbacks run before built-in handling; submission guards should return
  `softline.KEY_ACTION_PASS` during search so Enter selects an editable match.
- `sl:set_screen_width(width)` sets ordinary readline wrapping width; `0`
  returns to terminal-width probing. Native chat uses physical terminal width.
  Without an active readline, this hint does not move or resize native output.
- `sl:set_live_scroll_region(enabled)` opts an ordinary readline prompt into
  bottom-pinned scroll-region output after it reaches the terminal bottom.
  Ownership is acquired at readline startup and retried between producer
  callbacks as output advances the prompt; multiple feeds in one callback
  remain immediate and refresh ownership at the next input-owned boundary.
  It is disabled by default; native chat always uses its own scroll region.
- `sl:set_image_paste_path_template(template)` copies an override path; pass
  `nil` to restore the default. A template contains exactly one `*` for a
  20-character lowercase base32hex xid. Softline always appends `.png` or
  `.jpeg`: `/tmp/softline/*/image` yields
  `/tmp/softline/{xid}/image.png`. Directories are created recursively.
  Templates support `~/`, `{{HOME}}`, `{{home}}`, `{{XDG_CACHE_HOME}}`, and
  `{{xdg_cache_home}}`. The XDG token uses `XDG_CACHE_HOME` when absolute,
  otherwise `$HOME/.cache`. The default is
  `${XDG_CACHE_HOME}/softline/{xid}.png` or `.jpeg` with that fallback.
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
  Binding `softline.KEY_ENTER` to return `softline.KEY_ACTION_SUBMIT` opts into
  immediate submission of a reverse-search match instead of accepting it for
  editing.
  The default `softline.KEY_CTRL_V` action saves a PNG or JPEG from the Linux
  X11 clipboard and inserts its persistent cache path; a binding overrides
  that action. It uses `DISPLAY`, including one forwarded by `ssh -Y`, and a
  missing image leaves the buffer unchanged with a `last_error()` diagnostic.
  Linux static and shared libraries privately bundle MIT-licensed c-ares for
  literal IPv4/IPv6, `/etc/hosts`, then `/etc/resolv.conf` DNS (including search
  domains). Lookup shares the 10-second paste deadline without libc NSS modules
  or an additional link library.
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
  prompt starts at the bottom. Physical resize preserves the terminal's native
  prompt position; only prompt layout changes update its cells. Producer bytes
  pass through unchanged. Before the first frame, output uses the full terminal and
  leaves its cursor in place between writes, preserving native autowrap and
  Unicode clusters. Cursor reports occur at ownership and resize boundaries.
  Feed calls have no intentional delay and never request or wait for cursor
  replies, including during resize. Local scalar cell widths, shared with prompt
  layout, track cursor advances. Terminal Unicode versions and cluster shaping
  can differ from the Unicode 16.0 accounting.
  The delta fallback cannot infer earlier hard-ended ASCII reflow or unknown
  terminal history if a retained prompt resizes without an input-owned observation,
  including after readline returns. The active input loop observes resize
  before dispatching producer watches. This delta fallback is deliberately
  immediate; see the stream design's unobserved retained-frame resize exception.
  No producer transcript, glyph spans or grapheme tails are buffered. Physical
  resize can race an in-flight VT batch and misplace even ASCII output. This accepted
  concurrency limit has an unpaused VTE diagnostic (`--streaming-race`);
  there is no transcript replay or feed wait to conceal it. Reflow
  belongs to the terminal. With an active prompt, a known ASCII right-edge
  continuation advances a hard row after the cursor handoff. That row may not
  rejoin on width growth, and a split Unicode cluster can lose its attachment.
  At the bottom output margin, VTE and tmux can reflow that boundary
  differently on width growth; exact continuation there is not guaranteed.
  Unfinished Unicode/tab lines have no reflow model. Width resize retains the
  tracked column, clamping it only if offscreen; continuation can land in a gap
  or overwrite text, including CR tails. A last-column report cannot distinguish
  Unicode pending wrap from a cursor before the last cell. These are intentional
  opaque-feed limits; Softline never reconstructs or replays the transcript.
  The output margin follows the actual prompt height, including visible queue
  entries, nonempty status messages, status lines, and editor rows. Unused
  preview slots occupy no space. At physical capacity the editor pages, leaving
  at least two output rows. Native chat needs at least three terminal rows, uses physical
  terminal width, and supports no rectangular viewport.
  An unfinished line clipped into scrollback continues at the first visible
  output row without replay. If the clipped position was at a hard line
  boundary, new output starts next to the prompt. Existing scrollback cells
  and spacing are preserved. Rapid tmux resizes may update its visible grid
  before delivering the matching PTY size; output during that interval can be
  corrupted. The native stream does not replay transcript bytes to repair it.
  The application owns its renderer, wakeup, and
  response/document lifecycle; Softline has no Markdown dependency.
- `sl:output_stream_write(bytes)` sends a Lua byte string immediately into the
  open session. Calls from a watch callback can alternate with prompt typing.
  Chunk boundaries add no newline or response separator. The stream accepts
  printable UTF-8, LF/CR/Tab, and ANSI SGR styling; malformed or unsupported
  terminal controls return `nil, status`. An empty string succeeds without
  changing the screen. No full response is buffered. Native LF positioning
  follows the output TTY's OPOST/ONLCR settings without changing those settings
  or producer bytes. While Softline owns raw mode, kernel tab expansion
  (`TAB3`/`OXTABS`, such as `stty -tabs`) is temporarily disabled so the terminal
  receives tabs unchanged. Original flags are restored when raw mode is
  released, including when input and output use separate TTYs.
  Native writes after that release disable expansion for the write and restore
  it before returning, without flushing pending input.
- `sl:output_stream_write_quoted_prompt(text)` writes submitted text between
  renderer segments as a wrapped, themed quote, repeating the prefix on every
  visible row and keeping one empty row on each side. The text is literal, so
  Markdown punctuation remains visible. `sl:set_quoted_prompt_prefix(prefix)`
  changes the default `> ` prefix; `nil` restores it.
  `sl:set_quoted_prompt_style({prefix={r,g,b}, text={r,g,b}})` overrides the
  theme colours; `nil` restores theme defaults. The prefix stays faded and the
  text stays italic.
- `sl:output_stream_end()` ends the producer session without finishing an
  external renderer document. While an editor is active it retains the
  prompt and scroll region for later finite output or another stream. Otherwise
  native chat closes using `clear_prompt_on_exit`: by default it clears only
  input rows, keeps queue/status rows, and leaves the cursor at column zero on
  the current input row without a newline or scroll. Setting
  `clear_prompt_on_exit = true` clears the whole prompt area and returns below
  the transcript. With no rendered prompt it returns below output.
  Closing an idle native session also releases terminal
  input ownership. Reopening a closed native session probes the cursor again.
  A nonzero column restarts quote separation for an unfinished externally
  written line; otherwise known spacing remains. A caller may run a foreground
  child between editor calls, then call
  `output_stream_begin()` to probe its final cursor and build a fresh prompt
  without replacing the handle or its history/settings. Do this outside
  editor callbacks; child execution belongs to the application. Non-TTY output
  adds no teardown controls. If the last write left an ANSI or UTF-8 sequence
  incomplete, it returns `nil, softline.ERROR_INVALID` and keeps the session
  open so the missing bytes can be supplied. Only one session may be open per
  editor. Output-session and quoted-prompt methods run on the Lua/editor owner
  thread; foreign producers should notify a watched descriptor instead of
  calling Lua.
- Physical resize preserves an unchanged prompt and cursor at their native
  terminal positions. Changed prompt layout updates only owned cells.
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
- `softline.KEY_CTRL_V`
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
The Lua chat example demonstrates queue delivery without libmdf.

The exact commands `!sh` and `/shell` open `$SHELL -i` (`/bin/sh -i` when
unset or empty), only between turns on an interactive terminal. Enter or idle
Alt-Enter launches the shell; busy submissions and attempts to queue/steer it
are blocked, keeping the draft editable. Arguments are not interpreted as shell
commands. The example ends its output session before `os.execute()` and begins
a new session on return. Clear and resize inside the shell are allowed; history,
queue and settings survive, and the transcript is never replayed. Failed launches
and nonzero exits report an error and return to chat.

```sh
make lua-test
softline_lua_env="$(make lua-env)" && eval "${softline_lua_env}"
lua examples/simple.lua
lua examples/chat.lua
```

`examples/chat.lua` accepts the same `SOFTLINE_PROMPT_THEME` values as the C
chat example. It uses the default native persistent output session: transcript
output starts at the original cursor, while the prompt starts at the bottom
and follows the terminal's native resize position.
Output feeds preserve unchanged prompt cells; prompt updates patch only cells
that differ from the previous frame.

To run against the in-tree debug `libsoftline` instead of the installed local
SDK:

```sh
make lua-debug-test
make lua-debug-simple
make lua-debug-chat
```

## Persistent history

```lua
local sl = softline.new()
assert(sl:history_open("my-app:project-id"))
assert(sl:history_set_auto_add(true))
```

The key is a nonempty string copied by Softline. Native storage uses
`$XDG_STATE_HOME/softline/history/<sha256-of-key>.history`, falling back to
`$HOME/.local/state` when the XDG value is absent, empty or relative. An absolute
`directory` overrides the complete directory. Parents are recursively created
with mode 0700; the store directory and regular, user-owned files are private
(files and lock files 0600; file symlinks/hard links are rejected).

One physical line holds one escaped prompt: `\\`, `\n`, `\r`, `\t` represent
backslash, LF (including Ctrl-J), CR and Tab. Unicode remains UTF-8. Entries
are appended before the accepting call returns, without exit-time saves or
per-entry fsync. Same-key processes and independent handles use a stable flock lock. Loading ignores a torn
final record; appending removes it first. Recall is bounded by `history_max_len`;
the file grows until explicit compaction. Zero cap disables recording. Same-key
handles share storage, but refresh recall only when they load it.

Custom storage uses the same acceptance boundary:

```lua
assert(sl:history_set_backend("my-app:project-id", {
  load = function(key, emit)
    -- Iterate your storage oldest first; emit entries individually.
    for prompt in storage:prompts(key) do
      assert(emit(prompt))
    end
    return true
  end,
  append = function(key, prompt)
    return storage:append(key, prompt) -- true or nil, negative status
  end,
}))
```

`load` is optional; `append` is required. Hooks return `true` or `nil, status`,
where status is a negative Softline error. Exceptions return `nil, status,
message` to the caller. Hooks run synchronously in the coroutine calling the
history/editor method, even if another coroutine created the handle. They
cannot yield across the C call. Automatic recording failures return `nil,
softline.READLINE_ERROR` from readline/next_prompt, with an additional message
for a Lua callback exception. Exception messages belong to the current hook
invocation; a later status-only failure does not repeat an earlier handled
exception. The emitter works only during load; retaining and
calling it later raises an error. Imports never call append. Attachment failure
preserves the previous hooks and recall; append failure preserves recall.
External storage rollback is the backend's responsibility. Do not reenter
history operations or close the handle from a hook. Callback closures are owned
by the handle and can be garbage-collected even when they capture that handle.
An explicit `queue_draft()` append failure returns `nil, status` (and the
callback exception message when present), retaining the editable draft without
admitting a queue entry. Once an explicit history call returns an exception
message, that message is consumed: a later cancellation, EOF, or unrelated
editor error does not return it again. An unhandled automatic recording
exception still supplies the message for its `READLINE_ERROR` result.

Manual `history_add()` remains the default recording policy. With auto-add,
programmatic queue insertion/replacement still requires an explicit add;
applications must not add delivered queued entries again. The explicit
`history_save(filename)` snapshot API does not participate in native locking or
compaction and should not rewrite an active shared native file.

The C and Lua chat examples attach `softline.examples.chat` at startup and enable
auto-add for accepted prompts/commands, including queued and steered drafts.
All themes use the same key. `SOFTLINE_HISTORY_DIR` overrides their directory.
