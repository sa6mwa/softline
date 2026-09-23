local softline = require("softline")

local sl = assert(softline.new())
assert(sl:set_bounds(2, 1, 30, 6))
assert(sl:output_stream_begin())

-- The producer owns its process and fd. Its one-byte events are consumed on
-- the Lua/editor thread; no foreign thread invokes Lua or the Softline handle.
local producer = assert(io.popen("sh -c 'printf A; sleep 0.5; printf B'", "r"))
local watch
watch = assert(sl:watch_add(producer,
    softline.WATCH_READ | softline.WATCH_HANGUP | softline.WATCH_ERROR,
    function()
      local byte = producer:read(1)
      if byte == "A" then
        assert(sl:output_stream_write("first"))
      elseif byte == "B" then
        assert(sl:set_bounds(4, 1, 24, 6))
        assert(sl:set_screen_width(24))
        assert(sl:output_stream_write(" second"))
      elseif byte == nil then
        assert(sl:watch_remove(watch))
        assert(producer:close())
      else
        error("unexpected producer byte")
      end
    end))

local line = assert(sl:readline("lua> "))
assert(sl:output_stream_end())
sl:close()
io.write("RESULT:", line, "\n")
