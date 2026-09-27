"""Exercise native chat in tmux's terminal emulator, including real reflow."""

import pathlib
import fcntl
import struct
import termios
import re
import bisect
import os
import select
import shlex
import subprocess
import sys
import tempfile
import time


def example_case(tmux, example, build, height_only=False, prefilled=False,
                 live_resize=True, paced=True):
    with tempfile.TemporaryDirectory(prefix="terminal-", dir=build) as work:
        socket = str(pathlib.Path(work) / "socket")

        def run(*args):
            return subprocess.check_output(
                [tmux, "-S", socket, "-f", "/dev/null", *args], text=True
            ).strip("\n")

        def screen():
            return run("capture-pane", "-p", "-t", "chat:0.0")

        def wait(predicate, description):
            deadline = time.monotonic() + 4
            while time.monotonic() < deadline:
                text = screen()
                if text and predicate(text):
                    return text
                time.sleep(0.02)
            if recording.exists():
                pathlib.Path(build, "terminal-last.bin").write_bytes(recording.read_bytes())
            pathlib.Path(build, "terminal-last.txt").write_text(run("capture-pane", "-p", "-S", "-", "-t", "chat:0.0"))
            raise AssertionError(description + "\n" + screen())

        command = "exec env SOFTLINE_CHAT_CHAR_MS=5 SOFTLINE_PROMPT_THEME=plain " + shlex.quote(example)
        if prefilled:
            prior = "".join(f"existing terminal row {i:03d}\n" for i in range(100))
            command = "printf %s " + shlex.quote(prior) + "; " + command
        try:
            run("new-session", "-d", "-s", "chat", "-x", "80", "-y", "24", command)
            run("set-option", "-w", "remain-on-exit", "on")
            run("set-option", "-w", "remain-on-exit-format", "")
            recording = pathlib.Path(work) / "output.bin"
            run("pipe-pane", "-t", "chat:0.0", "cat > " + shlex.quote(str(recording)))
            wait(lambda text: text.splitlines()[-1].startswith(">"), "initial prompt missing")
            run("send-keys", "-t", "chat:0.0", "-l", "draft " + "x" * 93 + " END")
            wait(lambda text: text.splitlines()[-1].endswith("END"), "draft missing")
            for width, height in ([] if height_only else [(30, 24), (80, 24), (9, 3), (35, 18), (80, 28), (30, 24)]):
                # tmux coalesces PTY resize notifications for 250 ms after
                # each resize. Exercise one acknowledged size at a time.
                if paced:
                    time.sleep(0.3)
                mark = recording.stat().st_size
                run("resize-window", "-t", "chat:0", "-x", str(width), "-y", str(height))
                wait(lambda text: b"END" in recording.read_bytes()[mark:] and
                     b"\x1b[6n" in recording.read_bytes()[mark:] and
                     text.splitlines()[-1].endswith("END") and
                     run("display-message", "-p", "-t", "chat:0.0", "#{cursor_y},#{pane_height}") == f"{height - 1},{height}",
                     f"prompt missing at {width}x{height}")
                cursor = run("display-message", "-p", "-t", "chat:0.0", "#{cursor_x},#{cursor_y},#{cursor_flag}")
                assert cursor.split(",")[1:] == [str(height - 1), "1"], cursor
            # Exercise the real example while its Markdown producer is live.
            run("send-keys", "-t", "chat:0.0", "C-u")
            run("send-keys", "-t", "chat:0.0", "-l", "hello")
            run("send-keys", "-t", "chat:0.0", "Enter")
            wait(lambda text: "A short answer" in text, "stream did not start")
            run("send-keys", "-t", "chat:0.0", "-l", "draft")
            height = 24
            live_sizes = [(80, 8), (80, 24), (80, 6), (80, 24)] if height_only else [(45, 24), (30, 18), (80, 28)]
            for width, height in (live_sizes if live_resize else []):
                # tmux coalesces PTY resize notifications for 250 ms after
                # each resize. Exercise one acknowledged size at a time.
                if paced:
                    time.sleep(0.3)
                mark = recording.stat().st_size
                run("resize-window", "-t", "chat:0", "-x", str(width), "-y", str(height))
                wait(lambda text: text.splitlines()[-1] == "> draft" and
                     b"\x1b[6n" in recording.read_bytes()[mark:] and
                     re.search(rb"\x1b\[1;[0-9]+r", recording.read_bytes()[mark:]) and
                     run("display-message", "-p", "-t", "chat:0.0", "#{cursor_y}") == str(height - 1),
                     "live resize displaced the draft")
                if height_only:
                    time.sleep(0.15)
            if not height_only:
                wait(lambda text: "Next step" in text, "stream stopped during resize")
            wait(lambda text: "Try another prompt" in text and "Reasoning" not in text,
                 "stream did not finish")
            transcript = run("capture-pane", "-p", "-S", "-", "-t", "chat:0.0")
            sentence = "Here is italic context, bold emphasis, and code in one paragraph."
            if "".join(sentence.split()) not in "".join(transcript.split()):
                pathlib.Path(build, "terminal-last.bin").write_bytes(recording.read_bytes())
                raise AssertionError("Resize corrupted transcript order\n" + transcript)
            time.sleep(0.1)  # Allow pipe-pane's recorder to drain its last bytes.
            raw = recording.read_bytes()
            assert b"\x1b[?25l" not in raw, "chat repeatedly hid the cursor"
            run("send-keys", "-t", "chat:0.0", "C-u")
            run("send-keys", "-t", "chat:0.0", "-l", "/quit")
            run("send-keys", "-t", "chat:0.0", "Enter")
            wait(lambda text: "! Goodbye." in text and
                 run("capture-pane", "-p", "-t", "chat:0.0",
                     "-S", str(height - 1), "-E", str(height - 1)) == "" and
                 "> /quit" not in text and
                 run("display-message", "-p", "-t", "chat:0.0", "#{pane_dead}") == "1",
                 "exit did not clear input while keeping the farewell")
            # A dead pane changes cursor visibility itself; the PTY frame tests
            # verify the application's final cursor position and visible state.
            print("Native terminal reflow, recorded cursor visibility, streaming and exit checks passed.")
        finally:
            subprocess.run([tmux, "-S", socket, "kill-server"], check=False, capture_output=True)


