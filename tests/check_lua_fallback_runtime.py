"""Exercise fallback RPATH policy with the real Lua interpreter and module.

The fixture disables native runtime selection but keeps a runnable Bootlin
loader on this x86-64 test host. No target code uses the host libc.
"""
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
if platform.system() != "Linux" or platform.machine().lower() not in ("x86_64", "amd64"):
    print("SKIP: fallback Lua runtime fixture requires native x86_64 Linux")
    raise SystemExit(0)
description = subprocess.check_output([str(root / "scripts/cpkt-toolchains.sh"),
                                       "discover", "x86_64-linux-gnu"], text=True)
tools = dict(line.split("=", 1) for line in description.splitlines() if "=" in line)
with tempfile.TemporaryDirectory(prefix="lua-fallback-runtime.", dir=root / "build") as tmp:
    work = Path(tmp)
    libraries = work / "private-libraries"
    libraries.mkdir()
    sdk_lib = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "build/lua-sdk/lib"
    shutil.copy2(sdk_lib / "libsoftline.so.1", libraries / "libsoftline.so.1")
    module = work / "softline.so"
    shutil.copy2(root / "build/luarocks/lib/lua/5.5/softline.so", module)
    fallback = work / "fallback.cmake"
    fallback.write_text('set(SL_TARGET_ID "")\n')
    loader = tools["sysroot"] + "/lib/ld-linux-x86-64.so.2"
    build = work / "lua-build"
    subprocess.run(["cmake", "-S", str(root / "cmake/local-lua"), "-B", str(build), "-G", "Ninja",
                    "-DCMAKE_TOOLCHAIN_FILE=" + str(root / "cmake/toolchains/bootlin-linux.cmake"),
                    "-DCMAKE_PROJECT_INCLUDE=" + str(fallback),
                    # Model fallback distributions whose linker defaults to new dtags.
                    "-DCMAKE_EXE_LINKER_FLAGS=-Wl,--enable-new-dtags,--dynamic-linker," + loader,
                    "-DSOFTLINE_LOCAL_LIBRARY_DIR=" + str(libraries) + ";" + tools["sysroot"] + "/lib"],
                   check=True)
    subprocess.run(["cmake", "--build", str(build)], check=True)
    env = {key: value for key, value in os.environ.items() if not key.startswith("LUA_")
           and key not in ("LD_LIBRARY_PATH", "LD_PRELOAD")}
    env["LUA_CPATH"] = str(work / "?.so")
    script = 'local s=require("softline"); local h=assert(s.new()); h:close()'
    subprocess.run([str(build / "lua"), "-e", script], env=env, check=True)
    metadata = subprocess.check_output([tools["readelf"], "-d", str(build / "lua")], text=True)
    assert "(RPATH)" in metadata and "(RUNPATH)" not in metadata, metadata
    assert "--dynamic-linker" not in (build / "lua-runtime.flags").read_text()
    # Prove the load cannot be satisfied by a system-installed softline.
    (libraries / "libsoftline.so.1").rename(libraries / "hidden-library")
    missing = subprocess.run([str(build / "lua"), "-e", script], env=env,
                             capture_output=True, text=True)
    assert missing.returncode and "libsoftline.so.1" in missing.stderr, missing
print("Fallback Linux policy loads the real Lua module through private transitive RPATH")
