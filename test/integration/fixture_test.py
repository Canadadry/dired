import os
import shutil
import struct
import subprocess
import unittest
from unittest import mock

import fixture


class BuildFixtureTest(unittest.TestCase):
    def setUp(self):
        self.roots = []

    def tearDown(self):
        for root in self.roots:
            shutil.rmtree(root, ignore_errors=True)

    def build(self, spec):
        root = fixture.build_fixture(spec)
        self.roots.append(root)
        return root

    def test_flat_tree(self):
        root = self.build({"tree": {"a.txt": "hello", "b.txt": "world"}})
        with open(os.path.join(root, "a.txt")) as f:
            self.assertEqual(f.read(), "hello")
        with open(os.path.join(root, "b.txt")) as f:
            self.assertEqual(f.read(), "world")

    def test_nested_tree(self):
        root = self.build({"tree": {"dir/sub/c.txt": "nested"}})
        with open(os.path.join(root, "dir", "sub", "c.txt")) as f:
            self.assertEqual(f.read(), "nested")

    def test_no_git_dir_when_setup_key_absent(self):
        root = self.build({"tree": {"a.txt": "hello"}})
        self.assertFalse(os.path.isdir(os.path.join(root, ".git")))

    def test_no_git_dir_when_setup_empty(self):
        root = self.build({"tree": {"a.txt": "hello"}, "setup": []})
        self.assertFalse(os.path.isdir(os.path.join(root, ".git")))

    def test_commit_identity_local_not_global(self):
        root = self.build({
            "tree": {"a.txt": "hello"},
            "setup": [["add", "a.txt"], ["commit", "-m", "init"]],
        })
        email = subprocess.run(
            ["git", "config", "--local", "user.email"],
            cwd=root, check=True, capture_output=True, text=True,
        ).stdout.strip()
        name = subprocess.run(
            ["git", "config", "--local", "user.name"],
            cwd=root, check=True, capture_output=True, text=True,
        ).stdout.strip()
        self.assertEqual(email, "test@test")
        self.assertEqual(name, "test")

    def status(self, root):
        return subprocess.run(
            ["git", "status", "--porcelain", "--ignored"],
            cwd=root, check=True, capture_output=True, text=True,
        ).stdout

    def test_git_states(self):
        cases = [
            ("tracked-clean", {}, ""),
            ("untracked", {"untracked.txt": "new"}, "?? untracked.txt\n"),
        ]
        for name, extra_tree, expected in cases:
            with self.subTest(name=name):
                tree = {"tracked.txt": "hello"}
                tree.update(extra_tree)
                root = self.build({
                    "tree": tree,
                    "setup": [["add", "tracked.txt"], ["commit", "-m", "init"]],
                })
                self.assertEqual(self.status(root), expected)

    def test_git_state_modified(self):
        root = self.build({
            "tree": {"tracked.txt": "hello"},
            "setup": [["add", "tracked.txt"], ["commit", "-m", "init"]],
        })
        with open(os.path.join(root, "tracked.txt"), "w") as f:
            f.write("changed")
        self.assertEqual(self.status(root), " M tracked.txt\n")

    def test_git_state_deleted(self):
        root = self.build({
            "tree": {"tracked.txt": "hello"},
            "setup": [["add", "tracked.txt"], ["commit", "-m", "init"]],
        })
        os.remove(os.path.join(root, "tracked.txt"))
        self.assertEqual(self.status(root), " D tracked.txt\n")

    def test_git_state_ignored(self):
        root = self.build({
            "tree": {".gitignore": "ignored.txt\n", "ignored.txt": "skip"},
            "setup": [["add", ".gitignore"], ["commit", "-m", "init"]],
        })
        self.assertEqual(self.status(root), "!! ignored.txt\n")

    def test_rejects_non_whitelisted_command(self):
        with mock.patch("fixture.subprocess.run") as run, mock.patch("fixture.tempfile.mkdtemp") as mkdtemp:
            with self.assertRaises(ValueError):
                fixture.build_fixture({
                    "tree": {"a.txt": "hello"},
                    "setup": [["push"]],
                })
            run.assert_not_called()
            mkdtemp.assert_not_called()

    def test_rejects_mixed_valid_and_invalid(self):
        with mock.patch("fixture.subprocess.run") as run, mock.patch("fixture.tempfile.mkdtemp") as mkdtemp:
            with self.assertRaises(ValueError):
                fixture.build_fixture({
                    "tree": {"a.txt": "hello"},
                    "setup": [["add", "a.txt"], ["push"]],
                })
            run.assert_not_called()
            mkdtemp.assert_not_called()

    def test_rejects_empty_entry(self):
        with mock.patch("fixture.subprocess.run") as run, mock.patch("fixture.tempfile.mkdtemp") as mkdtemp:
            with self.assertRaises(ValueError):
                fixture.build_fixture({
                    "tree": {"a.txt": "hello"},
                    "setup": [[]],
                })
            run.assert_not_called()
            mkdtemp.assert_not_called()