def fixture_case(tmux, fixture, build, document, resize, height_resize=True, static_resize=False, height_roundtrip=False, height_live=False, paged=False, prefilled=False, grow_both=False, quoted=False):
    kind = "height-live" if height_live else "height-roundtrip" if height_roundtrip else "static" if static_resize else ("resize" if height_resize else "width")
    name = f"content-{document}-{kind if resize else 'control'}" + ("-paged" if paged else "")
    if grow_both:
        name += "-grow-both"
    if prefilled:
        name += "-prefilled"
    if quoted:
        name += "-quoted"
    draft = "draft" if not paged else "draft " + "x" * 250 + " END"
    draft_tail = "> draft" if not paged else " END"
    quote = ["hello", "poih", "café"][document]
    expected = [
        ["Heading", "A short paragraph with italic, bold, and code.",
         "Finish", "END short."],
        ["Lists", "First item has several words that wrap across columns.",
         "Second item stays distinct.", "Ordered first.", "Ordered second.",
         "A quoted paragraph.", "END lists."],
        ["Code and Unicode", "int value = 42;", "return value;",
         "Räksmörgås café e\u0301 中文 日本語.",
         "A longer paragraph has different word lengths, punctuation, and enough "
         "text to cross multiple terminal rows during the width transitions.",
         "END unicode."],
    ][document]
    with tempfile.TemporaryDirectory(prefix="r-", dir=build) as work:
        socket = str(pathlib.Path(work, "socket"))
        commands = pathlib.Path(work, "commands")
        acknowledgments = pathlib.Path(work, "acknowledgments")
        recording = pathlib.Path(work, "output.bin")
        os.mkfifo(commands)
        os.mkfifo(acknowledgments)
        command_fd = os.open(commands, os.O_RDWR | os.O_NONBLOCK)
        ack_fd = os.open(acknowledgments, os.O_RDWR | os.O_NONBLOCK)
        producer_line_open = False

        def run(*args):
            return subprocess.check_output(
                [tmux, "-S", socket, "-f", "/dev/null", *args], text=True,
                timeout=5).strip("\n")

        def capture(history=False):
            return run("capture-pane", "-p", "-t", "fixture:0.0",
                       *(["-S", "-"] if history else []))

        def physical_rows(history=False):
            # Keep leading empty rows: stripping them hides where a clipped
            # producer resumes and makes a bottom-row fallback look correct.
            return subprocess.check_output(
                [tmux, "-S", socket, "capture-pane", "-p", "-t", "fixture:0.0",
                 *(["-S", "-"] if history else [])], text=True).splitlines()

        def output_rows(rows):
            owned = next((i for i, row in enumerate(rows)
                          if row.startswith(("! Thinking", "+ fixture", "Q 1."))), len(rows))
            return rows[:owned]

        def exchange(command, geometry=None):
            nonlocal producer_line_open
            os.write(command_fd, command.encode())
            if not select.select([ack_fd], [], [], 4)[0]:
                raise AssertionError(f"{name}: producer failed to acknowledge {command}")
            reply = b""
            deadline = time.monotonic() + 4
            while len(reply) < 6:
                if time.monotonic() >= deadline or not select.select([ack_fd], [], [], 4)[0]:
                    raise AssertionError(name + ": incomplete acknowledgment")
                reply += os.read(ack_fd, 6 - len(reply))
            producer_line_open = bool(reply[5])
            actual = (int.from_bytes(reply[1:3], "big"), int.from_bytes(reply[3:5], "big"))
            if geometry and actual != geometry:
                return actual
            # The emulator may still be consuming its PTY after the application
            # has written the ACK to a different fd. Wait for its visible frame.
            deadline = time.monotonic() + 4
            while True:
                cursor = run("display-message", "-p", "-t", "fixture:0.0",
                             "#{cursor_y},#{pane_height},#{cursor_flag}").split(",")
                if (int(cursor[0]) == int(cursor[1]) - 1 and cursor[2] == "1" and
                        capture().endswith(draft_tail)):
                    break
                if time.monotonic() >= deadline:
                    raise AssertionError(f"{name}: prompt displaced after {command}: {cursor}")
                time.sleep(0.02)
            return actual if geometry else reply[:1]

        try:
            launch = "exec " + shlex.join([fixture, str(commands), str(acknowledgments), str(document)])
            if prefilled:
                prior = "".join(f"existing terminal row {i:03d}\n" for i in range(100))
                launch = "printf %s " + shlex.quote(prior) + "; " + launch
            run("new-session", "-d", "-s", "fixture", "-x", "30" if grow_both else "80", "-y", "12" if grow_both else "24", launch)
            run("set-option", "-w", "remain-on-exit", "on")
            run("set-option", "-w", "remain-on-exit-format", "")
            run("pipe-pane", "-t", "fixture:0.0",
                "cat > " + shlex.quote(str(recording)))
            deadline = time.monotonic() + 4
            while not capture().endswith(">") and not capture().endswith("> "):
                if time.monotonic() >= deadline:
                    raise AssertionError(name + ": initial prompt missing")
                time.sleep(0.02)
            run("send-keys", "-t", "fixture:0.0", "-l", draft)
            exchange("r")
            if document == 2:
                exchange("q")
            transitions = {2: (45, 24), 5: (30, 18), 9: (80, 28)}
            if not height_resize:
                transitions = {step: (width, 24)
                               for step, (width, _) in transitions.items()}
            native_gaps = {}
            clipped_prefix = None
            for step in range(200):
                resumed = None
                if height_live and step in (5, 10, 15, 22):
                    for height in ({5: (8,), 10: (24,), 15: (6,), 22: (24,)}[step]):
                        run("resize-window", "-t", "fixture:0", "-x", "80", "-y", str(height))
                        deadline = time.monotonic() + 4
                        while exchange("r", (80, height)) != (80, height):
                            assert time.monotonic() < deadline, name + ": height resize timed out"
                            time.sleep(0.02)
                    resumed = subprocess.check_output([tmux, "-S", socket, "capture-pane", "-p", "-t", "fixture:0.0"], text=True).splitlines()
                if resize and not static_resize and not height_live and step in transitions:
                    width, height = transitions[step]
                    before = output_rows(physical_rows())
                    exchange("b")
                    try:
                        run("resize-window", "-t", "fixture:0", "-x", str(width),
                            "-y", str(height))
                        tty = os.open(run("display-message", "-p", "-t", "fixture:0.0", "#{pane_tty}"), os.O_RDONLY | os.O_NOCTTY)
                        try:
                            deadline = time.monotonic() + 4
                            while True:
                                rows, cols, _, _ = struct.unpack("HHHH", fcntl.ioctl(tty, termios.TIOCGWINSZ, bytes(8)))
                                if (cols, rows) == (width, height):
                                    break
                                assert time.monotonic() < deadline, name + ": native resize timed out"
                                time.sleep(0.02)
                        finally:
                            os.close(tty)
                        native = physical_rows()
                        history = physical_rows(history=True)
                        if producer_line_open and any(row.strip() for row in before) and not any(row.strip() for row in output_rows(native)):
                            # The terminal, while the application is blocked,
                            # has moved all output above the visible screen.
                            # Its existing blank history rows are immutable.
                            clipped_prefix = history[:len(history) - len(native)]
                            tail = next((i for i in range(len(clipped_prefix) - 1, -1, -1)
                                         if clipped_prefix[i].strip()), None)
                            if tail is not None:
                                anchor = "".join(clipped_prefix[tail].split())
                                native_gaps[anchor] = len(clipped_prefix) - tail - 1
                    finally:
                        os.write(command_fd, b"b")
                    deadline = time.monotonic() + 4
                    while exchange("r", (width, height)) != (width, height):
                        if time.monotonic() >= deadline:
                            raise AssertionError(name + ": PTY did not reach requested size")
                        time.sleep(0.02)
                done = exchange("s") == b"d"
                if clipped_prefix is not None:
                    continued = output_rows(physical_rows())
                    if any(row.strip() for row in continued):
                        assert continued[0].strip(), f"{name}: clipped continuation added empty screen rows\n{continued}"
                        history = physical_rows(history=True)
                        assert history[:len(clipped_prefix)] == clipped_prefix, f"{name}: clipped continuation changed terminal-owned history"
                        clipped_prefix = None
                if resumed is not None and not height_live:
                    continued = subprocess.check_output([tmux, "-S", socket, "capture-pane", "-p", "-t", "fixture:0.0"], text=True).splitlines()
                    owned = next((i for i, row in enumerate(resumed) if row.startswith(("! Thinking", "+ fixture", "Q 1."))), 24)
                    for row in range(owned):
                        if resumed[row].strip():
                            assert continued[row].startswith(resumed[row]), f"{name}: streaming overwrote row {row} after height roundtrip\nBefore: {resumed}\nAfter: {continued}"
                if done:
                    break
            else:
                raise AssertionError(name + ": producer did not finish")
            exchange("f")
            assert clipped_prefix is None, name + ": clipped producer never resumed"
            if quoted:
                exchange("h")
            if static_resize:
                original_marker_row = None
                marker_copies = 1
                if grow_both:
                    exchange("u")
                    exchange("p")
                if height_roundtrip:
                    exchange("p")
                    initial = subprocess.check_output([tmux, "-S", socket, "capture-pane", "-p", "-t", "fixture:0.0"], text=True).splitlines()
                    original_marker_row = next(i for i, row in enumerate(initial) if "AFTER RESIZE" in row)
                sizes = [(80, 8), (80, 24), (80, 6), (80, 24)] if height_roundtrip else [(30, 18), (80, 28), (16, 8), (45, 24)]
                if grow_both:
                    sizes = [(80, 28), (120, 36)]
                if quoted:
                    sizes = [(40, 24), (20, 24), (8, 24), (80, 28)]
                for width, height in sizes:
                    exchange("b")
                    try:
                        run("resize-window", "-t", "fixture:0", "-x", str(width), "-y", str(height))
                        tty = os.open(run("display-message", "-p", "-t", "fixture:0.0", "#{pane_tty}"), os.O_RDONLY | os.O_NOCTTY)
                        try:
                            deadline = time.monotonic() + 4
                            while True:
                                rows, cols, _, _ = struct.unpack("HHHH", fcntl.ioctl(tty, termios.TIOCGWINSZ, bytes(8)))
                                if (cols, rows) == (width, height):
                                    break
                                assert time.monotonic() < deadline, name + ": PTY resize timed out"
                                time.sleep(0.02)
                        finally:
                            os.close(tty)
                        native = physical_rows()
                        owned = next((i for i, row in enumerate(native) if row.startswith(("+", "Q 1."))), height)
                        time.sleep(0.03)
                        mark = recording.stat().st_size
                    finally:
                        os.write(command_fd, b"b")
                    exchange("v", (width, height))
                    actual = physical_rows()
                    actual_owned = next((i for i, row in enumerate(actual)
                                         if row.startswith(("+", "Q 1."))), height)
                    retained = min(owned, actual_owned)
                    shifted = 0
                    if quoted:
                        live_row = next(i for i, row in enumerate(native)
                                        if row == "~")
                        shifted = max(0, live_row - retained + 1)
                    if paged:
                        retained = min(retained, next((i for i, row in enumerate(actual) if row.startswith(("+ fixture", "Q 1."))), height))
                        live_row = next((i for i in range(len(native) - 1, -1, -1) if "AFTER RESIZE" in native[i]), -1)
                        shifted = max(0, live_row - retained + 1)
                    assert actual[:retained] == native[shifted:retained + shifted], f"{name}: application moved static transcript at {width}x{height}\nNative: {native}\nActual: {actual}"
                    time.sleep(0.03)
                    controls = recording.read_bytes()[mark:]
                    if quoted:
                        submitted = [row for row in capture(history=True).splitlines()
                                     if "> " + quote in row]
                        assert submitted == ["> " + quote], f"{name}: submitted prompt shifted or duplicated: {submitted}"
                        assert quote.encode() not in controls, name + ": submitted prompt repainted"
                    scrolls = re.findall(rb"\x1b\[([0-9]*)S", controls)
                    assert sum(int(count or b"1") for count in scrolls) == shifted, f"{name}: resize scrolled beyond prompt growth at {width}x{height}: {scrolls}, expected {shifted}\nNative: {native}\nActual: {actual}"
                    assert not re.search(rb"\x1b\[[0-?]*[ -/]*[TLM]", controls), name + ": resize inserted or deleted transcript cells"
                    if height_roundtrip and not paged:
                        assert not re.search(rb"\x1b\[[0-?]*[ -/]*K", controls), name + ": height-only resize erased prompt cells"
                        assert b"> draft" not in controls and b"+ fixture" not in controls, name + ": height-only resize repainted prompt"
                    if grow_both:
                        previous = "".join(capture(history=True).splitlines())
                        exchange("p")
                        continued = "".join(capture(history=True).splitlines())
                        marker_end = previous.rindex("AFTER RESIZE") + len("AFTER RESIZE")
                        assert continued == previous[:marker_end] + "AFTER RESIZE" + previous[marker_end:], f"{name}: combined growth overwrote existing output\nBefore: {previous}\nAfter: {continued}"
                    if height_roundtrip and (height == 24 or paged):
                        previous = physical_rows()
                        if paged:
                            marker_row = next((i for i in range(len(previous) - 1, -1, -1) if "AFTER RESIZE" in previous[i]), None)
                        exchange("p")
                        continued = physical_rows()
                        for row in range(retained):
                            if previous[row].strip():
                                assert continued[row].startswith(previous[row]), f"{name}: continued output overwrote row {row} after restoring height\nBefore: {previous}\nAfter: {continued}"
                        marker_copies += 1
                        if paged:
                            if marker_row is not None:
                                assert continued[marker_row] == previous[marker_row] + "AFTER RESIZE", f"{name}: height resize lost the live producer position\nBefore: {previous}\nAfter: {continued}"
                        else:
                            assert continued[original_marker_row] == "AFTER RESIZE" * marker_copies, f"{name}: height roundtrip lost the original producer position\n{continued}"
                print(name + ": static transcript matches terminal-native resize")
                # Earlier output may naturally have left the viewport. The
                # comparisons above assert the surviving cells exactly.
                return
            transcript = capture(history=True)
            rows = ["".join(row.split()) for row in transcript.splitlines()]
            starts = []
            length = 0
            for row in rows:
                starts.append(length)
                length += len(row)
            compact = "".join(rows)
            offset = 0
            for fragment in expected:
                fragment = "".join(fragment.split())
                found = compact.find(fragment, offset)
                assert found >= 0, f"{name}: missing or reordered {fragment}\n{transcript}"
                offset = found + len(fragment)
                first = bisect.bisect_right(starts, found) - 1
                last = bisect.bisect_right(starts, offset - 1) - 1
                if not height_live:
                    for index in range(first, last + 1):
                        if rows[index]:
                            continue
                        previous = next(i for i in range(index - 1, first - 1, -1) if rows[i])
                        following = next(i for i in range(index + 1, last + 1) if rows[i])
                        gap = following - previous - 1
                        assert native_gaps.get(rows[previous]) == gap, f"{name}: added blank rows within {fragment}; native gaps: {native_gaps}\n{transcript}"
            assert "Thinking..." not in transcript, f"{name}: obsolete prompt cells\n{transcript}"
            assert transcript.count("> draft") == 1, f"{name}: duplicated draft\n{transcript}"
            exchange("g")
            run("send-keys", "-t", "fixture:0.0", "C-u", "C-d")
            deadline = time.monotonic() + 4
            while run("display-message", "-p", "-t", "fixture:0.0", "#{pane_dead}") != "1":
                if time.monotonic() >= deadline:
                    raise AssertionError(name + ": fixture did not exit")
                time.sleep(0.02)
            time.sleep(0.05)
            assert b"\x1b[?25l" not in recording.read_bytes(), name + ": cursor hidden"
            last = str(int(run("display-message", "-p", "-t", "fixture:0.0",
                               "#{pane_height}")) - 1)
            assert run("capture-pane", "-p", "-t", "fixture:0.0", "-S", last,
                       "-E", last) == "", name + ": input retained on exit"
            print(name + ": passed")
        except (AssertionError, subprocess.SubprocessError):
            pathlib.Path(build, name + ".txt").write_text(capture(history=True))
            if recording.exists():
                pathlib.Path(build, name + ".bin").write_bytes(recording.read_bytes())
            raise
        finally:
            os.close(command_fd)
            os.close(ack_fd)
            subprocess.run([tmux, "-S", socket, "kill-server"], check=False,
                           capture_output=True)


