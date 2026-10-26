local softline = require('softline')
local sl = softline.new()
local editor, expected, calls
calls = 0
assert(sl:history_set_backend('editor-coroutine', {
  append = function(_, prompt)
    assert(coroutine.running() == expected)
    calls = calls + 1
    if calls == 1 then error('queue append failed') end
    if calls == 2 then return nil, softline.ERROR_IO end
    assert(prompt == (calls == 3 and 'queued' or 'direct'))
    return true
  end,
}))
assert(sl:history_set_auto_add(true))
assert(sl:set_prompt_queue(true, 8, 3))
assert(sl:set_queue_delivery('manual'))
local resets = 0
assert(sl:bind_key(softline.KEY_ALT_BASE + string.byte('r'), function()
  assert(sl:buffer() == 'queued' and sl:queue_count() == 0)
  resets = resets + 1
  return softline.KEY_ACTION_RESET
end))
assert(sl:bind_key(softline.KEY_TAB, function()
  local nested = coroutine.create(function()
    expected = coroutine.running()
    local ok, status, message = sl:queue_draft()
    if calls == 1 then
      assert(not ok and status == softline.ERROR)
      assert(message:match('queue append failed'))
      assert(sl:queue_count() == 0 and sl:buffer() == 'queued')
      io.write('QUEUE-ROLLBACK-PASS\n')
    elseif calls == 2 then
      assert(not ok and status == softline.ERROR_IO and message == nil)
      assert(sl:queue_count() == 0 and sl:buffer() == 'queued')
      io.write('QUEUE-STATUS-PASS\n')
    else
      assert(ok and sl:queue_count() == 1 and sl:buffer() == '')
      io.write('QUEUE-ACCEPT-PASS\n')
    end
    io.flush()
  end)
  local ok, err = coroutine.resume(nested)
  if not ok then io.stderr:write(tostring(err), '\n'); io.stderr:flush() end
  assert(ok, err)
  expected = editor
  return softline.KEY_ACTION_HANDLED
end))
editor = coroutine.create(function()
  expected = coroutine.running()
  local line = assert(sl:next_prompt('> '))
  assert(line == 'direct' and calls == 4 and resets == 1)
  assert(sl:queue_take(1) == 'queued')
end)
assert(coroutine.resume(editor))
sl:close()
print('COROUTINE-EDITOR-PASS')
