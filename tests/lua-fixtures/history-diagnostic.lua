local softline = require('softline')
local sl = softline.new()
local method, outcome = arg[1], arg[2]
local calls = 0
assert(sl:history_set_backend('automatic-diagnostic', {
  append = function(_, prompt)
    calls = calls + 1
    assert(prompt == (calls == 1 and 'queued' or 'direct'))
    if calls == 1 then error('handled queue exception') end
    if outcome == 'exception' then error('current submit exception') end
    if outcome == 'status' then return nil, softline.ERROR_IO end
    return false
  end,
}))
assert(sl:history_set_auto_add(true))
assert(sl:set_prompt_queue(true, 8, 3))
assert(sl:set_queue_delivery('manual'))
assert(sl:bind_key(softline.KEY_TAB, function()
  local ok, status, message = sl:queue_draft()
  assert(not ok and status == softline.ERROR)
  assert(message:match('handled queue exception'))
  assert(sl:queue_count() == 0 and sl:buffer() == 'queued')
  io.write('QUEUE-ERROR-HANDLED\n'); io.flush()
  return softline.KEY_ACTION_HANDLED
end))
assert(sl:bind_key(7, function()
  if outcome == 'key_error' then error('unrelated key exception') end
  assert(sl:cancel())
  return softline.KEY_ACTION_HANDLED
end))
assert(sl:bind_key(softline.KEY_ESCAPE, function()
  return softline.KEY_ACTION_CANCEL
end))
local result = table.pack(sl[method](sl, '> '))
io.write('EDITOR-RESULT\n'); io.flush()
local exits = {
  cancel = softline.READLINE_CANCELLED,
  cancel_hook = softline.READLINE_CANCELLED,
  eof = softline.READLINE_EOF,
  key_error = softline.READLINE_ERROR,
}
assert(result[1] == nil)
if exits[outcome] then
  assert(result[2] == exits[outcome] and calls == 1)
  assert(result.n == 2, 'handled exception leaked into editor exit: ' .. tostring(result[3]))
elseif outcome == 'exception' then
  assert(result[2] == softline.READLINE_ERROR and calls == 2)
  assert(result.n == 3 and result[3]:match('current submit exception'))
  assert(not result[3]:match('handled queue exception'))
else
  assert(result[2] == softline.READLINE_ERROR and calls == 2)
  assert(result.n == 2, 'stale queue exception: ' .. tostring(result[3]))
end
assert(sl:queue_count() == 0)
sl:close()
print('EDITOR-DIAGNOSTIC-PASS')
