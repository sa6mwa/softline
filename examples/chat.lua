local softline = require("softline")

local theme_name = os.getenv("SOFTLINE_PROMPT_THEME") or "accent"
local themes = {
  plain = softline.PROMPT_THEME_PLAIN,
  accent = softline.PROMPT_THEME_ACCENT,
  riced = softline.PROMPT_THEME_RICED,
}
assert(themes[theme_name], "invalid SOFTLINE_PROMPT_THEME: " .. theme_name)

io.write("\27[?1049h\27[2J\27[H")

local sl = softline.new()
sl:set_bounds(0, 0, 0, 0)
sl:set_prompt_queue(true, 64, 3)
sl:set_prompt_theme(themes[theme_name])
sl:bind_key(softline.KEY_CTRL_C, function()
  return softline.KEY_ACTION_CANCEL
end)
sl:print_above({ "softline Lua chat example. Type exit or press Ctrl-D.\n" })

while true do
  local line, source = sl:next_prompt("chat> ")
  if not line or line == "exit" then
    break
  end
  sl:print_above({ line, "\n" })
end

sl:close()
io.write("\27[?1049l")
