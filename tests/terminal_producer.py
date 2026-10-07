"""Coordinate this terminal fixture's private writers at resize seams."""
import contextlib
import fcntl
import os
import pathlib
import signal
import struct
import termios
import time


@contextlib.contextmanager
def pause_idle_editor(child, pump, enabled=True):
    """Freeze only our editor at a poll boundary during the physical resize.

    The worker pause does not stop spinner paints. Verify a stopped poll and
    drain pending input/replies on the owner before accepting a complete frame.
    Resume before asserting prompt reconciliation or input.
    This is Linux fixture coordination, not a production terminal workaround.
    """
    if not enabled:
        yield
        return
    descriptor = os.pidfd_open(child.pid)
    proc = pathlib.Path(f"/proc/{child.pid}")
    deadline = time.monotonic() + 5
    stopped = False
    try:
        while True:
            assert child.poll() is None, "editor exited before resize"
            assert time.monotonic() < deadline, "editor did not reach an idle poll"
            waiting = proc.joinpath("wchan").read_text().strip()
            before = proc.joinpath("syscall").read_text().split()
            if "poll" not in waiting or before[0] == "running":
                pump()
                continue
            signal.pidfd_send_signal(descriptor, signal.SIGSTOP)
            stopped = True
            while True:
                event = os.waitid(os.P_PIDFD, descriptor,
                                  os.WSTOPPED | os.WEXITED | os.WNOHANG | os.WNOWAIT)
                if event is not None:
                    assert event.si_code == os.CLD_STOPPED, "editor exited while pausing"
                    break
                assert time.monotonic() < deadline, "editor did not acknowledge stop"
                pump()
            after = proc.joinpath("syscall").read_text().split()
            pump()
            input_fd = os.open(proc / "fd/0", os.O_RDONLY | os.O_NONBLOCK | os.O_NOCTTY)
            try:
                pending = struct.unpack("i", fcntl.ioctl(input_fd, termios.FIONREAD, bytes(4)))[0]
            finally:
                os.close(input_fd)
            if after == before and pending == 0:
                break
            # A changed syscall or pending reply/input can belong to an
            # unfinished frame. Let the owner finish it before resizing.
            signal.pidfd_send_signal(descriptor, signal.SIGCONT)
            stopped = False
            pump()
        yield
    finally:
        if stopped:
            try:
                signal.pidfd_send_signal(descriptor, signal.SIGCONT)
            except ProcessLookupError:
                pass
        os.close(descriptor)


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
