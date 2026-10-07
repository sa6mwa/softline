"""Prove chat history is durable at acceptance, and loaded on the next startup.

All terminal traffic uses private PTYs and all persistent state stays in build/.
"""
import hashlib
import os
import pathlib
import sys
import tempfile
import time

sys.dont_write_bytecode = True
from check_chat_shell import Chat


def records(path):
    return path.read_bytes().splitlines() if path.exists() else []


def wait_records(chat, path, expected):
    deadline = time.monotonic() + 2
    while records(path) != expected:
        assert time.monotonic() < deadline, (records(path), expected, bytes(chat.output[-2000:]))
        chat.pump(0.02)


def check(command, build, kind):
    with tempfile.TemporaryDirectory(prefix='chat-history.', dir=build) as directory:
        env = dict(os.environ, SOFTLINE_HISTORY_DIR=directory,
                   SOFTLINE_PROMPT_THEME='plain', SOFTLINE_CHAT_CHAR_MS='10',
                   SOFTLINE_CHAT_OPERATION_STEP_MS='150')
        path = pathlib.Path(directory) / (hashlib.sha256(b'softline.examples.chat').hexdigest() + '.history')
        chat = Chat(command, env)
        try:
            chat.prompt()
            start = len(chat.output)
            chat.send(b'history-first\r')
            wait_records(chat, path, [b'history-first'])
            # C starts streaming; Lua starts its staged worker. In either case,
            # queue this draft while the turn is active and verify before dispatch.
            if kind == 'c':
                chat.wait(b'Thinking...')
            else:
                chat.wait(b'\x1b[?2004h', start)
            chat.send(b'!sh\r')
            chat.wait(b'Shell is available only between turns.')
            assert records(path) == [b'history-first'], records(path)
            chat.send(b'\x15queued-before-dispatch\r')
            wait_records(chat, path, [b'history-first', b'queued-before-dispatch'])
            chat.send(b'steer-before-dispatch\x1b\r')
            wait_records(chat, path, [b'history-first', b'queued-before-dispatch', b'steer-before-dispatch'])
            start = len(chat.output)
            delivered = b'> queued-before-dispatch' if kind == 'c' else b'[queued] queued-before-dispatch'
            chat.wait(delivered, start)
            chat.pump(0.05)
            assert records(path) == [b'history-first', b'queued-before-dispatch', b'steer-before-dispatch']
            # Kill without graceful cleanup: no exit-time save can explain success.
        finally:
            chat.close()
        assert records(path) == [b'history-first', b'queued-before-dispatch', b'steer-before-dispatch']
        chat = Chat(command, env)
        try:
            chat.prompt()
            start = len(chat.output)
            chat.send(b'cancelled-draft\x1b')
            chat.wait(b'\x1b[?2004h', start)
            assert len(records(path)) == 3, records(path)
            chat.send(b'\x12history-first')
            chat.wait(b'history-first')
            chat.send(b'\r')  # Accept search result into the editable draft only.
            chat.pump(0.15)
            assert len(records(path)) == 3, records(path)
            chat.send(b'-edited\r')
            wait_records(chat, path, [b'history-first', b'queued-before-dispatch',
                                     b'steer-before-dispatch', b'history-first-edited'])
        finally:
            chat.close()
        # Another theme loads the same key. Plain submissions persist too.
        import subprocess
        result = subprocess.run(command, input=b'plain-last\n/quit\n', env=dict(env, SOFTLINE_PROMPT_THEME='riced'),
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=5)
        assert result.returncode == 0, result.stderr
        assert records(path)[-2:] == [b'plain-last', b'/quit'], records(path)
    print(kind, 'chat: startup recall, editable Ctrl-R, immediate queue/steer persistence and plain input passed.')


if __name__ == '__main__':
    kind = sys.argv[1]
    command = [sys.argv[2]] if kind == 'c' else [sys.argv[2], str(pathlib.Path(__file__).resolve().parents[1] / 'examples/chat.lua')]
    check(command, sys.argv[3], kind)
