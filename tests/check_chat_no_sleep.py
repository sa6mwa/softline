"""Run both real zero-delay chat themes with a producer sleep trap."""
import errno
import fcntl
import os
import pty
import re
import select
import struct
import subprocess
import sys
import termios
import time

for theme in ("default", "riced"):
    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
    child = subprocess.Popen([sys.argv[1]], stdin=slave, stdout=slave, stderr=slave,
                             env=dict(os.environ, SOFTLINE_CHAT_CHAR_MS="0",
                                      SOFTLINE_PROMPT_THEME=theme,
                                      LD_PRELOAD=sys.argv[2]))
    os.close(slave)
    output, pending, sent, quit_sent = b"", b"", False, False
    deadline = time.monotonic() + 4
    try:
        while True:
            assert time.monotonic() < deadline, (theme, "zero-delay producer stalled", output[-2048:])
            if not select.select([master], [], [], 0.05)[0]:
                continue
            try:
                data = os.read(master, 65536)
            except OSError as error:
                if error.errno == errno.EIO:
                    break
                raise
            if not data:
                break
            output += data
            pending += data
            while b"\x1b[6n" in pending:
                _, pending = pending.split(b"\x1b[6n", 1)
                os.write(master, b"\x1b[24;3R")
            pending = pending[-3:]
            plain = re.sub(rb"\x1b\[[0-?]*[ -/]*[@-~]", b"", output)
            if not sent and b"> " in plain:
                os.write(master, b"hello\r")
                sent = True
            if not quit_sent and b"Try another prompt." in plain:
                os.write(master, b"/quit\r")
                quit_sent = True
        assert child.wait(timeout=1) == 0, output[-2048:]
        assert sent and quit_sent and b"Goodbye." in output, output[-2048:]
    finally:
        if child.poll() is None:
            child.kill()
            child.wait()
        os.close(master)
print("Default and riced zero-delay producers complete without any nanosleep call.")
