#!/usr/bin/env python3
import os
import pty
import select
import signal
import subprocess
import sys
import time


def main():
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {sys.argv[0]} REPO_ROOT")
    root = sys.argv[1]
    master, slave = pty.openpty()
    proc = subprocess.Popen(
        ["lua", os.path.join(root, "tests", "lua_watch_file_gc.lua")],
        stdin=slave,
        stdout=slave,
        stderr=slave,
        cwd=root,
        start_new_session=True,
    )
    os.close(slave)
    try:
        output = bytearray()
        deadline = time.monotonic() + 10.0
        while time.monotonic() < deadline:
            ready, _, _ = select.select([master], [], [], 0.05)
            if not ready:
                continue
            try:
                chunk = os.read(master, 4096)
            except OSError:
                break
            if not chunk:
                break
            output.extend(chunk)
            if b"lua watched file retention passed" in output:
                break
        if b"lua watched file retention passed" not in output:
            raise AssertionError(
                f"Lua watched-file test did not finish: {bytes(output)!r}"
            )
        if proc.wait(timeout=5.0) != 0:
            raise AssertionError("Lua watched-file test exited unsuccessfully")
    finally:
        os.close(master)
        if proc.poll() is None:
            os.killpg(proc.pid, signal.SIGTERM)
            proc.wait(timeout=5.0)


if __name__ == "__main__":
    main()
