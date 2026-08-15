import os
import shutil
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


if __name__ == "__main__":
    unittest.main()
