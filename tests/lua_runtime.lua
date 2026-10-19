local softline = require("softline")
assert(softline.new())
local maps = assert(io.open("/proc/self/maps")):read("a")
local runtime_root = assert(os.getenv("SOFTLINE_TOOLCHAIN_ROOT"))
local library_root = assert(os.getenv("SOFTLINE_EXPECTED_LIBRARY_ROOT"))
local libc = false
local shared = false
for line in maps:gmatch("[^\n]+") do
  if line:find("/libsoftline.so", 1, true) then
    assert(line:find(library_root .. "/", 1, true), line)
    shared = true
  end
  if line:find("libc.so", 1, true) then
    assert(line:find(runtime_root, 1, true), line)
    libc = true
  end
  if line:match("%.so[%d%.]*$") and not line:find("softline", 1, true) then
    assert(line:find(runtime_root, 1, true), line)
  end
end
assert(libc, "Lua process did not expose its libc mapping")
assert(shared, "Lua facade did not load the public shared libsoftline")
local child = assert(io.popen("cat /proc/self/maps"))
local host_maps = child:read("a")
assert(child:close())
assert(not host_maps:find(runtime_root, 1, true), host_maps)
assert(host_maps:find("libc.so", 1, true), host_maps)
print("Lua uses Bootlin libc; host subprocess runtime is independent")
