local softline = require("softline")

local sl = softline.new()
local theme_name = os.getenv("SOFTLINE_PROMPT_THEME") or "plain"
local themes = {
  plain = softline.PROMPT_THEME_PLAIN,
  accent = softline.PROMPT_THEME_ACCENT,
  dracula = softline.PROMPT_THEME_DRACULA,
  gruvbox = softline.PROMPT_THEME_GRUVBOX,
  monochrome = softline.PROMPT_THEME_MONOCHROME,
  monogreen = softline.PROMPT_THEME_MONOGREEN,
  outrun = softline.PROMPT_THEME_OUTRUN,
  riced = softline.PROMPT_THEME_RICED,
  synthwave = softline.PROMPT_THEME_SYNTHWAVE,
}
assert(themes[theme_name], "invalid SOFTLINE_PROMPT_THEME: " .. theme_name)
assert(sl:set_prompt_theme(themes[theme_name]))

while true do
  local line, status = sl:readline("softline> ")
  if not line then
    if status == softline.READLINE_EOF then
      break
    end
    local err = sl:last_error()
    if err then
      io.stderr:write(err, "\n")
    end
    break
  end
  if line == "exit" then
    break
  end
  io.write("submitted: ", line, "\n")
  sl:history_add(line)
end

sl:close()
