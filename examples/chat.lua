local softline = require("softline")

local theme_name = os.getenv("SOFTLINE_PROMPT_THEME") or "accent"
local themes = {
  plain = softline.PROMPT_THEME_PLAIN,
  accent = softline.PROMPT_THEME_ACCENT,
  riced = softline.PROMPT_THEME_RICED,
}

local function is_interactive_terminal()
  local ok, _, code = os.execute("test -t 0 && test -t 1")
  return ok == true or ok == 0 or code == 0
end

local interactive = is_interactive_terminal()
local alt_screen_active = false
local sl

local function leave_alt_screen()
  if alt_screen_active then
    io.write("\27[?1049l")
    alt_screen_active = false
  end
end

local function print_message(parts)
  local ok, status = sl:print_above(parts)
  if not ok then
    error(sl:last_error() or ("print_above failed: " .. tostring(status)))
  end
end

local function run()
  assert(themes[theme_name], "invalid SOFTLINE_PROMPT_THEME: " .. theme_name)
  if interactive then
    io.write("\27[?1049h\27[2J\27[H")
    alt_screen_active = true
  end

  sl = softline.new()
  assert(sl:set_prompt_theme(themes[theme_name]))
  if interactive then
    assert(sl:set_bounds(0, 0, 0, 0))
    assert(sl:set_prompt_queue(true, 64, 3))
    assert(sl:bind_key(softline.KEY_CTRL_C, function()
      return softline.KEY_ACTION_CANCEL
    end))
    print_message({ "softline Lua chat example. Tab queues; Alt-E recalls the newest queued prompt.\n" })
  end

  while true do
    local line, source_or_status = sl:next_prompt("chat> ")
    if line then
      if line == "exit" then
        break
      end
      local label = source_or_status == softline.PROMPT_SOURCE_QUEUED and "[queued] " or "[direct] "
      print_message({ label, line, "\n" })
    elseif source_or_status == softline.READLINE_CANCELLED or source_or_status == softline.READLINE_INTERRUPTED then
      if interactive then
        print_message({ "[cancelled]\n" })
      end
    elseif source_or_status == softline.READLINE_EOF then
      break
    else
      error(sl:last_error() or ("readline failed: " .. tostring(source_or_status)))
    end
  end
end

local ok, err = pcall(run)
if sl then
  sl:close()
end
leave_alt_screen()
if not ok then
  io.stderr:write(err, "\n")
  os.exit(1)
end