def _decode_arena(buf):
    """Mirrors history_arena_command_at(): position_from_newest=0 is the
    most-recently-pushed entry, matching what the folder/file picker itself
    would display."""
    size = len(buf)
    count = struct.unpack_from("<H", buf, size - 2)[0]
    entries = []
    for position_from_newest in range(count):
        i = count - 1 - position_from_newest
        addr = size - 4 - 2 * i
        offset = struct.unpack_from("<H", buf, addr)[0]
        end = buf.index(b"\x00", offset)
        entries.append(buf[offset:end].decode("utf-8"))
    return entries


class WriteHistoryTest(unittest.TestCase):
    def setUp(self):
        self.roots = []

    def tearDown(self):
        for root in self.roots:
            shutil.rmtree(root, ignore_errors=True)

    def build(self, spec):
        root = fixture.build_fixture(spec)
        self.roots.append(root)
        return root

    def history_path(self, root):
        return os.path.join(root, ".config", "dired_history")

    def test_no_file_written_when_history_spec_absent(self):
        root = self.build({"tree": {"a.txt": "hello"}})
        fixture.write_history(root, root, {})
        self.assertFalse(os.path.exists(self.history_path(root)))

    def test_header_fields(self):
        root = self.build({"tree": {"a.txt": "hello"}})
        fixture.write_history(root, root, {"folder_history": ["a"]})
        with open(self.history_path(root), "rb") as f:
            magic, version, occupied_count = struct.unpack("<III", f.read(12))
        self.assertEqual(magic, fixture.HISTORY_FILE_MAGIC)
        self.assertEqual(version, fixture.HISTORY_FILE_VERSION)
        self.assertEqual(occupied_count, 0)

    def test_folder_and_file_history_round_trip_most_recent_first(self):
        root = self.build({"tree": {"alpha/.keep": "", "beta/.keep": ""}})
        fixture.write_history(root, root, {
            "folder_history": ["alpha", "beta"],
            "file_history": ["alpha/.keep"],
        })
        with open(self.history_path(root), "rb") as f:
            data = f.read()

        folder_section = data[12:12 + fixture.FOLDER_HISTORY_ARENA_BYTES]
        file_section = data[12 + fixture.FOLDER_HISTORY_ARENA_BYTES:
                             12 + fixture.FOLDER_HISTORY_ARENA_BYTES + fixture.FILE_HISTORY_ARENA_BYTES]

        self.assertEqual(
            _decode_arena(folder_section),
            [os.path.join(root, "beta"), os.path.join(root, "alpha")],
        )
        self.assertEqual(_decode_arena(file_section), [os.path.join(root, "alpha/.keep")])

    def test_file_length_matches_header_plus_both_arenas(self):
        root = self.build({"tree": {"alpha/.keep": ""}})
        fixture.write_history(root, root, {"folder_history": ["alpha"]})
        expected_size = 12 + fixture.FOLDER_HISTORY_ARENA_BYTES + fixture.FILE_HISTORY_ARENA_BYTES
        self.assertEqual(os.path.getsize(self.history_path(root)), expected_size)

    def test_entry_too_large_for_arena_raises(self):
        root = self.build({"tree": {"a.txt": "hello"}})
        with self.assertRaises(ValueError):
            fixture.write_history(root, root, {"folder_history": ["x" * fixture.FOLDER_HISTORY_ARENA_BYTES]})


if __name__ == "__main__":
    unittest.main()
