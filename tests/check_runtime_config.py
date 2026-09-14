"""Configuration failure and verified archive cache regressions."""
import hashlib
import pathlib
import shutil
import subprocess
import sys
import tempfile

root = pathlib.Path(sys.argv[1]).resolve()
(root / "build").mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix="runtime-contract.", dir=root / "build") as tmp:
    work = pathlib.Path(tmp)
    if len(sys.argv) == 2:
        fresh = work / "fresh"
        (fresh / "cmake").mkdir(parents=True)
        for name in ("softline_local_runtime.cmake", "softline_verified_archive.cmake"):
            shutil.copy2(root / "cmake" / name, fresh / "cmake" / name)
        assert not (fresh / "build").exists()
        subprocess.run([sys.executable, __file__, str(fresh), "--fresh-fixture"], check=True)
        assert (fresh / "build").is_dir()

    def run(source, success=True):
        script = work / "check.cmake"
        script.write_text(source)
        result = subprocess.run(["cmake", "-P", str(script)], cwd=work,
                                capture_output=True, text=True)
        assert (result.returncode == 0) == success, result.stdout + result.stderr
        return result.stdout + result.stderr

    error = run(f'''
include("{root}/cmake/softline_local_runtime.cmake")
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_HOST_SYSTEM_PROCESSOR x86_64)
set(SL_TARGET_ID x86_64-linux-gnu)
set(CMAKE_SYSROOT "{work}/missing")
softline_local_runtime(probe)
''', False)
    assert "ELF interpreter is missing" in error, error

    upstream = work / "upstream"
    payload = b"verified archive fixture"
    upstream.write_bytes(payload)
    digest = hashlib.sha256(payload).hexdigest()
    script = f'''
set(CPKT_DEPENDENCY_CACHE "{work}/cache")
include("{root}/cmake/softline_verified_archive.cmake")
softline_verified_archive("{upstream.as_uri()}" "{digest}" fixture.tar archive)
'''
    run(script)
    cached = work / "cache/archives/sha256" / digest / "fixture.tar"
    assert cached.read_bytes() == payload
    upstream.unlink()
    run(script)  # Offline hit.
    cached.write_bytes(b"corrupt")
    assert "Corrupt cached archive" in run(script, False)
    cached.unlink()
    assert "Cannot acquire" in run(script, False)
    assert not list(cached.parent.iterdir()), "failed download left partial data"
print("Missing-runtime and archive cache contracts passed")
