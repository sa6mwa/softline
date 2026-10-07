"""Verify themed chat output through repeated width changes in a filled VTE."""
import fcntl
import os
import pathlib
import pty
import struct
import subprocess
import sys
import termios
import time

sys.dont_write_bytecode = True
import check_terminal_native_output as terminal
from terminal_producer import pause_idle_editor, pause_producer

vt = terminal.vt


def case(example, build, theme, resize_race=False):
    widget, window = terminal.terminal()
    terminal.resize(widget, window, 97, 24)
    prior = ''.join(f'existing row {i:03}\r\n' for i in range(100)).encode()
    vt.feed(widget, prior, len(prior))
    vt.pump()
    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 24, 97, 0, 0))
    terminal_pty = vt.foreign_pty(master, None, None)
    assert terminal_pty
    vt.set_pty(widget, terminal_pty)
    vt.unref(terminal_pty)
    def setup():
        os.setsid()
        fcntl.ioctl(slave, termios.TIOCSCTTY, 0)

    child = subprocess.Popen([example], stdin=slave, stdout=slave, stderr=slave,
                             preexec_fn=setup, env=dict(os.environ, TERM='xterm-256color', SOFTLINE_CHAT_CHAR_MS='20',
                                      SOFTLINE_PROMPT_THEME=theme))
    os.close(slave)

    def text():
        return terminal.rows(widget)[0]

    def wait(predicate, timeout=5):
        end = time.monotonic() + timeout
        while True:
            vt.pump()
            if predicate(text()):
                return
            assert child.poll() is None, ('early exit', child.returncode, text())
            assert time.monotonic() < end, ('timeout', text())
            time.sleep(0.001)

    def resize(width, height):
        pixels_x = vt.pixel_width(widget) + (width - vt.columns(widget)) * vt.cell_width(widget)
        pixels_y = vt.pixel_height(widget) + (height - vt.row_count(widget)) * vt.cell_height(widget)
        # One window allocation, not set_size followed by a second allocation.
        vt.resize_window(window, pixels_x, pixels_y)
        wait(lambda rows: (vt.columns(widget), vt.row_count(widget)) == (width, height) and
             struct.unpack('HHHH', fcntl.ioctl(master, termios.TIOCGWINSZ, bytes(8)))[:2] == (height, width))

    def send(data):
        vt.send(widget, data, len(data))

    snapshots = []

    try:
        wait(lambda rows: bool(rows) and rows[-1] == '> ')
        for index in range(4):
            send(f'hello{index}\r'.encode())
            wait(lambda rows: any('Thinking' in row or 'Reasoning' in row for row in rows))
            wait(lambda rows: any('Here is' in row for row in rows) if index == 0 else
                 sum(row.startswith('> hello') for row in rows) == index + 1)
            if index == 0:
                for length, ch in enumerate(b'typing', 1):
                    start = time.monotonic()
                    send(bytes([ch]))
                    wait(lambda rows: rows[-1] == '> ' + 'typing'[:length], 0.1)
                    assert time.monotonic() - start < 0.15
                send(b'\x15')
            if index >= 1:
                send(b'draft')
                wait(lambda rows: rows[-1] == '> draft', 0.1)
                # Exercise fitted widths, status wrapping and continuous
                # one-column shrink/grow while different responses are live.
                if index == 1:
                    widths = list(range(110, 30, -1)) + list(range(30, 111))
                    delay = 0.005
                elif index == 2:
                    widths = [80, 64, 45, 30, 44, 80, 97] * 2
                    delay = 0.06
                else:
                    widths = [96, 93, 97, 100, 95, 97] * 2
                    delay = 0.12
                with pause_producer(child, vt.pump, not resize_race):
                    for width in widths:
                        height = 24
                        with pause_idle_editor(child, vt.pump, not resize_race):
                            resize(width, height)
                        end = time.monotonic() + delay
                        while time.monotonic() < end:
                            vt.pump()
                            time.sleep(0.001)
                        snapshots.append(f'== {index} {width}x{height} ioctl {struct.unpack("HHHH", fcntl.ioctl(master, termios.TIOCGWINSZ, bytes(8)))[:2]} cursor {terminal.rows(widget)[1]} ==\n' +
                                         '\n'.join(text()[-height:]))
            expected_prompt = '> draft' if index >= 1 else '> '
            wait(lambda rows: rows[-1] == expected_prompt and not any(
                'Thinking' in row or 'Reasoning' in row for row in rows), 7)
            if index >= 1:
                send(b'\x15')
                wait(lambda rows: rows[-1] == '> ')
            normalized = ''.join(''.join(text()).split())
            documents = [
                ['# A short answer',
                 'Here is italic context, bold emphasis, and code in one paragraph.',
                 '## Next step', 'Try another prompt.'],
                ['## A longer answer', 'First, inspect the input.',
                 'Then use code for the operation; bold marks the result and italic marks a caveat.',
                 '# Summary', 'The stream is still live.'],
                ['# Notes', 'A single line can be italic, bold, or code.']]
            for turn in range(index + 1):
                for expected in documents[turn % 3]:
                    count = sum(previous % 3 == turn % 3 for previous in range(index + 1))
                    assert normalized.count(''.join(expected.split())) == count, ('corrupted response', index, expected, text())
            assert sum(row.startswith('> hello') for row in text()) == index + 1, text()
        # Exercise meaningful prompt reflow separately from streaming:
        # both the status bar and this long editor can gain or lose rows.
        send(b'a' * 60)
        wait(lambda rows: rows[-1] == '> ' + 'a' * 60)
        for width in (40, 30, 80, 30, 80):
            resize(width, 24)
            end = time.monotonic() + 0.15
            while time.monotonic() < end:
                vt.pump()
            frame = text()
            assert sum('streaming demo' in row for row in frame) == 1, (
                'duplicated status during prompt reflow', width, frame)
            first_input = next(i for i, row in enumerate(frame)
                               if row.startswith('> aaa'))
            assert ''.join(''.join(frame[first_input:]).split()) == '>' + 'a' * 60, (
                'changed draft during prompt reflow', width, frame)
            assert terminal.rows(widget)[1][0] == len(frame) - 1, (
                'cursor left the editable row', width, terminal.rows(widget)[1], frame)
        send(b'\x15')
        wait(lambda rows: rows[-1] == '> ')
        send(b'/quit\r')
        wait(lambda rows: any('Goodbye.' in row for row in rows))
        end = time.monotonic() + 2
        while child.poll() is None:
            vt.pump()
            assert time.monotonic() < end
            time.sleep(0.001)
        assert child.wait() == 0
    except BaseException:
        pathlib.Path(build, f'chat-width-{theme}-last.txt').write_text('\n'.join(text()))
        pathlib.Path(build, f'chat-width-{theme}-frames.txt').write_text('\n'.join(snapshots))
        raise
    finally:
        if child.poll() is None:
            child.kill()
            child.wait()
        vt.destroy(window)
        vt.pump()


if __name__ == '__main__':
    example, build, theme = sys.argv[1:4]
    assert sys.argv[4:] in ([], ["--resize-race"])
    case(example, build, theme, resize_race=sys.argv[4:] == ["--resize-race"])
    print(f'{theme}: typing, four response turns and prompt wrapping survive width resize.')
