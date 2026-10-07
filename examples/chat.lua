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
local exit_requested = false
local worker
local worker_pid
local watch_id
local dispatch_next_queued
local stream_open = false

local function is_shell_command(line)
  return line == "!sh" or line == "/shell"
end

-- Only the idle owner loop may hand the terminal to another process. End the
-- session to release input/scrolling, then begin a fresh frame at the shell's
-- final cursor position (including after clear or resize). Keep the handle,
-- history and queue. Quote SHELL as one executable, never as shell commands.
local function run_shell()
  assert(sl:output_stream_end())
  stream_open = false
  local ok, reason, status = os.execute('exec "${SHELL:-/bin/sh}" -i')
  if not ok then
    io.stderr:write("shell exited unsuccessfully (", tostring(reason), " ", tostring(status), ")\n")
  end
  assert(sl:output_stream_begin())
  stream_open = true
  assert(sl:set_status_message(nil))
end

local function operation_step_seconds()
  local value = tonumber(os.getenv("SOFTLINE_CHAT_OPERATION_STEP_MS"))
  if value and value >= 0 and value <= 60000 then
    return value / 1000
  end
  return 1
end

local function print_message(parts)
  for _, part in ipairs(parts) do
    local ok, status = sl:output_stream_write(part)
    if not ok then
      error(sl:last_error() or ("output_stream_write failed: " .. tostring(status)))
    end
  end
end

local function set_chat_busy(value)
  assert(sl:set_status_spinner(value))
  assert(sl:set_status_busy(value))
  assert(sl:set_status_message(nil))
  busy = value
end

local function consume_active_operation_input(line)
  print_message({ "[active operation] consumed: ", line, "\n" })
end

local function consume_steers()
  local index = 1
  while index <= sl:queue_count() do
    if sl:queue_mode(index) == softline.QUEUE_MODE_STEER then
      local line = assert(sl:queue_take(index))
      if line == "/quit" then
        exit_requested = true
        assert(sl:cancel())
        return
      end
      if is_shell_command(line) then
        print_message({ "Shell commands cannot be queued or steered.\n" })
      else
        consume_active_operation_input(line)
      end
    else
      index = index + 1
    end
  end
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
  consume_steers()
  set_chat_busy(false)
  if not exit_requested then
    dispatch_next_queued()
  end
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
        if event ~= nil then
          consume_steers()
        end
      end))
end

dispatch_next_queued = function()
  local index = 1
  while index <= sl:queue_count() do
    if sl:queue_mode(index) == softline.QUEUE_MODE_QUEUED then
      local line = assert(sl:queue_take(index))
      if line == "/quit" then
        exit_requested = true
        assert(sl:cancel())
        return
      end
      if is_shell_command(line) then
        print_message({ "Shell commands cannot be queued or steered.\n" })
      else
        print_message({ "[queued] ", line, "\n" })
        start_operation()
        return
      end
    else
      index = index + 1
    end
  end
end

local function run()
  sl = softline.new()
  assert(sl:history_open("softline.examples.chat", os.getenv("SOFTLINE_HISTORY_DIR")))
  assert(sl:history_set_auto_add(true))
  if interactive then
    assert(themes[theme_name], "invalid SOFTLINE_PROMPT_THEME: " .. theme_name)
    assert(sl:set_prompt_theme(themes[theme_name]))
    assert(sl:set_prompt_queue(true, 64, 3))
    assert(sl:set_queue_profile("queued_turns"))
    assert(sl:set_queue_delivery("manual"))
    assert(sl:set_statusline(true, 0))
    assert(sl:set_status_elements({
      "gpt-5.6-terra high",
      "ctx 36%",
      "demo/project",
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
    for _, key in ipairs({ softline.KEY_ENTER, softline.KEY_ALT_ENTER, softline.KEY_TAB }) do
      assert(sl:bind_key(key, function(pressed)
        -- Search selection must return to editing before submission guards run.
        if sl:history_search_active() then
          return softline.KEY_ACTION_PASS
        end
        if is_shell_command(sl:buffer()) and (busy or pressed == softline.KEY_TAB) then
          assert(sl:set_status_message(busy and "Shell is available only between turns."
              or "Use Enter to open the shell; it cannot be queued."))
          return softline.KEY_ACTION_HANDLED
        end
        return softline.KEY_ACTION_PASS
      end))
    end
    set_chat_busy(false)
    assert(sl:output_stream_begin())
    stream_open = true
    print_message({ "softline Lua turn processor. A staged operation streams for about four seconds. Enter sends while available and queues while an operation is running; Alt-Enter queues a steer for the next stage boundary; empty Alt-Enter marks the newest queued turn as steer; Alt-E edits it. Escape or Ctrl-C stops the operation; /quit leaves. !sh or /shell opens your shell between turns.\n" })
  else
    assert(sl:output_stream_begin())
    stream_open = true
  end

  while true do
    local line, source_or_status = sl:next_prompt()
    if exit_requested then
      break
    end
    if line then
      if line == "/quit" then
        break
      end
      if is_shell_command(line) then
        if not interactive then
          print_message({ "Shell requires an interactive terminal.\n" })
        elseif busy or source_or_status ~= softline.PROMPT_SOURCE_DIRECT then
          print_message({ "Shell is available only between turns.\n" })
        else
          assert(sl:output_stream_write_quoted_prompt(line))
          run_shell()
        end
      elseif line == "" then
        print_message({ "[empty turn ignored]\n" })
      else
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
    pcall(cancel_operation)
  end
  if stream_open then
    sl:output_stream_end()
  end
  sl:close()
end
if not ok then
  io.stderr:write(err, "\n")
  os.exit(1)
end
