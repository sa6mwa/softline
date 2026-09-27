#!/usr/bin/env python3
"""Exercise the Lua live output facade while an interactive draft is edited."""

import fcntl
import os
import pty
import re
import select
import signal
import struct
import subprocess
import sys
import termios
import time


def terminal_cursor(data, height=8, width=40):
    """Interpret cursor movement for this fixed-size ASCII PTY scenario."""
    row, col, top, bottom = 0, 0, 0, height - 1
    for token in re.findall(rb"\x1b\[[0-?]*[ -/]*[@-~]|[^\x1b]", data):
        if token.startswith(b"\x1b["):
            parameters = token[2:-1]
            if parameters.startswith(b"?"):
                continue
            values = [int(value or b"0") for value in parameters.split(b";")]
            count = values[0] or 1
            if token[-1:] in (b"H", b"f"):
                row = min(height - 1, count - 1)
                col = min(width - 1, (values[1] or 1) - 1 if len(values) > 1 else 0)
            elif token[-1:] == b"A":
                row = max(top if top <= row <= bottom else 0, row - count)
            elif token[-1:] == b"B":
                row = min(bottom if top <= row <= bottom else height - 1, row + count)
            elif token[-1:] == b"C":
                col = min(width - 1, col + count)
            elif token[-1:] == b"D":
                col = max(0, col - count)
            elif token[-1:] == b"r":
                top = count - 1
                bottom = (values[1] or height) - 1 if len(values) > 1 else height - 1
                row, col = 0, 0
        elif token == b"\r":
            col = 0
        elif token == b"\n":
            row = min(bottom if top <= row <= bottom else height - 1, row + 1)
        elif token >= b" ":
            if col == width:
                row = min(bottom if top <= row <= bottom else height - 1, row + 1)
                col = 0
            col += 1
    return row, col


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


def exercise(root, clear):
    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 8, 40, 0, 0))
    proc = subprocess.Popen(
        ["lua", os.path.join(root, "tests", "lua_live_output.lua"),
         "clear" if clear else "keep"],
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
        exit_mark = len(output)
        os.write(master, b"\r")
        read_until(master, output, b"RESULT:draft")
        if proc.wait(timeout=5.0) != 0:
            raise AssertionError("Lua live output child failed")
        ending = bytes(output[exit_mark:])
        before_result = bytes(output[:output.index(b"RESULT:")])
        source_end = output.index(b"second") + len(b"second")
        source_row, source_col = terminal_cursor(bytes(output[:source_end]))
        expected_row = min(7, source_row + bool(source_col)) if clear else 7
        if b"\x1b[r" not in ending or terminal_cursor(before_result) != (expected_row, 0):
            raise AssertionError("Lua exit did not restore the full region and expected cursor position")
        if b"\x1b7" in output or b"\x1b8" in output:
            raise AssertionError("Lua native output used terminal cursor save/restore")
        if not clear and b"\n" in ending[:ending.index(b"RESULT:")]:
            raise AssertionError("Lua exit advanced or scrolled the input row")
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
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {sys.argv[0]} REPO_ROOT")
    exercise(sys.argv[1], False)
    exercise(sys.argv[1], True)
