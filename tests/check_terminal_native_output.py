"""Compare native output with direct bytes in VTE, using real PTY replies."""
import ctypes
import fcntl
import os
import pathlib
import select
import tempfile
import pty
import struct
import subprocess
import sys
import termios
import time

sys.dont_write_bytecode = True
import check_terminal_blank_reflow as vt


def rows(terminal):
    col, row = vt.LONG(), vt.LONG()
    vt.cursor(terminal, ctypes.byref(col), ctypes.byref(row))
    result = []
    for index in range(row.value + 1):
        value = vt.get_row(terminal, index, 0, index, vt.columns(terminal),
                           None, None, None)
        try:
            result.append(ctypes.string_at(value).decode('utf-8'))
        finally:
            vt.free(value)
    return result, (row.value, col.value)


def render(fixture, text, chunk, handoff, grow):
    terminal, window = vt.new_terminal(), vt.new_window(0)
    vt.add(window, terminal)
    vt.size(terminal, 40, 8)
    vt.show(window)
    vt.pump()
    child = None
    try:
        if chunk == 0:
            payload = (text.replace('\n', '\r\n') + ('Y' if handoff else '') + '\r\n').encode()
            vt.feed(terminal, payload, len(payload))
            vt.pump()
        else:
            master, slave = pty.openpty()
            fcntl.ioctl(slave, termios.TIOCSWINSZ,
                        struct.pack('HHHH', vt.row_count(terminal), vt.columns(terminal), 0, 0))
            terminal_pty = vt.foreign_pty(master, None, None)
            assert terminal_pty
            vt.set_pty(terminal, terminal_pty)
            vt.unref(terminal_pty)
            child = subprocess.Popen([fixture, '--handoff' if handoff else '--chunks', text, str(chunk)],
                                     stdin=slave, stdout=slave, stderr=slave)
            os.close(slave)
            sent = False
            deadline = time.monotonic() + 5
            while child.poll() is None:
                vt.pump()
                if handoff and not sent and any('> ' in line for line in rows(terminal)[0]):
                    vt.send(terminal, b'\r', 1)
                    sent = True
                assert time.monotonic() < deadline, 'native PTY fixture timed out'
            assert child.wait() == 0, 'native PTY fixture failed'
            vt.pump()
        if grow:
            pixels = vt.pixel_width(terminal) + (80 - vt.columns(terminal)) * vt.cell_width(terminal)
            vt.size(terminal, 80, 8)
            vt.resize_window(window, pixels, vt.pixel_height(terminal))
            deadline = time.monotonic() + 2
            while vt.columns(terminal) != 80:
                vt.pump()
                assert time.monotonic() < deadline, "terminal did not actually grow"
            vt.pump()
            assert vt.columns(terminal) == 80
        observed, cursor = rows(terminal)
        if handoff and chunk:
            assert cursor == (vt.row_count(terminal) - 1, 0), ('editor exit cursor', cursor)
        return ([line for line in observed if line] if handoff else observed), (None if handoff else cursor)
    finally:
        if child is not None and child.poll() is None:
            child.kill()
            child.wait()
        vt.destroy(window)
        vt.pump()


get_adjustment = vt.bind(vt.gtk, 'gtk_scrollable_get_vadjustment', vt.PTR, [vt.PTR])
adjustment_value = vt.bind(vt.gtk, 'gtk_adjustment_get_value', ctypes.c_double, [vt.PTR])


def full_transcript(widget):
    # The hardware cursor may be parked above the last output row. Capture
    # the full viewport and retained history, not just rows above that cursor.
    last = int(adjustment_value(get_adjustment(widget))) + vt.row_count(widget)
    text = []
    for index in range(last):
        value = vt.get_row(widget, index, 0, index, vt.columns(widget), None, None, None)
        assert value
        try:
            text.append(ctypes.string_at(value).decode('utf-8'))
        finally:
            vt.free(value)
    while text and not text[-1]:
        text.pop()
    return text


def terminal():
    widget, window = vt.new_terminal(), vt.new_window(0)
    vt.add(window, widget)
    vt.size(widget, 40, 8)
    vt.show(window)
    vt.pump()
    assert vt.columns(widget) == 40
    return widget, window


def resize(widget, window, width, height):
    pixels_x = vt.pixel_width(widget) + (width - vt.columns(widget)) * vt.cell_width(widget)
    pixels_y = vt.pixel_height(widget) + (height - vt.row_count(widget)) * vt.cell_height(widget)
    vt.size(widget, width, height)
    vt.resize_window(window, pixels_x, pixels_y)
    deadline = time.monotonic() + 2
    while (vt.columns(widget), vt.row_count(widget)) != (width, height):
        vt.pump()
        assert time.monotonic() < deadline, 'terminal geometry did not change'
    vt.pump()


