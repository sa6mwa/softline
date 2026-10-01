"""X11 sequence zero is valid for requests, replies, and INCR transfers."""

import pathlib
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import zlib


def png_chunk(kind, data):
    body = kind + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))


def probe(fixture, directory, mode, sequence, payload=b"", chunk_size=512,
          broken=False):
    client, server = socket.socketpair()
    server.settimeout(12)
    output = directory / f"{mode}-{sequence}-{chunk_size}-{broken}.png"
    state = {"requests": 0, "served": 0, "wrapped": [], "error": None}
    if broken:
        server.close()

    def owner():
        initial = True

        def receive(count):
            data = bytearray()
            while len(data) < count:
                part = server.recv(count - len(data))
                if not part:
                    assert not data, "request ended midway"
                    return None
                data.extend(part)
            return data

        try:
            while True:
                head = receive(4)
                if head is None:
                    return
                units = struct.unpack_from("<H", head, 2)[0]
                assert units > 0
                assert receive(units * 4 - 4) is not None
                state["requests"] += 1
                serial = (sequence + state["requests"]) & 65535
                opcode = head[0]
                if serial == 0:
                    state["wrapped"].append(opcode)
                if opcode == 16:
                    reply = bytearray(32)
                    reply[0] = 1
                    struct.pack_into("<HI", reply, 2, serial, 0)
                    struct.pack_into("<I", reply, 8, 30)
                    server.sendall(reply)
                elif opcode == 20:
                    if initial:
                        data = struct.pack("<I", len(payload))
                        target, fmt, count = 40, 32, 1
                        initial = False
                    else:
                        data = payload[state["served"]:state["served"] + chunk_size]
                        state["served"] += len(data)
                        target, fmt, count = 30, 8, len(data)
                    reply = bytearray(32)
                    reply[0], reply[1] = 1, fmt
                    struct.pack_into("<HIIII", reply, 2, serial,
                                     (len(data) + 3) // 4, target, 0, count)
                    server.sendall(reply + data + b"\0" * (-len(data) % 4))
                elif opcode in (18, 19, 24):
                    event = bytearray(32)
                    event[0] = 31 if opcode == 24 else 28
                    struct.pack_into("<H", event, 2, serial)
                    if opcode == 24:
                        struct.pack_into("<IIIII", event, 4, 123, 10, 25, 30, 20)
                    else:
                        struct.pack_into("<III", event, 4, 10, 20, 123)
                    server.sendall(event)
                else:
                    assert opcode == 1, opcode
        except (BrokenPipeError, ConnectionResetError):
            pass
        except Exception as error:
            state["error"] = repr(error)
        finally:
            server.close()

    thread = None
    if not broken:
        thread = threading.Thread(target=owner, daemon=True)
        thread.start()
    try:
        result = subprocess.run(
            [fixture, mode, str(client.fileno()), str(sequence), str(output)],
            pass_fds=(client.fileno(),), timeout=15, capture_output=True)
    finally:
        client.close()
        if thread:
            thread.join(timeout=15)
            assert not thread.is_alive(), "protocol owner did not finish"
    assert state["error"] is None, state
    assert result.returncode == (1 if broken else 0), (
        mode, sequence, chunk_size, state, result.stderr.decode())
    if payload:
        assert output.read_bytes() == payload, "captured image bytes changed"
    return state


def main():
    fixture, build = sys.argv[1:]
    with tempfile.TemporaryDirectory(prefix="clipboard-protocol-", dir=build) as work:
        directory = pathlib.Path(work)
        for mode, sequence, opcode in (
                ("atom", 65535, 16), ("window", 65535, 1),
                ("delete", 65535, 19), ("timestamp", 65535, 18),
                ("select", 65534, 24)):
            assert opcode in probe(fixture, directory, mode, sequence)["wrapped"]
            probe(fixture, directory, mode, sequence, broken=True)
        image = (b"\x89PNG\r\n\x1a\n" +
                 png_chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0)) +
                 png_chunk(b"IDAT", zlib.compress(b"\0\xff\0\0")))
        tail = png_chunk(b"IEND", b"")
        small = image + tail
        for sequence in (65534, 65535):
            state = probe(fixture, directory, "transfer", sequence, small)
            assert state["wrapped"] == [19 if sequence == 65534 else 20], state
        # Within the 32 MiB limit, but enough 512-byte INCR chunks to wrap the
        # request counter naturally. Byte equality also checks reply matching.
        large = image + png_chunk(b"tEXt", b"Comment\0" + b"x" * (17 * 1024 * 1024)) + tail
        state = probe(fixture, directory, "transfer", 0, large)
        assert state["requests"] > 65536 and state["wrapped"] == [19], state
        probe(fixture, directory, "transfer", 0, large, chunk_size=4096)
    print("X11 request/reply wrap, INCR byte preservation and I/O failures passed.")


if __name__ == "__main__":
    main()
