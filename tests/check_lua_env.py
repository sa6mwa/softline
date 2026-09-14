"""Environment helpers must never silently select an ambient Lua interpreter."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
(root / "build").mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix="lua-env-test.", dir=root / "build") as tmp:
    work = Path(tmp) / "fixture with spaces"
    scripts = work / "scripts"
    scripts.mkdir(parents=True)
    host = work / "host"
    host.mkdir()
    marker = work / "host-used"
    (host / "lua").write_text('#!/bin/sh\ntouch "$HOST_MARKER"\n')
    (host / "lua").chmod(0o755)
    rocks = host / "luarocks"
    rocks.write_text("#!/bin/sh\nprintf '%s\\n' 'export LUA_PATH=fixture' 'export LUA_CPATH=fixture'\n")
    rocks.chmod(0o755)
    env = {**os.environ, "PATH": str(host) + os.pathsep + os.environ["PATH"],
           "HOST_MARKER": str(marker)}
    for script, directory, recovery in (("lua-env.sh", "local-lua", "make lua-test"),
                                         ("lua-debug-env.sh", "local-lua-debug", "make lua-debug-test")):
        helper = scripts / script
        shutil.copy2(root / "scripts" / script, helper)
        binary = work / "build" / directory / "lua"
        binary.parent.mkdir(parents=True)

        def run():
            return subprocess.run([str(helper)], cwd=host, env=env,
                                  capture_output=True, text=True)

        for state in ("missing", "not-executable", "directory", "broken-symlink"):
            if state == "not-executable":
                binary.write_text("not executable")
            elif state == "directory":
                binary.mkdir()
            elif state == "broken-symlink":
                binary.symlink_to(binary.parent / "absent")
            result = run()
            assert result.returncode and not result.stdout, (state, result)
            assert recovery in result.stderr, result.stderr
            # The documented assignment-and-eval form must stop before ambient lua.
            shell = subprocess.run(['sh', '-c', 'exports="$("$1")" && eval "$exports" && lua',
                                    'check', str(helper)], env=env, capture_output=True)
            assert shell.returncode and not marker.exists(), state
            if binary.is_dir():
                binary.rmdir()
            elif binary.exists() or binary.is_symlink():
                binary.unlink()
        binary.write_text("#!/bin/sh\nexit 0\n")
        binary.chmod(0o755)
        result = run()
        assert result.returncode == 0, result.stderr
        selected = subprocess.check_output(['sh', '-c', 'eval "$1"; command -v lua',
                                            'check', result.stdout], env=env, text=True).strip()
        assert selected == str(binary), selected
        saved = rocks.read_text()
        rocks.write_text("#!/bin/sh\necho partial-output\nexit 7\n")
        result = run()
        assert result.returncode == 7 and not result.stdout, result
        rocks.write_text(saved)
print("Both Lua environments: missing/non-executable/directory/broken-link rejection, local selection, and LuaRocks failure passed")
