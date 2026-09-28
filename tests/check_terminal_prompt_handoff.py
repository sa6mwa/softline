"""Input-gated cursor ownership through real VTE replies and a paused producer."""
import fcntl
import os
import pathlib
import pty
import select
import struct
import subprocess
import sys
import tempfile
import termios
import time

sys.dont_write_bytecode = True
import check_terminal_native_output as terminal

vt = terminal.vt


def pump_for(seconds):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        vt.pump()
        time.sleep(0.001)


class Session:
    def __init__(self, fixture, work, first, second, filled, timeout='default', clear=0):
        self.widget, self.window = terminal.terminal()
        self.work = work
        self.first, self.second = first, second
        self.emitted = b''
        if filled:
            prior = ''.join(f'existing row {i:03}\r\n' for i in range(100)).encode()
            vt.feed(self.widget, prior, len(prior))
            vt.pump()
        commands, replies = work / 'commands', work / 'replies'
        os.mkfifo(commands)
        os.mkfifo(replies)
        self.commands = os.open(commands, os.O_RDWR | os.O_NONBLOCK)
        self.replies = os.open(replies, os.O_RDWR | os.O_NONBLOCK)
        master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 8, 40, 0, 0))
        terminal_pty = vt.foreign_pty(master, None, None)
        assert terminal_pty
        vt.set_pty(self.widget, terminal_pty)
        vt.unref(terminal_pty)
        self.child = subprocess.Popen(
            [fixture, commands, replies, first, second, work / 'bytes', work / 'input',
             str(timeout), str(clear)], stdin=slave, stdout=slave, stderr=slave)
        os.close(slave)
        self.ack(b'R')
        assert self.prompt() == '> ' and cursor(self.widget) == (7, 2), self.text()

    def text(self):
        return terminal.full_transcript(self.widget)

    def prompt(self):
        return next((row for row in reversed(self.text()) if row.startswith('> ')), None)

    def ack(self, expected):
        deadline = time.monotonic() + 4
        while not select.select([self.replies], [], [], 0)[0]:
            vt.pump()
            assert time.monotonic() < deadline, ('ack timeout', expected, self.text())
            time.sleep(0.001)
        assert os.read(self.replies, 1) == expected
        vt.pump()
        assert expected == b'x' or self.child.poll() is None, ('early exit', self.child.returncode, self.text())

    def command(self, command):
        os.write(self.commands, command)
        self.ack(command)
        if command == b'a':
            self.emitted += (self.first if isinstance(self.first, bytes) else self.first.encode())
        elif command == b'b':
            self.emitted += (self.second if isinstance(self.second, bytes) else self.second.encode())
        elif command == b'c':
            self.emitted += b'\xa9'
        elif command == b'n':
            self.emitted += b'\n'
        assert (self.work / 'bytes').read_bytes() == self.emitted

    def wait(self, condition, timeout=1):
        deadline = time.monotonic() + timeout
        while not condition():
            vt.pump()
            assert self.child.poll() is None, ('early exit', self.child.returncode, self.text())
            assert time.monotonic() < deadline, ('condition timeout', self.text())
            time.sleep(0.001)

    def input(self, data):
        vt.send(self.widget, data, len(data))

    def finish(self, key=None):
        if key:
            self.input(key)
        else:
            self.command(b'x')
        deadline = time.monotonic() + 2
        while self.child.poll() is None:
            vt.pump()
            assert time.monotonic() < deadline, ('exit timeout', self.text())
            time.sleep(0.001)
        assert self.child.wait() == 0, ('fixture/termios restoration', self.text())
        vt.pump()
        assert not any(row.startswith('> ') for row in self.text()), self.text()
        assert terminal.rows(self.widget)[1][1] == 0

    def close(self):
        if self.child.poll() is None:
            self.child.kill()
            self.child.wait()
        os.close(self.commands)
        os.close(self.replies)
        vt.destroy(self.window)
        vt.pump()


def cursor(widget):
    row, col = terminal.rows(widget)[1]
    offset = int(terminal.adjustment_value(terminal.get_adjustment(widget)))
    return row - offset, col


def prompt_row(widget):
    offset = int(terminal.adjustment_value(terminal.get_adjustment(widget)))
    return next((i - offset for i, row in reversed(list(enumerate(
        terminal.full_transcript(widget)))) if row.startswith('> ')), -1)


def producer_rows(widget):
    return [row for row in terminal.full_transcript(widget)
            if row.startswith('a') or row in ('X', 'XYZ')]