def main():
    tmux, example, build, fixture = sys.argv[1:5]
    if sys.argv[5:] == ["--queued-resize"]:
        # Diagnostic for tmux's grid/PTY size mismatch during its 250 ms
        # notification throttle. Retain the reproducer rather than implying
        # that the paced regression proves this case is fixed.
        example_case(tmux, example, build, paced=False)
        return
    if sys.argv[5:] == ["--quoted"]:
        for document in range(3):
            for prefilled in (False, True):
                fixture_case(tmux, fixture, build, document, True,
                             static_resize=True, prefilled=prefilled, quoted=True)
        return
    if sys.argv[5:] == ["--no-resize"]:
        example_case(tmux, example, build, height_only=True, prefilled=True,
                     live_resize=False)
        return
    if sys.argv[5:] == ["--grow-both"]:
        for document in range(3):
            for prefilled in (False, True):
                fixture_case(tmux, fixture, build, document, True,
                             static_resize=True, grow_both=True,
                             prefilled=prefilled)
        return
    if sys.argv[5:] == ["--prefilled"]:
        for document in range(3):
            fixture_case(tmux, fixture, build, document, True, static_resize=True,
                         height_roundtrip=True, prefilled=True)
            fixture_case(tmux, fixture, build, document, True, height_live=True,
                         prefilled=True)
        example_case(tmux, example, build, height_only=True, prefilled=True)
        return
    if sys.argv[5:] in (["--static"], ["--height-roundtrip"]):
        for document in range(3):
            fixture_case(tmux, fixture, build, document, True, static_resize=True,
                         height_roundtrip=sys.argv[5:] == ["--height-roundtrip"])
            if sys.argv[5:] == ["--height-roundtrip"]:
                fixture_case(tmux, fixture, build, document, True, height_live=True)
                fixture_case(tmux, fixture, build, document, True, static_resize=True,
                             height_roundtrip=True, paged=True)
        return
    failures = []
    for document in range(3):
        for resize, height_resize in ((False, False), (True, False), (True, True)):
            try:
                fixture_case(tmux, fixture, build, document, resize, height_resize)
            except (AssertionError, subprocess.SubprocessError) as error:
                failures.append(str(error))
        try:
            fixture_case(tmux, fixture, build, document, True, prefilled=True)
        except (AssertionError, subprocess.SubprocessError) as error:
            failures.append(str(error))
    try:
        example_case(tmux, example, build)
    except AssertionError as error:
        failures.append(str(error))
    if failures:
        raise AssertionError("\n\n".join(failures))


if __name__ == "__main__":
    main()
