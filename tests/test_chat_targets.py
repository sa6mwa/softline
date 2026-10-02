"""Execute no-delay chat targets with fake build tools and a recording example."""
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

root = pathlib.Path(sys.argv[1]).resolve()
(root / "build").mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix="chat-targets.", dir=root / "build") as directory:
    fixture = pathlib.Path(directory)
    shutil.copy2(root / "Makefile", fixture / "Makefile")
    (fixture / "bin").mkdir()
    cmake = fixture / "bin/cmake"
    cmake.write_text("#!/bin/sh\nexit 0\n")
    cmake.chmod(0o755)
    (fixture / "build/debug/examples").mkdir(parents=True)
    example = fixture / "build/debug/examples/example_chat"
    example.write_text('#!/bin/sh\nprintf "%s %s\\n" "$SOFTLINE_CHAT_CHAR_MS" "$SOFTLINE_PROMPT_THEME"\n')
    example.chmod(0o755)
    env = dict(os.environ, PATH=str(fixture / "bin") + os.pathsep + os.environ["PATH"],
               SOFTLINE_CHAT_CHAR_MS="999", SOFTLINE_PROMPT_THEME="plain")
    for target, theme in (("run-chat-without-delay", "default"),
                          ("run-chat-without-delay-riced", "riced")):
        result = subprocess.run(["make", "--no-print-directory", target], cwd=fixture,
                                env=env, capture_output=True, text=True, check=True)
        assert result.stdout.strip() == "0 " + theme, result.stdout
print("Default and riced chat targets force zero producer delay.")
