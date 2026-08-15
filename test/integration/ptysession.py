import fcntl
import os
import pty
import select
import struct
import subprocess
import termios
import time

import pyte

from keynotation import encode_keys
from taggedscreen import Cell

COLS = 80
ROWS = 24
SYNC_TIMEOUT_S = 5.0

_ANSI_TO_TAG = {
    "black": "black",
    "red": "red",
    "green": "green",
    "brown": "yellow",
    "blue": "blue",
    "magenta": "magenta",
    "cyan": "white",
    "white": "white",
}


class DiredTimeout(Exception):
    pass


class DiredCrashed(Exception):
    pass


def _set_winsize(fd, rows, cols):
    packed = struct.pack("HHHH", rows, cols, 0, 0)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, packed)


def _split_bright(name):
    if name and name.startswith("bright"):
        return name[len("bright"):], True
    return name, False


def _tag_color(name):
    if not name or name == "default":
        return None
    return _ANSI_TO_TAG.get(name, name)


def pyte_screen_to_cells(screen):
    rows = []
    for y in range(screen.lines):
        line = screen.buffer[y]
        row = []
        for x in range(screen.columns):
            ch = line[x]
            fg_name, fg_bright = _split_bright(ch.fg)
            bg_name, _bg_bright = _split_bright(ch.bg)
            fg = _tag_color(fg_name)
            bg = _tag_color(bg_name)
            bright = bool(ch.bold) or fg_bright
            data = ch.data if ch.data else " "
            row.append(Cell(data, fg, bg, bright))
        rows.append(row)
    return rows


class PtySession:
    def __init__(self, diredd_path, cwd):
        self.master_fd, self.slave_fd = pty.openpty()
        _set_winsize(self.slave_fd, ROWS, COLS)

        self.sync_r, self.sync_w = os.pipe()

        env = dict(os.environ)
        env["DIRED_TEST_SYNC_FD"] = str(self.sync_w)

        slave_fd = self.slave_fd
        sync_r = self.sync_r
        master_fd = self.master_fd

        def preexec():
            os.setsid()
            fcntl.ioctl(slave_fd, termios.TIOCSCTTY, 0)
            os.close(master_fd)
            os.close(sync_r)

        self.proc = subprocess.Popen(
            [diredd_path],
            stdin=self.slave_fd,
            stdout=self.slave_fd,
            stderr=self.slave_fd,
            cwd=cwd,
            env=env,
            preexec_fn=preexec,
            pass_fds=(self.sync_w,),
        )

        os.close(self.slave_fd)
        os.close(self.sync_w)

        self.screen = pyte.Screen(COLS, ROWS)
        self.stream = pyte.ByteStream(self.screen)

        self._await_render(time.monotonic() + SYNC_TIMEOUT_S)

    def _drain_master_nonblocking(self):
        while True:
            r, _, _ = select.select([self.master_fd], [], [], 0)
            if self.master_fd not in r:
                return
            try:
                data = os.read(self.master_fd, 65536)
            except OSError:
                return
            if not data:
                return
            self.stream.feed(data)

    def _await_render(self, deadline):
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                self._drain_master_nonblocking()
                raise DiredTimeout("no response within 5s")

            watch = [self.sync_r, self.master_fd]
            rlist, _, _ = select.select(watch, [], [], remaining)

            if self.master_fd in rlist:
                try:
                    data = os.read(self.master_fd, 65536)
                except OSError:
                    data = b""
                if data:
                    self.stream.feed(data)
                else:
                    if self.proc.poll() is not None:
                        raise DiredCrashed(
                            "dired crashed/exited (exit code {})".format(self.proc.returncode)
                        )

            if self.sync_r in rlist:
                os.read(self.sync_r, 1)
                self._drain_master_nonblocking()
                return

            if self.proc.poll() is not None:
                self._drain_master_nonblocking()
                raise DiredCrashed(
                    "dired crashed/exited (exit code {})".format(self.proc.returncode)
                )

    def send_keys(self, keys):
        for key in keys:
            data = encode_keys([key])
            if data:
                os.write(self.master_fd, data)
            self._await_render(time.monotonic() + SYNC_TIMEOUT_S)

    def capture(self):
        return pyte_screen_to_cells(self.screen)

    def close(self):
        try:
            if self.proc.poll() is None:
                self.proc.terminate()
                try:
                    self.proc.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    self.proc.kill()
                    self.proc.wait(timeout=2)
        finally:
            for fd in (self.master_fd, self.sync_r):
                try:
                    os.close(fd)
                except OSError:
                    pass
