"""Configuration failure and verified archive cache regressions."""
import hashlib
import http.server
import os
import pathlib
import select
import shutil
import subprocess
import sys
import tempfile
import time
import threading

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
softline_configure_dependency_cache()
softline_verified_archive("fixture" "{upstream.as_uri()}" "{digest}" fixture.tar archive)
'''
    run(script)
    cached = work / "cache/archives/sha256" / digest / "fixture.tar"
    assert cached.read_bytes() == payload
    cached.write_bytes(b"corrupt")
    ready = work / "lock-held"
    release = work / "release-lock"
    holder_script = work / "hold-lock.cmake"
    holder_script.write_text(f'''
file(LOCK "{work}/cache/locks/{digest}.lock" GUARD PROCESS TIMEOUT 0)
file(WRITE "{ready}" "ready")
while(NOT EXISTS "{release}")
  execute_process(COMMAND "${{CMAKE_COMMAND}}" -E sleep 0.1)
endwhile()
''')
    holder = subprocess.Popen(["cmake", "-P", str(holder_script)], cwd=work,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    consumer = None
    try:
        deadline = time.monotonic() + 5
        while not ready.exists() and holder.poll() is None and time.monotonic() < deadline:
            time.sleep(0.01)
        assert ready.exists(), holder.communicate(timeout=5)
        consumer = subprocess.Popen(
            ["cmake", f"--trace-source={root}/cmake/softline_verified_archive.cmake",
             "--trace-expand", "-P", str(work / "check.cmake")],
            cwd=work, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        trace = b""
        deadline = time.monotonic() + 5
        while b"file(LOCK" not in trace and consumer.poll() is None and time.monotonic() < deadline:
            readable, _, _ = select.select([consumer.stderr], [], [],
                                           max(0, deadline - time.monotonic()))
            if readable:
                trace += os.read(consumer.stderr.fileno(), 4096)
        assert b"file(LOCK" in trace, trace.decode(errors="replace")
        assert cached.read_bytes() == b"corrupt", "archive changed before locking"
    finally:
        release.write_text("release")
        holder_output = holder.communicate(timeout=5)
        assert holder.returncode == 0, holder_output
        if consumer is not None:
            consumer_output = consumer.communicate(timeout=5)
            assert consumer.returncode == 0, consumer_output
    assert cached.read_bytes() == payload
    upstream.unlink()
    run(script)  # Offline hit.
    cached.write_bytes(b"corrupt")
    assert "cannot acquire" in run(script, False).lower()
    assert not list(cached.parent.iterdir()), "failed download left partial data"
    class TruncatedDownload(http.server.BaseHTTPRequestHandler):
        requests = 0

        def do_GET(self):
            type(self).requests += 1
            self.send_response(200)
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload if self.requests == 3 else payload[:3])
            self.close_connection = True

        def log_message(self, *args):
            pass

    server = http.server.HTTPServer(("127.0.0.1", 0), TruncatedDownload)
    thread = threading.Thread(target=server.serve_forever)
    thread.start()
    try:
        run(script.replace(upstream.as_uri(), f"http://127.0.0.1:{server.server_port}/archive"))
        assert TruncatedDownload.requests == 3
        assert cached.read_bytes() == payload
        assert sorted(path.name for path in cached.parent.iterdir()) == [cached.name]
    finally:
        server.shutdown()
        thread.join()
        server.server_close()
print("Missing-runtime and archive cache contracts passed")
