"""Run both Lua workflows with controlled Bootlin selection and host fallback."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
(root / "build").mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix="lua-selection.", dir=root / "build") as tmp:
    work = Path(tmp)
    scripts = work / "scripts"
    scripts.mkdir()
    commands = work / "commands"
    commands.mkdir()
    (work / "build/lua-sdk/lib/pkgconfig").mkdir(parents=True)

    def stub(path, body):
        path.write_text("#!/bin/sh\n" + body)
        path.chmod(0o755)

    for name in ("lua-test.sh", "lua-debug.sh"):
        shutil.copy2(root / "scripts" / name, scripts / name)
    for name in ("render_lua_rockspec.sh", "build-local-lua.sh", "lua-env.sh", "lua-debug-env.sh"):
        stub(scripts / name, "exit 0\n")
    stub(scripts / "release_version.sh", "echo 0.0.0\n")
    stub(scripts / "cpkt-toolchains.sh", '''
case "$1" in
  native-linux-target)
    [ "$SELECT_BOOTLIN" = 1 ] || exit 1
    echo x86_64-linux-gnu ;;
  ensure) exit 0 ;;
  env)
    [ "$ENV_FAIL" = 0 ] || exit 7
    echo 'export CC=fixture LD=fixture AR=fixture RANLIB=fixture CPKT_TOOLCHAIN_ROOT=fixture' ;;
esac
''')
    for name in ("cmake", "luarocks", "python3"):
        stub(commands / name, "exit 0\n")
    stub(commands / "cmake", 'if [ "$1" = --install ]; then mkdir -p "$4/lib/pkgconfig"; fi\n')
    stub(commands / "uname", "echo Linux\n")
    stub(commands / "lua", '''
printf '%s\\n' "$1" >> "$LUA_CALLS"
case "$1" in
  */lua_runtime.lua) exit "$RUNTIME_FAIL" ;;
  */chat.lua) echo '[turn] hello' ;;
esac
''')
    calls = work / "calls"
    for runner in ("lua-test.sh", "lua-debug.sh"):
        for selected, runtime_fail, env_fail in ((0, 9, 0), (1, 0, 0), (1, 9, 0), (1, 0, 1)):
            calls.write_text("")
            env = {**os.environ, "PATH": str(commands) + os.pathsep + os.environ["PATH"],
                   "SELECT_BOOTLIN": str(selected), "RUNTIME_FAIL": str(runtime_fail),
                   "ENV_FAIL": str(env_fail), "LUA_CALLS": str(calls),
                   "CPKT_TOOLCHAIN_ROOT": "inherited-stale-root", "SOFTLINE_LUA_BOOTLIN": "1"}
            result = subprocess.run([str(scripts / runner)], env=env, capture_output=True, text=True)
            expected = 7 if env_fail else runtime_fail if selected else 0
            assert result.returncode == expected, (runner, selected, result)
            invoked = calls.read_text()
            assert ("lua_runtime.lua" in invoked) == bool(selected and not env_fail), invoked
            if expected == 0:
                assert "lua_smoke.lua" in invoked and "chat.lua" in invoked, invoked
print("Both Lua workflows: fallback, selected runtime, assertion failure, and resolver failure passed")
