"""Portable privacy checks must not require an ELF tool until ELF is encountered."""
import io
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile

root = Path(__file__).resolve().parents[1]
(root / "build").mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix="artifact-portability.", dir=root / "build") as tmp:
    work = Path(tmp)
    scripts = work / "scripts"
    scripts.mkdir()
    scanner = scripts / "verify_artifact_runtime.py"
    shutil.copy2(root / "scripts/verify_artifact_runtime.py", scanner)
    artifact = work / "fixture.tar.gz"
    empty_path = work / "empty-path"
    empty_path.mkdir()
    env = {**os.environ, "PATH": str(empty_path)}

    def verify(data, expected=None):
        with tarfile.open(artifact, "w:gz") as archive:
            member = tarfile.TarInfo("sdk/payload")
            member.size = len(data)
            archive.addfile(member, io.BytesIO(data))
        result = subprocess.run([sys.executable, str(scanner), str(artifact)],
                                env=env, capture_output=True, text=True)
        if expected:
            assert result.returncode and expected in result.stderr, result
            assert "fixture.tar.gz!sdk/payload" in result.stderr, result.stderr
        else:
            assert result.returncode == 0, result.stderr

    verify(b"ordinary source-only payload")
    # Mach-O magic: this scanner handles privacy, not Darwin loader validation.
    verify(b"\xcf\xfa\xed\xfe" + bytes(28))
    verify(str(Path.home()).encode(), "forbidden local/cache path")
    verify(b"\x7fELF" + bytes(60), "readelf required")
    # Even a stale configured ELF tool must not affect non-ELF artifacts.
    cache = work / "build/x86_64-linux-gnu-release/CMakeCache.txt"
    cache.parent.mkdir(parents=True)
    cache.write_text("CMAKE_READELF:FILEPATH=" + str(work / "missing-readelf") + "\n")
    verify(b"source with stale ELF cache")
    verify(b"\x7fELF" + bytes(60), "ELF inspection failed")

    # Prove unsupported hosts exit before resolving or invoking Bootlin tools.
    for system, machine in (("Darwin", "arm64"), ("Darwin", "x86_64"), ("Linux", "aarch64")):
        program = '''
import runpy, sys
from unittest.mock import patch
with patch("platform.system", return_value=sys.argv[2]), \\
     patch("platform.machine", return_value=sys.argv[3]), \\
     patch("subprocess.check_output", side_effect=AssertionError("Bootlin invoked")):
    runpy.run_path(sys.argv[1], run_name="__main__")
'''
        result = subprocess.run([sys.executable, "-c", program,
                                str(root / "tests/check_artifact_runtime.py"), system, machine],
                                env=env, capture_output=True, text=True)
        assert result.returncode == 0 and "SKIP:" in result.stdout, result
print("Artifact portability: no-tool source/Mach-O scans, ELF refusal, and unsupported-host skips passed")
