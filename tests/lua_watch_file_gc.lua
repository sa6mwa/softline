local softline = require("softline")

local sl = assert(softline.new())
local watch_id
local callback_called = false

watch_id = assert(sl:watch_add(io.popen("printf retained", "r"),
  softline.WATCH_READ, function()
    callback_called = true
    assert(sl:watch_remove(watch_id))
    assert(sl:submit())
  end))
collectgarbage("collect")

local line = assert(sl:next_prompt("watch> "))
assert(line == "", "watch submit result mismatch")
assert(callback_called, "temporary watched file was collected")
sl:close()

print("lua watched file retention passed")
