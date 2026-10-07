"""Exercise coroutine-owned history hooks in a real editor on a private PTY."""
import os
import sys
import time

sys.dont_write_bytecode = True
from check_chat_shell import Chat


SCRIPT = r'''
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
  assert(line == 'direct' and calls == 4)
  assert(sl:queue_take(1) == 'queued')
end)
assert(coroutine.resume(editor))
sl:close()
print('COROUTINE-EDITOR-PASS')
'''


chat = Chat([sys.argv[1], '-e', SCRIPT], dict(os.environ))
try:
    chat.wait(b'\x1b[?2004h')
    chat.send(b'queued\t')
    chat.wait(b'QUEUE-ROLLBACK-PASS')
    chat.send(b'\t')
    chat.wait(b'QUEUE-STATUS-PASS')
    chat.send(b'\t')
    chat.wait(b'QUEUE-ACCEPT-PASS')
    chat.send(b'direct\r')
    chat.wait(b'COROUTINE-EDITOR-PASS')
    deadline = time.monotonic() + 5
    while True:
        pid, status = os.waitpid(chat.pid, os.WNOHANG)
        if pid:
            chat.reaped = True
            assert os.waitstatus_to_exitcode(status) == 0, bytes(chat.output)
            break
        assert time.monotonic() < deadline, bytes(chat.output)
        chat.pump(0.05)
finally:
    chat.close()
print('Lua history: coroutine editor submission, nested queue hooks and rollback passed.')

SEARCH_SCRIPT = r'''
local softline = require('softline')
local sl = softline.new()
assert(not sl:history_search_active())
assert(sl:history_add('match'))
local expected = { true, true, false }
local enters, cancels = 0, 0
assert(sl:bind_key(softline.KEY_ENTER, function()
  enters = enters + 1
  assert(sl:history_search_active() == expected[enters])
  return softline.KEY_ACTION_PASS
end))
assert(sl:bind_key(7, function()
  assert(sl:history_search_active())
  cancels = cancels + 1
  return softline.KEY_ACTION_PASS
end))
assert(sl:readline('> ') == 'draft!')
assert(enters == 3 and cancels == 2 and not sl:history_search_active())
sl:close()
print('SEARCH-QUERY-PASS')
'''

chat = Chat([sys.argv[1], '-e', SEARCH_SCRIPT], dict(os.environ))
try:
    chat.wait(b'\x1b[?2004h')
    chat.send(b'draft\x12match\r\x15draft\x12missing\r\x12match\x07\x12match\x07!\r')
    chat.wait(b'SEARCH-QUERY-PASS')
    deadline = time.monotonic() + 5
    while True:
        pid, status = os.waitpid(chat.pid, os.WNOHANG)
        if pid:
            chat.reaped = True
            assert os.waitstatus_to_exitcode(status) == 0, bytes(chat.output)
            break
        assert time.monotonic() < deadline, bytes(chat.output)
        chat.pump(0.05)
finally:
    chat.close()
print('Lua search query: match, failed match, acceptance, cancellation and editor exit passed.')

DIAGNOSTIC_SCRIPT = r'''
local softline = require('softline')
local sl = softline.new()
local method, outcome = '@METHOD@', '@OUTCOME@'
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
'''

exit_keys = {'cancel': b'\x1b', 'cancel_hook': b'\x07', 'eof': b'\x15\x04', 'key_error': b'\x07'}
for method in ('readline', 'next_prompt'):
    for outcome in ('status', 'false', 'exception', *exit_keys):
        script = DIAGNOSTIC_SCRIPT.replace('@METHOD@', method).replace('@OUTCOME@', outcome)
        chat = Chat([sys.argv[1], '-e', script], dict(os.environ))
        try:
            chat.wait(b'\x1b[?2004h')
            chat.send(b'queued\t')
            chat.wait(b'QUEUE-ERROR-HANDLED')
            chat.send(exit_keys.get(outcome, b'\x15direct\r'))
            chat.wait(b'EDITOR-RESULT')
            deadline = time.monotonic() + 5
            while True:
                chat.pump(0.05)
                pid, status = os.waitpid(chat.pid, os.WNOHANG)
                if pid:
                    chat.reaped = True
                    assert os.waitstatus_to_exitcode(status) == 0, (method, outcome, bytes(chat.output))
                    assert b'EDITOR-DIAGNOSTIC-PASS' in chat.output
                    break
                assert time.monotonic() < deadline, (method, outcome, bytes(chat.output))
        finally:
            chat.close()
print('Lua history diagnostics: handled errors do not leak into submission, cancellation, EOF or key errors; current exceptions survive.')
