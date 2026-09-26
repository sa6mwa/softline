local softline = require("softline")

local function assert_eq(actual, expected, label)
  if actual ~= expected then
    error(label .. ": expected " .. tostring(expected) .. ", got " .. tostring(actual), 2)
  end
end

local prompt_methods = {
  "next_prompt",
  "set_live_scroll_region",
  "output_stream_begin",
  "output_stream_write",
  "output_stream_write_quoted_prompt",
  "output_stream_end",
  "set_prompt_queue",
  "queue_count",
  "queue_capacity",
  "queue_peek",
  "queue_insert",
  "queue_append",
  "queue_replace",
  "queue_take",
  "queue_mode",
  "queue_set_mode",
  "queue_clear",
  "queue_draft",
  "set_queue_delivery",
  "queue_delivery",
  "set_queue_profile",
  "queue_profile",
  "set_queue_keys",
  "queue_keys",
  "set_prompt_theme",
  "set_quoted_prompt_prefix",
  "set_quoted_prompt_style",
  "set_statusline",
  "set_status_message",
  "set_status_message_prefix",
  "set_status_message_colors",
  "set_status_elements",
  "set_status_element",
  "set_status_busy",
  "set_status_spinner",
  "set_status_idle_marker",
}

for _, field in ipairs({ "bounded", "screen_x", "screen_y", "screen_height",
                         "unknown_option", "screen_wdith", "screen_width\0extra" }) do
  local ok, message = pcall(softline.new, { [field] = true })
  assert(not ok and message:find("unknown configuration field", 1, true),
         "unknown configuration option accepted: " .. field)
end
local ok, message = pcall(softline.new, { [1] = true })
assert(not ok and message:find("configuration field names must be strings", 1, true))

local sl = assert(softline.new({ line_max_len = 32 }))
assert_eq(sl:last_readline_status(), softline.READLINE_NONE, "initial status")
assert(sl:history_add("history entry"))
assert(sl:set_buffer("draft"))
assert_eq(sl:buffer(), "draft", "buffer")
assert(sl:set_cursor(2))
assert_eq(sl:cursor(), 2, "cursor")
assert(sl:insert("++"))
assert_eq(sl:buffer(), "dr++aft", "insert")
assert(sl:set_buffer("å"))
assert(sl:set_cursor(1))
assert_eq(sl:cursor(), 0, "UTF-8 cursor clamp")
assert(sl:insert("x"))
assert_eq(sl:buffer(), "xå", "UTF-8 cursor insert")
assert(sl:set_idle_callback(function() end))
assert(sl:set_idle_callback(nil))
assert(sl:print_above({ "alpha", "-", "beta\n" }))
assert(sl:print_above(function(i)
  if i == 1 then
    return "gamma\n"
  end
  return nil
end))
assert(sl:output_stream_begin())
assert(not sl:output_stream_begin())
assert(sl:output_stream_write("delta"))
assert(sl:output_stream_write(""))
assert(sl:output_stream_write("-echo\n"))
assert(sl:output_stream_end())
assert(not sl:output_stream_write("after end"))
sl:close()

assert_eq(sl.set_bounds, nil, "boxed viewport method removed")

local status = assert(softline.new({
  prompt_queue = true,
  prompt_queue_max_entries = 7,
  prompt_queue_preview_entries = 2,
  prompt_theme = softline.PROMPT_THEME_RICED,
  statusline = true,
  statusline_start_element = 15,
  status_spinner = true,
  status_busy = true,
  status_idle_marker = "-",
  live_scroll_region = true,
}))
assert(status:set_statusline(true, 15))
assert(status:set_status_message("Thinking..."))
assert(status:set_status_message_prefix("? "))
assert(status:set_status_message_colors(softline.THEME_COLOR_MUTED,
                                        softline.THEME_COLOR_ELEMENT_2))
assert(status:set_status_message_prefix(""))
assert(status:set_status_message_prefix(nil))
assert(not status:set_status_message_colors(-1, softline.THEME_COLOR_MUTED))
assert(status:set_status_message("Reasoning..."))
assert(not status:set_status_message("two\nlines"))
assert(status:set_status_message(nil))
assert(status:set_live_scroll_region(true))
assert(status:set_live_scroll_region(false))
assert(status:set_status_elements({ "model", "context" }))
assert(status:set_status_element(1, "context 36%"))
assert(status:set_status_busy(false))
assert(status:set_status_spinner(false))
assert(status:set_status_idle_marker("-"))
assert(status:set_status_idle_marker(nil))
assert_eq(status:queue_count(), 0, "initial queue count")
assert_eq(status:queue_capacity(), 7, "queue capacity")
assert(status:queue_append("one"))
assert(status:queue_append("three"))
assert(status:queue_insert(2, "two"))
if string.packsize("T") < 8 then
  assert(not status:queue_take(4294967297))
  assert_eq(status:queue_peek(1), "one", "overflowing queue index changed queue")
