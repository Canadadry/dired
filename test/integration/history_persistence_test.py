import os
import shutil
import time
import unittest

import fixture
import ptysession
import run as run_module
from keynotation import encode_keys

# Mirrors src/history.h/src/history.c's on-disk layout: a 12-byte header
# (3 x uint32_t, no padding) followed by the folder-history arena, then the
# file-history arena. See fixture.py's write_history() for the same layout
# used to *seed* history; this reads it back the same way, independently of
# the running diredd process, per PRD 01-history-persistence-fix.md's
# testing decision to verify the on-disk file directly.
HEADER_BYTES = 12
FOLDER_HISTORY_ARENA_BYTES = 20480
FILE_HISTORY_ARENA_BYTES = 40960

POLL_TIMEOUT_S = 3.0
POLL_INTERVAL_S = 0.05


def _read_history_arenas(history_path):
    with open(history_path, "rb") as f:
        data = f.read()
    folder_start = HEADER_BYTES
    folder_end = folder_start + FOLDER_HISTORY_ARENA_BYTES
    file_end = folder_end + FILE_HISTORY_ARENA_BYTES
    return data[folder_start:folder_end], data[folder_end:file_end]


def _wait_for_entry(history_path, arena_index, needle, timeout_s=POLL_TIMEOUT_S):
    """Polls the on-disk history file directly - a second, independent read
    of the file while diredd is conceptually still "running" (blocked
    forking its preview/editor child process) - since the fix under test
    writes synchronously, from the impure shell in dired.c, immediately
    after record_folder_history()/record_file_history() mutate the
    in-memory arena and before the preview/editor Cmd is ever executed.
    arena_index: 0 for folder history, 1 for file history."""
    deadline = time.monotonic() + timeout_s
    needle_bytes = (needle + "\x00").encode("utf-8")
    while time.monotonic() < deadline:
        if os.path.exists(history_path):
            arenas = _read_history_arenas(history_path)
            if needle_bytes in arenas[arena_index]:
                return True
        time.sleep(POLL_INTERVAL_S)
    return False


class HistoryPersistenceTest(unittest.TestCase):
    def setUp(self):
        self.fixture_root = fixture.build_fixture({"tree": {"one.txt": "hello\n"}})
        # ensure_history_file() (src/history.c) requires the parent
        # directory to already exist - it does not create intermediate
        # directories. A real user's ~/.config usually already exists;
        # create it here so the test isolates the behavior under test (the
        # missing write-after-record call) from that unrelated
        # precondition.
        os.makedirs(os.path.join(self.fixture_root, ".config"), exist_ok=True)
        self.history_path = os.path.join(self.fixture_root, ".config", "dired_history")
        self.session = ptysession.PtySession(run_module.diredd_path(), self.fixture_root)

    def tearDown(self):
        self.session.close()
        shutil.rmtree(self.fixture_root, ignore_errors=True)

    def test_preview_persists_folder_history_before_pager_exits(self):
        resolved_root = os.path.realpath(self.fixture_root)

        # Space triggers MSG_PREVIEW on the selected entry, which forks
        # `less` and blocks diredd in wait(2) - so session.send_keys() can't
        # be used here, since it awaits a post-keystroke render that won't
        # happen until the pager exits (see the comment in fixture.py on
        # why this path can't be driven through the normal per-keystroke
        # protocol). Write directly to the pty instead.
        os.write(self.session.master_fd, encode_keys([" "]))

        self.assertTrue(
            _wait_for_entry(self.history_path, 0, resolved_root),
            "expected {!r} to be persisted to the on-disk folder history "
            "immediately after preview, without waiting for the pager to "
            "exit".format(resolved_root),
        )

    def test_edit_persists_file_and_folder_history_before_editor_exits(self):
        resolved_root = os.path.realpath(self.fixture_root)
        expected_file = os.path.join(resolved_root, "one.txt")

        # Enter triggers MSG_ACTIVATE on the selected regular file, which
        # forks the editor and blocks diredd the same way preview does.
        os.write(self.session.master_fd, encode_keys(["<Enter>"]))

        self.assertTrue(
            _wait_for_entry(self.history_path, 1, expected_file),
            "expected {!r} to be persisted to the on-disk file history "
            "immediately after edit, without waiting for the editor to "
            "exit".format(expected_file),
        )
        self.assertTrue(
            _wait_for_entry(self.history_path, 0, resolved_root),
            "expected {!r} to also be persisted to the on-disk folder "
            "history (editing a file records its folder too)".format(resolved_root),
        )


if __name__ == "__main__":
    unittest.main()
