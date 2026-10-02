"""Exercise Ctrl+V against a real X11 clipboard owner, including SSH DISPLAY."""

import ctypes
import base64
import fcntl
import os
import pathlib
import re
import pty
import select
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import termios
import threading
import time
import zlib


PTR = ctypes.c_void_p
INT = ctypes.c_int
UINT = ctypes.c_uint
GET = ctypes.CFUNCTYPE(None, PTR, PTR, UINT, PTR)
CLEAR = ctypes.CFUNCTYPE(None, PTR, PTR)


def bind(lib, name, result, arguments):
    function = getattr(lib, name)
    function.restype = result
    function.argtypes = arguments
    return function


gtk = ctypes.CDLL("libgtk-3.so.0")
gdk = ctypes.CDLL("libgdk-3.so.0")
glib = ctypes.CDLL("libglib-2.0.so.0")
assert bind(gtk, "gtk_init_check", INT, [PTR, PTR])(None, None)
atom = bind(gdk, "gdk_atom_intern", PTR, [ctypes.c_char_p, INT])
clipboard_get = bind(gtk, "gtk_clipboard_get", PTR, [PTR])
set_with_data = bind(gtk, "gtk_clipboard_set_with_data", INT,
                     [PTR, PTR, UINT, GET, CLEAR, PTR])
set_text = bind(gtk, "gtk_clipboard_set_text", None,
                [PTR, ctypes.c_char_p, INT])
selection_set = bind(gtk, "gtk_selection_data_set", None,
                     [PTR, PTR, INT, PTR, INT])
iterate = bind(glib, "g_main_context_iteration", INT, [PTR, INT])
clipboard = clipboard_get(atom(b"CLIPBOARD", 0))
assert clipboard


class Target(ctypes.Structure):
    _fields_ = [("target", ctypes.c_char_p), ("flags", UINT), ("info", UINT)]


def pump():
    while iterate(None, 0):
        pass


owners = []


def offer(mime, payload):
    target_atom = atom(mime, 0)
    source = ctypes.create_string_buffer(payload)
    entry = Target(mime, 0, 0)

    @GET
    def deliver(_clipboard, selection, _info, _data):
        selection_set(selection, target_atom, 8, source, len(payload))

    @CLEAR
    def clear(_clipboard, _data):
        pass

    owners.append((source, entry, deliver, clear))
    assert set_with_data(clipboard, ctypes.byref(entry), 1,
                         deliver, clear, None)
    pump()


def png_chunk(kind, data):
    body = kind + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))


def png(width, height):
    pixels = os.urandom(width * height * 3)
    scanlines = b"".join(b"\x00" + pixels[y * width * 3:(y + 1) * width * 3]
                         for y in range(height))
    return (b"\x89PNG\r\n\x1a\n" +
            png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
            png_chunk(b"IDAT", zlib.compress(scanlines, level=0)) +
            png_chunk(b"IEND", b""))


def run_case(fixture, base, keys, mode=None, environment=None):
    master, slave = pty.openpty()
    result_read, result_write = os.pipe()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 12, 80, 0, 0))
    child_env = dict(os.environ, XDG_CACHE_HOME=str(base), TERM="xterm-256color")
    if environment:
        child_env.update(environment)
    command = [fixture, str(result_write)] + ([mode] if mode else [])
    child = subprocess.Popen(command, stdin=slave, stdout=slave, stderr=slave,
                             pass_fds=(result_write,), env=child_env)
    os.close(slave)
    os.close(result_write)
    deadline = time.monotonic() + 20
    terminal = bytearray()
    try:
        while b"> " not in terminal:
            pump()
            assert child.poll() is None and time.monotonic() < deadline, (
                "clipboard fixture did not display prompt", terminal)
            if select.select([master], [], [], 0.01)[0]:
                terminal.extend(os.read(master, 4096))
        os.write(master, keys)
        while child.poll() is None:
            pump()
            assert time.monotonic() < deadline, (
                "clipboard fixture stalled", terminal)
            if select.select([master], [], [], 0.01)[0]:
                try:
                    terminal.extend(os.read(master, 4096))
                except OSError:
                    pass
        assert child.wait() == 0, ("clipboard fixture failed", terminal)
        output = bytearray()
        while True:
            chunk = os.read(result_read, 4096)
            if not chunk:
                break
            output.extend(chunk)
        line, error, empty = output.decode().split("\n")
        assert empty == ""
        return line, error
    finally:
        if child.poll() is None:
            child.kill()
            child.wait()
        os.close(master)
        os.close(result_read)


