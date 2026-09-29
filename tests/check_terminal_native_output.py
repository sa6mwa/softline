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
                    os.write(command_fd, b'w')
                    ack()
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


def handoff_resize_case(fixture, build, source, prefilled=False):
    direct, direct_window = terminal()
    actual, actual_window = terminal()
    child = None
    if prefilled:
        prior = ''.join(f'existing row {i:03}\r\n' for i in range(100)).encode()
        for widget in (direct, actual):
            vt.feed(widget, prior, len(prior))
        vt.pump()
    try:
        with tempfile.TemporaryDirectory(prefix='handoff-', dir=build) as work:
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
                    assert child.poll() is None, 'handoff fixture exited early'
                    if select.select([reply_fd], [], [], 0)[0]:
                        reply += os.read(reply_fd, 4 - len(reply))
                    assert time.monotonic() < deadline, 'handoff acknowledgement timed out'
                vt.pump()
                return struct.unpack('HH', reply)

            try:
                ack()
                os.write(command_fd, b's')
                ack()
                payload = source.replace('\n', '\r\n').encode()
                vt.feed(direct, payload, len(payload))
                os.write(command_fd, b'i')
                deadline = time.monotonic() + 4
                while not any(line.startswith('> ') for line in full_transcript(actual)):
                    vt.pump()
                    assert child.poll() is None and time.monotonic() < deadline, 'handoff timed out'
                vt.send(actual, b'\r', 1)
                ack()
                for width, height in ((80, 12), (30, 8), (80, 8), (40, 12)):
                    resize(direct, direct_window, width, height)
                    resize(actual, actual_window, width, height)
                    os.write(command_fd, b'p')
                    assert ack() == (width, height)
                    vt.feed(direct, b'Y', 1)
                    vt.pump()
                    observed = full_transcript(actual)
                    assert observed.pop() == '> ', ('retained input lost', source, observed)
                    while observed and not observed[-1]:
                        observed.pop()
                    assert observed == full_transcript(direct), (
                        'first handoff cursor drift', source, prefilled, width, height,
                        full_transcript(direct), observed)
                os.write(command_fd, b'x')
                deadline = time.monotonic() + 4
                while child.poll() is None:
                    vt.pump()
                    assert time.monotonic() < deadline, 'handoff teardown timed out'
                assert child.wait() == 0
                print('PASS handoff resize', prefilled, repr(source), flush=True)
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


def finite_retained_case(fixture, build, source, prefilled=False):
    direct, direct_window = terminal()
    actual, actual_window = terminal()
    child = None
    if prefilled:
        prior = ''.join(f'existing row {i:03}\r\n' for i in range(100)).encode()
        for widget in (direct, actual):
            vt.feed(widget, prior, len(prior))
        vt.pump()
    try:
        with tempfile.TemporaryDirectory(prefix='finite-', dir=build) as work:
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
            child = subprocess.Popen([fixture, '--finite-retained', str(commands), str(replies), source],
                                     stdin=slave, stdout=slave, stderr=slave)
            os.close(slave)

            def ack():
                reply = b''
                deadline = time.monotonic() + 4
                while len(reply) < 4:
                    vt.pump()
                    assert child.poll() is None, 'finite fixture exited early'
                    if select.select([reply_fd], [], [], 0)[0]:
                        reply += os.read(reply_fd, 4 - len(reply))
                    assert time.monotonic() < deadline, 'finite acknowledgement timed out'
                vt.pump()
                return struct.unpack('HH', reply)

            def compare():
                observed = full_transcript(actual)
                assert sum(line.startswith('> ') for line in observed) == 1, ('stale finite prompt', observed)
                assert observed[-1] == '> ', ('finite prompt is not last', observed)
                observed.pop()
                while observed and not observed[-1]:
                    observed.pop()
                expected = full_transcript(direct)
                assert observed == expected, ('finite cursor drift', source, prefilled, expected, observed)
                cursor = rows(actual)[1]
                assert cursor == (len(full_transcript(actual)) - 1, 2), ('finite input cursor', cursor)

            try:
                assert ack() == (40, 8)
                payload = source.replace('\n', '\r\n').encode()
                vt.feed(direct, payload, len(payload))
                vt.pump()
                compare()
                # With prior scrollback, the producer is near the bottom and
                # survives a four-row shrink. Sparse output starts at row zero;
                # keep it visible so direct continuation is the right oracle.
                dimensions = ((40, 12), (52, 16),
                              (32, 12 if prefilled else 16),
                              (40, 16 if prefilled else 20),
                              (40, 12 if prefilled else 24))
                for stage, (width, height) in enumerate(dimensions):
                    resize(direct, direct_window, width, height)
                    resize(actual, actual_window, width, height)
                    command = b'm' if stage == 2 else b'e' if stage == 3 else b'f'
                    os.write(command_fd, command)
                    assert ack() == (width, height)
                    first = b'X' if command == b'm' else b'XYZ'
                    vt.feed(direct, first, len(first))
                    vt.pump()
                    compare()
                    if command != b'f':
                        # Resize while the producer callback is blocked, once
                        # between byte chunks and once before it reports EOF.
                        width, height = width + 8, height + 4
                        resize(direct, direct_window, width, height)
                        resize(actual, actual_window, width, height)
                        os.write(command_fd, b'g')
                        assert ack() == (width, height)
                        if command == b'm':
                            vt.feed(direct, b'YZ', 2)
                            vt.pump()
                        compare()
                input_row = rows(actual)[1][0]
                os.write(command_fd, b'x')
                deadline = time.monotonic() + 4
                while child.poll() is None:
                    vt.pump()
                    assert time.monotonic() < deadline, 'finite teardown timed out'
                assert child.wait() == 0
                vt.pump()
                assert full_transcript(actual) == full_transcript(direct), ('finite teardown transcript', source)
                cursor = rows(actual)[1]
                assert cursor == (input_row, 0), ('finite exit cursor', cursor)
                print('PASS finite', prefilled, repr(source), flush=True)
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


