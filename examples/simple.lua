local softline = require("softline")

local sl = softline.new()

while true do
  local line, status = sl:readline("softline> ")
  if not line then
    if status == softline.READLINE_EOF then
      break
    end
    local err = sl:last_error()
    if err then
      io.stderr:write(err, "\n")
    end
    break
  end
  if line == "exit" then
    break
  end
  io.write("submitted: ", line, "\n")
  sl:history_add(line)
end

sl:close()