def assert_image_path(line, prefix, suffix, payload, directory):
    assert line.startswith(prefix) and line.endswith(suffix), line
    path = pathlib.Path(line[len(prefix):len(line) - len(suffix) if suffix else None])
    assert path.is_absolute() and path.parent == directory, path
    xid = path.stem
    assert re.fullmatch(r"[0-9a-v]{20}", xid), path
    raw = base64.b32hexdecode(xid.upper() + "====")
    assert abs(int.from_bytes(raw[:4], "big") - time.time()) < 60, path
    assert path.read_bytes() == payload, (path, len(payload))
    assert path.stat().st_mode & 0o777 == 0o600, path
    assert path.parent.stat().st_mode & 0o777 == 0o700, path
    return path


def forwarded_display(base, family):
    """Forward a TCP X11 display to Xvfb with a separate matching auth entry."""
    original_name = os.environ["DISPLAY"]
    original_host, original_number = original_name.rsplit(":", 1)
    original = int(original_number.split(".")[0])
    authority_path = os.environ.get("XAUTHORITY")
    xauthority = pathlib.Path(authority_path).read_bytes() if authority_path else b""
    offset = 0
    cookie = None
    while offset < len(xauthority):
        record_family = struct.unpack_from(">H", xauthority, offset)[0]
        offset += 2
        fields = []
        for _ in range(4):
            length = struct.unpack_from(">H", xauthority, offset)[0]
            offset += 2
            fields.append(xauthority[offset:offset + length])
            offset += length
        if (record_family in (0, 6, 256, 65535) and
                fields[1] == str(original).encode() and
                fields[2] == b"MIT-MAGIC-COOKIE-1"):
            cookie = fields[3]
            break
    if not cookie:
        cookie = os.urandom(16)

    listener_family = socket.AF_INET6 if family == 6 else socket.AF_INET
    listener_host = "::1" if family == 6 else "127.0.0.1"
    listener = socket.socket(listener_family, socket.SOCK_STREAM)
    if family == 6:
        listener.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 1)
    for number in range(300, 400):
        try:
            listener.bind((listener_host, 6000 + number))
            break
        except OSError:
            continue
    else:
        raise AssertionError("no test TCP display port available")
    listener.listen(1)
    listener.settimeout(15)

    def relay():
        try:
            client, _ = listener.accept()
            client.settimeout(15)
            if original_host:
                server = socket.create_connection((original_host, 6000 + original))
            else:
                server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                server.connect(f"/tmp/.X11-unix/X{original}")
            with client, server:
                def receive(length):
                    data = bytearray()
                    while len(data) < length:
                        chunk = client.recv(length - len(data))
                        assert chunk, "X11 setup ended before authentication"
                        data.extend(chunk)
                    return bytes(data)

                header = receive(12)
                assert header[0] == ord("l")
                name_len, data_len = struct.unpack_from("<HH", header, 6)
                body = receive((name_len + 3) // 4 * 4 + (data_len + 3) // 4 * 4)
                data_offset = (name_len + 3) // 4 * 4
                assert body[:name_len] == name
                assert body[data_offset:data_offset + data_len] == cookie
                server.sendall(header + body)
                while True:
                    readable, _, _ = select.select([client, server], [], [], 15)
                    if not readable:
                        break
                    for source in readable:
                        target = server if source is client else client
                        chunk = source.recv(65536)
                        if not chunk:
                            return
                        target.sendall(chunk)
        except (OSError, AssertionError) as exc:
            relay_errors.append(exc)

    name = b"MIT-MAGIC-COOKIE-1"
    relay_errors = []
    thread = threading.Thread(target=relay, daemon=True)
    thread.start()
    addresses = {256: socket.gethostname().encode(),
                 0: socket.inet_aton("127.0.0.1"),
                 6: socket.inet_pton(socket.AF_INET6, "::1"),
                 65535: b""}

    def record(record_family, secret):
        fields = (addresses[record_family], str(number).encode(), name, secret)
        return struct.pack(">H", record_family) + b"".join(
            struct.pack(">H", len(field)) + field for field in fields)

    decoy_family = {256: 65535, 0: 6, 6: 0}[family]
    authority_data = record(decoy_family, os.urandom(16)) + record(family, cookie)
    authority = base / "forwarded.Xauthority"
    authority.write_bytes(authority_data)
    authority.chmod(0o600)
    display = f"[::1]:{number}.0" if family == 6 else f"localhost:{number}.0"
    return display, str(authority), listener, thread, relay_errors


def authenticated_server_case(fixture, base, payload):
    binary = os.environ.get("SOFTLINE_TEST_XVFB") or shutil.which("Xvfb")
    if not binary:
        print("SKIP: standalone Xvfb unavailable for server-authentication check")
        return

    for number in range(200, 250):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
            try:
                probe.bind(("127.0.0.1", 6000 + number))
                break
            except OSError:
                continue
    else:
        raise AssertionError("no authenticated test display port available")

    name = b"MIT-MAGIC-COOKIE-1"
    cookie = os.urandom(16)

    def record(family, address, secret):
        fields = (address, str(number).encode(), name, secret)
        return struct.pack(">H", family) + b"".join(
            struct.pack(">H", len(field)) + field for field in fields)

    server_auth = base / "server.Xauthority"
    server_auth.write_bytes(record(65535, b"", cookie))
    server_auth.chmod(0o600)
    client_auth = base / "client.Xauthority"
    client_auth.write_bytes(record(65535, b"", cookie) +
                            record(0, socket.inet_aton("127.0.0.1"), cookie))
    client_auth.chmod(0o600)
    wrong_auth = base / "wrong.Xauthority"
    wrong_cookie = os.urandom(16)
    wrong_auth.write_bytes(record(65535, b"", wrong_cookie) +
                           record(0, socket.inet_aton("127.0.0.1"),
                                  wrong_cookie))
    wrong_auth.chmod(0o600)

    command = [binary, f":{number}", "-nolock", "-nolisten", "unix",
               "-listen", "tcp", "-auth", str(server_auth),
               "-screen", "0", "800x600x24"]
    xkb_root = pathlib.Path(binary).resolve().parent
    if (xkb_root / "rules").is_dir():
        command.extend(["-xkbdir", str(xkb_root)])
    with open(base / "authenticated-xvfb.log", "wb") as log:
        server = subprocess.Popen(command, stdout=log, stderr=log,
                                  cwd=xkb_root if (xkb_root / "rules").is_dir()
                                  else None)
        owner_process = None
        try:
            deadline = time.monotonic() + 8
            while True:
                assert server.poll() is None, (base / "authenticated-xvfb.log").read_text()
                try:
                    with socket.create_connection(("127.0.0.1", 6000 + number),
                                                  timeout=0.1):
                        break
                except OSError:
                    assert time.monotonic() < deadline, (
                        "authenticated Xvfb did not start\n" +
                        (base / "authenticated-xvfb.log").read_text())
                    time.sleep(0.02)

            child_env = {"DISPLAY": f"127.0.0.1:{number}.0",
                         "XAUTHORITY": str(client_auth)}
            source = base / "authenticated-source.png"
            source.write_bytes(payload)
            owner_env = dict(os.environ, **child_env)
            owner_command = [sys.executable, __file__, "--auth-owner", str(source)]
            if os.environ.get("SOFTLINE_TERMINAL_TEST_ROOT"):
                owner_command.insert(0, str(pathlib.Path(__file__).resolve().parents[1] /
                                            "scripts/terminal-test-runtime.sh"))
            owner_process = subprocess.Popen(
                owner_command,
                stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE, env=owner_env)
            assert select.select([owner_process.stdout], [], [], 5)[0], (
                "authenticated clipboard owner did not start")
            ready = owner_process.stdout.readline()
            assert ready == b"READY\n", (ready, owner_process.stderr.read())
            line, error = run_case(fixture, base, b"auth\x16\r",
                                   environment=child_env)
            assert_image_path(line, "auth", "", payload, base / "softline")
            assert not error, error

            child_env["XAUTHORITY"] = str(wrong_auth)
            line, error = run_case(fixture, base, b"draft\x16\r",
                                   environment=child_env)
            assert line == "draft" and "check XAUTHORITY" in error, (line, error)
        finally:
            if owner_process:
                owner_process.stdin.close()
                owner_process.wait(timeout=5)
            if server.poll() is None:
                server.terminate()
            server.wait(timeout=5)


def main():
    fixture, build = sys.argv[1:]
    with tempfile.TemporaryDirectory(prefix="clipboard-", dir=build) as work:
        base = pathlib.Path(work)
        small = png(1, 1)
        offer(b"image/png", small)
        line, error = run_case(fixture, base, b"ab\x02\x16\r",
                               environment={"SSH_CONNECTION": "client 123 server 22"})
        path = assert_image_path(line, "a", "b", small, base / "softline")
        assert not error, error
        assert path.exists(), "image vanished when readline exited"

        for token in ("{{XDG_CACHE_HOME}}", "{{xdg_cache_home}}",
                      "{{HOME}}", "{{home}}", "~"):
            line, error = run_case(
                fixture, base, b"\x16\r",
                environment={"SOFTLINE_TEST_IMAGE_TEMPLATE":
                             token + "/captures/*",
                             "HOME": str(base)})
            assert_image_path(line, "", "", small, base / "captures")
            assert not error, error

        line, error = run_case(
            fixture, base, b"\x16\r", mode="setter",
            environment={"SOFTLINE_TEST_IMAGE_TEMPLATE":
                         "{{XDG_CACHE_HOME}}/unused/*",
                         "SOFTLINE_TEST_IMAGE_SETTER":
                         "{{XDG_CACHE_HOME}}/deep/*/image"})
        nested = pathlib.Path(line)
        assert re.fullmatch(r"[0-9a-v]{20}", nested.parent.name), nested
        assert nested.parent.parent == base / "deep", nested
        assert nested.name == "image.png" and nested.read_bytes() == small
        assert nested.parent.stat().st_mode & 0o777 == 0o700
        assert not error, error

        before = set(base.rglob("*"))
        line, error = run_case(
            fixture, base, b"draft\x16\r", mode="limit",
            environment={"SOFTLINE_TEST_IMAGE_TEMPLATE":
                         "{{XDG_CACHE_HOME}}/deep/*/image"})
        assert line == "draft" and "line limit" in error, (line, error)
        assert set(base.rglob("*")) == before, "nested paste left empty directories"

        line, error = run_case(
            fixture, base, b"\x16\r",
            environment={"SOFTLINE_TEST_IMAGE_TEMPLATE":
                         "{{XDG_CACHE_HOME}}/extensions/*/.png"})
        duplicate_extension = pathlib.Path(line)
        assert duplicate_extension.name == ".png.png", duplicate_extension
        assert duplicate_extension.read_bytes() == small and not error

        line, error = run_case(
            fixture, base, b"\x16\r",
            environment={"XDG_CACHE_HOME": "relative", "HOME": str(base)})
        assert_image_path(line, "", "", small,
                          base / ".cache" / "softline")
        assert not error, error

        line, error = run_case(
            fixture, base, b"\x16\r",
            environment={"XDG_CACHE_HOME": "relative", "HOME": str(base),
                         "SOFTLINE_TEST_IMAGE_TEMPLATE":
                         "{{xdg_cache_home}}/fallback/*"})
        assert_image_path(line, "", "", small,
                          base / ".cache" / "fallback")
        assert not error, error

        before = set(base.rglob("*"))
        line, error = run_case(
            fixture, base, b"draft\x16\r",
            environment={"SOFTLINE_TEST_DISABLE_IMAGE_PASTE": "1"})
        assert line == "draft" and "disabled" in error, (line, error)
        assert set(base.rglob("*")) == before, "disabled paste created a file"

        original_authority = os.environ.get("XAUTHORITY")
        if not original_authority:
            original_number = os.environ["DISPLAY"].rsplit(":", 1)[1].split(".")[0]
            fields = (b"", original_number.encode(), b"MIT-MAGIC-COOKIE-1",
                      os.urandom(16))
            original_file = base / "original.Xauthority"
            original_file.write_bytes(struct.pack(">H", 65535) + b"".join(
                struct.pack(">H", len(field)) + field for field in fields))
            original_file.chmod(0o600)
            os.environ["XAUTHORITY"] = str(original_file)
        try:
            for family in (256, 0, 6):
                display, authority, listener, thread, relay_errors = (
                    forwarded_display(base, family))
                try:
                    line, error = run_case(
                        fixture, base, b"remote\x16\r",
                        environment={"SSH_CONNECTION": "client 123 server 22",
                                     "DISPLAY": display, "XAUTHORITY": authority})
                    assert_image_path(line, "remote", "", small, base / "softline")
                    assert not error, error
                finally:
                    listener.close()
                    thread.join(timeout=15)
                assert not thread.is_alive() and not relay_errors, relay_errors
        finally:
            if not original_authority:
                os.environ.pop("XAUTHORITY", None)

        line, error = run_case(fixture, base, b"\x16 \x16\r")
        first, second = [pathlib.Path(part) for part in line.split(" ")]
        assert first != second and first.read_bytes() == small
        assert second.read_bytes() == small and not error, (line, error)

        before = set(base.rglob("*"))
        line, error = run_case(fixture, base,
                               b"pre\x1b[200~\x16\x1b[201~\r")
        assert line == "pre\x16" and not error, (line, error)
        assert set(base.rglob("*")) == before, "text paste created an image file"

        jpeg = b"\xff\xd8\xff\xe0" + b"JPEG-test-data" + b"\xff\xd9"
        offer(b"image/jpeg", jpeg)
        line, error = run_case(fixture, base, b"\x16\r")
        assert_image_path(line, "", "", jpeg, base / "softline")
        assert line.endswith(".jpeg") and not error, (line, error)

        large = png(2500, 2500)
        assert len(large) > 16 * 1024 * 1024
        offer(b"image/png", large)
        line, error = run_case(fixture, base, b"\x16\r")
        assert_image_path(line, "", "", large, base / "softline")
        assert not error, error

        oversized = png(3400, 3400)
        assert len(oversized) > 32 * 1024 * 1024
        offer(b"image/png", oversized)
        before = set(base.rglob("*"))
        line, error = run_case(fixture, base, b"draft\x16\r")
        assert line == "draft" and "exceeded 32 MiB" in error, (line, error)
        assert set(base.rglob("*")) == before, "oversized image leaked a file"

        before = set(base.rglob("*"))
        offer(b"image/png", b"not actually a PNG")
        line, error = run_case(fixture, base, b"draft\x16\r")
        assert line == "draft" and "transfer failed" in error, (line, error)
        assert set(base.rglob("*")) == before, "invalid image leaked a file"

        offer(b"image/png", small)
        before = set(base.rglob("*"))
        line, error = run_case(fixture, base, b"draft\x16\r", mode="limit")
        assert line == "draft" and "line limit" in error, (line, error)
        assert set(base.rglob("*")) == before, "rejected paste leaked an image file"

        set_text(clipboard, b"plain clipboard text", -1)
        pump()
        line, error = run_case(fixture, base, b"draft\x16\r")
        assert line == "draft" and "no PNG or JPEG" in error, (line, error)

        line, error = run_case(fixture, base, b"\x16\r", mode="override",
                               environment={"DISPLAY": "invalid:0"})
        assert line == "OVERRIDE" and not error, (line, error)

        line, error = run_case(fixture, base, b"draft\x16\r",
                               environment={"DISPLAY": ""})
        assert line == "draft" and "DISPLAY is unavailable" in error, (line, error)
        authenticated_server_case(fixture, base, small)
    print("X11 PNG/JPEG, large transfer, forwarded TCP display, failure, limit, and override passed.")


if __name__ == "__main__":
    if sys.argv[1:2] == ["--auth-owner"]:
        offer(b"image/png", pathlib.Path(sys.argv[2]).read_bytes())
        print("READY", flush=True)
        while True:
            pump()
            if select.select([sys.stdin.buffer], [], [], 0.01)[0] and not os.read(0, 1):
                break
    else:
        main()
