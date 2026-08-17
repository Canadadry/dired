import os
import shutil
import struct
import subprocess
import tarfile
import unittest
import zipfile
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


class BuildArchiveFixtureTest(unittest.TestCase):
    def setUp(self):
        self.roots = []

    def tearDown(self):
        for root in self.roots:
            shutil.rmtree(root, ignore_errors=True)

    def build(self, spec):
        root = fixture.build_fixture(spec)
        self.roots.append(root)
        return root

    def test_zip_archive_has_real_members(self):
        root = self.build({
            "archives": {
                "sample.zip": {"a.txt": "hello", "dir/b.txt": "world"},
            },
        })
        archive_path = os.path.join(root, "sample.zip")
        self.assertTrue(os.path.isfile(archive_path))
        with zipfile.ZipFile(archive_path) as zf:
            file_names = {n for n in zf.namelist() if not n.endswith("/")}
            self.assertEqual(file_names, {"a.txt", "dir/b.txt"})
            self.assertEqual(zf.read("a.txt").decode(), "hello")
            self.assertEqual(zf.read("dir/b.txt").decode(), "world")

    def test_tar_archive_has_real_members(self):
        root = self.build({
            "archives": {
                "sample.tar": {"a.txt": "hello", "dir/b.txt": "world"},
            },
        })
        archive_path = os.path.join(root, "sample.tar")
        self.assertTrue(os.path.isfile(archive_path))
        with tarfile.open(archive_path, "r") as tf:
            names = {m.name for m in tf.getmembers() if m.isfile()}
            self.assertEqual(names, {"a.txt", "dir/b.txt"})
            self.assertEqual(tf.extractfile("a.txt").read().decode(), "hello")
            self.assertEqual(tf.extractfile("dir/b.txt").read().decode(), "world")

    def test_tar_gz_archive_has_real_members(self):
        root = self.build({
            "archives": {
                "sample.tar.gz": {"a.txt": "hello"},
            },
        })
        archive_path = os.path.join(root, "sample.tar.gz")
        self.assertTrue(os.path.isfile(archive_path))
        with tarfile.open(archive_path, "r:gz") as tf:
            names = {m.name for m in tf.getmembers() if m.isfile()}
            self.assertEqual(names, {"a.txt"})
            self.assertEqual(tf.extractfile("a.txt").read().decode(), "hello")

    def test_tgz_suffix_also_builds_gzip_tar(self):
        root = self.build({
            "archives": {
                "sample.tgz": {"a.txt": "hello"},
            },
        })
        archive_path = os.path.join(root, "sample.tgz")
        with tarfile.open(archive_path, "r:gz") as tf:
            self.assertEqual(tf.extractfile("a.txt").read().decode(), "hello")

    def test_multiple_archives_in_one_fixture(self):
        root = self.build({
            "archives": {
                "one.zip": {"x.txt": "1"},
                "two.tar": {"y.txt": "2"},
            },
        })
        self.assertTrue(os.path.isfile(os.path.join(root, "one.zip")))
        self.assertTrue(os.path.isfile(os.path.join(root, "two.tar")))

    def test_archive_coexists_with_tree_and_plain_files(self):
        root = self.build({
            "tree": {"plain.txt": "plain"},
            "archives": {"sample.zip": {"a.txt": "hello"}},
        })
        with open(os.path.join(root, "plain.txt")) as f:
            self.assertEqual(f.read(), "plain")
        self.assertTrue(os.path.isfile(os.path.join(root, "sample.zip")))

    def test_no_archives_when_key_absent(self):
        root = self.build({"tree": {"a.txt": "hello"}})
        self.assertEqual(os.listdir(root), ["a.txt"])

    def test_unrecognized_extension_raises(self):
        with self.assertRaises(ValueError):
            self.build({"archives": {"sample.rar": {"a.txt": "hello"}}})

    def test_empty_member_tree_raises(self):
        with self.assertRaises(ValueError):
            self.build({"archives": {"sample.zip": {}}})

    def test_zip_shells_out_to_real_zip_binary(self):
        with mock.patch("fixture.subprocess.run", wraps=subprocess.run) as run:
            root = self.build({"archives": {"sample.zip": {"a.txt": "hello"}}})
        zip_calls = [c for c in run.call_args_list if c.args[0][0] == "zip"]
        self.assertEqual(len(zip_calls), 1)
        argv = zip_calls[0].args[0]
        self.assertEqual(argv[:2], ["zip", "-r"])
        self.assertEqual(argv[2], os.path.join(root, "sample.zip"))
        self.assertIn("a.txt", argv)

    def test_tar_gz_shells_out_to_real_tar_binary(self):
        with mock.patch("fixture.subprocess.run", wraps=subprocess.run) as run:
            root = self.build({"archives": {"sample.tar.gz": {"a.txt": "hello"}}})
        tar_calls = [c for c in run.call_args_list if c.args[0][0] == "tar"]
        self.assertEqual(len(tar_calls), 1)
        argv = tar_calls[0].args[0]
        self.assertEqual(argv[:2], ["tar", "-czf"])
        self.assertEqual(argv[2], os.path.join(root, "sample.tar.gz"))
        self.assertIn("a.txt", argv)


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
