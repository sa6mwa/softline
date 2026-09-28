"""Check physical transcript rows in a real GTK3 VTE terminal.

Run under xvfb-run, or with DISPLAY set. VTE is a test dependency only; the
application uses the same VT sequences in every terminal.
"""

import ctypes
import os
import pathlib
import pty
import fcntl
import struct
import subprocess
import sys
import termios
import time


PTR = ctypes.c_void_p
LONG = ctypes.c_long
INT = ctypes.c_int


def bind(library, name, result, arguments):
    function = getattr(library, name)
    function.restype = result
    function.argtypes = arguments
    return function


gtk = ctypes.CDLL("libgtk-3.so.0")
glib = ctypes.CDLL("libglib-2.0.so.0")
vte = ctypes.CDLL("libvte-2.91.so.0")
bind(gtk, "gtk_init", None, [PTR, PTR])(None, None)
new_window = bind(gtk, "gtk_window_new", PTR, [INT])
new_terminal = bind(vte, "vte_terminal_new", PTR, [])
add = bind(gtk, "gtk_container_add", None, [PTR, PTR])
show = bind(gtk, "gtk_widget_show_all", None, [PTR])
destroy = bind(gtk, "gtk_widget_destroy", None, [PTR])
resize_window = bind(gtk, "gtk_window_resize", None, [PTR, INT, INT])
pixel_width = bind(gtk, "gtk_widget_get_allocated_width", INT, [PTR])
pixel_height = bind(gtk, "gtk_widget_get_allocated_height", INT, [PTR])
cell_width = bind(vte, "vte_terminal_get_char_width", LONG, [PTR])
cell_height = bind(vte, "vte_terminal_get_char_height", LONG, [PTR])
columns = bind(vte, "vte_terminal_get_column_count", LONG, [PTR])
row_count = bind(vte, "vte_terminal_get_row_count", LONG, [PTR])
size = bind(vte, "vte_terminal_set_size", None, [PTR, LONG, LONG])
feed = bind(vte, "vte_terminal_feed", None, [PTR, ctypes.c_char_p, LONG])
send = bind(vte, "vte_terminal_feed_child", None,
            [PTR, ctypes.c_char_p, LONG])
foreign_pty = bind(vte, "vte_pty_new_foreign_sync", PTR, [INT, PTR, PTR])
set_pty = bind(vte, "vte_terminal_set_pty", None, [PTR, PTR])
cursor = bind(vte, "vte_terminal_get_cursor_position", None, [PTR, PTR, PTR])
get_row = bind(vte, "vte_terminal_get_text_range", PTR,
               [PTR, LONG, LONG, LONG, LONG, PTR, PTR, PTR])
free = bind(glib, "g_free", None, [PTR])
unref = bind(ctypes.CDLL("libgobject-2.0.so.0"), "g_object_unref", None, [PTR])
iteration = bind(glib, "g_main_context_iteration", INT, [PTR, INT])
get_adjustment = bind(gtk, "gtk_scrollable_get_vadjustment", PTR, [PTR])
adjustment_value = bind(gtk, "gtk_adjustment_get_value", ctypes.c_double, [PTR])


def pump():
    deadline = time.monotonic() + 0.03
    while time.monotonic() < deadline:
        while iteration(None, 0):
            pass
        time.sleep(0.001)


def physical_rows(terminal):
    # The producer cursor may be above the prompt. Include the whole visible
    # screen and history, independently of that cursor's current owner.
    last = int(adjustment_value(get_adjustment(terminal))) + row_count(terminal)
    result = []
    # Query one physical row at a time: a whole-buffer text export joins soft
    # wraps, hiding exactly the extra blank rows this regression must observe.
    for index in range(last):
        value = get_row(terminal, index, 0, index, columns(terminal),
                        None, None, None)
        try:
            result.append(ctypes.string_at(value).decode("utf-8"))
        finally:
            free(value)
    return result


def transcript(rows):
    first = rows.index("> p[oj")
    last = max(index for index, row in enumerate(rows)
               if row.startswith("+ streaming demo"))
    return rows[first:last]


