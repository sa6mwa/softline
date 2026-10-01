local softline = require("softline")

local sl = assert(softline.new({clear_prompt_on_exit = arg[1] == "clear"}))
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
        assert(sl:set_quoted_prompt_prefix("?? "))
        assert(sl:set_quoted_prompt_style({prefix = {1, 2, 3},
            text = {4, 5, 6}}))
        assert(sl:output_stream_write_quoted_prompt("*literal*"))
        assert(sl:set_quoted_prompt_style(nil))
        assert(sl:set_quoted_prompt_prefix(nil))
        assert(sl:output_stream_end())
        for _, partial in ipairs({"\27[", "\226\130"}) do
          local ok, err = sl:print_above(partial)
          assert(not ok and err, "incomplete finite output was accepted")
          assert(sl:print_above("recovered"))
        end
        assert(sl:output_stream_begin())
      elseif byte == "B" then
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
