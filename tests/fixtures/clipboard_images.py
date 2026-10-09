"""Prepare deterministic PNG inputs; the compiled test owns protocol assertions."""
import pathlib
import struct
import sys
import zlib


def chunk(kind, data):
    body = kind + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))


image = (b"\x89PNG\r\n\x1a\n" +
         chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0)) +
         chunk(b"IDAT", zlib.compress(b"\0\xff\0\0")))
root = pathlib.Path(sys.argv[1])
root.mkdir(parents=True, exist_ok=True)
(root / "small.png").write_bytes(image + chunk(b"IEND", b""))
(root / "large.png").write_bytes(
    image + chunk(b"tEXt", b"Comment\0" + b"x" * (17 * 1024 * 1024)) +
    chunk(b"IEND", b""))
