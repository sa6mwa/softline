local softline = require("softline")

local function assert_eq(actual, expected, label)
  if actual ~= expected then
    error(label .. ": expected " .. tostring(expected) .. ", got " .. tostring(actual), 2)
  end
end

local prompt_methods = {
  "next_prompt",
  "set_live_scroll_region",
  "set_prompt_queue",
  "set_prompt_theme",
  "set_statusline",
  "set_status_elements",
  "set_status_element",
  "set_status_busy",
  "set_status_spinner",
  "set_status_idle_marker",
}

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
sl:close()

local bounded = assert(softline.new({ bounded = true, screen_height = 1 }))
local ok = bounded:print_above("should not fit\n")
assert_eq(ok, nil, "bounded config should affect print_above")
bounded:close()

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
assert(status:set_live_scroll_region(true))
assert(status:set_live_scroll_region(false))
assert(status:set_status_elements({ "model", "context" }))
assert(status:set_status_element(1, "context 36%"))
assert(status:set_status_busy(false))
assert(status:set_status_spinner(false))
assert(status:set_status_idle_marker("-"))
assert(status:set_status_idle_marker(nil))
for _, name in ipairs(prompt_methods) do
  assert_eq(type(status[name]), "function", "prompt method " .. name)
end
assert_eq(softline.PROMPT_SOURCE_NONE, 0, "no prompt source constant")
assert_eq(softline.PROMPT_SOURCE_DIRECT, 1, "direct prompt source constant")
assert_eq(softline.PROMPT_SOURCE_QUEUED, 2, "queued prompt source constant")
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
assert_eq(softline.KEY_UP, 1000, "Up key constant")
assert_eq(softline.KEY_DOWN, 1001, "Down key constant")
assert_eq(softline.KEY_ALT_E, 4096 + string.byte("e"), "Alt-E key constant")
status:close()

print("lua softline smoke passed")
