local softline = require("softline")
local sl = assert(softline.new())

assert(sl:set_idle_callback(function()
  local ok, err = pcall(function() sl:close() end)
  assert(not ok and string.find(err, "cannot close"), "close was not rejected")
  error("intentional idle callback failure")
end))

local line, status = sl:readline("p> ")
assert(line == nil, "idle callback unexpectedly submitted text")
assert(status == softline.READLINE_ERROR, "idle callback failure was not an error")
assert(sl:last_readline_status() == softline.READLINE_ERROR,
       "last readline status hid the idle callback failure")
assert(string.find(sl:last_error() or "", "intentional idle callback failure"),
       "idle callback error text was not retained")
sl:close()
print("lua idle callback smoke passed")