def interrupt_case(fixture, disposition):
    widget, window = terminal()
    child = None
    try:
        master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 8, 40, 0, 0))
        terminal_pty = vt.foreign_pty(master, None, None)
        assert terminal_pty
        vt.set_pty(widget, terminal_pty)
        vt.unref(terminal_pty)
        child = subprocess.Popen([fixture, '--interrupt', disposition],
                                 stdin=slave, stdout=slave, stderr=slave)
        os.close(slave)
        sent = False
        deadline = time.monotonic() + 5
        while child.poll() is None:
            vt.pump()
            if not sent and any(line.startswith('> ') for line in full_transcript(widget)):
                vt.send(widget, b'\x03', 1)
                sent = True
            assert time.monotonic() < deadline, 'post-interrupt fixture timed out'
        assert sent and child.wait() == 0, 'post-interrupt fixture failed'
        vt.pump()
        transcript = full_transcript(widget)
        assert transcript == ['AFTER INTERRUPT'], ('cursor report leaked', disposition, transcript)
        assert rows(widget)[1] == (1, 0), ('post-interrupt exit cursor', disposition, rows(widget)[1])
    finally:
        if child is not None and child.poll() is None:
            child.kill()
            child.wait()
        vt.destroy(window)
        vt.pump()


def clamped_native_prompt_case(fixture, build, first_frame_resize=False):
    widget, window = terminal()
    child = None
    with tempfile.TemporaryDirectory(prefix='clamped-prompt-', dir=build) as work:
        commands, replies = pathlib.Path(work, 'commands'), pathlib.Path(work, 'replies')
        os.mkfifo(commands)
        os.mkfifo(replies)
        command_fd = os.open(commands, os.O_RDWR | os.O_NONBLOCK)
        reply_fd = os.open(replies, os.O_RDWR | os.O_NONBLOCK)
        try:
            master, slave = pty.openpty()
            fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 8, 40, 0, 0))
            terminal_pty = vt.foreign_pty(master, None, None)
            assert terminal_pty
            vt.set_pty(widget, terminal_pty)
            vt.unref(terminal_pty)
            child = subprocess.Popen([fixture, '--gated', str(commands), str(replies), ''],
                                     stdin=slave, stdout=slave, stderr=slave)
            os.close(slave)

            def ack():
                reply = b''
                deadline = time.monotonic() + 4
                while len(reply) < 4:
                    vt.pump()
                    if select.select([reply_fd], [], [], 0)[0]:
                        reply += os.read(reply_fd, 4 - len(reply))
                    assert child.poll() is None and time.monotonic() < deadline

            ack()
            if first_frame_resize:
                resize(widget, window, 50, 10)
            os.write(command_fd, b'i')
            deadline = time.monotonic() + 4
            while '> ' not in full_transcript(widget):
                vt.pump()
                assert child.poll() is None and time.monotonic() < deadline
            draft = b'a' * 18
            vt.send(widget, draft, len(draft))
            deadline = time.monotonic() + 4
            while ('> ' + draft.decode()) not in full_transcript(widget):
                vt.pump()
                assert child.poll() is None and time.monotonic() < deadline
            resize(widget, window, 20, 8)
            for _ in range(15):
                vt.pump()
            transcript = full_transcript(widget)
            assert transcript.count('> ' + draft.decode()) == 1, (
                'clamped cursor duplicated native prompt', transcript)
            vt.send(widget, b'\x15\r', 2)
            ack()
            os.write(command_fd, b'x')
            deadline = time.monotonic() + 4
            while child.poll() is None:
                vt.pump()
                assert time.monotonic() < deadline
            assert child.wait() == 0
        finally:
            if child is not None and child.poll() is None:
                child.kill()
                child.wait()
            os.close(command_fd)
            os.close(reply_fd)
            vt.destroy(window)
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
    for disposition in ('ignore', 'handler'):
        interrupt_case(fixture, disposition)
    clamped_native_prompt_case(fixture, build)
    clamped_native_prompt_case(fixture, build, first_frame_resize=True)
    for source in ('ABC', 'a' * 39 + 'X', '🇸🇪', '\x1b[1m🇸🇪\x1b[0mX',
                   '👩‍💻X', 'é 中文', '🇸🇪\nnext'):
        for prefilled in (False, True):
            handoff_resize_case(fixture, build, source, prefilled)
    for flags in (20, 22):
        handoff_resize_case(fixture, build, '🇸🇪' * flags)
    for source in ('ABC', 'one\nline', 'café', '\x1b[1mStyled\x1b[0m',
                   '🇸🇪', '\x1b[1m🇸🇪\x1b[0m', '👩‍💻', 'é 中文'):
        for prefilled in (False, True):
            finite_retained_case(fixture, build, source, prefilled)
    print('Native output preserves bytes, reflow, cursor handoff, and exit.')


if __name__ == '__main__':
    main()
