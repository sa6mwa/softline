local softline = require("softline")
assert(softline.new())
local maps = assert(io.open("/proc/self/maps")):read("a")
local runtime_root = assert(os.getenv("CPKT_TOOLCHAIN_ROOT"))
local libc = false
for line in maps:gmatch("[^\n]+") do
  if line:find("libc.so", 1, true) then
    assert(line:find(runtime_root, 1, true), line)
    libc = true
  end
  if line:match("%.so[%d%.]*$") and not line:find("softline", 1, true) then
    assert(line:find(runtime_root, 1, true), line)
  end
end
assert(libc, "Lua process did not expose its libc mapping")
local child = assert(io.popen("cat /proc/self/maps"))
local host_maps = child:read("a")
assert(child:close())
assert(not host_maps:find("c.pkt.systems", 1, true), host_maps)
assert(host_maps:find("libc.so", 1, true), host_maps)
print("Lua uses Bootlin libc; host subprocess runtime is independent")
