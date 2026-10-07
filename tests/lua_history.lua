local softline = require('softline')
local base = assert(arg[1]) .. '/lua-history-' .. tostring(os.time())
local function read(path)
  local f = assert(io.open(path, 'rb'))
  local value = f:read('*a')
  assert(f:close())
  return value
end
local sl = softline.new()
assert(not pcall(function() sl:history_open('nul\0key', base) end))
assert(not pcall(function() sl:history_add('nul\0prompt') end))
local appended = {}
local retained_emit
local snapshot = base .. '.snapshot'
local load_seen = false
assert(sl:history_set_backend('lua-key', {
  load = function(key, emit)
    assert(key == 'lua-key')
    assert(emit('older\nmultiline'))
    assert(emit('last\r\t\\n ☃'))
    retained_emit = emit
    load_seen = true
    return true
  end,
  append = function(key, prompt)
    assert(key == 'lua-key')
    local ok, code = sl:history_add('reentrant')
    assert(not ok and code == softline.ERROR_INVALID)
    ok, code = sl:history_close()
    assert(not ok and code == softline.ERROR_INVALID)
    assert(not pcall(function() sl:close() end))
    appended[#appended + 1] = prompt
    return true
  end,
}))
assert(load_seen and #appended == 0)
assert(not pcall(retained_emit, 'expired'))
assert(sl:history_add('new'))
assert(sl:history_add('new'))
assert(#appended == 1)
assert(sl:history_save(snapshot))
assert(read(snapshot) == 'older\\nmultiline\nlast\\r\\t\\\\n ☃\nnew\n')
assert(sl:history_load(snapshot) and #appended == 1)
local ok, code, message = sl:history_set_backend('broken', {
  load = function(_, emit)
    assert(emit('discard this import'))
    error('storage unavailable')
  end,
  append = function() error('must not attach') end,
})
assert(not ok and code == softline.ERROR and message:match('storage unavailable'))
assert(sl:history_add('old backend retained') and #appended == 2)
ok, code = sl:history_set_backend('invalid import', {
  load = function(_, emit)
    assert(not emit('nul\0byte'))
    return true
  end,
  append = function() return true end,
})
-- Invalid Lua strings must propagate a rejected import even if load ignores it.
assert(not ok and code == softline.ERROR_INVALID)
assert(sl:history_set_backend('throwing', {
  append = function() error('append failed') end,
}))
ok, code, message = sl:history_add('not committed')
assert(not ok and code == softline.ERROR and message:match('append failed'))
assert(sl:history_close())
assert(sl:history_add('detached'))
assert(sl:history_open('abc', base .. '/native'))
assert(sl:history_add('native\nline\r\t\\ ☃'))
local path = base .. '/native/ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad.history'
assert(read(path) == 'native\\nline\\r\\t\\\\ ☃\n')
local reader = softline.new()
assert(reader:history_open('abc', base .. '/native'))
assert(reader:history_save(snapshot))
assert(read(snapshot) == 'native\\nline\\r\\t\\\\ ☃\n')
assert(reader:history_add('other process'))
assert(sl:history_set_max_len(1))
assert(sl:history_compact())
assert(read(path) == 'other process\n')
assert(sl:history_set_auto_add(true))
assert(sl:history_close())
sl:close()
reader:close()
-- Hooks capturing their receiver are a GC-visible cycle, not registry roots.
local collected = setmetatable({}, {__mode = 'v'})
do
  local handle = softline.new()
  collected[1] = handle
  assert(handle:history_set_backend('cycle', {
    append = function() assert(handle:buffer()); return true end,
  }))
end
collectgarbage('collect')
collectgarbage('collect')
assert(collected[1] == nil)
-- Callbacks run in the coroutine calling the method, not the constructor's
-- state. A coroutine must not leave the backend busy after an append failure.
local shared = softline.new()
local caller, fail, calls = nil, false, 0
local coroutine_emit
local function append_in_caller(_, prompt)
  assert(coroutine.running() == caller)
  calls = calls + 1
  if fail then error('coroutine append failed') end
  assert(prompt ~= 'reentrant')
  local nested = coroutine.create(function()
    local result, status = shared:history_add('reentrant')
    assert(not result and status == softline.ERROR_INVALID)
  end)
  assert(coroutine.resume(nested))
  assert(coroutine.running() == caller)
  return true
end
caller = coroutine.create(function()
  assert(shared:history_set_backend('coroutine-key', {
    load = function(_, emit)
      assert(coroutine.running() == caller)
      coroutine_emit = emit
      assert(emit('coroutine-loaded'))
      return true
    end,
    append = append_in_caller,
  }))
  assert(shared:history_add('coroutine-added'))
  fail = true
  local result, status, diagnostic = shared:history_add('not committed')
  assert(not result and status == softline.ERROR)
  assert(diagnostic:match('coroutine append failed'))
  fail = false
  assert(shared:history_add('recovered'))
end)
assert(coroutine.resume(caller))
assert(not pcall(coroutine_emit, 'expired'))
caller = coroutine.running()
assert(shared:history_add('main-again'))
assert(calls == 4)
assert(shared:history_save(snapshot))
assert(read(snapshot) == 'coroutine-loaded\ncoroutine-added\nrecovered\nmain-again\n')
shared:close()
-- The creating coroutine may be collected before another caller uses hooks.
local orphan
do
  local creator = coroutine.create(function() orphan = softline.new() end)
  assert(coroutine.resume(creator))
end
collectgarbage('collect')
assert(orphan:history_set_backend('creator-gone', {
  append = function() assert(coroutine.running() == caller); return true end,
}))
assert(orphan:history_add('after creator collection'))
orphan:close()
print('Lua history: native storage, hooks, failures, imports, emitter lifetime and GC passed.')
