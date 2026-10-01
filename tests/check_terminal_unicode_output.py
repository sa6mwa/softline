"""Compare raw and libmdf producers through real terminal cursor reports."""
import fcntl
import os
import pathlib
import pty
import struct
import subprocess
import sys
import tempfile
import termios
import time

sys.dont_write_bytecode = True
import check_terminal_native_output as terminal

vt = terminal.vt


def render(fixture, work, mode, producer, source, chunk, filled):
    widget, window = terminal.terminal()
    child = None
    try:
        if filled:
            prior = ''.join(f'existing row {i:03}\r\n' for i in range(100)).encode()
            vt.feed(widget, prior, len(prior))
            vt.pump()
        master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 8, 40, 0, 0))
        terminal_pty = vt.foreign_pty(master, None, None)
        assert terminal_pty
        vt.set_pty(widget, terminal_pty)
        vt.unref(terminal_pty)
        trace = work / (mode + '.bytes')
        child = subprocess.Popen([fixture, mode, producer, str(chunk), source, trace],
                                 stdin=slave, stdout=slave, stderr=slave)
        os.close(slave)
        sent = False
        deadline = time.monotonic() + 8
        while child.poll() is None:
            vt.pump()
            if mode == 'interrupted' and not sent and any(
                    line.startswith('> ') for line in terminal.full_transcript(widget)):
                vt.send(widget, b'\x03', 1)
                sent = True
            assert time.monotonic() < deadline, 'Unicode fixture timed out'
        assert child.wait() == 0, ('Unicode fixture failed', mode, producer, source)
        vt.pump()
        before = terminal.full_transcript(widget)
        terminal.resize(widget, window, 80, 12)
        return before, terminal.full_transcript(widget), trace.read_bytes()
    finally:
        if child is not None and child.poll() is None:
            child.kill()
            child.wait()
        vt.destroy(window)
        vt.pump()


def main():
    assert len(sys.argv) == 3, 'usage: check_terminal_unicode_output.py FIXTURE BUILD'
    fixture, build = sys.argv[1:]
    raw = ('\x1b[1m🇸🇪\x1b[0mXYZ\n', '\x1b[3m👩‍💻\x1b[0mXYZ\n',
           'éXYZ 中文.\n', '❤️XYZ 1️⃣.\n', 'a' * 35 + '🇸🇪XYZ\n',
           '🇸🇪👩‍💻\nnext line\n', '🄀中X\n', '中🄀X\n',
           'a' * 29 + '🄀中X\n', 'é🄀中X\n')
    markdown = ('Hello **world** and `code`.\n', '**🇸🇪**XYZ\n',
                '🇸🇪**XYZ**\n', '`👩‍💻`XYZ\n', 'éXYZ 中文.\n',
                '❤️XYZ 1️⃣.\n')
    count = 0
    with tempfile.TemporaryDirectory(prefix='unicode-output-', dir=build) as directory:
        work = pathlib.Path(directory)
        for producer, sources in (('raw', raw), ('mdf', markdown)):
            for source in sources:
                for chunk in (1, len(source.encode())):
                    for filled in (False, True):
                        expected = render(fixture, work, 'direct', producer, source, chunk, filled)
                        actual = render(fixture, work, 'active', producer, source, chunk, filled)
                        assert actual == expected, (producer, source, chunk, filled, actual, expected)
                        count += 1
        for source in raw[:4]:
            for filled in (False, True):
                expected = render(fixture, work, 'direct', 'raw', source, 1, filled)
                actual = render(fixture, work, 'interrupted', 'raw', source, 1, filled)
                assert actual == expected, ('restored input mode', source, filled, actual, expected)
                count += 1
    print(f'{count} Unicode comparisons preserve producer bytes, endpoint, reflow and input mode.')


if __name__ == '__main__':
    main()
