"""Assert actual loader resolution and exec behavior, without loader wrappers."""
import pathlib
import re
import subprocess
import sys


def check(binary, sysroot, readelf):
    metadata = subprocess.check_output([readelf, "-l", binary], text=True)
    loader = re.search(r"interpreter: (.*?)\]", metadata)
    assert loader and loader[1].startswith(sysroot + "/"), metadata
    # --list reports the executable's complete dependency closure.
    listing = subprocess.check_output([loader[1], "--list", binary], text=True)
    for line in listing.splitlines():
        if "=> /" in line:
            path = line.split("=> ", 1)[1].split()[0]
            if "libsoftline" not in path:
                assert pathlib.Path(path).resolve().is_relative_to(
                    pathlib.Path(sysroot).resolve().parent.parent
                ), listing


if __name__ == "__main__":
    sysroot, readelf, probe, *binaries = sys.argv[1:]
    for binary in [probe, *binaries]:
        check(binary, sysroot, readelf)
    try:
        check("/bin/true", sysroot, readelf)
    except AssertionError:
        pass
    else:
        raise AssertionError("runtime check accepted a host-linked executable")
    for args in ([probe], [probe, "self"]):
        maps = subprocess.check_output(args, text=True)
        libc = [line for line in maps.splitlines() if "libc.so" in line]
        assert libc and all(sysroot in line for line in libc), maps
    host_maps = subprocess.check_output([probe, "host"], text=True)
    assert sysroot not in host_maps and "libc.so" in host_maps, host_maps
    print("Bootlin runtime, self exec, and host subprocess checks passed")
