"""Plain readline reflow must preserve prior terminal output and the draft."""
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
import check_terminal_native_output as t

vt = t.vt


def settle():
    for _ in range(10):
        vt.pump()


def start(example, width, prior, source, position):
    widget, window = t.terminal()
    t.resize(widget, window, width, 8)
    vt.feed(widget, prior, len(prior))
    vt.pump()
    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 8, width, 0, 0))
    terminal_pty = vt.foreign_pty(master, None, None)
    assert terminal_pty
    vt.set_pty(widget, terminal_pty)
    vt.unref(terminal_pty)
    child = subprocess.Popen([example], stdin=slave, stdout=slave, stderr=slave,
                             env=dict(os.environ, SOFTLINE_PROMPT_THEME='plain'))
    os.close(slave)
    deadline = time.monotonic() + 3
    while not any(line.startswith('softline> ') for line in t.full_transcript(widget)):
        vt.pump()
        assert child.poll() is None and time.monotonic() < deadline, 'readline did not start'
    payload = source.encode() + position
    vt.send(widget, payload, len(payload))
    settle()
    return widget, window, child


def stop(window, child):
    if child.poll() is None:
        child.kill()
    child.wait()
    vt.destroy(window)
    vt.pump()


def prompt(widget):
    text = t.full_transcript(widget)
    begin = next(i for i, line in enumerate(text) if line.startswith('softline> '))
    row, col = t.rows(widget)[1]
    return text[begin:], (row - begin, col)


def case(example, source, position, prefilled):
    count = 20 if prefilled else 2
    history = [f'keep{i:03}' for i in range(count)]
    prior = ''.join(line + '\r\n' for line in history).encode()
    actual, window, child = start(example, 40, prior, source, position)
    try:
        for width in (20, 36, 18):
            t.resize(actual, window, width, 8)
            settle()
            text = t.full_transcript(actual)
            assert [line for line in text if line.startswith('keep')] == history, (
                'readline overwrote transcript', source, position, prefilled, width, text)
            reference, reference_window, reference_child = start(example, width, prior, source, position)
            try:
                assert prompt(actual) == prompt(reference), (
                    'readline left stale prompt cells', source, position, prefilled, width,
                    prompt(actual), prompt(reference))
            finally:
                stop(reference_window, reference_child)
        vt.send(actual, b'\r', 1)
        deadline = time.monotonic() + 3
        while not any(line.startswith('submitted: ') for line in t.full_transcript(actual)):
            vt.pump()
            assert child.poll() is None and time.monotonic() < deadline, 'submission timed out'
        text = t.full_transcript(actual)
        submitted = text.index(next(line for line in text if line.startswith('submitted: ')))
        end = next(i for i in range(submitted + 1, len(text)) if text[i].startswith('softline> '))
        assert ''.join(text[submitted:end]) == 'submitted: ' + source, ('draft changed', text)
        print('PASS readline resize', prefilled, repr(position), repr(source), flush=True)
    finally:
        stop(window, child)


def main():
    assert len(sys.argv) == 2, 'usage: check_terminal_readline_resize.py EXAMPLE'
    example = str(pathlib.Path(sys.argv[1]).resolve())
    for source in ('one two three four', 'alpha beta gamma delta epsilon zeta eta',
                   'café 中文 é words', 'abcdefghij'):
        for position in (b'\x01', b'\x01\x1b[C\x1b[C\x1b[C', b''):
            for prefilled in (False, True):
                case(example, source, position, prefilled)


if __name__ == '__main__':
    main()
