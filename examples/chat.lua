local softline = require("softline")

local theme_name = os.getenv("SOFTLINE_PROMPT_THEME") or "accent"
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

local function is_interactive_terminal()
  local ok, _, code = os.execute("test -t 0 && test -t 1")
  return ok == true or ok == 0 or code == 0
end

local interactive = is_interactive_terminal()
local sl
local next_peer_message_at
local status_started_at
local status_phase
local peer_messages = {
  "I found a calm corner of the conversation.",
  "The kettle is on; take your time.",
  "A small detail can change the whole picture.",
  "I am following along from the other side of the room.",
}

local function print_message(parts)
  local ok, status = sl:print_above(parts)
  if not ok then
    error(sl:last_error() or ("print_above failed: " .. tostring(status)))
  end
end

local function print_reply(source, line)
  local label = source == softline.PROMPT_SOURCE_QUEUED and "[queued] " or "[direct] "
  print_message({ label, "I read back: ", line, "\n" })
end

local function update_status_presentation(now)
  local phase = math.floor((now - status_started_at) / 5) % 8
  if phase == status_phase then
    return
  end
  assert(sl:set_status_spinner(phase == 1 or phase == 3))
  assert(sl:set_status_busy(phase < 5 or phase == 6))
  status_phase = phase
end

local function print_peer_message()
  local now = os.time()
  update_status_presentation(now)
  if now < next_peer_message_at then
    return
  end
  next_peer_message_at = now + 2
  print_message({ "[peer] ", peer_messages[math.random(#peer_messages)], "\n" })
end

local function run()
  assert(themes[theme_name], "invalid SOFTLINE_PROMPT_THEME: " .. theme_name)
  sl = softline.new()
  assert(sl:set_prompt_theme(themes[theme_name]))
  assert(sl:set_prompt_queue(true, 64, 3))
  assert(sl:set_statusline(true, 0))
  assert(sl:set_status_elements({
    "gpt-5.6-terra high",
    "ctx 36%",
    "~/g/softline",
    "weekly 56%",
    "feat/prompt-queue",
    "pursuing chat",
  }))
  if interactive then
    assert(sl:bind_key(softline.KEY_CTRL_C, function()
      return softline.KEY_ACTION_CANCEL
    end))
    math.randomseed(os.time())
    next_peer_message_at = os.time() + 2
    status_started_at = os.time()
    status_phase = nil
    update_status_presentation(status_started_at)
    assert(sl:set_idle_callback(print_peer_message))
    print_message({ "softline Lua chat example. Tab queues; Alt-E recalls the newest queued prompt. Status cycles every five seconds.\n" })
  end

  while true do
    local line, source_or_status = sl:next_prompt("chat> ")
    if line then
      if line == "exit" then
        break
      end
      print_reply(source_or_status, line)
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
if not ok then
  io.stderr:write(err, "\n")
  os.exit(1)
end