def case(example, build, theme, height, streaming=False):
    terminal, window = new_terminal(), new_window(0)
    add(window, terminal)
    size(terminal, 97, height)
    show(window)
    pump()
    prior = "".join(f"existing terminal row {index:03}\r\n"
                    for index in range(100)).encode()
    feed(terminal, prior, len(prior))
    pump()
    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ,
                struct.pack("HHHH", height, 97, 0, 0))
    terminal_pty = foreign_pty(master, None, None)
    assert terminal_pty, "could not attach the real terminal PTY"
    set_pty(terminal, terminal_pty)
    unref(terminal_pty)

    def setup():
        os.setsid()
        fcntl.ioctl(slave, termios.TIOCSCTTY, 0)

    child = subprocess.Popen(
        [example], stdin=slave, stdout=slave, stderr=slave, preexec_fn=setup,
        env=dict(os.environ, TERM="xterm-256color",
                 SOFTLINE_CHAT_CHAR_MS="20" if streaming else "0",
                 SOFTLINE_PROMPT_THEME=theme))
    os.close(slave)

    def wait(predicate):
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            pump()
            rows = physical_rows(terminal)
            if predicate(rows):
                return rows
            assert child.poll() is None, "chat exited prematurely"
        raise AssertionError("terminal did not settle\n" + "\n".join(rows))

    def submit(message, ending):
        data = message.encode() + b"\r"
        send(terminal, data, len(data))
        wait(lambda rows: ending in rows and rows[-1].startswith(">") and
             not any("Thinking" in row or "Reasoning" in row for row in rows))

    def resize(width):
        pixels = pixel_width(terminal) + (width - columns(terminal)) * cell_width(terminal)
        size(terminal, width, height)
        resize_window(window, pixels, pixel_height(terminal))
        wait(lambda rows: columns(terminal) == width and rows[-1].startswith(">"))
        pump()

    try:
        wait(lambda rows: rows[-1].startswith(">"))
        if streaming:
            responses = (
                ("p[oj", "  # A short answer", "  Try another prompt.",
                 "Here is italic context, bold emphasis, and code in one paragraph."),
                ("oijh", "  ## A longer answer", "  The stream is still live.",
                 "Then use code for the operation; bold marks the result and italic marks a caveat."),
                ("café", "  # Notes", "  A single line can be italic, bold, or code.",
                 "A single line can be italic, bold, or code."))
            for message, heading, ending, sentence in responses:
                data = b"\x15" + message.encode() + b"\r"
                send(terminal, data, len(data))
                wait(lambda rows: heading in rows)
                send(terminal, b"draft", 5)
                for width, rows in ((45, 24), (30, 18), (80, 28),
                                    (40, 12), (97, 24)):
                    pixels_x = pixel_width(terminal) + (width - columns(terminal)) * cell_width(terminal)
                    pixels_y = pixel_height(terminal) + (rows - row_count(terminal)) * cell_height(terminal)
                    # Resize the actual window once. Calling set_size first
                    # briefly changes PTY geometry before GTK's allocation,
                    # creating extra resize events that no window requested.
                    resize_window(window, pixels_x, pixels_y)
                    wait(lambda frame: columns(terminal) == width and
                         row_count(terminal) == rows and frame[-1] == "> draft")
                    deadline = time.monotonic() + 0.15
                    while time.monotonic() < deadline:
                        pump()
                # The producer may emit hard wraps at any intermediate width.
                # Check its text across rows and its actual completion status.
                frame = wait(lambda frame: "".join(ending.split()) in
                             "".join("".join(row.split()) for row in frame) and
                             not any("Thinking" in row or "Reasoning" in row
                                     for row in frame))
                actual = "".join("".join(row.split()) for row in frame)
                assert "".join(sentence.split()) in actual, "streaming resize lost or reordered text\n" + "\n".join(frame)
                assert frame[-1] == "> draft", "resize lost the editable prompt"
            print(f"{theme}: three continuously generated responses survive mixed resizing", flush=True)
            return
        submit("p[oj", "  Try another prompt.")
        submit("oijh", "  The stream is still live.")
        baseline = transcript(physical_rows(terminal))
        quote = baseline.index("> oijh")
        assert baseline.index("  ## A longer answer") - quote == 2
        assert len(baseline) - baseline.index("  The stream is still live.") == 3
        widths = [96, 97, 96, 97, 95, 97, 98, 96, 97]
        widths += list(range(110, 89, -1)) + list(range(91, 111))
        for width in widths:
            resize(width)
            actual = transcript(physical_rows(terminal))
            assert actual == baseline, f"{theme} {width}x{height}: transcript rows changed\nExpected: {baseline}\nActual: {actual}"
        submit("café", "  A single line can be italic, bold, or code.")
        resize(97)
        baseline = transcript(physical_rows(terminal))
        assert "> café" in baseline and "  # Notes" in baseline
        for width in [96, 97, 95, 97, 96, 97]:
            resize(width)
            assert transcript(physical_rows(terminal)) == baseline, f"{theme}: later response gained blank rows at {width} columns"
        print(f"{theme} 97↔96, stepwise 110↔90, three responses, {height} rows: passed", flush=True)
    except Exception:
        pathlib.Path(build, "terminal-blank-reflow-last.txt").write_text(
            "\n".join(physical_rows(terminal)))
        raise
    finally:
        child.terminate()
        child.wait(timeout=5)
        destroy(window)
        pump()


if __name__ == "__main__":
    if sys.argv[3:] == ["--streaming"]:
        for theme in ("plain", "gruvbox"):
            case(sys.argv[1], sys.argv[2], theme, 24, streaming=True)
    else:
        for theme in ("plain", "default", "gruvbox"):
            for height in (24, 40):
                case(sys.argv[1], sys.argv[2], theme, height)
