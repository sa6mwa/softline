local softline = require("softline")

local sl = assert(softline.new())
local line, status = sl:readline("p> ")
if line ~= "hello" then
  error("readline mismatch: " .. tostring(line) .. " status=" .. tostring(status))
end
if sl:last_readline_status() ~= softline.READLINE_SUBMITTED then
  error("submitted status mismatch")
end
sl:close()

print("lua readline smoke passed")
