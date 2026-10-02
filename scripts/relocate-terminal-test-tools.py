"""Relocate the pinned Xvfb keyboard compiler lookup to the staged PATH."""
import pathlib
import re
import subprocess
import sys

root = pathlib.Path(sys.argv[1])
if sys.argv[2:] != ["--verify"]:
    assert not sys.argv[2:]
    server = root / "usr/bin/Xvfb"
    binary = server.read_bytes()
    needle = b"/usr/bin\0"
    assert binary.count(needle) == 1, "pinned Xvfb keyboard compiler prefix changed"
    # An empty prefix selects the cached xkbcomp from PATH. The archive is immutable.
    server.write_bytes(binary.replace(needle, b"\0" * len(needle)))

# All linked runtime libraries, including libc and compiler runtimes, are
# staged. Terminal tests use their cached loader rather than the host loader.
files = [p for p in root.rglob("*") if p.is_file()]
providers = {p.name for p in files if p.resolve().is_relative_to(root.resolve())}
libc = root / "usr/lib/x86_64-linux-gnu/libc.so.6"
provided_versions = set()
if libc.is_file():
    versions = subprocess.run(["readelf", "--version-info", str(libc)], check=True,
                              capture_output=True, text=True).stdout
    definitions = versions.partition("Version definition section")[2].partition("Version needs section")[0]
    provided_versions = set(re.findall(r"Name: (GLIBC_[A-Za-z0-9_.]+)", definitions))
for file in files:
    if file.is_symlink():
        continue
    with file.open("rb") as stream:
        if stream.read(4) != b"\x7fELF":
            continue
    dynamic = subprocess.run(["readelf", "-d", str(file)], check=True,
                             capture_output=True, text=True).stdout
    versions = subprocess.run(["readelf", "--version-info", str(file)], check=True,
                              capture_output=True, text=True).stdout
    # Inspect imported versions only, not a library's own version definitions.
    needed_versions = versions.partition("Version needs section")[2]
    for version in re.findall(r"Name: (GLIBC_[A-Za-z0-9_.]+)", needed_versions):
        assert version in provided_versions, (
            f"{file.relative_to(root)}: required libc version {version} is not staged")
    for library in re.findall(r"\(NEEDED\).*\[(.*?)\]", dynamic):
        assert library in providers, (
            f"{file.relative_to(root)}: required library {library} is not staged")
