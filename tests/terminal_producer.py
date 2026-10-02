"""Pause only this terminal fixture's private example worker at resize seams."""
import contextlib
import os
import pathlib
import signal


@contextlib.contextmanager
def pause_producer(child, pump, enabled=True):
    descriptor = None
    try:
        if enabled:
            children = pathlib.Path(
                f"/proc/{child.pid}/task/{child.pid}/children").read_text().split()
            if children:
                assert len(children) == 1, "unexpected chat workers"
                try:
                    descriptor = os.pidfd_open(int(children[0]))
                    signal.pidfd_send_signal(descriptor, signal.SIGSTOP)
                    # Let the owner finish any pending producer batch first.
                    for _ in range(3):
                        pump()
                except ProcessLookupError:
                    pass  # The response already finished.
        yield
    finally:
        if descriptor is not None:
            try:
                signal.pidfd_send_signal(descriptor, signal.SIGCONT)
            except ProcessLookupError:
                pass
            os.close(descriptor)
