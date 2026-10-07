"""Exercise Linux fixture stop/resume with explicit process and I/O barriers.

Python shares the GTK fixtures' process-coordination helper; no terminal or
library behavior is mocked into production. Inspect only this test's child.
"""

import os
import pathlib
import select
import signal
import subprocess
import sys
import time

sys.dont_write_bytecode = True
from terminal_producer import pause_idle_editor


SCRIPT = r'''
import os, select
os.write(1, b'MIDFRAME\n')
select.select([0], [], [])
assert os.read(0, 1) == b'F'
os.write(1, b'FRAME\n')
poller = select.poll()
poller.register(0, select.POLLIN)
turn = 0
while True:
    if turn % 2:
        select.select([0], [], [])
    else:
        poller.poll()
    byte = os.read(0, 1)
    if byte == b'Q': break
    assert byte == b'I'
    os.write(1, b'INPUT\n')
    turn += 1
'''

child = subprocess.Popen([sys.executable, '-u', '-c', SCRIPT],
                         stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         cwd=pathlib.Path(sys.argv[1]).resolve())
output = bytearray()
finish_frame = False
fail_drain = False
pause_requested = False
original_signal = signal.pidfd_send_signal


def send_signal(descriptor, number, *arguments):
    global pause_requested
    original_signal(descriptor, number, *arguments)
    pause_requested = number == signal.SIGSTOP


signal.pidfd_send_signal = send_signal


def stopped():
    return pathlib.Path(f'/proc/{child.pid}/stat').read_text().split(') ', 1)[1][0] in ('T', 't')


def pump():
    global finish_frame
    if fail_drain and pause_requested and stopped():
        raise RuntimeError('drain failed')
    if finish_frame and pause_requested and stopped():
        os.write(child.stdin.fileno(), b'F')
        finish_frame = False
    if select.select([child.stdout], [], [], 0.01)[0]:
        output.extend(os.read(child.stdout.fileno(), 4096))


def wait(needle, start=0):
    deadline = time.monotonic() + 5
    while needle not in output[start:]:
        assert child.poll() is None, bytes(output)
        assert time.monotonic() < deadline, bytes(output)
        pump()


def input_reply():
    start = len(output)
    os.write(child.stdin.fileno(), b'I')
    wait(b'INPUT\n', start)


try:
    wait(b'MIDFRAME\n')
    finish_frame = True
    with pause_idle_editor(child, pump):
        assert stopped() and b'\nFRAME\n' in output, 'unfinished frame reached resize'
        start = len(output)
        os.write(child.stdin.fileno(), b'I')
        pump()
        assert b'INPUT\n' not in output[start:], 'stopped editor consumed input'
    wait(b'INPUT\n', start)

    try:
        with pause_idle_editor(child, pump):
            raise RuntimeError('resize failed')
    except RuntimeError as error:
        assert str(error) == 'resize failed'
    input_reply()

    fail_drain = True
    try:
        with pause_idle_editor(child, pump):
            raise AssertionError('failed drain reached resize')
    except RuntimeError as error:
        assert str(error) == 'drain failed'
    fail_drain = False
    input_reply()

    with pause_idle_editor(child, pump, enabled=False):
        assert not stopped(), 'unpaused diagnostic stopped the editor'
        input_reply()
    os.write(child.stdin.fileno(), b'Q')
    assert child.wait(timeout=5) == 0
finally:
    signal.pidfd_send_signal = original_signal
    if child.poll() is None:
        child.kill()
        child.wait(timeout=5)
    child.stdin.close()
    child.stdout.close()
print('Resize coordination: poll/select frame barriers, queued input, failure cleanup and unpaused mode passed.')
