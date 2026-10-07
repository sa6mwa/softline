"""Exercise shell handoff in either chat example on a private controlling PTY."""

import errno
import fcntl
import hashlib
import os
import pathlib
import re
import select
import shlex
import signal
import struct
import subprocess
import sys
import tempfile
import termios
import time


class Chat:
    def __init__(self, command, env):
        import pty

        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.execvpe(command[0], command, env)
        self.rows, self.cols = 24, 100
        self.row, self.col = 0, 0
        self.top, self.bottom = 0, self.rows - 1
        self.escape = bytearray()
        self.output = bytearray()
        self.printed = bytearray()
        self.locations = []
        self.reaped = False
        self.resize(self.rows, self.cols)

    def resize(self, rows, cols):
        fcntl.ioctl(self.fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
        self.rows, self.cols = rows, cols
        self.row, self.col = min(self.row, rows - 1), min(self.col, cols - 1)
        self.top, self.bottom = 0, rows - 1

    def consume(self, chunk):
        # Interpret only cursor/margin controls needed to answer real CPR
        # requests and assert prompt placement. No external emulator dependency.
        for byte in chunk:
            if self.escape:
                self.escape.append(byte)
                if len(self.escape) == 2 and byte != ord("["):
                    if byte == ord("D"):
                        self.row = min(self.bottom, self.row + 1)
                    self.escape.clear()
                elif len(self.escape) > 2 and 0x40 <= byte <= 0x7e:
                    parameters = bytes(self.escape[2:-1])
                    if not parameters.startswith(b"?"):
                        values = [int(p or b"0") for p in parameters.split(b";")]
                        count = values[0] or 1
                        if byte in (ord("H"), ord("f")):
                            self.row = min(self.rows - 1, count - 1)
                            self.col = min(self.cols - 1, (values[1] or 1) - 1 if len(values) > 1 else 0)
                        elif byte == ord("A"):
                            self.row = max(0, self.row - count)
                        elif byte == ord("B"):
                            self.row = min(self.rows - 1, self.row + count)
                        elif byte == ord("C"):
                            self.col = min(self.cols - 1, self.col + count)
                        elif byte == ord("D"):
                            self.col = max(0, self.col - count)
                        elif byte == ord("r"):
                            self.top = count - 1
                            self.bottom = (values[1] or self.rows) - 1 if len(values) > 1 else self.rows - 1
                            self.row = self.col = 0
                        elif byte == ord("n") and count == 6:
                            self.send(f"\x1b[{self.row + 1};{min(self.col + 1, self.cols)}R".encode())
                    self.escape.clear()
            elif byte == 27:
                self.escape.append(byte)
            elif byte == 13:
                self.col = 0
            elif byte == 10:
                self.row = min(self.bottom if self.top <= self.row <= self.bottom else self.rows - 1, self.row + 1)
            elif 32 <= byte < 127:
                if self.col >= self.cols:
                    self.row = min(self.bottom, self.row + 1)
                    self.col = 0
                self.printed.append(byte)
                self.locations.append((self.row, self.col))
                self.col += 1

    def pump(self, seconds=0.1):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            ready, _, _ = select.select([self.fd], [], [], max(0, deadline - time.monotonic()))
            if not ready:
                break
            try:
                chunk = os.read(self.fd, 65536)
            except OSError as exc:
                if exc.errno == errno.EIO:
                    break
                raise
            if not chunk:
                break
            self.output.extend(chunk)
            self.consume(chunk)

    def wait(self, needle, start=0, timeout=10):
        deadline = time.monotonic() + timeout
        def visible():
            data = bytes(self.output[start:])
            return data if b"\x1b" in needle else re.sub(rb"\x1b\[[0-?]*[ -/]*[@-~]", b"", data)

        while needle not in visible():
            assert time.monotonic() < deadline, f"missing {needle!r}: {bytes(self.output[-3000:])!r}"
            self.pump(0.05)

    def send(self, data):
        os.write(self.fd, data)

    def prompt(self, start=0):
        self.wait(b"\x1b[?2004h", start)
        self.pump()
        assert self.row == self.rows - 1, (self.row, self.rows, bytes(self.output[-2000:]))
        attrs = termios.tcgetattr(self.fd)
        assert not attrs[3] & termios.ICANON, "editor did not regain raw input"
        assert self.bottom < self.rows - 1, "editor did not reserve prompt rows"

    def finish(self):
        self.send(b"/quit\r")
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            self.pump(0.05)
            pid, status = os.waitpid(self.pid, os.WNOHANG)
            if pid:
                self.reaped = True
                assert os.waitstatus_to_exitcode(status) == 0, bytes(self.output[-2000:])
                attrs = termios.tcgetattr(self.fd)
                assert attrs[3] & termios.ICANON and attrs[3] & termios.ECHO
                assert self.top == 0 and self.bottom == self.rows - 1
                return
        raise AssertionError("chat did not exit")

    def close(self):
        if not self.reaped:
            # Capture the foreground shell group too, if job control split it.
            foreground = os.tcgetpgrp(self.fd)
            for group in {self.pid, foreground}:
                if group > 0:
                    try:
                        os.killpg(group, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
            os.waitpid(self.pid, 0)
        os.close(self.fd)


def exercise(command, env, fake_shell):
    chat = Chat(command, dict(env, SHELL=str(fake_shell)))
    try:
        chat.prompt()
        mark = len(chat.output)
        chat.send(b"!sh\t")
        chat.wait(b"Use Enter to open the shell", mark)
        assert b"SHELL_READY" not in chat.output[mark:]
        chat.send(b"\x15")
        for alias, key in ((b"!sh", b"\r"), (b"/shell", b"\x1b\r")):
            start = len(chat.output)
            chat.send(alias + key)
            chat.wait(b"SHELL_READY -i cooked foreground", start)
            assert chat.top == 0 and chat.bottom == chat.rows - 1, "shell inherited restricted scrolling"
            assert b"\x1b[?2004l" in chat.output[start:], "editor left bracketed paste enabled"
            chat.send(b"clear\n")
            chat.wait(b"SHELL_CLEARED", start)
            for rows, cols in ((12, 50), (30, 110), (18, 60)):
                mark = len(chat.output)
                chat.resize(rows, cols)
                chat.send(b"size\n")
                chat.wait(f"SHELL_SIZE {rows} {cols}".encode(), mark)
            mark = len(chat.output)
            chat.send(b"exit\n")
            chat.prompt(mark)
        # A failing child must still return terminal ownership to the chat.
        mark = len(chat.output)
        chat.send(b"!sh\r")
        chat.wait(b"SHELL_READY", mark)
        mark = len(chat.output)
        chat.send(b"fail\n")
        chat.wait(b"shell exited unsuccessfully", mark)
        chat.prompt(mark)
        # An external writer can leave an unfinished line. A new quote must
        # not use newline bookkeeping from before the shell and append to it.
        mark = len(chat.output)
        chat.send(b"/shell\r")
        chat.wait(b"SHELL_READY", mark)
        mark = len(chat.output)
        chat.send(b"partial\n")
        chat.wait(b"SHELL_PARTIAL", mark)
        chat.prompt(mark)
        mark = len(chat.output)
        printed_mark = len(chat.printed)
        chat.send(b"!sh\r")
        chat.wait(b"SHELL_READY", mark)
        positions = [chat.locations[printed_mark + match.start()]
                     for match in re.finditer(rb"> !sh", chat.printed[printed_mark:])]
        positions = [(row, col) for row, col in positions if row < chat.rows - 1]
        assert positions and all(col == 0 for _, col in positions), positions
        mark = len(chat.output)
        chat.send(b"exit\n")
        chat.prompt(mark)
        # History stays on the original handle after multiple handoffs.
        mark = len(chat.output)
        chat.send(b"\x1b[A")
        chat.wait(b"!sh", mark)
        chat.send(b"\x15start\r")
        chat.wait(b"Thinking..." if env["CHAT_KIND"] == "c" else b"[operation] processing input.", mark)
        # Enter, Alt-Enter and Tab cannot queue/steer a shell while busy.
        for alias in (b"!sh", b"/shell"):
            for key in (b"\r", b"\x1b\r", b"\t"):
                mark = len(chat.output)
                chat.send(b"\x15" + alias + key)
                chat.pump(0.1)
                assert b"SHELL_READY" not in chat.output[mark:], "shell ran during a turn"
                assert b"Q 1." not in chat.output[mark:] and b"S 1." not in chat.output[mark:], "shell was queued"
        assert b"Shell is available only between turns." in chat.output
        mark = len(chat.output)
        chat.send(b"\x15queued\r")
        chat.wait(b"Q 1. queued", mark)
        # Cancel the active turn and the blocked draft, then reopen normally.
        mark = len(chat.output)
        chat.send(b"\x03")
        chat.wait(b"Operation cancelled" if env["CHAT_KIND"] == "c" else b"[operation] cancelled", mark)
        mark = len(chat.output)
        chat.send(b"/shell\r")
        chat.wait(b"SHELL_READY", mark)
        mark = len(chat.output)
        chat.send(b"exit\n")
        chat.prompt(mark)
        assert b"Q 1. queued" in chat.output[mark:], "shell handoff lost the queued turn"
        chat.finish()
    finally:
        chat.close()


def fallback(command, env, shell):
    env = dict(env)
    if shell is None:
        env.pop("SHELL", None)
    else:
        env["SHELL"] = shell
    chat = Chat(command, env)
    try:
        chat.prompt()
        mark = len(chat.output)
        chat.send(b"/shell\r")
        chat.pump(0.3)
        if shell != "/bin/bash":  # Bash's own readline temporarily uses raw mode.
            assert termios.tcgetattr(chat.fd)[3] & termios.ICANON
        chat.send(b"printf 'REAL_SHELL_READY\\n'\n")
        chat.wait(b"REAL_SHELL_READY\r\n", mark)
        # Ctrl-C must interrupt the shell's command, not kill the chat.
        chat.send(b"sleep 30\n")
        chat.pump(0.1)
        chat.send(b"\x03")
        chat.pump(0.1)
        mark = len(chat.output)
        chat.send(b"exit\n")
        chat.prompt(mark)
        chat.finish()
    finally:
        chat.close()


def failed_launch(command, env, build):
    chat = Chat(command, dict(env, SHELL=str(pathlib.Path(build) / "nonexistent-shell")))
    try:
        chat.prompt()
        mark = len(chat.output)
        chat.send(b"/shell\r")
        chat.wait(b"shell exited unsuccessfully", mark)
        chat.prompt(mark)
        chat.finish()
    finally:
        chat.close()


def busy_history_selection(command, env, fake_shell, build):
    with tempfile.TemporaryDirectory(prefix="shell-search.", dir=build) as directory:
        history = pathlib.Path(directory) / (hashlib.sha256(b"softline.examples.chat").hexdigest() + ".history")
        history.write_bytes(b"!sh\n/shell\n")
        history.chmod(0o600)
        chat = Chat(command, dict(env, SHELL=str(fake_shell),
                                 SOFTLINE_HISTORY_DIR=str(pathlib.Path(directory).resolve()),
                                 SOFTLINE_CHAT_OPERATION_STEP_MS="5000"))
        try:
            chat.prompt()
            mark = len(chat.output)
            chat.send(b"busy-history-start\r")
            chat.wait(b"Thinking..." if env["CHAT_KIND"] == "c" else b"[operation] processing input.", mark)
            expected = [b"!sh", b"/shell", b"busy-history-start"]
            for alias in (b"!sh", b"/shell"):
                mark = len(chat.output)
                chat.send(b"\x12" + alias)
                chat.wait(alias, mark)
                chat.pump(0.05)
                chat.send(b"\r")  # Selection is editing, not a shell submission.
                chat.pump(0.1)
                assert history.read_bytes().splitlines() == expected
                for key in (b"\r", b"\x1b\r", b"\t"):
                    chat.send(key)
                    chat.wait(b"Shell is available only between turns.")
                    chat.pump(0.05)
                    assert history.read_bytes().splitlines() == expected
                chat.send(b"-edited\r")
                expected.append(alias + b"-edited")
                deadline = time.monotonic() + 2
                while history.read_bytes().splitlines() != expected:
                    assert time.monotonic() < deadline, (history.read_bytes(), expected, bytes(chat.output[-2000:]))
                    chat.pump(0.02)
                assert b"SHELL_READY" not in chat.output, "shell launched during generation"
        finally:
            chat.close()


def main():
    kind, executable, build = sys.argv[1:]
    root = pathlib.Path(__file__).resolve().parents[1]
    command = [executable] if kind == "c" else [executable, str(root / "examples/chat.lua")]
    env = dict(os.environ, CHAT_KIND=kind, SOFTLINE_PROMPT_THEME="plain",
               SOFTLINE_CHAT_CHAR_MS="50", SOFTLINE_CHAT_OPERATION_STEP_MS="500")
    with tempfile.TemporaryDirectory(prefix="chat-shell.", dir=build) as directory:
        # Real shell probes must not read personal rc files or write history
        # outside the repository. The product itself inherits the user's HOME.
        env.update(HOME=str(pathlib.Path(directory).resolve()),
                   HISTFILE=str(pathlib.Path(directory).resolve() / "history"),
                   SOFTLINE_HISTORY_DIR=str(pathlib.Path(directory).resolve() / "prompt-history"),
                   ENV="", BASH_ENV="", INPUTRC="/dev/null")
        script = pathlib.Path(directory) / "helper.py"
        script.write_text('''import os, signal, sys, termios
attrs = termios.tcgetattr(0)
assert attrs[3] & termios.ICANON and attrs[3] & termios.ECHO
assert os.tcgetpgrp(0) == os.getpgrp()
print("SHELL_READY", " ".join(sys.argv[1:]), "cooked foreground", flush=True)
for line in sys.stdin:
    line = line.strip()
    if line == "clear":
        print("\\x1b[2J\\x1b[HSHELL_CLEARED", flush=True)
    elif line == "size":
        cols, rows = os.get_terminal_size(0)
        print("SHELL_SIZE", rows, cols, flush=True)
    elif line in ("exit", "fail"):
        sys.exit(7 if line == "fail" else 0)
    elif line == "partial":
        print("SHELL_PARTIAL", end="", flush=True)
        sys.exit(0)
''')
        shell = pathlib.Path(directory) / "shell ' helper"
        shell.write_text("#!/bin/sh\nexec " + shlex.join([sys.executable, str(script)]) + ' "$@"\n')
        shell.chmod(0o755)
        busy_history_selection(command, env, shell, build)
        exercise(command, env, shell)
        fallback(command, env, None)
        fallback(command, env, "")
        fallback(command, env, "/bin/bash")
        failed_launch(command, env, directory)
        result = subprocess.run(command, input=b"!sh arg\n/shell arg\n!sh\n/shell\n/quit\n", env=dict(env, SHELL=str(shell)),
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=5)
        assert result.returncode == 0, result.stderr
        assert result.stdout.count(b"Shell requires an interactive terminal.") == 2, result.stdout
        assert b"!sh arg" in result.stdout and b"/shell arg" in result.stdout
        assert b"SHELL_READY" not in result.stdout and b"\x1b[" not in result.stdout
    print(kind + " chat shell handoff, resize, history, busy gating, fallback and interrupts passed.")


if __name__ == "__main__":
    main()
