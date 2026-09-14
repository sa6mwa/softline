"""Fail-closed privacy and ELF checks, including recursively nested archives."""
import argparse
import gzip
import io
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]


class Guard:
    def __init__(self, readelf, scratch):
        self.readelf = readelf
        self.scratch = scratch
        cache = Path(os.environ.get("XDG_CACHE_HOME", str(Path.home() / ".cache")))
        self.forbidden = {str(ROOT), str(Path.home())}
        for key, default in (("CPKT_TOOLCHAIN_CACHE", cache / "c.pkt.systems/toolchains"),
                             ("CPKT_DEPENDENCY_CACHE", cache / "c.pkt.systems/deps")):
            self.forbidden.add(str(Path(os.environ.get(key, str(default))).resolve()))
        for key in ("CPKT_TOOLCHAIN_ROOT", "CPKT_TOOLCHAIN_SYSROOT"):
            if os.environ.get(key):
                self.forbidden.add(str(Path(os.environ[key]).resolve()))

    def scan(self, name, data, depth=0):
        if depth > 16:
            raise ValueError(f"{name}: archive nesting limit exceeded")
        for path in self.forbidden:
            if path.encode() in data:
                raise ValueError(f"{name}: forbidden local/cache path {path}")
        # Catch a collection relocated outside this machine's configured cache.
        if re.search(rb'/[^\s\x00\"\']*--(?:glibc|musl)--stable-[0-9][^\s\x00\"\']*/', data):
            raise ValueError(f"{name}: pinned Bootlin collection path")
        if data.startswith(b"\x7fELF"):
            self.elf(name, data)
        elif name.endswith((".tar.gz", ".tgz", ".tar.xz", ".tar")):
            with tarfile.open(fileobj=io.BytesIO(data)) as archive:
                for member in archive:
                    label = f"{name}!{member.name}"
                    if member.isfile():
                        self.scan(label, archive.extractfile(member).read(), depth + 1)
                    elif member.issym() or member.islnk():
                        if member.linkname.startswith("/"):
                            raise ValueError(f"{label}: absolute archive link")
                        self.scan(label, member.linkname.encode(), depth + 1)
        elif name.endswith((".zip", ".rock", ".src.rock")):
            with zipfile.ZipFile(io.BytesIO(data)) as archive:
                for member in archive.infolist():
                    if not member.is_dir():
                        self.scan(f"{name}!{member.filename}", archive.read(member), depth + 1)
        elif name.endswith(".gz"):
            self.scan(name[:-3], gzip.decompress(data), depth + 1)

    def elf(self, name, data):
        if not self.readelf:
            cache = ROOT / "build/x86_64-linux-gnu-release/CMakeCache.txt"
            if cache.exists():
                match = re.search(r"^CMAKE_READELF:[^=]+=(.+)$", cache.read_text(), re.M)
                if match:
                    self.readelf = match[1]
            self.readelf = self.readelf or shutil.which("readelf")
        if not self.readelf:
            raise ValueError(f"{name}: readelf required for ELF artifact verification")
        binary = self.scratch / "inspect-elf"
        binary.write_bytes(data)
        try:
            output = subprocess.run([self.readelf, "-W", "-d", "-l", str(binary)],
                                    capture_output=True, text=True, env={**os.environ, "LC_ALL": "C"})
        except OSError as error:
            raise ValueError(f"{name}: ELF inspection failed: {error}") from error
        if output.returncode or output.stderr:
            raise ValueError(f"{name}: ELF inspection failed: {output.stderr}")
        metadata = output.stdout
        for value in re.findall(r"\((?:RPATH|RUNPATH)\).*?\[(.*?)\]", metadata):
            for entry in value.split(":"):
                if not re.fullmatch(r"\$ORIGIN(?:/[A-Za-z0-9_.+-]+)*", entry):
                    raise ValueError(f"{name}: non-relocatable ELF runtime path {entry!r}")
        for value in re.findall(r"\((?:NEEDED|SONAME)\).*?\[(.*?)\]", metadata):
            if "/" in value or re.search(r"lib(?:asan|ubsan|tsan|lsan|msan)", value):
                raise ValueError(f"{name}: forbidden ELF dependency/SONAME {value}")
        for loader in re.findall(r"Requesting program interpreter: (.*?)\]", metadata):
            if not re.fullmatch(r"/(?:lib|lib64)/(?:ld-linux[^/]*\.so\.[0-9]+|ld-musl-[^/]+\.so\.1)", loader):
                raise ValueError(f"{name}: non-system ELF interpreter {loader}")
        if Path(name).name.startswith("libsoftline.so"):
            abi = os.environ.get("SOFTLINE_ABI_VERSION", "1")
            if re.findall(r"\(SONAME\).*?\[(.*?)\]", metadata) != [f"libsoftline.so.{abi}"]:
                raise ValueError(f"{name}: missing or incorrect softline SONAME")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", type=Path)
    parser.add_argument("--readelf")
    args = parser.parse_args()
    if not args.path.exists():
        raise ValueError(f"missing artifact path: {args.path}")
    (ROOT / "build").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="artifact-runtime.", dir=ROOT / "build") as tmp:
        guard = Guard(args.readelf, Path(tmp))
        files = sorted(args.path.rglob("*")) if args.path.is_dir() else [args.path]
        for file in files:
            if file.is_symlink():
                link = os.readlink(file)
                if link.startswith("/"):
                    raise ValueError(f"{file}: absolute artifact symlink")
                guard.scan(str(file), link.encode())
            elif file.is_file():
                guard.scan(str(file), file.read_bytes())
    print("Artifact privacy and ELF runtime verification passed.")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, tarfile.TarError, zipfile.BadZipFile) as error:
        raise SystemExit(f"ERROR: {error}")
