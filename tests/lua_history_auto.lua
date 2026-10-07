local softline = require('softline')
local sl = softline.new()
local caller
local calls = 0
assert(sl:history_set_backend('auto', {
  append = function(_, prompt)
    assert(coroutine.running() == caller)
    calls = calls + 1
    assert(prompt == (calls == 1 and 'auto' or 'readline'))
    error('automatic append failed')
  end,
}))
assert(sl:history_set_auto_add(true))
caller = coroutine.create(function()
  local line, status, message = sl:next_prompt()
  assert(line == nil and status == softline.READLINE_ERROR)
  assert(message:match('automatic append failed'))
  line, status, message = sl:readline()
  assert(line == nil and status == softline.READLINE_ERROR)
  assert(message:match('automatic append failed'))
end)
assert(coroutine.resume(caller))
assert(calls == 2)
sl:close()
print('Lua automatic history failures preserve callback diagnostics.')
