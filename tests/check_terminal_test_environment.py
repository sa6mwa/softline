"""Keep cached terminal tests working after an ordinary CMake reconfigure."""
import json
import os
import pathlib
import subprocess
import sys

assert len(sys.argv) == 4, 'usage: check_terminal_test_environment.py BUILD CTEST CMAKE'
build = pathlib.Path(sys.argv[1]).resolve()
source = pathlib.Path(__file__).resolve().parents[1]
environment = dict(os.environ)
for name in ('SOFTLINE_TERMINAL_TEST_ROOT', 'CMAKE_LIBRARY_PATH',
             'LD_LIBRARY_PATH', 'FONTCONFIG_PATH', 'FONTCONFIG_FILE',
             'XKB_CONFIG_ROOT', 'NO_AT_BRIDGE', 'TMPDIR'):
    environment.pop(name, None)
environment['PATH'] = os.defpath

def inventory():
    result = subprocess.run([sys.argv[2], '--test-dir', str(build), '--show-only=json-v1'],
                            env=environment, capture_output=True, text=True, check=True)
    return {test['name']: test for test in json.loads(result.stdout)['tests']}

before = inventory()
assert 'softline_terminal_test_environment' in before, 'prepared runtime test is missing'
# make build/run-chat and Ninja regeneration may configure without the temporary
# environment from make test. Cached tool paths must keep their matching runtime.
result = subprocess.run([sys.argv[3], '-S', str(source), '-B', str(build)],
                        env=environment, capture_output=True, text=True)
assert result.returncode == 0, result.stdout + result.stderr
result = subprocess.run([sys.argv[2], '--test-dir', str(build), '--output-on-failure',
                         '-j', '1', '-R', '^softline_terminal_tab_expansion$'],
                        env=environment, capture_output=True, text=True)
assert result.returncode == 0, result.stdout + result.stderr
assert '1/1 Test' in result.stdout, result.stdout
after = inventory()
assert before.keys() == after.keys(), 'unprepared reconfigure changed the test inventory'
for name, test in before.items():
    if 'terminal-test-runtime.sh' in ' '.join(test.get('command', [])):
        assert test == after[name], f'unprepared reconfigure changed {name}'
print('Unprepared CMake reconfigure and direct CTest preserve the cached terminal runtime.')
