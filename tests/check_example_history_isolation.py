"""Direct example tests must not read or write the caller's chat history."""

import hashlib
import os
import pathlib
import subprocess
import sys
import tempfile


runner, simple, chat, build = map(lambda value: pathlib.Path(value).resolve(), sys.argv[1:])
name = hashlib.sha256(b"softline.examples.chat").hexdigest() + ".history"
with tempfile.TemporaryDirectory(prefix="example-history-check.", dir=build) as directory:
    root = pathlib.Path(directory)
    home, state, override = root / "home", root / "state", root / "override"
    for path in (home / ".local/state/softline/history", state / "softline/history", override):
        path.mkdir(parents=True, mode=0o700)
        (path / name).write_bytes(b"caller-owned prompt\n")
        (path / name).chmod(0o600)

    def snapshot():
        return {str(path.relative_to(root)): path.read_bytes() for path in root.rglob("*") if path.is_file()}

    for inherited_override in (False, True):
        before = snapshot()
        existing = set(build.glob("example-history.*"))
        environment = dict(os.environ, HOME=str(home), XDG_STATE_HOME=str(state))
        environment.pop("SOFTLINE_HISTORY_DIR", None)
        if inherited_override:
            environment["SOFTLINE_HISTORY_DIR"] = str(override)
        result = subprocess.run([str(runner), str(simple), str(chat), "bottom"],
                                cwd=root, env=environment, capture_output=True, timeout=30)
        assert result.returncode == 0, result.stdout + result.stderr
        assert snapshot() == before, "direct runner changed caller history"
        created = set(build.glob("example-history.*")) - existing
        assert len(created) == 1, "runner did not use a fresh build-local history directory"
        records = (created.pop() / name).read_bytes().splitlines()
        assert records and b"caller-owned prompt" not in records, records
print("Direct example runner isolates history from default and explicit caller storage, independent of cwd.")
