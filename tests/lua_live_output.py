#!/usr/bin/env python3
"""Exercise the Lua live output facade while an interactive draft is edited."""

import fcntl
import os
import pty
import select
import signal
import struct
import subprocess
import sys
import termios
import time


def read_until(fd, data, needle, timeout=8.0):
    deadline = time.monotonic() + timeout
    while needle not in data and time.monotonic() < deadline:
        ready, _, _ = select.select([fd], [], [], 0.05)
        if ready:
            chunk = os.read(fd, 4096)
            if not chunk:
                break
            data.extend(chunk)
    if needle not in data:
        raise AssertionError(f"missing {needle!r} in {bytes(data)!r}")


def main():
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {sys.argv[0]} REPO_ROOT")
    root = sys.argv[1]
    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 8, 40, 0, 0))
    proc = subprocess.Popen(
        ["lua", os.path.join(root, "tests", "lua_live_output.lua")],
        stdin=slave,
        stdout=slave,
        stderr=slave,
        cwd=root,
        start_new_session=True,
    )
    os.close(slave)
    output = bytearray()
    try:
        read_until(master, output, b"first")
        if b"second" in output:
            raise AssertionError("second fragment arrived before its producer event")
        os.write(master, b"draft")
        read_until(master, output, b"second")
        os.write(master, b"\r")
        read_until(master, output, b"RESULT:draft")
        if proc.wait(timeout=5.0) != 0:
            raise AssertionError("Lua live output child failed")
        if output.index(b"first") > output.index(b"second"):
            raise AssertionError("stream fragment order reversed")
        if (b"\x1b[2m\x1b[38;2;1;2;3m" not in output
                or b"\x1b[3m\x1b[38;2;4;5;6m" not in output):
            raise AssertionError("Lua quoted prompt did not apply its custom styles")
    finally:
        os.close(master)
        if proc.poll() is None:
            os.killpg(proc.pid, signal.SIGTERM)
            proc.wait(timeout=5.0)


if __name__ == "__main__":
    main()
