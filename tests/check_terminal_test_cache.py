"""Prove pinned terminal tools reconstruct offline and reject corrupt archives."""
import hashlib
import io
import json
import os
import pathlib
import platform
import sys
import shutil
import subprocess
import tarfile
import tempfile

if platform.system() != "Linux" or platform.machine().lower() not in ("x86_64", "amd64"):
    print("SKIP: pinned terminal test tools require native x86_64 Linux")
    sys.exit(0)

root = pathlib.Path(__file__).resolve().parents[1]
subprocess.run(["sh", str(root / "scripts/terminal-test-env.sh"), "true"], check=True)
(root / "build").mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix="terminal-cache-", dir=root / "build") as tmp:
    work = pathlib.Path(tmp)
    missing = work / "missing-runtime"
    missing.mkdir()
    gtk = root / ".cache/deps/x86_64-linux-gnu/terminal-tests/install/usr/lib/x86_64-linux-gnu/libgtk-3.so.0"
    shutil.copyfile(gtk, missing / gtk.name)
    cached_libs = gtk.parent
    for name in ("libc.so.6", "ld-linux-x86-64.so.2"):
        destination = missing / "usr/lib/x86_64-linux-gnu" / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(cached_libs / name, destination)
    result = subprocess.run([sys.executable, str(root / "scripts/relocate-terminal-test-tools.py"),
                             str(missing), "--verify"], capture_output=True, text=True)
    assert result.returncode != 0 and "required library" in result.stderr and "is not staged" in result.stderr, result.stderr
    # Control imported symbol versions independently of the current workstation.
    # Definitions of a library's own versions are not host libc requirements.
    version_root = work / "version-runtime"
    version_root.mkdir()
    (version_root / "fixture.so").write_bytes(b"\x7fELFfixture")
    libc = version_root / "usr/lib/x86_64-linux-gnu/libc.so.6"
    libc.parent.mkdir(parents=True)
    libc.write_bytes(b"\x7fELFfixture")
    mock_bin = work / "mock-bin"
    mock_bin.mkdir()
    readelf = mock_bin / "readelf"
    readelf.write_text("#!/bin/sh\ncase \"$1\" in\n"
                       "--version-info) printf '%s\\n' 'Version definition section' "
                       "'Name: GLIBC_2.35' 'Version needs section' "
                       "\"Name: GLIBC_${TEST_GLIBC_VERSION}\";;\nesac\n")
    readelf.chmod(0o755)
    for version, accepted in (("2.35", True), ("2.43", False)):
        env = dict(os.environ, PATH=str(mock_bin) + os.pathsep + os.environ["PATH"],
                   TEST_GLIBC_VERSION=version)
        result = subprocess.run([sys.executable,
                                 str(root / "scripts/relocate-terminal-test-tools.py"),
                                 str(version_root), "--verify"], env=env,
                                capture_output=True, text=True)
        assert (result.returncode == 0) == accepted, result.stderr
        if not accepted:
            assert "required libc version GLIBC_2.43" in result.stderr, result.stderr
    isolated = subprocess.run(["sh", str(root / "scripts/terminal-test-env.sh"),
                              "sh", "-c", 'test -z "${LD_LIBRARY_PATH:-}"'],
                             env={k: v for k, v in os.environ.items() if k != "LD_LIBRARY_PATH"},
                             capture_output=True, text=True)
    assert isolated.returncode == 0, isolated.stdout + isolated.stderr
    fixture = work / "source"
    (fixture / "cmake/terminal-tests").mkdir(parents=True)
    (fixture / "scripts").mkdir()
    shutil.copy2(root / "cmake/terminal-tests/CMakeLists.txt", fixture / "cmake/terminal-tests")
    shutil.copy2(root / "cmake/softline_verified_archive.cmake", fixture / "cmake")
    shutil.copy2(root / "scripts/relocate-terminal-test-tools.py", fixture / "scripts")
    payload = work / "data.tar.gz"
    original = b"test Xvfb /usr/bin\0 prefix"
    with tarfile.open(payload, "w:gz") as archive:
        entry = tarfile.TarInfo("usr/bin/Xvfb")
        entry.size, entry.mode = len(original), 0o755
        archive.addfile(entry, io.BytesIO(original))
    upstream = work / "tools.deb"
    subprocess.run(["ar", "qc", str(upstream), str(payload)], check=True)
    digest = hashlib.sha256(upstream.read_bytes()).hexdigest()
    with tarfile.open(payload, "w:gz") as archive:
        entry = tarfile.TarInfo("usr/share/terminal-fixture")
        entry.size = 4
        archive.addfile(entry, io.BytesIO(b"done"))
    extra = work / "extra.deb"
    subprocess.run(["ar", "qc", str(extra), str(payload)], check=True)
    extra_bytes = extra.read_bytes()
    extra_digest = hashlib.sha256(extra_bytes).hexdigest()
    manifest = {"archives": [{"name": "fixture", "url": upstream.as_uri(),
                              "sha256": digest, "archive": "tools.deb"},
                             {"name": "extra", "url": extra.as_uri(),
                              "sha256": extra_digest, "archive": "extra.deb"}]}
    (fixture / "cmake/terminal-tests/archives.json").write_text(json.dumps(manifest))
    cache = work / "cache"
    cmd = ["cmake", "-S", str(fixture / "cmake/terminal-tests"), "-B", str(work / "configure"),
           f"-DCPKT_DEPENDENCY_CACHE={cache}"]
    env = dict(os.environ, CPKT_DEPENDENCY_CACHE=str(work / "unused-cache"))
    def configure(success=True):
        result = subprocess.run(cmd, env=env, capture_output=True, text=True)
        assert (result.returncode == 0) == success, result.stdout + result.stderr
        return result.stdout + result.stderr
    configure()
    cached = cache / "archives/sha256" / digest / "tools.deb"
    assert cached.read_bytes() == upstream.read_bytes()
    installed = fixture / ".cache/deps/x86_64-linux-gnu/terminal-tests/install/usr/bin/Xvfb"
    assert installed.read_bytes() == original.replace(b"/usr/bin\0", b"\0" * 9)
    sentinel = installed.parents[1] / "share/terminal-fixture"
    extra_cached = cache / "archives/sha256" / extra_digest / "extra.deb"
    installed.unlink()
    sentinel.unlink()
    extra_cached.write_bytes(b"corrupt")
    extra.unlink()
    configure(False)  # Fail after the first archive has already been staged.
    assert installed.exists() and not sentinel.exists()
    extra.write_bytes(extra_bytes)
    configure()
    assert sentinel.read_bytes() == b"done", "partial reconstruction reused an old contract"
    assert installed.read_bytes() == original.replace(b"/usr/bin\0", b"\0" * 9)
    upstream.unlink()
    shutil.rmtree(fixture / ".cache")
    configure()  # Reconstruct entirely from the immutable verified cache.
    assert installed.is_file() and hashlib.sha256(cached.read_bytes()).hexdigest() == digest
    assert not (work / "unused-cache").exists(), "explicit cache lost to environment"
    cached.write_bytes(b"corrupt")
    assert "cannot acquire" in configure(False), "corruption was silently reused"
    assert not cached.exists(), "corrupt cache survived validation"
print("Terminal test-tool cache, runtime closure, reconstruction and corruption checks passed")
