"""Generate PNG/JPEG inputs; the native test drives the clipboard and assertions."""
import pathlib
import struct
import sys
import zlib


def chunk(kind, data):
    body = kind + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))


def png(width, height):
    row = b"\0" + b"\xff\0\x80" * width
    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(row * height, level=0)) +
            chunk(b"IEND", b""))


root = pathlib.Path(sys.argv[1])
root.mkdir(parents=True, exist_ok=True)
for name, size in (("small", 1), ("large", 2500), ("oversized", 3400)):
    (root / (name + ".png")).write_bytes(png(size, size))
(root / "sample.jpeg").write_bytes(b"\xff\xd8\xff\xe0JPEG-test-data\xff\xd9")
