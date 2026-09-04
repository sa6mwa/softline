#!/usr/bin/env python3
import os
import pty
import subprocess
import sys
import termios


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
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=root,
        start_new_session=True,
    )
    os.close(slave)
    try:
        os.write(master, b"hello\rexit\r")
        output, error = proc.communicate(timeout=5.0)
        if proc.returncode != 0:
            raise AssertionError(
                f"Lua mixed-TTY chat failed with {proc.returncode}: {error!r}"
            )
        if output != b"[turn] hello\n":
            raise AssertionError(f"unexpected Lua mixed-TTY output: {output!r}")
        if b"\x1b[" in output:
            raise AssertionError("Lua mixed-TTY chat emitted terminal controls")
    finally:
        os.close(master)
        if proc.poll() is None:
            proc.terminate()
            proc.wait(timeout=5.0)


if __name__ == "__main__":
    main()
