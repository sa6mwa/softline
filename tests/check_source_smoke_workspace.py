#!/usr/bin/env python3
"""Exercise source-smoke workspace placement and cleanup through its script."""

import io
import os
import pathlib
import shutil
import subprocess
import sys
import tarfile
import tempfile


def main():
    root = pathlib.Path(sys.argv[1]).resolve()
    (root / "build").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="source-smoke-test.",
                                     dir=root / "build") as scratch:
        base = pathlib.Path(scratch)
        fixture = base / "source root"
        scripts = fixture / "scripts"
        scripts.mkdir(parents=True)
        for name in ("package-source-smoke.sh", "release_version.sh"):
            shutil.copy2(root / "scripts" / name, scripts / name)
        (fixture / "VERSION").write_text("0.0.0\n")
        (fixture / "dist").mkdir()
        archive = fixture / "dist/softline-0.0.0.tar.gz"
        payload = {"VERSION": b"0.0.0\n",
                   "RELEASE_MANIFEST": b"RELEASE_MANIFEST\nVERSION\n"}
        with tarfile.open(archive, "w:gz") as output:
            for name, data in payload.items():
                member = tarfile.TarInfo("softline-0.0.0/" + name)
                member.size = len(data)
                output.addfile(member, io.BytesIO(data))

        tools = base / "tools"
        tools.mkdir()
        trace = base / "trace"
        helper = f"#!{sys.executable}\n" + """
import os
import pathlib
import sys
workspace = pathlib.Path(os.environ['SMOKE_FIXTURE_ROOT']) / 'build'
if pathlib.Path(sys.argv[0]).name == 'cmake' and '-S' in sys.argv:
    source = pathlib.Path(sys.argv[sys.argv.index('-S') + 1])
    assert source.parent.parent == workspace, source
    destination = pathlib.Path(sys.argv[sys.argv.index('-B') + 1])
    destination.mkdir()
elif pathlib.Path(sys.argv[0]).name == 'ctest':
    assert pathlib.Path.cwd().parent.parent.parent == workspace
with open(os.environ['SMOKE_TRACE'], 'a') as trace:
    trace.write(pathlib.Path(sys.argv[0]).name + '\\n')
if os.environ.get('SMOKE_FAIL') and pathlib.Path(sys.argv[0]).name == 'cmake':
    sys.exit(17)
if os.environ.get('SMOKE_INTERRUPT'):
    import signal
    os.kill(os.getppid(), signal.SIGTERM)
"""
        for name in ("cmake", "ctest"):
            path = tools / name
            path.write_text(helper)
            path.chmod(0o755)
        outside = base / "outside"
        outside.mkdir()
        forbidden = base / "wrong temporary directory"
        forbidden.mkdir()
        env = dict(os.environ, PATH=str(tools) + os.pathsep + os.environ['PATH'],
                   TMPDIR=str(forbidden), SMOKE_FIXTURE_ROOT=str(fixture),
                   SMOKE_TRACE=str(trace))
        env.pop("SL_VERSION_OVERRIDE", None)
        for mode, exit_code in (('success', 0), ('failure', 17), ('signal', 143)):
            trace.unlink(missing_ok=True)
            env.pop('SMOKE_FAIL', None)
            env.pop('SMOKE_INTERRUPT', None)
            if mode == 'failure':
                env['SMOKE_FAIL'] = '1'
            if mode == 'signal':
                env['SMOKE_INTERRUPT'] = '1'
            result = subprocess.run(['sh', str(scripts / 'package-source-smoke.sh')],
                                    cwd=outside, env=env, capture_output=True,
                                    text=True, timeout=15)
            assert result.returncode == exit_code, (
                result.returncode, result.stdout, result.stderr)
            assert trace.read_text().splitlines() == (
                ['cmake', 'cmake', 'ctest'] if mode == 'success' else ['cmake'])
            assert not list((fixture / 'build').iterdir()), 'workspace leaked'
            assert not list(forbidden.iterdir()), 'TMPDIR was used'
            assert not list(outside.iterdir()), 'caller directory was used'
        print('Source smoke uses repository build/ and cleans success/failure/signal workspaces.')


if __name__ == '__main__':
    main()
