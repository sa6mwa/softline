"""Compare native chunks and exit cursor with direct bytes in tmux."""

import pathlib
import shlex
import subprocess
import sys
import tempfile
import time


def main():
    assert len(sys.argv) == 4, "usage: check_terminal_native_output_tmux.py TMUX FIXTURE BUILD"
    tmux, fixture, build = sys.argv[1:]
    texts = ("a" * 40 + "X", "🇸🇪" * 22 + "X", "👩‍💻" * 10 + "X", "❤️" * 22 + "X", "1️⃣" * 22 + "X", "a" * 39 + "éX", "a" * 40 + "́X")
    with tempfile.TemporaryDirectory(prefix="unicode-", dir=build) as directory:
        socket = str(pathlib.Path(directory) / "tmux.sock")

        def run(*args):
            return subprocess.check_output([tmux, "-S", socket, "-f", "/dev/null",
                                            *args], text=True).strip()

        def render(text, chunk):
            command = ("exec " + shlex.join([fixture, "--chunks", text, str(chunk)]) if chunk
                       else shlex.join(["printf", "%s\\r\\n", text]))
            run("respawn-pane", "-k", "-t", "unicode:0.0", command)
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                if run("display-message", "-p", "-t", "unicode:0.0",
                       "#{pane_dead}") == "1":
                    break
                time.sleep(0.01)
            else:
                raise AssertionError("Unicode fixture did not finish")
            assert run("display-message", "-p", "-t", "unicode:0.0",
                       "#{pane_dead_status}") == "0", "Unicode fixture failed"
            return (run("capture-pane", "-p", "-S", "-", "-t", "unicode:0.0").splitlines(),
                    run("display-message", "-p", "-t", "unicode:0.0", "#{cursor_y},#{cursor_x}"))

        try:
            run("new-session", "-d", "-s", "unicode", "-x", "40", "-y", "8", "cat")
            run("set-option", "-g", "remain-on-exit", "on")
            run("set-option", "-g", "remain-on-exit-format", "")
            run("set-option", "-g", "status", "off")
            failures = []
            for text in texts:
                expected = render(text, 0)
                assert expected[0] and expected[0][-1].endswith("X"), f"tmux baseline missing sentinel: {expected!r}"
                for chunk in sorted({1, 4, 8, len(text.encode())}):
                    actual = render(text, chunk)
                    if actual != expected:
                        failures.append(f"{text!r}, chunk {chunk}: {actual!r} != {expected!r}")
            assert not failures, "\n".join(failures)
        finally:
            subprocess.run([tmux, "-S", socket, "kill-server"], check=False,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print("Native chunks and exit cursor match direct tmux output.")


if __name__ == "__main__":
    main()
