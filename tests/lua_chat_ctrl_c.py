#!/usr/bin/env python3
import os
import pty
import select
import signal
import subprocess
import sys
import termios
import time


def read_until(fd, needle, timeout=5.0):
    data = bytearray()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        ready, _, _ = select.select([fd], [], [], 0.05)
        if not ready:
            continue
        try:
            chunk = os.read(fd, 4096)
        except OSError:
            break
        if not chunk:
            break
        data.extend(chunk)
        if needle in data:
            return bytes(data)
    raise AssertionError(f"missing {needle!r} in terminal output {bytes(data)!r}")


def main():
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {sys.argv[0]} REPO_ROOT")
    root = sys.argv[1]
    master, slave = pty.openpty()
    attrs = termios.tcgetattr(slave)
    attrs[3] = attrs[3] & ~termios.ECHO
    termios.tcsetattr(slave, termios.TCSANOW, attrs)
    proc = subprocess.Popen(
        ["lua", os.path.join(root, "examples", "chat.lua")],
        stdin=slave,
        stdout=slave,
        stderr=slave,
        cwd=root,
        start_new_session=True,
    )
    os.close(slave)
    try:
        read_until(master, b"chat> ")
        os.write(master, b"\x03")
        read_until(master, b"[cancelled]")
        time.sleep(0.1)
        os.write(master, b"exit\r")
        read_until(master, b"\x1b[?1049l")
        status = proc.wait(timeout=5.0)
        if status != 0:
            raise AssertionError(f"lua chat exited with status {status}")
    finally:
        try:
            os.close(master)
        except OSError:
            pass
        if proc.poll() is None:
            os.killpg(proc.pid, signal.SIGTERM)
            proc.wait(timeout=5.0)


if __name__ == "__main__":
    main()
