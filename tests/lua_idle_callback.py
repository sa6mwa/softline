#!/usr/bin/env python3
import os
import errno
import pty
import select
import signal
import subprocess
import sys
import termios
import time


SCRIPT = r'''
local softline = require("softline")
local sl = assert(softline.new())

assert(sl:set_idle_callback(function()
  local ok, err = pcall(function() sl:close() end)
  assert(not ok and string.find(err, "cannot close"), "close was not rejected")
  error("intentional idle callback failure")
end))

local line, status = sl:readline("p> ")
assert(line == nil, "idle callback unexpectedly submitted text")
assert(status == softline.READLINE_ERROR, "idle callback failure was not an error")
assert(sl:last_readline_status() == softline.READLINE_ERROR,
       "last readline status hid the idle callback failure")
assert(string.find(sl:last_error() or "", "intentional idle callback failure"),
       "idle callback error text was not retained")
sl:close()
print("lua idle callback smoke passed")
'''


def main():
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {sys.argv[0]} REPO_ROOT")
    root = sys.argv[1]
    master, slave = pty.openpty()
    attrs = termios.tcgetattr(slave)
    attrs[3] = attrs[3] & ~termios.ECHO
    termios.tcsetattr(slave, termios.TCSANOW, attrs)
    proc = subprocess.Popen(
        ["lua", "-e", SCRIPT],
        stdin=slave,
        stdout=slave,
        stderr=slave,
        cwd=root,
        start_new_session=True,
    )
    os.close(slave)
    try:
        output = bytearray()
        deadline = time.monotonic() + 5.0
        while proc.poll() is None and time.monotonic() < deadline:
            ready, _, _ = select.select([master], [], [], 0.05)
            if ready:
                try:
                    output.extend(os.read(master, 4096))
                except OSError as exc:
                    if exc.errno != errno.EIO:
                        raise
                    break
        status = proc.wait(timeout=0.1)
        while True:
            ready, _, _ = select.select([master], [], [], 0)
            if not ready:
                break
            try:
                output.extend(os.read(master, 4096))
            except OSError as exc:
                if exc.errno != errno.EIO:
                    raise
                break
        if status != 0:
            raise AssertionError(
                f"Lua idle callback smoke failed with {status}: {output!r}"
            )
        if b"lua idle callback smoke passed" not in output:
            raise AssertionError(f"missing Lua idle callback result: {output!r}")
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