def reference(first, second, filled):
    widget, window = terminal.terminal()
    try:
        if filled:
            prior = ''.join(f'existing row {i:03}\r\n' for i in range(100)).encode()
            vt.feed(widget, prior, len(prior))
        setup = ('\x1b[1;7r\x1b[7;1H' if filled else '\x1b[1;7r\x1b[1;1H').encode()
        vt.feed(widget, setup, len(setup))
        payload = (first + second).replace('\n', '\r\n').encode()
        vt.feed(widget, payload, len(payload))
        vt.pump()
        terminal.resize(widget, window, 80, 12)
        return producer_rows(widget)
    finally:
        vt.destroy(window)
        vt.pump()


def main():
    fixture, build = sys.argv[1:3]
    resize_only = sys.argv[3:] == ['--resize-only']
    input_only = sys.argv[3:] == ['--input-only']
    assert not sys.argv[3:] or resize_only or input_only
    count = 0
    with tempfile.TemporaryDirectory(prefix='prompt-handoff-', dir=build) as directory:
        root = pathlib.Path(directory)

        def new(first, second, filled, timeout='default', clear=0):
            nonlocal count
            work = root / str(count)
            work.mkdir()
            count += 1
            return Session(fixture, work, first, second, filled, timeout, clear)

        if not input_only:
            for filled in (False, True):
                for command in (b'b', b'd'):
                    for width, height, continuation in ((40, 12, '\n'), (80, 12, '\n'),
                                                        (40, 12, '\nXYZ'), (80, 12, '\nXYZ'),
                                                        (40, 12, '\n' + 'X' * 40),
                                                        (80, 12, '\n' + 'X' * 80),
                                                        (40, 12, '\nXYZ\nTAIL'),
                                                        (80, 12, '\nXYZ\nTAIL'),
                                                        (40, 12, '\n\tXYZ'),
                                                        (80, 12, '\n\tXYZ')):
                        s = new('a' * 20, continuation, filled)
                        try:
                            s.command(b's')
                            s.wait(lambda: any('Working.' in row for row in s.text()))
                            s.command(b'a')
                            # Leave LF queued at the PTY until the terminal has
                            # resized and reset its margins. The producer already
                            # counted that LF under the previous scroll region.
                            os.write(s.commands, command)
                            # Do not process terminal input yet: the first LF and
                            # its cursor-report fence remain queued during resize.
                            time.sleep(0.01)
                            terminal.resize(s.widget, s.window, width, height)
                            s.ack(command)
                            s.emitted += continuation.encode()
                            s.command(b'r')
                            s.wait(lambda: prompt_row(s.widget) == height - 1)
                            assert sum('Working.' in row for row in s.text()) == 1, s.text()
                            assert sum('a' * 20 in row for row in s.text()) == 1, s.text()
                            if 'XYZ' in continuation:
                                expected = '        XYZ' if '\t' in continuation else 'XYZ'
                                assert sum(row == expected for row in s.text()) == 1, s.text()
                            if 'TAIL' in continuation:
                                assert sum(row == 'TAIL' for row in s.text()) == 1, s.text()
                            if 'X' * width in continuation:
                                assert sum(row == 'X' * width for row in s.text()) == 1, s.text()
                            assert sum(row.startswith('> ') for row in s.text()) == 1, s.text()
                            s.finish()
                        finally:
                            s.close()

        sources = [('a' * 40, 'X\n'), ('a' * 40, '\u0301X\n'),
                   ('a' * 38 + '🇸', '🇪XYZ\n'), ('a' * 38 + '👩', '\u200d💻XYZ\n')]
        for first, second in (() if resize_only or input_only else sources):
            for filled in (False, True):
                for action in ('none', 'input', 'status', 'queue'):
                    s = new(first, second, filled)
                    try:
                        s.command(b'a')
                        # The first write returned and its bytes are already visible;
                        # the producer remains paused without a prompt cursor jump.
                        assert first in ''.join(s.text()), s.text()
                        assert cursor(s.widget)[0] < 7, (first, filled, cursor(s.widget), s.text())
                        started = time.monotonic()
                        if action == 'input':
                            s.input(b'draft')
                            s.wait(lambda: s.prompt() == '> draft', 0.1)
                            assert cursor(s.widget) == (7, 7)
                            s.command(b'g')
                            assert (s.work / 'input').read_bytes() == b'draft'
                        elif action == 'status':
                            s.command(b's')
                            s.wait(lambda: any('Working.' in row for row in s.text()), 0.1)
                        elif action == 'queue':
                            s.command(b'q')
                            s.wait(lambda: any('queued draft' in row for row in s.text()), 0.1)
                        else:
                            pump_for(0.04)
                            assert s.prompt() == '> ', s.text()
                            assert cursor(s.widget)[0] < 7, s.text()
                        assert time.monotonic() - started < 0.15
                        s.command(b'b')
                        if action == 'input':
                            s.wait(lambda: s.prompt() == '> draft', 0.15)
                        elif action == 'status':
                            s.wait(lambda: any('Working.' in row for row in s.text()), 0.15)
                            assert s.prompt() == '> ', ('status lost prompt', s.text())
                        elif action == 'queue':
                            s.wait(lambda: any('queued draft' in row for row in s.text()), 0.15)
                            assert s.prompt() == '> ', ('queue lost prompt', s.text())
                        s.finish()
                        terminal.resize(s.widget, s.window, 80, 12)
                        if action == 'none':
                            assert producer_rows(s.widget) == reference(first, second, filled), (
                                'native continuation', first, action, filled, s.text())
                        else:
                            assert ''.join(producer_rows(s.widget)).count('a') == first.count('a'), s.text()
                            assert ('XYZ' if 'XYZ' in second else 'X') in ''.join(s.text()), s.text()
                    finally:
                        s.close()

        for first, second in (() if resize_only or input_only else (
                (b'a' * 35 + b'\xc3', b'\xa9X'),
                (b'a' * 35 + b'\x1b[3', b'1mX'))):
            for filled in (False, True):
                for action in ('input', 'status', 'queue'):
                    s = new(first, second, filled)
                    try:
                        s.command(b'a')
                        started = time.monotonic()
                        if action == 'input':
                            s.input(b'draft')
                        else:
                            s.command(b's' if action == 'status' else b'q')
                        pump_for(0.04)
                        assert s.prompt() == '> ' and not any(
                            'Working.' in row or 'queued draft' in row for row in s.text()), s.text()
                        s.command(b'b')
                        s.wait(lambda: s.prompt() == '> draft' if action == 'input' else
                               any(('Working.' if action == 'status' else 'queued draft') in row
                                   for row in s.text()), 0.1)
                        assert time.monotonic() - started < 0.25
                        if action == 'input':
                            s.input(b'!')
                            s.wait(lambda: s.prompt() == '> draft!', 0.1)
                            assert cursor(s.widget) == (7, 8), s.text()
                        s.finish()
                    finally:
                        s.close()

        for timeout in (() if resize_only or input_only else ('default', 80, 0, 450)):
            for filled in (False, True):
                for active in (False, True):
                    s = new(b'a' * 40 + b'\xc3', b'\xa9\xc3', filled, timeout)
                    try:
                        s.command(b'a')
                        started = time.monotonic()
                        s.input(b'draft')
                        milliseconds = 250 if timeout == 'default' else timeout
                        if milliseconds:
                            pump_for(min(milliseconds / 2000, 0.08))
                            assert s.prompt() == '> ', ('early handoff', timeout, s.text())
                        # A quiet producer and continuously unsafe output must both
                        # yield to the original deadline.
                        while time.monotonic() - started < milliseconds / 1000:
                            if active:
                                s.command(b'b')
                            pump_for(0.02)
                        s.wait(lambda: s.prompt() == '> draft', 0.12)
                        elapsed = time.monotonic() - started
                        assert elapsed <= milliseconds / 1000 + 0.12, (timeout, active, elapsed)
                        s.command(b'g')
                        assert (s.work / 'input').read_bytes() == b'draft'
                        s.command(b'c')
                        s.command(b'n')
                        s.finish()
                    finally:
                        s.close()

        for first, second in (() if resize_only or input_only else sources[1:]):
            for filled in (False, True):
                s = new(first, second, filled)
                try:
                    s.command(b'a')
                    s.input(b'draft')
                    s.wait(lambda: s.prompt() == '> draft', 0.4)
                    s.command(b'b')
                    s.finish()
                    # A forced handoff may disrupt this Unicode cluster. It
                    # must preserve earlier ASCII and subsequent plain text.
                    output = ''.join(producer_rows(s.widget))
                    assert output.count('a') == first.count('a'), s.text()
                    tail = 'X' if second.startswith('\u0301') else 'XYZ'
                    assert tail in ''.join(s.text()), s.text()
                finally:
                    s.close()

        for filled in (() if resize_only or input_only else (False, True)):
            for action in ('status', 'queue', 'paste'):
                s = new(b'a' * 40 + b'\xc3', b'', filled)
                try:
                    s.command(b'a')
                    started = time.monotonic()
                    if action == 'paste':
                        draft = ('draft 中文 ' * 8 + '\nsecond line').encode()
                        s.input(b'\x1b[200~' + draft + b'\x1b[201~')
                    else:
                        s.command(b's' if action == 'status' else b'q')
                    pump_for(0.04)
                    assert s.prompt() == '> ' and not any(
                        'Working.' in row or 'queued draft' in row for row in s.text()), s.text()
                    if action == 'paste':
                        s.command(b'g')
                        assert (s.work / 'input').read_bytes() == draft
                        s.command(b'c')
                        s.wait(lambda: any('second line' in row for row in s.text()), 0.15)
                    else:
                        needle = 'Working.' if action == 'status' else 'queued draft'
                        s.wait(lambda: any(needle in row for row in s.text()), 0.35)
                        assert s.prompt() == '> ', ('pending frame lost prompt', s.text())
                        assert time.monotonic() - started < 0.4
                    s.finish()
                finally:
                    s.close()

        for filled in (() if resize_only else (False, True)):
            for sequence in ('escape', 'utf8'):
                s = new(b'a' * 40 + b'\xc3', b'', filled)
                try:
                    s.command(b'a')
                    s.command(b's')
                    s.input(b'draft')
                    pump_for(0.18)
                    assert s.prompt() == '> ', s.text()
                    s.input(b'\x1b' if sequence == 'escape' else b'\xc3')
                    pump_for(0.075)
                    s.input(b'[D' if sequence == 'escape' else b'\xa9')
                    pump_for(0.02)
                    if sequence == 'escape':
                        s.input(b'Z')
                        pump_for(0.01)
                    s.command(b'g')
                    expected = b'drafZt' if sequence == 'escape' else 'drafté'.encode()
                    assert (s.work / 'input').read_bytes() == expected, (sequence, s.text())
                    s.wait(lambda: s.prompt() == '> ' + expected.decode(), 0.15)
                    s.finish()
                finally:
                    s.close()

        if input_only:
            actions = ()
        elif resize_only:
            actions = ('resize', 'resize-idle', 'resize-panel')
        else:
            actions = ('cancel', 'submit', 'end', 'end-idle',
                       'resize', 'resize-idle', 'resize-panel')
        for action in actions:
            for filled in (False, True):
                for clear in (0, 1):
                    s = new('a' * 40, 'X\n', filled, 2000, clear)
                    try:
                        if action == 'resize-panel':
                            s.command(b's')
                            s.command(b'q')
                            s.wait(lambda: any('queued draft' in row for row in s.text()) and
                                   any('Working.' in row for row in s.text()) and s.prompt() == '> ')
                        s.command(b'a')
                        if action not in ('end-idle', 'resize-idle', 'resize-panel'):
                            s.input(b'draft')
                        pump_for(0.03)
                        if action in ('cancel', 'submit'):
                            started = time.monotonic()
                            s.finish(b'\x03' if action == 'cancel' else b'\r')
                            assert time.monotonic() - started < 0.25
                        elif action in ('end', 'end-idle'):
                            s.command(b'e')
                            expected = '> ' if action == 'end-idle' else '> draft'
                            s.wait(lambda: s.prompt() == expected and cursor(s.widget)[0] == 7, 0.15)
                            s.finish()
                        else:
                            sizes = ((36, 7), (36, 6), (44, 10), (40, 8), (80, 12))
                            expected = '> draft' if action == 'resize' else '> '
                            for width, height in sizes:
                                previous_height = vt.row_count(s.widget)
                                terminal.resize(s.widget, s.window, width, height)
                                # A prefix can already be at the bottom before the app
                                # handles SIGWINCH. Await its completed frame as well.
                                s.command(b'r')
                                s.wait(lambda: s.prompt() == expected and prompt_row(s.widget) == height - 1, 0.25)
                                pump_for(0.03)
                                assert prompt_row(s.widget) == height - 1, (cursor(s.widget), s.text())
                                assert sum(row.startswith('> ') for row in s.text()) == 1, s.text()
                                if height < previous_height:
                                    assert cursor(s.widget)[0] == height - 1, ('shrink must leave the cursor at the prompt', cursor(s.widget), s.text())
                                if action == 'resize-panel':
                                    assert sum('Working.' in row for row in s.text()) == 1, s.text()
                                    assert sum('queued draft' in row for row in s.text()) == 1, s.text()
                            s.command(b'b')
                            s.finish()
                            assert ''.join(producer_rows(s.widget)) == 'a' * 40 + 'X', s.text()
                    finally:
                        s.close()
    print(f'{count} real-terminal cases: immediate one-byte output, gated input/status/queue, '
          'native Unicode continuation, bounded configurable waits, resize and teardown.')


if __name__ == '__main__':
    main()