end
assert_eq(status:queue_peek(2), "two", "queue peek")
assert_eq(status:queue_mode(2), softline.QUEUE_MODE_QUEUED, "default queue mode")
assert(status:queue_set_mode(2, softline.QUEUE_MODE_STEER))
assert_eq(status:queue_mode(2), softline.QUEUE_MODE_STEER, "steer queue mode")
assert(status:queue_replace(2, "second"))
assert_eq(status:queue_mode(2), softline.QUEUE_MODE_STEER, "replacement kept mode")
assert_eq(status:queue_take(1), "one", "queue take")
assert_eq(status:queue_count(), 2, "queue count after take")
assert(status:set_queue_delivery("manual"))
assert_eq(status:queue_delivery(), "manual", "manual queue delivery")
assert(status:set_queue_profile("queued_turns"))
assert_eq(status:queue_profile(), "queued_turns", "queued-turns profile")
assert_eq(status:queue_delivery(), "auto", "idle queued-turns delivery")
assert(status:set_status_busy(true))
assert_eq(status:queue_delivery(), "manual", "busy queued-turns delivery")
assert(status:set_status_busy(false))
assert_eq(status:queue_delivery(), "auto", "idle queued-turns delivery")
assert(status:set_queue_delivery("manual"))
assert_eq(status:queue_delivery(), "manual", "host-controlled queued turns")
assert(status:set_status_busy(true))
assert(status:set_status_busy(false))
assert_eq(status:queue_delivery(), "manual", "manual delivery survives idle")
assert(status:set_queue_delivery("auto"))
local keys = status:queue_keys()
assert_eq(keys.enqueue_draft, softline.KEY_ENTER, "queued-turns enqueue key")
assert_eq(keys.edit_newest, softline.KEY_ALT_E, "queued-turns edit key")
assert_eq(keys.submit_or_promote_newest, softline.KEY_ALT_ENTER,
  "queued-turns submit/promote key")
keys.enqueue_draft = nil
assert(status:set_queue_keys(keys))
assert(status:queue_clear())
assert_eq(status:queue_count(), 0, "queue clear")
for _, name in ipairs(prompt_methods) do
  assert_eq(type(status[name]), "function", "prompt method " .. name)
end
assert_eq(softline.PROMPT_SOURCE_NONE, 0, "no prompt source constant")
assert_eq(softline.PROMPT_SOURCE_DIRECT, 1, "direct prompt source constant")
assert_eq(softline.PROMPT_SOURCE_QUEUED, 2, "queued prompt source constant")
assert_eq(softline.PROMPT_SOURCE_PROMOTED, 3, "promoted prompt source constant")
assert_eq(softline.QUEUE_MODE_QUEUED, 0, "queued mode constant")
assert_eq(softline.QUEUE_MODE_STEER, 1, "steer mode constant")
assert_eq(softline.ERROR_FULL, -5, "queue full status constant")
assert_eq(softline.ERROR, -1, "generic error status constant")
assert_eq(softline.ERROR_INVALID, -2, "invalid argument status constant")
assert_eq(softline.ERROR_NOMEM, -3, "allocation status constant")
assert_eq(softline.ERROR_IO, -4, "I/O status constant")
assert_eq(softline.PROMPT_THEME_PLAIN, 0, "plain prompt theme constant")
assert_eq(softline.PROMPT_THEME_ACCENT, 1, "accent prompt theme constant")
assert_eq(softline.PROMPT_THEME_DRACULA, 2, "dracula prompt theme constant")
assert_eq(softline.PROMPT_THEME_GRUVBOX, 3, "gruvbox prompt theme constant")
assert_eq(softline.PROMPT_THEME_MONOCHROME, 4, "monochrome prompt theme constant")
assert_eq(softline.PROMPT_THEME_MONOGREEN, 5, "monogreen prompt theme constant")
assert_eq(softline.PROMPT_THEME_OUTRUN, 6, "outrun prompt theme constant")
assert_eq(softline.PROMPT_THEME_RICED, 7, "riced prompt theme constant")
assert_eq(softline.PROMPT_THEME_SYNTHWAVE, 8, "synthwave prompt theme constant")
assert_eq(softline.PROMPT_THEME_DEFAULT, 9, "default prompt theme constant")
assert_eq(softline.STATUS_MAX_ELEMENTS, 32, "status element limit constant")
assert_eq(softline.KEY_TAB, 9, "Tab key constant")
assert_eq(softline.KEY_CTRL_N, 14, "Ctrl-N key constant")
assert_eq(softline.KEY_CTRL_P, 16, "Ctrl-P key constant")
assert_eq(softline.KEY_ESCAPE, 27, "Escape key constant")
assert_eq(softline.KEY_UP, 1000, "Up key constant")
assert_eq(softline.KEY_DOWN, 1001, "Down key constant")
assert_eq(softline.KEY_CTRL_ENTER, 1022, "Ctrl-Enter key constant")
assert_eq(softline.KEY_ALT_ENTER, 1023, "Alt-Enter key constant")
assert_eq(softline.KEY_ALT_E, 4096 + string.byte("e"), "Alt-E key constant")
assert_eq(softline.KEY_NONE, 0, "disabled key constant")
assert_eq(softline.KEY_CTRL_R, 18, "reverse-search key constant")
assert_eq(softline.KEY_F1, 1012, "function key constant")
assert_eq(softline.KEY_F10, 1021, "last function key constant")
assert_eq(softline.KEY_ALT_BASE, 4096, "Alt-letter base constant")
assert_eq(softline.KEY_ALT_M, 4205, "Alt-M key constant")
status:close()

print("lua softline smoke passed")
