local softline = require("softline")

local function assert_eq(actual, expected, label)
  if actual ~= expected then
    error(label .. ": expected " .. tostring(expected) .. ", got " .. tostring(actual), 2)
  end
end

local sl = assert(softline.new({ line_max_len = 32 }))
assert_eq(sl:last_readline_status(), softline.READLINE_NONE, "initial status")
assert(sl:history_add("history entry"))
assert(sl:set_buffer("draft"))
assert_eq(sl:buffer(), "draft", "buffer")
assert(sl:set_cursor(2))
assert_eq(sl:cursor(), 2, "cursor")
assert(sl:insert("++"))
assert_eq(sl:buffer(), "dr++aft", "insert")
assert(sl:set_buffer("å"))
assert(sl:set_cursor(1))
assert_eq(sl:cursor(), 0, "UTF-8 cursor clamp")
assert(sl:insert("x"))
assert_eq(sl:buffer(), "xå", "UTF-8 cursor insert")
assert(sl:set_idle_callback(function() end))
assert(sl:set_idle_callback(nil))
assert(sl:print_above({ "alpha", "-", "beta\n" }))
assert(sl:print_above(function(i)
  if i == 1 then
    return "gamma\n"
  end
  return nil
end))
sl:close()

local bounded = assert(softline.new({ bounded = true, screen_height = 1 }))
local ok = bounded:print_above("should not fit\n")
assert_eq(ok, nil, "bounded config should affect print_above")
bounded:close()

print("lua softline smoke passed")
