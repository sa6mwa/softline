"""Acquire an ordinary scroll region between callbacks, with no feed CPR."""
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

master, slave = pty.openpty()
fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 5, 20, 0, 0))
child = subprocess.Popen([sys.argv[1]], stdin=slave, stdout=slave, stderr=slave)
os.close(slave)
output, pending, probes = b"", b"", 0
deadline = time.monotonic() + 4
try:
    while True:
        assert time.monotonic() < deadline, "ordinary prompt stalled"
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
            probes += 1
            os.write(master, b"\x1b[1;3R" if probes == 1 else b"\x1b[5;3R")
        pending = pending[-3:]
    assert child.wait(timeout=1) == 0, output
    feeds = [part.split(b"\x1b]777;feed-end\x07", 1)[0]
             for part in output.split(b"\x1b]777;feed-start\x07")[1:]]
    assert len(feeds) == 2 and all(b"\x1b[6n" not in part for part in feeds), output
    assert b"one\r\ntwo\r\nthree\r\nfour\r\n" in feeds[0], output
    assert b"\x1b[1;4r" in feeds[1], "late ordinary prompt did not acquire region"
    assert b"> " not in feeds[1], "pinned output repainted the ordinary prompt"
    assert probes == 2, output
finally:
    if child.poll() is None:
        child.kill()
        child.wait()
    os.close(master)
print("Late scroll ownership activates between callbacks; both finite feeds have zero CPR requests.")
