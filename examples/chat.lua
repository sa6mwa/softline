local softline = require("softline")

local theme_name = os.getenv("SOFTLINE_PROMPT_THEME") or "default"
local themes = {
  default = softline.PROMPT_THEME_DEFAULT,
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
local busy = false
local worker
local worker_pid
local watch_id

local function operation_step_seconds()
  local value = tonumber(os.getenv("SOFTLINE_CHAT_OPERATION_STEP_MS"))
  if value and value >= 0 and value <= 60000 then
    return value / 1000
  end
  return 1
end

local function print_message(parts)
  local ok, status = sl:print_above(parts)
  if not ok then
    error(sl:last_error() or ("print_above failed: " .. tostring(status)))
  end
end

local function set_chat_busy(value)
  assert(sl:set_status_spinner(value))
  assert(sl:set_status_busy(value))
  busy = value
end

local function consume_active_operation_input(line)
  print_message({ "[active operation] consumed: ", line, "\n" })
end

local function finish_operation()
  if watch_id then
    assert(sl:watch_remove(watch_id))
    watch_id = nil
  end
  if worker then
    assert(worker:close())
    worker = nil
  end
  worker_pid = nil
  set_chat_busy(false)
end

-- Escape and Ctrl-C arrive here as READLINE_CANCELLED. The worker belongs to
-- the example application, so cancellation stops it instead of only clearing
-- the editor.
local function cancel_operation()
  if watch_id then
    assert(sl:watch_remove(watch_id))
    watch_id = nil
  end
  if worker_pid then
    os.execute("kill -TERM " .. worker_pid .. " >/dev/null 2>&1")
  end
  if worker then
    worker:close()
    worker = nil
  end
  worker_pid = nil
  set_chat_busy(false)
end

local function start_operation()
  if busy then
    return
  end
  local delay = string.format("%.3f", operation_step_seconds())
  worker = assert(io.popen("exec sh -c 'echo $$; printf P; sleep " .. delay ..
      "; printf L; sleep " .. delay .. "; printf W; sleep " .. delay ..
      "; printf C; sleep " .. delay .. "; printf R'", "r"))
  worker_pid = assert(tonumber(worker:read("*l")), "operation PID missing")
  set_chat_busy(true)
  watch_id = assert(sl:watch_add(worker,
      softline.WATCH_READ | softline.WATCH_HANGUP | softline.WATCH_ERROR,
      function()
        local event = worker:read(1)
        if event == "P" then
          print_message({ "[operation] processing input.\n" })
        elseif event == "L" then
          print_message({ "[operation] planning next steps.\n" })
        elseif event == "W" then
          print_message({ "[operation] running work.\n" })
        elseif event == "C" then
          print_message({ "[operation] checking result.\n" })
        elseif event == "R" then
          print_message({ "[operation] produced a result.\n" })
        elseif event == nil then
          finish_operation()
        end
      end))
end

local function run()
  sl = softline.new()
  if interactive then
    assert(themes[theme_name], "invalid SOFTLINE_PROMPT_THEME: " .. theme_name)
    local live_scroll_region = os.getenv("SOFTLINE_LIVE_SCROLL_REGION")
    assert(live_scroll_region == nil or live_scroll_region == "" or
        live_scroll_region == "0" or live_scroll_region == "1" or
        live_scroll_region == "false" or live_scroll_region == "true",
        "invalid SOFTLINE_LIVE_SCROLL_REGION: " .. tostring(live_scroll_region))
    assert(sl:set_live_scroll_region(
        live_scroll_region == "1" or live_scroll_region == "true"))
    assert(sl:set_prompt_theme(themes[theme_name]))
    assert(sl:set_prompt_queue(true, 64, 3))
    assert(sl:set_queue_profile("queued_turns"))
    assert(sl:set_statusline(true, 0))
    assert(sl:set_status_elements({
      "gpt-5.6-terra high",
      "ctx 36%",
      "~/g/softline",
      "weekly 56%",
      "queue demo",
      "turn processor",
    }))
    assert(sl:bind_key(softline.KEY_CTRL_C, function()
      return softline.KEY_ACTION_CANCEL
    end))
    assert(sl:bind_key(softline.KEY_ESCAPE, function()
      return softline.KEY_ACTION_CANCEL
    end))
    set_chat_busy(false)
    print_message({ "softline Lua turn processor. A staged operation streams for about four seconds. Enter sends while available and queues while an operation is running; Alt-Enter sends immediate input to the running operation; empty Alt-Enter promotes the newest queued turn; Alt-E edits it. Escape or Ctrl-C stops the operation.\n" })
  end

  while true do
    local line, source_or_status = sl:next_prompt()
    if line then
      if line == "exit" then
        break
      end
      if line == "" then
        print_message({ "[empty turn ignored]\n" })
      else
        assert(sl:history_add(line))
        local prefix = source_or_status == softline.PROMPT_SOURCE_PROMOTED
            and "[promoted] " or source_or_status == softline.PROMPT_SOURCE_QUEUED
            and "[queued] " or "[turn] "
        print_message({ prefix, line, "\n" })
        if interactive and busy then
          consume_active_operation_input(line)
        elseif interactive then
          start_operation()
        end
      end
    elseif source_or_status == softline.READLINE_CANCELLED or source_or_status == softline.READLINE_INTERRUPTED then
      if interactive then
        if busy then
          cancel_operation()
          print_message({ "[operation] cancelled\n" })
        else
          print_message({ "[cancelled]\n" })
        end
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
  if worker then
    pcall(finish_operation)
  end
  sl:close()
end
if not ok then
  io.stderr:write(err, "\n")
  os.exit(1)
end
