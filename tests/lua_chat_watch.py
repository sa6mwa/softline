#!/usr/bin/env python3
import os
import pty
import select
import signal
import subprocess
import sys
import termios
import time


def read_until(fd, data, needle, start=0, timeout=10.0):
    if data.find(needle, start) >= 0:
        return
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
        if data.find(needle, start) >= 0:
            return
    raise AssertionError(f"missing {needle!r} in terminal output {bytes(data)!r}")


def test_queued_exit(root):
    master, slave = pty.openpty()
    attrs = termios.tcgetattr(slave)
    attrs[3] &= ~termios.ECHO
    termios.tcsetattr(slave, termios.TCSANOW, attrs)
    proc = subprocess.Popen(
        ["lua", os.path.join(root, "examples", "chat.lua")],
        stdin=slave,
        stdout=slave,
        stderr=slave,
        cwd=root,
        env={**os.environ, "SOFTLINE_CHAT_OPERATION_STEP_MS": "150"},
        start_new_session=True,
    )
    os.close(slave)
    try:
        output = bytearray()
        read_until(master, output, b"> ")
        os.write(master, b"start\r")
        read_until(master, output, b"[turn] start\r\n")
        os.write(master, b"exit\r")
        read_until(master, output, b"Q 1.")
        if proc.wait(timeout=10.0) != 0:
            raise AssertionError(f"queued exit failed: {bytes(output)!r}")
    finally:
        os.close(master)
        if proc.poll() is None:
            os.killpg(proc.pid, signal.SIGTERM)
            proc.wait(timeout=5.0)


def test_eof_with_queued_work(root):
    master, slave = pty.openpty()
    attrs = termios.tcgetattr(slave)
    attrs[3] &= ~termios.ECHO
    termios.tcsetattr(slave, termios.TCSANOW, attrs)
    proc = subprocess.Popen(
        ["lua", os.path.join(root, "examples", "chat.lua")],
        stdin=slave,
        stdout=slave,
        stderr=slave,
        cwd=root,
        env={**os.environ, "SOFTLINE_CHAT_OPERATION_STEP_MS": "150"},
        start_new_session=True,
    )
    os.close(slave)
    try:
        output = bytearray()
        read_until(master, output, b"> ")
        os.write(master, b"start\r")
        read_until(master, output, b"[turn] start\r\n")
        os.write(master, b"followup\r")
        read_until(master, output, b"Q 1.")
        eof_offset = len(output)
        os.write(master, b"\x04")
        if proc.wait(timeout=10.0) != 0:
            raise AssertionError("Lua chat failed to exit on EOF")
        while True:
            ready, _, _ = select.select([master], [], [], 0.1)
            if not ready:
                break
            try:
                chunk = os.read(master, 4096)
            except OSError:
                break
            if not chunk:
                break
            output.extend(chunk)
        if b"[queued] followup" in output[eof_offset:]:
            raise AssertionError("shutdown dispatched queued work")
    finally:
        os.close(master)
        if proc.poll() is None:
            os.killpg(proc.pid, signal.SIGTERM)
            proc.wait(timeout=5.0)


def main():
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {sys.argv[0]} REPO_ROOT")
    root = sys.argv[1]
    master, slave = pty.openpty()
    attrs = termios.tcgetattr(slave)
    attrs[3] &= ~termios.ECHO
    termios.tcsetattr(slave, termios.TCSANOW, attrs)
    proc = subprocess.Popen(
        ["lua", os.path.join(root, "examples", "chat.lua")],
        stdin=slave,
        stdout=slave,
        stderr=slave,
        cwd=root,
        env={**os.environ, "SOFTLINE_CHAT_OPERATION_STEP_MS": "150"},
        start_new_session=True,
    )
    os.close(slave)
    try:
        output = bytearray()
        read_until(master, output, b"> ")
        os.write(master, b"start\r")
        start_offset = len(output)
        read_until(master, output, b"[turn] start\r\n", start_offset)
        read_until(master, output, b"\x1b[?2004h", start_offset)
        os.write(master, b"queued\r")
        queued_offset = len(output)
        read_until(master, output, b"Q 1.", queued_offset)
        ctrl_c_offset = len(output)
        os.write(master, b"\x03")
        read_until(master, output, b"[operation] cancelled\r\n", ctrl_c_offset)
        read_until(master, output, b"\x1b[?2004h", ctrl_c_offset)
        if output.find(b"[queued] queued\r\n", ctrl_c_offset) >= 0:
            raise AssertionError("cancellation automatically dispatched queued work")
        os.write(master, b"\x1b\r")
        promote_offset = len(output)
        read_until(master, output, b"[promoted] queued\r\n", promote_offset)
        read_until(master, output, b"[operation] processing input.", promote_offset)
        os.write(master, b"steer\x1b\r")
        steer_offset = len(output)
        read_until(master, output, b"S 1. ", steer_offset)
        read_until(master, output, b"[active operation] consumed: steer", steer_offset)
        escape_offset = len(output)
        os.write(master, b"\x1b")
        read_until(master, output, b"[operation] cancelled\r\n", escape_offset)
        read_until(master, output, b"\x1b[?2004h", escape_offset)
        history_offset = len(output)
        os.write(master, b"\x1b[A")
        read_until(master, output, b"\x1b[0msteer", history_offset)
        if b"\x1b[0mstart" in output[history_offset:]:
            raise AssertionError("Up recalled an older turn instead of the delivered steer")
        os.write(master, b"\x15")
        os.write(master, b"exit\r")
        if proc.wait(timeout=10.0) != 0:
            raise AssertionError("Lua watch chat exited unsuccessfully")
    finally:
        os.close(master)
        if proc.poll() is None:
            os.killpg(proc.pid, signal.SIGTERM)
            proc.wait(timeout=5.0)
    test_queued_exit(root)
    test_eof_with_queued_work(root)


if __name__ == "__main__":
    main()
