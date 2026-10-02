"""Run a real cached-terminal test from an ordinary, unprepared CTest shell."""
import os
import pathlib
import subprocess
import sys

assert len(sys.argv) == 3, 'usage: check_terminal_test_environment.py BUILD CTEST'
build = pathlib.Path(sys.argv[1]).resolve()
environment = dict(os.environ)
for name in ('SOFTLINE_TERMINAL_TEST_ROOT', 'CMAKE_LIBRARY_PATH',
             'LD_LIBRARY_PATH', 'FONTCONFIG_PATH', 'FONTCONFIG_FILE',
             'XKB_CONFIG_ROOT', 'NO_AT_BRIDGE', 'TMPDIR'):
    environment.pop(name, None)
environment['PATH'] = os.defpath
result = subprocess.run([sys.argv[2], '--test-dir', str(build), '--output-on-failure',
                         '-j', '1', '-R', '^softline_terminal_tab_expansion$'],
                        env=environment, capture_output=True, text=True)
assert result.returncode == 0, result.stdout + result.stderr
assert '1/1 Test' in result.stdout, result.stdout
print('Direct CTest preserves the cached terminal runtime and environment.')