def case(fixture, build, source, prefilled=False):
    direct, direct_window = terminal()
    actual, actual_window = terminal()
    child = None
    if prefilled:
        prior = ''.join(f'existing row {i:03}\r\n' for i in range(100)).encode()
        for widget in (direct, actual):
            vt.feed(widget, prior, len(prior))
        vt.pump()
    try:
        with tempfile.TemporaryDirectory(prefix='live-', dir=build) as work:
            commands, replies = pathlib.Path(work, 'commands'), pathlib.Path(work, 'replies')
            os.mkfifo(commands)
            os.mkfifo(replies)
            command_fd = os.open(commands, os.O_RDWR | os.O_NONBLOCK)
            reply_fd = os.open(replies, os.O_RDWR | os.O_NONBLOCK)
            master, slave = pty.openpty()
            fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 8, 40, 0, 0))
            terminal_pty = vt.foreign_pty(master, None, None)
            assert terminal_pty
            vt.set_pty(actual, terminal_pty)
            vt.unref(terminal_pty)
            child = subprocess.Popen([fixture, '--gated', str(commands), str(replies), source],
                                     stdin=slave, stdout=slave, stderr=slave)
            os.close(slave)

            def ack():
                reply = b''
                deadline = time.monotonic() + 4
                while len(reply) < 4:
                    vt.pump()
                    assert child.poll() is None, 'gated fixture exited early'
                    if select.select([reply_fd], [], [], 0)[0]:
                        reply += os.read(reply_fd, 4 - len(reply))
                    assert time.monotonic() < deadline, 'gated acknowledgement timed out'
                vt.pump()
                return struct.unpack('HH', reply)

            def advance(command, data=b''):
                os.write(command_fd, command)
                ack()
                if data:
                    vt.feed(direct, data, len(data))
                    vt.pump()
                expected = full_transcript(direct)
                observed = full_transcript(actual)
                assert observed == expected, (source, command, expected, observed)

            try:
                ack()
                advance(b's', source.replace('\n', '\r\n').encode())
                for width, height in ((20, 6), (80, 12), (38, 8), (40, 8)):
                    resize(direct, direct_window, width, height)
                    resize(actual, actual_window, width, height)
                    os.write(command_fd, b'r')
                    assert ack() == (width, height)
                    advance(b'p', b'Y')
                advance(b'w')
                advance(b'p', b'Y')
                advance(b'f', b'\r\nF: ' + source.replace('\n', '\r\n').encode() + b'\r\n')
                advance(b'p', b'Y')
                # The last readline has returned but its frame is retained.
                # Shrink before ending the stream: teardown must clear the
                # actual input row and leave the cursor at its new position.
                os.write(command_fd, b'i')
                deadline = time.monotonic() + 4
                while not any(line.startswith('> ') for line in rows(actual)[0]):
                    vt.pump()
                    assert child.poll() is None and time.monotonic() < deadline, 'editor handoff timed out'
                vt.send(actual, b'\r', 1)
                ack()
                if source == 'abcdefghijklmnopqrstuvwxyz':
                    resize(actual, actual_window, 40, 12)
                    os.write(command_fd, b'r')
                    assert ack() == (40, 12)
                    os.write(command_fd, b'p')
                    ack()
                    assert any(line == 'YY' for line in rows(actual)[0]), (
                        'post-editor resize split producer line', rows(actual)[0])
                    resize(actual, actual_window, 40, 16)
                    os.write(command_fd, b'r')
                    assert ack() == (40, 16)
                    os.write(command_fd, b'p')
                    ack()
                    assert any(line == 'YYY' for line in rows(actual)[0]), (
                        'repeated growth overwrote producer line', rows(actual)[0])
                resize(actual, actual_window, 32 if prefilled else 40, 6)
                os.write(command_fd, b'x')
                deadline = time.monotonic() + 4
                while child.poll() is None:
                    vt.pump()
                    assert time.monotonic() < deadline, 'teardown timed out'
                assert child.wait() == 0
                vt.pump()
                final_rows, final_cursor = rows(actual)
                visible_cursor = (final_cursor[0] - int(adjustment_value(get_adjustment(actual))),
                                  final_cursor[1])
                assert visible_cursor == (5, 0), ('stale exit cursor', source, visible_cursor)
                assert not any(line.startswith('> ') for line in final_rows), ('stale input row', source, final_rows)
                print('PASS', prefilled, repr(source), flush=True)
            finally:
                os.close(command_fd)
                os.close(reply_fd)
    finally:
        if child is not None and child.poll() is None:
            child.kill()
            child.wait()
        vt.destroy(direct_window)
        vt.destroy(actual_window)
        vt.pump()


def main():
    assert len(sys.argv) == 3, 'usage: check_terminal_native_output.py FIXTURE BUILD'
    fixture, build = sys.argv[1:]
    texts = ('a' * 40, 'a' * 40 + 'X', 'a' * 39 + 'éX', 'a' * 40 + '́X',
             '🇸🇪' * 22 + 'X', '👩‍💻' * 10 + 'X', '❤️' * 22 + 'X', '1️⃣' * 22 + 'X')
    for handoff, grow in ((False, False), (False, True), (True, False)):
        for text in texts:
            expected = render(fixture, text, 0, handoff, grow)
            assert ''.join(expected[0]) == text + ('Y' if handoff else ''), (text, expected)
            for chunk in sorted({1, 4, 8, len(text.encode())}):
                actual = render(fixture, text, chunk, handoff, grow)
                assert actual == expected, (text, chunk, handoff, grow, actual, expected)
    for source in ('abcdefghijklmnopqrstuvwxyz', 'one\nABC', 'café 中文',
                   '🇸🇪' * 22 + 'X', '👩‍💻' * 10 + 'X', '❤️' * 22 + 'X',
                   '1️⃣' * 22 + 'X', 'a' * 40):
        for prefilled in (False, True):
            case(fixture, build, source, prefilled)
    print('Native output preserves bytes, reflow, cursor handoff, and exit.')


if __name__ == '__main__':
    main()
