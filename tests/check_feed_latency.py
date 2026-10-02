"""Keep CPR support enabled, but forbid requests while feed calls execute."""
import errno
import fcntl
import os
import pty
import select
import struct
import subprocess
import sys
import termios
import time


def check(fixture, resize):
    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
    reader, writer = os.pipe()
    child = subprocess.Popen([fixture, str(writer), str(resize)], stdin=slave,
                             stdout=slave, stderr=slave, pass_fds=(writer,))
    os.close(slave)
    os.close(writer)
    phase, probes, feed_probes, started = "setup", 0, 0, None
    output = b""
    pending = b""
    deadline = time.monotonic() + 8
    try:
        while child.poll() is None:
            assert time.monotonic() < deadline, "feed or input stalled"
            ready, _, _ = select.select([master, reader], [], [], 0.01)
            # The marker precedes feed output; process it before terminal bytes.
            if reader in ready:
                markers = os.read(reader, 1024)
                if b"S" in markers:
                    phase, started = "feed", time.monotonic()
                    os.write(master, b"typed\r")
                if b"D" in markers:
                    phase = "done"
                    assert time.monotonic() - started < 0.5, "feed contains a timed wait"
            if master in ready:
                try:
                    data = os.read(master, 65536)
                except OSError as error:
                    if error.errno == errno.EIO:
                        break
                    raise
                output += data
                pending += data
                while b"\x1b[6n" in pending:
                    _, pending = pending.split(b"\x1b[6n", 1)
                    probes += 1
                    if phase == "feed":
                        feed_probes += 1
                        # Deliberately withhold replies: accidental waits must fail.
                    else:
                        os.write(master, b"\x1b[24;1R")
                pending = pending[-3:]
        assert child.wait(timeout=2) == 0, output[-2048:]
        assert started is not None and phase == "done", "feed did not finish"
        assert probes > 0, "test disabled cursor probing instead of testing the feed"
        assert feed_probes == 0, f"feed requested {feed_probes} cursor replies"
        framed = output.split(b"\x1b]777;feed-start\x07", 1)[1].split(
            b"\x1b]777;feed-end\x07", 1)[0]
        assert b"\x1b[6n" not in framed, "feed request hidden by control-pipe scheduling"
        assert output.count("─".encode()) == 128, "producer bytes were lost"
    finally:
        if child.poll() is None:
            child.kill()
            child.wait()
        os.close(master)
        os.close(reader)


for resized in (0, 1):
    check(sys.argv[1], resized)
print("Small UTF-8/SGR writes, quoted output, resize and concurrent input incur no feed round trips.")
