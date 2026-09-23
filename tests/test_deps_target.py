"""Exercise dependency dispatch without downloading SDKs."""

import os
import pathlib
import shutil
import subprocess
import sys
import tempfile


root = pathlib.Path(sys.argv[1]).resolve()
(root / "build").mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix="deps-target.", dir=root / "build") as tmp:
    fixture = pathlib.Path(tmp)
    (fixture / "bin").mkdir()
    (fixture / "scripts").mkdir()
    shutil.copy2(root / "Makefile", fixture / "Makefile")
    cmake = fixture / "bin" / "cmake"
    cmake.write_text('#!/bin/sh\nprintf "cmake %s\\n" "$*" >> "$CALL_LOG"\n')
    cmake.chmod(0o755)
    lua = fixture / "scripts" / "build-local-lua.sh"
    lua.write_text('#!/bin/sh\nprintf "lua %s\\n" "$*" >> "$CALL_LOG"\n')
    calls = fixture / "calls"
    env = os.environ.copy()
    env["PATH"] = str(fixture / "bin") + os.pathsep + env["PATH"]
    env["CALL_LOG"] = str(calls)

    def check(arguments, expected, success=True):
        calls.write_text("")
        result = subprocess.run(
            ["make", "deps", *arguments],
            cwd=fixture,
            env=env,
            capture_output=True,
            text=True,
        )
        assert (result.returncode == 0) == success, result.stdout + result.stderr
        assert calls.read_text().splitlines() == expected, result.stdout + result.stderr

    check(["DEPENDENCY=libmdf"], ["cmake --preset debug"])
    check(
        ["DEPENDENCY=lua", "PRESET=debug-lua"],
        ["cmake --preset debug-lua", f"lua {fixture}/build/debug-lua"],
    )
    check(
        ["DEPENDENCY=lua"],
        ["cmake --preset debug", f"lua {fixture}/build/debug"],
    )
    check(["DEPENDENCY=libmdf", "PRESET=debug-lua"], [], False)
    check([], [], False)

print("Dependency target dispatch tests passed.")
