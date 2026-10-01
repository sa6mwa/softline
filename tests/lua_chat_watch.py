#!/usr/bin/env python3
import os
import pty
import re
import select
import signal
import subprocess
import sys
import termios
import time


def contains_output(data, needle, start=0):
    span = bytes(data[start:])
    if b"\x1b" not in needle:
        # These producer markers do not occur in the draft or status text.
        # A marker may span writes surrounded by cursor/style controls.
        span = re.sub(rb"\x1b(?:\[[0-?]*[ -/]*[@-~]|[78])", b"", span)
    return needle in span


def read_until(fd, data, needle, start=0, timeout=10.0):
    if contains_output(data, needle, start):
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
        if contains_output(data, needle, start):
            return
    raise AssertionError(f"missing {needle!r} in terminal output {bytes(data)!r}")


def test_queued_quit(root):
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
        os.write(master, b"followup\r/quit\r")
        read_until(master, output, b"Q 2. ")
        read_until(master, output, b"[queued] followup")
        if proc.wait(timeout=10.0) != 0:
            raise AssertionError(f"queued /quit failed: {bytes(output)!r}")
    finally:
        os.close(master)
        if proc.poll() is None:
            os.killpg(proc.pid, signal.SIGTERM)
            proc.wait(timeout=5.0)


def test_steered_quit(root):
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
        os.write(master, b"/quit\x1b\r")
        read_until(master, output, b"S 1. ")
        if proc.wait(timeout=10.0) != 0:
            raise AssertionError(f"steered /quit failed: {bytes(output)!r}")
    finally:
        os.close(master)
        if proc.poll() is None:
            os.killpg(proc.pid, signal.SIGTERM)
            proc.wait(timeout=5.0)


def test_promoted_quit(root):
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
        os.write(master, b"/quit\r")
        read_until(master, output, b"Q 1. ")
        cancel_offset = len(output)
        os.write(master, b"\x03")
        read_until(master, output, b"[operation] cancelled")
        read_until(master, output, b"\x1b[?2004h", cancel_offset)
        os.write(master, b"\x1b\r")
        if proc.wait(timeout=10.0) != 0:
            raise AssertionError(f"promoted /quit failed: {bytes(output)!r}")
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
        if contains_output(output, b"[queued] queued\r\n", ctrl_c_offset):
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
        read_until(master, output, b"steer", history_offset)
        if contains_output(output, b"start", history_offset):
            raise AssertionError("Up recalled an older turn instead of the delivered steer")
        os.write(master, b"\x15")
        os.write(master, b"/quit\r")
        if proc.wait(timeout=10.0) != 0:
            raise AssertionError("Lua watch chat exited unsuccessfully")
    finally:
        os.close(master)
        if proc.poll() is None:
            os.killpg(proc.pid, signal.SIGTERM)
            proc.wait(timeout=5.0)
    test_queued_quit(root)
    test_steered_quit(root)
    test_promoted_quit(root)
    test_eof_with_queued_work(root)


if __name__ == "__main__":
    main()
