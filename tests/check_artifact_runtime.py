"""Exercise the release entrypoint with real ELF files inside nested artifacts."""
import io
import os
from pathlib import Path
import platform
import subprocess
import tarfile
import tempfile
import zipfile

root = Path(__file__).resolve().parents[1]
if platform.system() != "Linux" or platform.machine().lower() not in ("x86_64", "amd64"):
    print("SKIP: ELF fixture builds require native x86_64 Linux")
    raise SystemExit(0)
description = subprocess.check_output(
    [str(root / "scripts/cpkt-toolchains.sh"), "discover", "x86_64-linux-gnu"], text=True)
tools = dict(line.split("=", 1) for line in description.splitlines() if "=" in line)
(root / "build").mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix="artifact-guard-test.", dir=root / "build") as tmp:
    work = Path(tmp)
    dist = work / "dist"
    dist.mkdir()
    binary = work / "fixture"
    cases = 0

    def build(*flags):
        subprocess.run([tools["cc"], "-x", "c", "-", "-o", str(binary), *flags],
                       input="int main(void) { return 0; }\n", text=True, check=True)
        return binary.read_bytes()

    def verify(payload, expected=None, member="sdk/bin/no-extension", env=None):
        global cases
        tarbytes = io.BytesIO()
        with tarfile.open(fileobj=tarbytes, mode="w:xz") as archive:
            info = tarfile.TarInfo(member)
            info.size = len(payload)
            archive.addfile(info, io.BytesIO(payload))
        # Two archive levels, including the Lua source-rock container format.
        with zipfile.ZipFile(dist / "fixture.src.rock", "w") as archive:
            archive.writestr("payload.tar.xz", tarbytes.getvalue())
        result = subprocess.run([str(root / "scripts/verify-release-privacy.sh")],
                                env={**os.environ, "SOFTLINE_DIST_DIR": str(dist), **(env or {})},
                                capture_output=True, text=True)
        output = result.stdout + result.stderr
        if expected:
            assert result.returncode and expected in output, output
            assert "fixture.src.rock!payload.tar.xz!" + member in output, output
        else:
            assert result.returncode == 0, output
        cases += 1

    verify(build())
    verify(build("-static", "-nostdlib", "-Wl,-e,main"))
    for tag in ("--enable-new-dtags", "--disable-new-dtags"):
        for path in ("$ORIGIN", "$ORIGIN/../lib", "$ORIGIN:$ORIGIN/lib"):
            verify(build(f"-Wl,{tag},-rpath,{path}"))
        for path in ("relative", ".", "/opt/private/lib", "$ORIGIN:", ":$ORIGIN", "$ORIGINbad"):
            verify(build(f"-Wl,{tag},-rpath,{path}"), "non-relocatable ELF runtime path")
    verify(build("-Wl,--dynamic-linker,/opt/private/ld.so"), "non-system ELF interpreter")
    verify(build(f"-Wl,--dynamic-linker,{tools['sysroot']}/lib/ld-linux-x86-64.so.2"),
           "forbidden local/cache path")
    verify(build("-shared", "-Wl,-soname,libsoftline.so.1"), member="sdk/lib/libsoftline.so.1")
    verify(build("-shared", "-Wl,-soname,libsoftline.so.9"),
           "incorrect softline SONAME", "sdk/lib/libsoftline.so.1")
    verify(build("-shared", "-Wl,-soname,/opt/private/libfoo.so"),
           "forbidden ELF dependency/SONAME", "sdk/lib/module")
    verify(build(f"-Wl,-rpath,{tools['sysroot']}/lib"), "forbidden local/cache path")
    verify(b"\x7fELFbroken", "ELF inspection failed")
    verify(str(root).encode(), "forbidden local/cache path", "sdk/share/metadata.txt")
    verify(str(Path.home()).encode(), "forbidden local/cache path", "sdk/share/metadata.txt")
    custom_cache = "/opt/" + "runtime-test-cache"
    verify((custom_cache + "/roots/libc").encode(), "forbidden local/cache path",
           "sdk/lib/static.a", {"CPKT_TOOLCHAIN_CACHE": custom_cache})
    relocated = "/opt/" + "x86-64--glibc--stable-2026.08-1/sysroot/lib"
    verify(relocated.encode(), "pinned Bootlin collection path", "sdk/lib/static.a")
    verify(b"ordinary text", member="sdk/share/ordinary.txt")
    (dist / "fixture.src.rock").write_bytes(b"broken archive")
    result = subprocess.run([str(root / "scripts/verify-release-privacy.sh")],
                            env={**os.environ, "SOFTLINE_DIST_DIR": str(dist)},
                            capture_output=True, text=True)
    assert result.returncode, "corrupt archive accepted"
    print(f"Artifact runtime guard: {cases + 1} positive/negative archive cases passed")
